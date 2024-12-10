#pragma once

#include "mftTypes.h"
#include "windowsInclude.h"
#include "utility.h"
#include "commonFileReading.h"

#include <atomic>
#include <array>
#include <memory>
#include <iostream>

constexpr uint32_t FileRecordSizeInBytes = 1024;

struct ProgressInfo {
    std::atomic<double>& progress;
    uint64_t recordCount;
    std::atomic<int64_t> recordsProcessed = 0;

    ProgressInfo(std::atomic<double>& progress) : progress(progress), recordCount(1) {}
    void addRecordsProcessed(int64_t count) {
        recordsProcessed += count;
        progress = double(recordsProcessed) / recordCount;
        static int64_t prevWrittenSize = 0;
        if (recordsProcessed - prevWrittenSize >= 100'000) {
            prevWrittenSize = recordsProcessed;
        }
    }
};

static void readVolume(void* buffer, uint64_t from, uint32_t count, HANDLE volume) {
    OVERLAPPED overlapped;
    overlapped.Offset = from & 0xffffffff;
    overlapped.OffsetHigh = from >> 32;
    overlapped.hEvent = CreateEventW(nullptr, false, false, nullptr);
    DWORD bytesAccessed;
    ReadFile(volume, buffer, count, &bytesAccessed, &overlapped);
    GetOverlappedResult(volume, &overlapped, &bytesAccessed, true);
    CloseHandle(overlapped.hEvent);
}

static void readMftFileRecord(uint8_t*& buffer, uint32_t recordNumber, HANDLE volume) {
    /*
        Curious note:
        When I just use ReadFile to read first record with location I get from BootSector 
        it takes very long time (~0.4 sec), while this takes almost no time (as expected - It's only 1 KB).
        I use the same handle and DeviceIoControl has to do exactly the same read operation from the same location
        so I don't understand what's going wrong with just using ReadFile.

        The same problem appears also while reading all of the other file records, where the first operation takes way longer then it should,
        however there the problem is partly mitigated with multi-threading. 
        Still maybe if I figure out the problem I could make it a bit faster
    */
    OVERLAPPED overlapped;
    overlapped.Offset = 0;
    overlapped.OffsetHigh = 0;
    overlapped.hEvent = CreateEventW(nullptr, false, false, nullptr);
    DWORD bytesAccessed;
    NTFS_FILE_RECORD_INPUT_BUFFER inputBuffer;
    inputBuffer.FileReferenceNumber.QuadPart = recordNumber;
    DeviceIoControl(volume, FSCTL_GET_NTFS_FILE_RECORD, &inputBuffer, sizeof(NTFS_FILE_RECORD_INPUT_BUFFER), buffer, sizeof(NTFS_FILE_RECORD_OUTPUT_BUFFER) + FileRecordSizeInBytes - 1, &bytesAccessed, &overlapped);
    GetOverlappedResult(volume, &overlapped, &bytesAccessed, true);
    CloseHandle(overlapped.hEvent);
    auto outputBuffer = (NTFS_FILE_RECORD_OUTPUT_BUFFER*)buffer;
    buffer = outputBuffer->FileRecordBuffer;
}

static bool resolveFixup(FileRecordHeader& header) {
    uint16_t* usa = reinterpret_cast<uint16_t*>(&reinterpret_cast<char*>(&header)[header.updateSequenceOffset]);
    bool allUpdateSequenceNumbersMatched = true;
    int loopCount = std::min<int>(header.updateSequenceSize, 3);
    for (uint16_t i = 1; i < loopCount; i++) {
        auto offset = i * 512 - sizeof(uint16_t);
        uint16_t& check = *(uint16_t*)((char*)&header + offset);
        allUpdateSequenceNumbersMatched &= check == usa[0];
        check = usa[i];
    }
    return allUpdateSequenceNumbersMatched;
}

struct DataRun {
    struct Entry {
        uint32_t lcn;
        uint32_t clusterCount;
    };
    constexpr static Entry NullEntry = Entry{ 0,0 };

    RunHeader* curPtr;
    void* endPtr;
    Entry remainingEntry = NullEntry;
    uint32_t baseLcn = 0;

    DataRun(NonResidentAttributeHeader* attribute) {
        curPtr = (RunHeader*)((uint8_t*)attribute + attribute->dataRunsOffset);
        endPtr = (uint8_t*)attribute + attribute->length;
    }
    Entry getNextEntry(uint32_t clusterCountLimitPerEntry) {
        while (remainingEntry.clusterCount == 0) {
            if (curPtr >= endPtr || !curPtr->lengthFieldBytes)
                return NullEntry;

            uint32_t length = 0;
            int32_t offset = 0;

            for (int i = 0; i < curPtr->lengthFieldBytes; i++) {
                length |= uint32_t(((uint8_t*)curPtr)[1 + i]) << (i * 8);
            }
            for (int i = 0; i < curPtr->offsetFieldBytes; i++) {
                offset |= uint32_t(((uint8_t*)curPtr)[1 + curPtr->lengthFieldBytes + i]) << (i * 8);
            }
            if (offset & (uint32_t(1) << (curPtr->offsetFieldBytes * 8 - 1))) { // negative offset
                for (int i = curPtr->offsetFieldBytes; i < 4; i++) {
                    offset |= uint32_t(0xFF) << (i * 8);
                }
            }
            baseLcn += offset;
            remainingEntry.lcn = baseLcn;
            if (offset != 0) { // is not sparse
                remainingEntry.clusterCount = length;
            }
            curPtr = (RunHeader*)((uint8_t*)curPtr + 1 + curPtr->lengthFieldBytes + curPtr->offsetFieldBytes);
        }
        auto returnedClusterCount = std::min<uint32_t>(remainingEntry.clusterCount, clusterCountLimitPerEntry);
        auto entry = remainingEntry;
        remainingEntry.lcn += returnedClusterCount;
        remainingEntry.clusterCount -= returnedClusterCount;
        entry.clusterCount = returnedClusterCount;
        return entry;
    }
};

struct ThreadSafeFreeList {
    std::vector<void*> list;
    int size;
    int alignment;
    std::mutex mutex;

    ThreadSafeFreeList(int size, int alignment) : size(size), alignment(alignment) {}
    ThreadSafeFreeList(const ThreadSafeFreeList&) = delete;
    ThreadSafeFreeList& operator=(const ThreadSafeFreeList&) = delete;
    ~ThreadSafeFreeList() {
        std::unique_lock l{ mutex };
        while (!list.empty()) {
#if defined(COMPILER_MSVC) || defined(COMPILER_CLANG)
            _aligned_free(list.back());
#else
            std::free(list.back());
#endif
            list.pop_back();
        }
    }

    void* allocate() {
        std::unique_lock l{ mutex };
        if (!list.empty()) {
            auto ptr = list.back();
            list.pop_back();
            return ptr;
        }
#if defined(COMPILER_MSVC) || defined(COMPILER_CLANG)
        return _aligned_malloc(size, alignment);
#else
        return std::aligned_alloc(alignment, size);
#endif
    }
    void deallocate(void* ptr) {
        std::unique_lock l{ mutex };
        if (ptr)
            list.push_back(ptr);
    }
};

struct FileNameToIndex {
    std::string fileName;
    uint32_t index;

    bool operator==(const FileNameToIndex& other) const {
        return fileName == other.fileName;
    }
};
namespace std {
    template <> struct hash<FileNameToIndex> {
        std::size_t operator()(const FileNameToIndex& v) const {
            return hash<std::string>()(v.fileName);
        }
    };
}

static void readMft(ThreadSafeSerializableFileList& fileList, ThreadSafeVec<int>& dirRecordNumberToUniqueFileId, ThreadSafeVec<int>& uniqueFileIndToRecordNumber, ThreadSafeVec<float>& recordNumberToSize, ProgressInfo& progressInfo) {
    fileList.fileNameTable.addString("\0", 1);
    std::atomic<int> uniqueFileId = 1; // each file, directory and hard link gets unique id; root gets 0

    auto volume = CreateFileW(L"\\\\.\\C:", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    
    BootSector bootSector;
    readVolume(&bootSector, 0, sizeof(BootSector), volume);
    CloseHandle(volume);

    volume = CreateFileW(L"\\\\.\\C:", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, NULL);

    uint32_t clusterSizeInBytes = bootSector.bytesPerSector * bootSector.sectorsPerCluster;
    uint32_t fileRecordsPerCluster = clusterSizeInBytes / FileRecordSizeInBytes;

    int fileRecordLimit = 1 << 10;
    int clusterCountLimit = fileRecordLimit / fileRecordsPerCluster;
    ThreadSafeFreeList freeList(clusterCountLimit * clusterSizeInBytes, clusterSizeInBytes);

    auto mftFilePtr = (uint8_t*)freeList.allocate();
    readMftFileRecord(mftFilePtr, 0, volume);

    FileRecordHeader* fileRecord = (FileRecordHeader*)mftFilePtr;
    AttributeHeader* attribute = (AttributeHeader*)(mftFilePtr + fileRecord->firstAttributeOffset);
    NonResidentAttributeHeader* dataAttribute = nullptr;
    NonResidentAttributeHeader* bitmapAttribute = nullptr;

    while (true) {
        if (attribute->attributeType == 0x80) {
            dataAttribute = (NonResidentAttributeHeader*)attribute;
        } else if (attribute->attributeType == 0xB0) {
            bitmapAttribute = (NonResidentAttributeHeader*)attribute;
        } else if (attribute->attributeType == 0xFFFFFFFF) {
            break;
        }
        attribute = (AttributeHeader*)((uint8_t*)attribute + attribute->length);
    }

    uint64_t recordCount = bitmapAttribute->attributeSize * 8;
    progressInfo.recordCount = recordCount;
    FastThreadSafeishHashSet<FileNameToIndex> fileNameToPos(std::log2(recordCount));
    ThreadPool threadPool;
    auto dataRun = DataRun(dataAttribute);
    for (auto dataRunEntry = dataRun.getNextEntry(clusterCountLimit); dataRunEntry.clusterCount > 0; dataRunEntry = dataRun.getNextEntry(clusterCountLimit)) {
        threadPool.addTask([dataRunEntry, clusterSizeInBytes, fileRecordsPerCluster, volume, &fileNameToPos, &freeList, &dirRecordNumberToUniqueFileId, &uniqueFileIndToRecordNumber, &recordNumberToSize, &fileList, &uniqueFileId, &progressInfo]() {
            auto fileRecordBuffer = (uint8_t*)freeList.allocate();
            readVolume(fileRecordBuffer, uint64_t(dataRunEntry.lcn) * clusterSizeInBytes, dataRunEntry.clusterCount * clusterSizeInBytes, volume);

            int filesToLoad = dataRunEntry.clusterCount * fileRecordsPerCluster;
            for (int i = 0; i < filesToLoad; ++i) {
                FileRecordHeader* fileRecord = (FileRecordHeader*)(&fileRecordBuffer[FileRecordSizeInBytes * i]);

                if (!fileRecord->inUse || fileRecord->magic != 'ELIF')
                    continue;
                if (!resolveFixup(*fileRecord))
                    continue;

                std::array<char, 256 * 2> fileNameBuf;
                float fileSize = 0;
                FastSmallVector<FileInfo*> files;
                uint32_t modificationDateInMinutes = 0;
                AttributeHeader* attribute = (AttributeHeader*)((uint8_t*)fileRecord + fileRecord->firstAttributeOffset);
                while (attribute->attributeType != 0xFFFFFFFF && (uint8_t*)attribute - (uint8_t*)fileRecord < FileRecordSizeInBytes) {
                    if (attribute->attributeType == 0x10) { // $STANDARD_INFORMATION (used for modify date)
                        auto standardInfo = (StandardInformationAttribute*)attribute;
                        modificationDateInMinutes = uint32_t(standardInfo->fileAlteredTime / Date100nsTo1MinPrecisionFactor);
                    }
                    if (attribute->attributeType == 0x80) { // $DATA (used for file size)
                        if (attribute->nonResident) {
                            auto nonResidentAttribute = (NonResidentAttributeHeader*)attribute;
                            if (nonResidentAttribute->flags & 0x8000) { // sprase file
                                auto dataRun = DataRun(nonResidentAttribute);
                                for (auto dataRunEntry = dataRun.getNextEntry(std::numeric_limits<uint32_t>::max()); dataRunEntry.clusterCount > 0; dataRunEntry = dataRun.getNextEntry(std::numeric_limits<uint32_t>::max())) {
                                    fileSize += dataRunEntry.clusterCount * clusterSizeInBytes;
                                }
                            } else if (nonResidentAttribute->firstCluster == 0) {
                                fileSize += nonResidentAttribute->validDataLength;
                            }
                        } else {
                            fileSize += attribute->length - sizeof(ResidentAttributeHeader) - attribute->nameLength;
                        }
                    }
                    if (attribute->attributeType == 0x30) { // $FILE_NAME (used for file name and parent index)
                        FileNameAttributeHeader* fileNameAttribute = (FileNameAttributeHeader*)attribute;
                        if (fileNameAttribute->namespaceType != 2 && !fileNameAttribute->nonResident) {
                            FileInfo* file;
                            if (fileRecord->recordNumber == 5) { // root dir
                                dirRecordNumberToUniqueFileId[fileRecord->recordNumber] = 0;
                                uniqueFileIndToRecordNumber[0] = fileRecord->recordNumber;
                                file = fileList.files.addFile(0);
                            } else {
                                auto id = uniqueFileId++;
                                dirRecordNumberToUniqueFileId[fileRecord->recordNumber] = id;
                                uniqueFileIndToRecordNumber[id] = fileRecord->recordNumber;
                                file = fileList.files.addFile(id);
                            }
                            file->nameTableIndexAndInfo = (uint32_t(bool(fileRecord->isDirectory)) << 31u);
                            file->size = 0;
                            file->parentIndex = fileNameAttribute->parentRecordNumber;
                            int size = wideStringToUtf8(fileNameBuf.data(), int(fileNameBuf.size()), fileNameAttribute->fileName, fileNameAttribute->fileName + fileNameAttribute->fileNameLength);
                            if (fileNameBuf[0] == '.' && fileNameBuf[1] == '\0') {
                                fileNameBuf[0] = 'C';
                                fileNameBuf[1] = ':';
                                fileNameBuf[2] = '\0';
                                file->nameTableIndexAndInfo |= fileList.fileNameTable.addString(fileNameBuf.data(), 2);
                            } else {
                                FileNameToIndex fileNameToIndex = { std::string(fileNameBuf.data(), size), 0 };
                                if (auto ptr = fileNameToPos.find(fileNameToIndex); !ptr) {
                                    fileNameToIndex.index = fileList.fileNameTable.addString(fileNameBuf.data(), size);
                                    file->nameTableIndexAndInfo |= fileNameToIndex.index;
                                    fileNameToPos.emplace(std::move(fileNameToIndex));
                                } else {
                                    file->nameTableIndexAndInfo |= ptr->index;
                                }
                            }
                            files.push_back(file);
                        }
                    }
                    attribute = (AttributeHeader*)((uint8_t*)attribute + attribute->length);
                }
                for (auto& file : files) {
                    file->lastModificationDateInMinutes = modificationDateInMinutes;
                }
                auto baseNumber = uint32_t(fileRecord->baseFileRecordSegment);
                if (baseNumber) {
                    recordNumberToSize[baseNumber] += fileSize;
                } else {
                    recordNumberToSize[fileRecord->recordNumber] += fileSize;
                }
            }
            progressInfo.addRecordsProcessed(filesToLoad);
            freeList.deallocate(fileRecordBuffer);
        });
    }
    threadPool.wait();
    CloseHandle(volume);
}

static FileList getVolumeFileListWithMftParsing(std::atomic<double>& progress, uint64_t& outProcessedRecordCount) {
    ThreadSafeSerializableFileList serFileList;
    ThreadSafeVec<int> dirRecordNumberToUniqueFileId;
    ThreadSafeVec<int> uniqueFileIndToRecordNumber;
    ThreadSafeVec<float> recordNumberToSize;
    ProgressInfo progressInfo(progress);
    readMft(serFileList, dirRecordNumberToUniqueFileId, uniqueFileIndToRecordNumber, recordNumberToSize, progressInfo);
    outProcessedRecordCount = progressInfo.recordCount;

    FileList fileList;
    for (auto& block : serFileList.files.data.blocks) {
        if (block.get() == serFileList.files.data.blocks.back().get()) {
            fileList.files.insert(fileList.files.end(), block->begin(), block->begin() + (serFileList.files.size - fileList.files.size()) + 1);
        } else {
            fileList.files.insert(fileList.files.end(), block->begin(), block->end());
        }
    }

    for (auto& block : serFileList.fileNameTable.data.blocks) {
        if (block.get() == serFileList.fileNameTable.data.blocks.back().get()) {
            fileList.nameTable.append(block->begin(), block->begin() + (serFileList.fileNameTable.size - fileList.nameTable.size()) + 1);
        } else {
            fileList.nameTable.append(block->begin(), block->end());
        }
    }

    // update parent index to correct value and fill the size
    for (auto& file : fileList.files) {
        file.parentIndex = dirRecordNumberToUniqueFileId[file.parentIndex];
    }
    
    // fill sizes of files and compute sizes of directories
    for (int i = 0; i < fileList.files.size(); ++i) {
        if (!(fileList.files[i].nameTableIndexAndInfo >> 31)) {
            int index = i;
            float fileSize = fileList.files[index].size = recordNumberToSize[uniqueFileIndToRecordNumber[index]];
            while (fileList.files[index].parentIndex != index) {
                fileList.files[fileList.files[index].parentIndex].size += fileSize;
                index = fileList.files[index].parentIndex;
            }
        }
    }

    fileList.lowerNameTable = fileList.nameTable;
    fastBigStringToLower(fileList.lowerNameTable.data(), int(fileList.lowerNameTable.size()));

    return fileList;
}

