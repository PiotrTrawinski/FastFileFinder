#pragma once

#include "commonFileReading.h"
#include "utility.h"

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <future>
#include <atomic>
#include <string_view>
#include <iostream>

struct FileListSearchResults {
    std::vector<uint32_t> indexes;
    int count = 0;
};

struct SearchSettings {
    enum class Index {
        Direct = 0,
        Name = 1,
        Size = 2,
        Date = 3
    };
    bool allowSubstrings = true;
    bool isCaseSensitive = false;
    bool includeFiles = true;
    bool includeDirs = true;
    bool reverseIndex = false;
    Index index = Index::Direct;
};

static std::vector<std::string> splitPath(std::string_view str) {
    std::vector<std::string> result;
    for (std::size_t pos; (pos = str.find_last_of("/\\")) != std::string::npos;) {
        result.emplace_back(str.substr(pos + 1));
        str = str.substr(0, pos);
    }
    result.emplace_back(str);
    return result;
}

static bool compareStrToDir(const char* str, const std::string& dir) {
    return !strcmp(str, dir.c_str());
}

template<typename Index> static void findFilesWithString(FileListSearchResults& results, FileList& fileList, const Index& sortIndex, const std::string& str, SearchSettings searchSettings, ThreadPool& threadPool, std::atomic<bool>& cancelSearch) {
    auto& files = fileList.files;

    std::string searchString = str;
    if (!searchSettings.isCaseSensitive) {
        fastBigStringToLower(searchString.data(), int(searchString.size()));
    }
    auto path = splitPath(searchString);

    DynamicBitset toAddMap(int(files.size()));
    int stepSize = toAddMap.IntTypeBitSize * 1024;
    for (int i = 0; i < files.size(); i += stepSize) {
        threadPool.addTask([startIndex=i, stepSize, &path, &cancelSearch, &searchSettings, &files, &fileList, &toAddMap]() {
            int endIndex = std::min(startIndex + stepSize, int(files.size()));
            for (int i = startIndex; i < endIndex; ++i) {
                if (cancelSearch)
                    return;
                auto& file = files[i];
        
                if (!searchSettings.includeDirs && file.isDir())
                    continue;
                if (!searchSettings.includeFiles && !file.isDir())
                    continue;

                if (path.size() == 1 && path[0].size() == 0) {
                    toAddMap.set(i);
                    continue;
                }

                const char* fileName = file.getName(searchSettings.isCaseSensitive ? fileList.nameTable : fileList.lowerNameTable);
                if (searchSettings.allowSubstrings) {
                    if (!strstr(fileName, path[0].c_str())) {
                        continue;
                    }
                } else {
                    if (strncmp(fileName, path[0].c_str(), path[0].size())) {
                        continue;
                    }
                }

                if (path.size() >= 2) {
                    auto index = file.parentIndex;
                    while (true) {
                        bool isInDir = false;
                        while (true) {
                            if (compareStrToDir(files[index].getName(searchSettings.isCaseSensitive ? fileList.nameTable : fileList.lowerNameTable), path[1])) {
                                isInDir = true;
                                break;
                            }
                            if (index == files[index].parentIndex)
                                break;
                            index = files[index].parentIndex;
                        }
                        if (!isInDir)
                            goto ContinueMainLoop;

                        bool pathMatches = true;
                        for (int i = 2; i < path.size(); ++i) {
                            index = files[index].parentIndex;
                            if (strcmp(files[index].getName(searchSettings.isCaseSensitive ? fileList.nameTable : fileList.lowerNameTable), path[i].c_str())) {
                                pathMatches = false;
                                break;
                            }
                        }
                        if (pathMatches)
                            break;
                    }
                }
                toAddMap.set(i);
            ContinueMainLoop: (void)0;
            }
        });
    }
    threadPool.wait();

    if (searchSettings.reverseIndex) {
        for (int i = int(files.size() - 1); i >= 0; --i) {
            if (cancelSearch)
                return;
            auto index = sortIndex[i];
            if (toAddMap.test(index)) {
                results.indexes[results.count++] = index;
            }
        }
    } else {
        for (int i = 0; i < files.size(); ++i) {
            if (cancelSearch)
                return;
            auto index = sortIndex[i];
            if (toAddMap.test(index)) {
                results.indexes[results.count++] = index;
            }
        }
    }
}

static void searchThread(FileList& fileList, FileListExtension& fileListExt, FileListSearchResults& shownResults, 
    char (&searchFileName)[512], SearchSettings& searchSettings, std::atomic<double>& searchTime, 
    std::atomic<bool>& shouldRunSearch, std::mutex& searchNotifyMutex, std::condition_variable& searchNotifyCondVar
) {
    std::atomic<bool> cancelSearch = false;
    std::thread fileSearchTask;
    ThreadPool threadPool(32);
    while (true) {
        std::unique_lock l{ searchNotifyMutex };
        searchNotifyCondVar.wait(l, [&shouldRunSearch] { return bool(shouldRunSearch); });
        shouldRunSearch = false;
        l.unlock();

        cancelSearch = true;
        if (fileSearchTask.joinable())
            fileSearchTask.join();

        cancelSearch = false;
        static FileListSearchResults workShownResults;

        fileSearchTask = std::thread([&]() {
            std::shared_lock lg{ fileListExt.globalMutex };
            std::shared_lock li{ fileListExt.indexesMutex };
            if (workShownResults.indexes.size() != shownResults.indexes.size()) {
                workShownResults.indexes.resize(shownResults.indexes.size());
            }
            workShownResults.count = 0;
            auto timer = Timer();
            std::vector<uint32_t>* sortIndex = nullptr;
            switch (searchSettings.index) {
            case SearchSettings::Index::Direct: sortIndex = nullptr; break;
            case SearchSettings::Index::Name: sortIndex = &fileListExt.nameSortIndex; break;
            case SearchSettings::Index::Size: sortIndex = &fileListExt.sizeSortIndex; break;
            case SearchSettings::Index::Date: sortIndex = &fileListExt.dateSortIndex; break;
            }
            if (sortIndex == nullptr) {
                struct DummyDirectIndex {
                    uint32_t operator[](uint32_t i) const { return i; }
                } dummyDirectIndex;
                findFilesWithString(workShownResults, fileList, dummyDirectIndex, std::string(searchFileName), searchSettings, threadPool, cancelSearch);
            } else if (sortIndex->size() != fileList.files.size()) {
                return;
            } else {
                findFilesWithString(workShownResults, fileList, *sortIndex, std::string(searchFileName), searchSettings, threadPool, cancelSearch);
            }
            if (cancelSearch)
                return;
            searchTime = timer.getTime();
            std::unique_lock ls{ fileListExt.searchResultsMutex };
            shownResults.indexes.swap(workShownResults.indexes);
            shownResults.count = workShownResults.count;
        });
    }
}
