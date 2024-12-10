#pragma once

#include "compression.h"
#include "commonFileReading.h"
#include "utility.h"

#include <fstream>
#include <cstdint>
#include <numeric>
#include <algorithm>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <execution>

static void saveFileList(const std::string& fileName, const FileList& fileList, std::mutex& mutex) {
    int32_t fileCount = int32_t(fileList.files.size());
    int32_t nameTableSize = int32_t(fileList.nameTable.size());
    int32_t size = fileCount * sizeof(fileList.files[0]) + nameTableSize;
    int32_t filesDataOffset = 0;
    int32_t fileNameTableOffset = filesDataOffset + fileCount * sizeof(fileList.files[0]);

    std::vector<char> data(size);
    std::copy((char*)fileList.files.data(), (char*)(fileList.files.data() + fileCount), data.data() + filesDataOffset);
    std::copy(fileList.nameTable.data(), fileList.nameTable.data() + nameTableSize, data.data() + fileNameTableOffset);

    auto [compressedData, compressedSize] = compress(data.data(), size);

    std::lock_guard l{ mutex };
    std::ofstream fileOut(fileName, std::ios::binary);
    fileOut.write((char*)&size, sizeof(size));
    fileOut.write((char*)&compressedSize, sizeof(compressedSize));
    fileOut.write((char*)&fileCount, sizeof(fileCount));
    fileOut.write((char*)&nameTableSize, sizeof(nameTableSize));
    fileOut.write((char*)&filesDataOffset, sizeof(filesDataOffset));
    fileOut.write((char*)&fileNameTableOffset, sizeof(fileNameTableOffset));
    fileOut.write(compressedData.data(), compressedSize);
}

static FileList loadFileList(const std::string& fileName, std::mutex& mutex) {
    FileList fileList;
    int32_t originalSize, compressedSize, fileCount, nameTableSize, filesDataOffset, fileNameTableOffset;
    std::vector<char> compressedData;
    
    {
        std::lock_guard l{ mutex };
        std::ifstream fileIn(fileName, std::ios::binary);
        if (!fileIn)
            return fileList;

        fileIn.read((char*)&originalSize, sizeof(originalSize));
        fileIn.read((char*)&compressedSize, sizeof(compressedSize));
        fileIn.read((char*)&fileCount, sizeof(fileCount));
        fileIn.read((char*)&nameTableSize, sizeof(nameTableSize));
        fileIn.read((char*)&filesDataOffset, sizeof(filesDataOffset));
        fileIn.read((char*)&fileNameTableOffset, sizeof(fileNameTableOffset));

        compressedData.resize(compressedSize);
        fileIn.read(compressedData.data(), compressedSize);
    }

    auto data = decompress(compressedData.data(), originalSize);

    auto sizeOfFileData = fileCount * sizeof(fileList.files[0]);
    fileList.files.resize(fileCount);
    fileList.nameTable.resize(nameTableSize);
    std::copy(data.data() + filesDataOffset, data.data() + filesDataOffset + sizeOfFileData, (char*)fileList.files.data());
    std::copy(data.data() + fileNameTableOffset, data.data() + fileNameTableOffset + nameTableSize, fileList.nameTable.data());

    fileList.lowerNameTable = fileList.nameTable;
    fastBigStringToLower(fileList.lowerNameTable.data(), int(fileList.lowerNameTable.size()));

    return fileList;
}
