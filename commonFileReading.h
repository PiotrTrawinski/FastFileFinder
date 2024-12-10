#pragma once

#include "utility.h"

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <shared_mutex>

constexpr inline uint32_t Date100nsTo1MinPrecisionFactor = 600'000'000;

#pragma pack(push, 1)
struct FileInfo {
    uint32_t parentIndex;
    float size = 0;
    uint32_t nameTableIndexAndInfo; // First bit is used to signify if the file is directory or not. 0 means its file, 1 means its directory
    uint32_t lastModificationDateInMinutes;
    
    const char* getName(const std::string& fileNameTable) const {
        return &fileNameTable[nameTableIndexAndInfo & 0x7fffffff];
    }
    bool isDir() const {
        return nameTableIndexAndInfo >> 31;
    }
};
#pragma pack(pop)

struct FileList {
    std::vector<FileInfo> files;
    std::string nameTable;
    std::string lowerNameTable;
};

struct FileListExtension {
    std::vector<uint32_t> sizeSortIndex;
    std::vector<uint32_t> nameSortIndex;
    std::vector<uint32_t> dateSortIndex;
    std::shared_mutex globalMutex;
    std::shared_mutex searchResultsMutex;
    std::shared_mutex indexesMutex;
    std::mutex fileListFileMutex;
};

struct ThreadSafeNameTable {
    ThreadSafeVec<char> data;
    std::atomic<int> lastWritePosition = 0;
    std::atomic<int> size = 0;

    int addString(const char* str, int length) {
        int position = lastWritePosition.fetch_add(length + 1);
        atomicMax<int>(size, position + length + 1);

        auto [blockId, posInBlock] = data.getBlockIdAndPosInBlock(position);
        if (blockId < data.blockCount && posInBlock + length + 1 < data.blocks[blockId]->size()) {
            memcpy(&(*data.blocks[blockId])[posInBlock], str, length);
            (*data.blocks[blockId])[posInBlock + length] = '\0';
        } else {
            for (int i = 0; i < length; ++i) {
                data[position + i] = str[i];
            }
            data[position + length] = '\0';
        }
        return position;
    }
};

struct ThreadSafeFileList {
    ThreadSafeVec<FileInfo> data;
    std::atomic<int> size = 0;

    FileInfo* addFile(int index) {
        atomicMax<int>(size, index);
        return &data[index];
    }
};

struct ThreadSafeSerializableFileList {
    ThreadSafeNameTable fileNameTable;
    ThreadSafeFileList files;
};

static std::string fullFilePath(FileInfo& file, FileList& fileList) {
    std::string result(file.getName(fileList.nameTable));
    if (file.nameTableIndexAndInfo == fileList.files[0].nameTableIndexAndInfo) // is root
        return result;
    auto f = file;
    bool isRootDir = false;
    while (!isRootDir) {
        isRootDir = f.parentIndex == 0;
        f = fileList.files[f.parentIndex];
        result.insert(0, "\\");
        result.insert(0, f.getName(fileList.nameTable));
    }
    return result;
}
