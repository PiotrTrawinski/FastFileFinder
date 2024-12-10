#pragma once

#include "windowsInclude.h"
#include "commonFileReading.h"

#include <vector>
#include <atomic>
#include <map>

struct ProgressInfoBySize {
    std::atomic<double>& progress;
    double totalSize;
    std::atomic<int64_t> sizeFound = 0;

    int64_t getTakenSpaceSize() {
        DWORD sectorsPerCluster;
        DWORD bytesPerSector;
        DWORD numberOfFreeClusters;
        DWORD numberOfClusters;
        GetDiskFreeSpaceA("C:\\", &sectorsPerCluster, &bytesPerSector, &numberOfFreeClusters, &numberOfClusters);
        int64_t clusterSize = bytesPerSector * sectorsPerCluster;
        int64_t takenSpace = numberOfClusters * clusterSize - numberOfFreeClusters * clusterSize;
        return takenSpace;
    }

    ProgressInfoBySize(std::atomic<double>& progress) : progress(progress), totalSize(double(getTakenSpaceSize())) {}
    void addSize(float size) {
        sizeFound += int64_t(size);
        progress = sizeFound / totalSize;
        static int64_t prevWrittenSize = 0;
        if (sizeFound - prevWrittenSize >= 10'000'000'000) {
            prevWrittenSize = sizeFound;
        }
    }
};

static float iterateDirWithFindFirstFile(const std::string& dirPath, ThreadSafeSerializableFileList& fileList, uint32_t parentId, std::atomic<int>& id, ThreadPool& threadPool, std::mutex& mutex, std::vector<uint32_t>& addToParentSizeIds, ProgressInfoBySize& progressInfo, int level = 0) {
    WIN32_FIND_DATAA ffd;
    auto hFind = FindFirstFileA((dirPath + "\\*").c_str(), &ffd);
    if (INVALID_HANDLE_VALUE == hFind) {
        return 0;
    }
    float sizeSum = 0;
    do {
        if (!strcmp(ffd.cFileName, ".") || !strcmp(ffd.cFileName, ".."))
            continue;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT && ffd.dwReserved0 == IO_REPARSE_TAG_SYMLINK)
            continue;

        int index = id++;
        auto file = fileList.files.addFile(index);

        std::string fileName(ffd.cFileName);
        file->nameTableIndexAndInfo = fileList.fileNameTable.addString(fileName.data(), int(fileName.size()));
        file->parentIndex = parentId;
        file->lastModificationDateInMinutes = ((uint64_t(ffd.ftLastWriteTime.dwHighDateTime) << 32) + ffd.ftLastWriteTime.dwLowDateTime) / Date100nsTo1MinPrecisionFactor;
        auto fullFileName = dirPath + "\\" + fileName; // TODO: can make it faster with static buffer instead of creating new string every time
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            file->nameTableIndexAndInfo |= (1 << 31u);
            if (level == 4) {
                addToParentSizeIds.emplace_back(index);
                threadPool.addTask([fullFileName, &fileList, file, index, &id, &threadPool, &mutex, &addToParentSizeIds, &progressInfo, level]() {
                    file->size = iterateDirWithFindFirstFile(fullFileName, fileList, index, id, threadPool, mutex, addToParentSizeIds, progressInfo, level + 1);
                    });
            } else {
                file->size = iterateDirWithFindFirstFile(fullFileName, fileList, index, id, threadPool, mutex, addToParentSizeIds, progressInfo, level + 1);
                sizeSum += file->size;
            }
        } else {
            LARGE_INTEGER filesize;
            filesize.LowPart = ffd.nFileSizeLow;
            filesize.HighPart = ffd.nFileSizeHigh;
            file->size = float(filesize.QuadPart);
            sizeSum += file->size;
            progressInfo.addSize(file->size);
        }
    } while (FindNextFileA(hFind, &ffd) != 0);

    //if (GetLastError() != ERROR_NO_MORE_FILES) {
    //    std::cerr << "ERROR_NO_MORE_FILES\n";
    //}
    FindClose(hFind);
    return sizeSum;
}

static FileList getVolumeFileListWithFindFirstFile(std::atomic<double>& progress) {
    ThreadSafeSerializableFileList serFileList;

    std::atomic<int> id = 0;
    auto firstFile = serFileList.files.addFile(0);
    serFileList.fileNameTable.addString("C:", 2);
    firstFile->parentIndex = 0;
    firstFile->nameTableIndexAndInfo = (1 << 31u);
    id++;
    ThreadPool threadPool;
    std::vector<uint32_t> addToParentSizeIds;
    ProgressInfoBySize progressInfo(progress);
    std::mutex m;
    firstFile->size = iterateDirWithFindFirstFile("C:", serFileList, 0, id, threadPool, m, addToParentSizeIds, progressInfo, 0);
    threadPool.wait();

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

    std::map<uint32_t, float> parentToSize;
    for (auto idx : addToParentSizeIds) {
        parentToSize[idx] = fileList.files[idx].size;
    }
    while (!parentToSize.empty()) {
        auto parentToSizeCopy = parentToSize;
        parentToSize.clear();
        for (auto [idx, size] : parentToSizeCopy) {
            if (idx == 0) // root
                continue;
            auto newIdx = fileList.files[idx].parentIndex;
            parentToSize[newIdx] += size;
            fileList.files[newIdx].size += size;
        }
    }

    fileList.lowerNameTable = fileList.nameTable;
    fastBigStringToLower(fileList.lowerNameTable.data(), int(fileList.lowerNameTable.size()));

    return fileList;
}

