#include "utility.h"
#include "windowsInclude.h"
#include "fileReadingWithFindFirstFile.h"
#include "fileReadingWithMftParsing.h"
#include "fileListStoreAndLoadFromFile.h"
#include "fileIcons.h"
#include "fileSearching.h"
#include "imgui_directx11.h"

#include <array>
#include <cstdlib>
#include <charconv>
#include <future>
#include <iostream>
#include <map>
#include <shared_mutex>
#include <unordered_set>
#include <vector>
#include <algorithm>

std::string dateToStr(uint32_t lastModificationDateInMinutes) {
    auto date = uint64_t(lastModificationDateInMinutes) * Date100nsTo1MinPrecisionFactor;
    SYSTEMTIME systemTime;
    FILETIME filetime;
    filetime.dwLowDateTime = date & 0xffffffff;
    filetime.dwHighDateTime = date >> 32;
    FILETIME localfiletime;
    FileTimeToLocalFileTime(&filetime, &localfiletime);
    FileTimeToSystemTime(&localfiletime, &systemTime);

    auto toStringWith0Padding = [](int value) -> std::string {
        auto str = std::to_string(value);
        if (str.size() == 1)
            str = "0" + str;
        return str;
    };
    std::string result;
    result += toStringWith0Padding(systemTime.wDay) + '.' + toStringWith0Padding(systemTime.wMonth) + '.' + std::to_string(systemTime.wYear) + ' '
        + toStringWith0Padding(systemTime.wHour) + ':' + toStringWith0Padding(systemTime.wMinute);
    return result;
}

std::string doubleToString(double value, int precision) {
    std::array<char, 64> buf;
    auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), value, std::chars_format::fixed, precision);
    return std::string(buf.data(), ptr);
}

void updateFileList(FileList& fileList, FileList&& newFileList, FileListExtension& fileListExt, FileListSearchResults& shownResults) {
    std::unique_lock lg{ fileListExt.globalMutex };
    shownResults.count = 0;
    shownResults.indexes.resize(newFileList.files.size());
            
    fileList.files = std::move(newFileList.files);
    fileList.nameTable = std::move(newFileList.nameTable);
    fileList.lowerNameTable = std::move(newFileList.lowerNameTable);
        
    fileListExt.nameSortIndex.clear();
    fileListExt.sizeSortIndex.clear();
    fileListExt.dateSortIndex.clear();
}

void setImGuiStyle() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig config;
    config.GlyphOffset.y -= 2;
    static const ImWchar glyphRanges[] = {
        0x0020, 0xffff,
        0
    };
    config.GlyphRanges = glyphRanges;
    ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16 * 2, &config);
    if (font != NULL) {
        io.FontDefault = font;
    } else {
        io.Fonts->AddFontDefault();
    }
    io.Fonts->Build();

    ImGuiStyle* style = &ImGui::GetStyle();
    float hspacing = 8;
    float vspacing = 1;
    style->DisplaySafeAreaPadding = ImVec2(0, 0);
    style->WindowPadding = ImVec2(hspacing / 2, vspacing);
    style->FramePadding = ImVec2(hspacing, vspacing);
    style->ItemSpacing = ImVec2(hspacing, vspacing);
    style->ItemInnerSpacing = ImVec2(hspacing, vspacing);
    style->IndentSpacing = 20.0f;

    style->WindowRounding = 0.0f;
    style->FrameRounding = 0.0f;

    style->WindowBorderSize = 0.0f;
    style->FrameBorderSize = 1.0f;
    style->PopupBorderSize = 1.0f;

    style->ScrollbarSize = 15.0f;
    style->ScrollbarRounding = 0.0f;
    style->GrabMinSize = 15.0f;
    style->GrabRounding = 0.0f;

    ImVec4 transparent = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    ImVec4 background = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    ImVec4 text = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    ImVec4 border = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);

    style->Colors[ImGuiCol_Text] = text;
    style->Colors[ImGuiCol_CheckMark] = text;

    style->Colors[ImGuiCol_WindowBg] = background;
    style->Colors[ImGuiCol_ChildBg] = background;

    style->Colors[ImGuiCol_Border] = border;
    style->Colors[ImGuiCol_BorderShadow] = transparent;

    style->Colors[ImGuiCol_ButtonHovered].x *= 0.6f;
    style->Colors[ImGuiCol_ButtonHovered].y *= 0.6f;
    style->Colors[ImGuiCol_ButtonHovered].z *= 0.6f;

    style->Colors[ImGuiCol_ButtonActive].x *= 0.8f;
    style->Colors[ImGuiCol_ButtonActive].y *= 0.8f;
    style->Colors[ImGuiCol_ButtonActive].z *= 0.8f;

    style->Colors[ImGuiCol_TableHeaderBg] = style->Colors[ImGuiCol_Button];
    style->Colors[ImGuiCol_HeaderHovered] = style->Colors[ImGuiCol_ButtonHovered];
    style->Colors[ImGuiCol_HeaderActive] = style->Colors[ImGuiCol_ButtonActive];

    style->Colors[ImGuiCol_TableRowBg] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
    style->Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);

    style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.00f, 0.40f, 0.00f, 1.00f);
}

static std::vector<uint32_t> createNameSortIndex(const FileList& fileList, const std::string& lowerNameTable) {
    auto& files = fileList.files;
    std::vector<uint32_t> nameIndex(files.size());
    std::iota(nameIndex.begin(), nameIndex.end(), uint32_t(0));
    std::sort(std::execution::par, nameIndex.begin(), nameIndex.end(), [&](auto i, auto j) {
        return std::strcmp(&lowerNameTable[files[i].nameTableIndexAndInfo & 0x7fffffff], &lowerNameTable[files[j].nameTableIndexAndInfo & 0x7fffffff]) > 0;
    });
    return nameIndex;
}
static std::vector<uint32_t> createDateSortIndex(const FileList& fileList) {
    auto& files = fileList.files;
    std::vector<uint32_t> dateIndex(files.size());
    std::iota(dateIndex.begin(), dateIndex.end(), uint32_t(0));
    std::sort(std::execution::par, dateIndex.begin(), dateIndex.end(), [&](auto i, auto j) {
        return files[i].lastModificationDateInMinutes > files[j].lastModificationDateInMinutes;
    });
    return dateIndex;
}
static std::vector<uint32_t> createSizeSortIndex(const FileList& fileList) {
    auto& files = fileList.files;
    std::vector<uint32_t> sizeIndex(files.size());
    std::iota(sizeIndex.begin(), sizeIndex.end(), uint32_t(0));
    std::sort(std::execution::par, sizeIndex.begin(), sizeIndex.end(), [&](auto i, auto j) {
        return files[i].size > files[j].size;
    });
    return sizeIndex;
}

void refreshIndexesAsync(const FileList& fileList, FileListExtension& fileListExt, std::function<void(void)> notifySearchThread) {
    static std::future<void> refreshIndexesTask;
    if (refreshIndexesTask.valid())
        refreshIndexesTask.wait();
    refreshIndexesTask = std::async(std::launch::async, [notifySearchThread, &fileList, &fileListExt]() {
        {
            std::shared_lock lg{ fileListExt.globalMutex };
            ThreadPoolAsync tp;
            std::vector<uint32_t> sizeSortIndex, nameSortIndex, dateSortIndex;
            tp.addTask([&]() { sizeSortIndex = createSizeSortIndex(fileList); });
            tp.addTask([&]() { nameSortIndex = createNameSortIndex(fileList, fileList.lowerNameTable); });
            tp.addTask([&]() { dateSortIndex = createDateSortIndex(fileList); });
            tp.wait();
            {
                std::unique_lock li{ fileListExt.indexesMutex };
                fileListExt.sizeSortIndex = std::move(sizeSortIndex);
                fileListExt.nameSortIndex = std::move(nameSortIndex);
                fileListExt.dateSortIndex = std::move(dateSortIndex);
            }
        }
        notifySearchThread();
    });
}

bool haveAdminPrivileges() {
    bool result = false;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize)) {
            result = elevation.TokenIsElevated;
        }
    }
    if (hToken) {
        CloseHandle(hToken);
    }
    return result;
}

enum class ErrorType {
    None,
    PathNoLongerExists,
    FailedToRunExplorer,
    DeniedPrivileges,
    FailedToRunAsAdmin
};

ErrorType runRefreshFileTaskAsync(FileList& fileList, FileListExtension& fileListExt, FileListSearchResults& shownResults,
    std::atomic<double>& refreshProgress, std::atomic<double>& lastFileListCreateTime, uint64_t& processedRecordCount,
    std::function<void(void)> notifySearchThread, std::future<void>& saveFileListTask, char** argv
) {
    static std::future<void> refreshFileListTask;
    if (isRunning(refreshFileListTask))
        return ErrorType::None;
    refreshProgress = 0;
    if (haveAdminPrivileges()) {
        refreshFileListTask = std::async(std::launch::async, [&, notifySearchThread]() {
            lastFileListCreateTime = 0;
            auto timer = Timer();
            auto newFileList = getVolumeFileListWithMftParsing(refreshProgress, processedRecordCount);
            lastFileListCreateTime = timer.getTime();
            if (saveFileListTask.valid())
                saveFileListTask.wait();
            saveFileListTask = std::async(std::launch::async, [&fileListExt, newFileList]() {
                saveFileList("fileList", newFileList, fileListExt.fileListFileMutex);
            });
            updateFileList(fileList, std::move(newFileList), fileListExt, shownResults);
            refreshProgress = 0;
            notifySearchThread();
            refreshIndexesAsync(fileList, fileListExt, notifySearchThread);
        });
        return ErrorType::None;
    } else {
        RECT rect;
        std::string args = "-refreshFileList";
        if (GetWindowRect(MyImGui::hwnd, &rect)) {
            args += " " + std::to_string(rect.left);
            args += " " + std::to_string(rect.top);
            args += " " + std::to_string(rect.right - rect.left);
            args += " " + std::to_string(rect.bottom - rect.top);
        }
        auto errorCode = uint64_t(ShellExecuteA(nullptr, "runas", argv[0], args.c_str(), nullptr, SW_NORMAL));
        if (errorCode > 32) {
            std::quick_exit(0);
        } else if (SE_ERR_ACCESSDENIED) {
            return ErrorType::DeniedPrivileges;
        } else {
            return ErrorType::FailedToRunAsAdmin;
        }
    }
}

ErrorType runExplorer(const std::string& fullPath) {
    if (std::filesystem::exists(fullPath)) { // TODO: if non-latin characters it is wrong
        STARTUPINFO si;
        PROCESS_INFORMATION pi;
        std::wstring fullPathWideStr(1024, wchar_t(0));
        int argSize = ImTextStrFromUtf8((ImWchar*)fullPathWideStr.data(), int(fullPathWideStr.size()), fullPath.data(), fullPath.data() + fullPath.size());
        fullPathWideStr.resize(argSize);
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));
        auto explorerArg = std::wstring(L" /select, \"") + fullPathWideStr + L"\"";
        if (CreateProcessW(L"C:\\Windows\\explorer.exe", explorerArg.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return ErrorType::None;
        } else {
            return ErrorType::FailedToRunExplorer;
        }
    } else {
        return ErrorType::PathNoLongerExists;
    }
}

void setClipboardText(const std::string& text) {
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        auto clipbuffer = GlobalAlloc(GMEM_DDESHARE, text.size() + 1);
        auto buffer = (char*)GlobalLock(clipbuffer);
        strcpy(buffer, text.c_str());
        GlobalUnlock(clipbuffer);
        SetClipboardData(CF_TEXT, clipbuffer);
        CloseClipboard();
    }
}

std::optional<int> tryParseInt(char* str) {
    char* end;
    int result = int(std::strtol(str, &end, 10));
    if (str == end)
        return std::nullopt;
    return result;
}

/*
    TODO:
    - make slow option (findFirstFile) available
    - make findFirstFile use W option (maybe WEx? Saw somewhere it's faster)
    - search in swiftsearch how he discards $MFT and other such files
    - clean the code
    - make it search all volumes not just C:/
        - have to figure out how do I save/work with that. Might just work on them separetly and only combine when creating search results
*/

//#pragma comment(linker, "/SUBSYSTEM:console /ENTRY:main")
int main(int argc, char** argv) {
    std::string debugText = "";
    bool showDebugWindow = false;

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    FileList fileList;
    FileListExtension fileListExt;
    FileListSearchResults shownResults;
    char searchFileName[512] = { 0 };
    
    SearchSettings searchSettings;

    std::atomic<double> refreshProgress;
    std::future<void> saveFileListTask;

    std::string errorPopupFileName = "";

    bool firstFrame = true;
    bool setResultColumnsWidth = false;
    std::atomic<double> lastSearchTime = 0;
    std::atomic<double> lastFileListCreateTime = 0;
    std::atomic<bool> shouldRunSearchFunc = false;
    uint64_t processedRecordCount;
    std::mutex searchNotifyMutex;
    std::condition_variable searchNotifyCondVar;
    auto searchThreadHandle = std::thread([&] {
        searchThread(fileList, fileListExt, shownResults, searchFileName, searchSettings, lastSearchTime, shouldRunSearchFunc, searchNotifyMutex, searchNotifyCondVar);
    });

    auto notifySearchThread = [&searchNotifyMutex, &searchNotifyCondVar, &shouldRunSearchFunc] {
        {
            std::lock_guard l(searchNotifyMutex);
            shouldRunSearchFunc = true;
        }
        searchNotifyCondVar.notify_all();
    };

    auto loadListTask = std::async(std::launch::async, [&]() {
        auto newFileList = loadFileList("fileList", fileListExt.fileListFileMutex);
        updateFileList(fileList, std::move(newFileList), fileListExt, shownResults);
        notifySearchThread();
        refreshIndexesAsync(fileList, fileListExt, notifySearchThread);
    });

    ErrorType error = ErrorType::None;

    if (argc >= 2 && !strcmp(argv[1], "-refreshFileList")) {
        error = runRefreshFileTaskAsync(fileList, fileListExt, shownResults, refreshProgress, lastFileListCreateTime, processedRecordCount, notifySearchThread, saveFileListTask, argv);
    }
    
    int windowX = 100;
    int windowY = 100;
    int windowW = 1200;
    int windowH = 800;
    if (argc >= 6) {
        if (auto i = tryParseInt(argv[2]); i) windowX = *i;
        if (auto i = tryParseInt(argv[3]); i) windowY = *i;
        if (auto i = tryParseInt(argv[4]); i) windowW = *i;
        if (auto i = tryParseInt(argv[5]); i) windowH = *i;
    }
    MyImGui::Init(u"FastFileFinder", windowX, windowY, windowW, windowH, false);

    constexpr int MinFontSize = 16;
    constexpr int MaxFontSize = 35;
    int fontSize = 25;
    float fontScaleFactor = 0.625 / 20;

    auto& io = ImGui::GetIO();

    std::string itemFormatStr = "%s";
    for (int i = 0; i < 500; ++i) {
        itemFormatStr += ' ';
    }

    setImGuiStyle();

    bool errorPopupOpen = false;
    std::string errorMessage;

    MyImGui::Run([&] {
        io.FontGlobalScale = fontSize * fontScaleFactor;
        
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y));
        ImGui::Begin("Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        if (ImGui::GetIO().KeyCtrl) {
            if (ImGui::GetIO().MouseWheel > 0) {
                fontSize = std::clamp(fontSize + 1, MinFontSize, MaxFontSize);
            }
            if (ImGui::GetIO().MouseWheel < 0) {
                fontSize = std::clamp(fontSize - 1, MinFontSize, MaxFontSize);
            }
        }

        if (firstFrame)
            ImGui::SetKeyboardFocusHere();

        ImGui::PushItemWidth((ImGui::GetWindowWidth() - ImGui::GetStyle().ItemSpacing.x * 2) * 0.7f);
        if (ImGui::InputText("##SearchInput", searchFileName, sizeof(searchFileName))) {
            notifySearchThread();
        }
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (ImGui::Button("Refresh file list", ImVec2((ImGui::GetWindowWidth() - ImGui::GetStyle().ItemSpacing.x * 2) * 0.3f, 0))) {
            error = runRefreshFileTaskAsync(fileList, fileListExt, shownResults, refreshProgress, lastFileListCreateTime, processedRecordCount, notifySearchThread, saveFileListTask, argv);
        }

        if (ImGui::BeginTable("searchSettingsTable", 4, ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingStretchSame)) {
            auto columnFlags = ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoResize;
            ImGui::TableSetupColumn("##0", columnFlags);
            ImGui::TableSetupColumn("##1", columnFlags);
            ImGui::TableSetupColumn("##2", columnFlags);
            ImGui::TableSetupColumn("##3", columnFlags);

            ImGui::TableNextRow(ImGuiTableRowFlags_None, float(fontSize));
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Checkbox("Allow substring search", &searchSettings.allowSubstrings)) {
                notifySearchThread();
            }
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Checkbox("Case-sensitive search", &searchSettings.isCaseSensitive)) {
                notifySearchThread();
            }
            ImGui::TableSetColumnIndex(2);
            if (ImGui::Checkbox("Show files", &searchSettings.includeFiles)) {
                notifySearchThread();
            }
            ImGui::TableSetColumnIndex(3);
            if (ImGui::Checkbox("Show folders", &searchSettings.includeDirs)) {
                notifySearchThread();
            }

            ImGui::EndTable();
        }

        static int hoveredItem = 0;
        {
            std::shared_lock lg{ fileListExt.globalMutex };
            std::shared_lock ls{ fileListExt.searchResultsMutex };

            auto tableFlags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable 
                | ImGuiTableFlags_Reorderable | ImGuiTableFlags_ScrollY 
                | ImGuiTableFlags_Hideable | ImGuiTableFlags_RowBg  | ImGuiTableFlags_Sortable| ImGuiTableFlags_SortTristate;
            if (ImGui::BeginTable("searchResultsTable", 4, tableFlags, ImVec2(0.0f, -(fontSize * 2.5f)), 0.0)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_PreferSortDescending, 0.4f, 0);
                ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoSort, 0.6f, 1);
                ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_PreferSortDescending, (fontSize / 20.0f) * 65.0f, 2);
                ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_PreferSortDescending, (fontSize / 20.0f) * 120.0f, 3);
                ImGui::TableSetupScrollFreeze(0, 1);

                ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
                if (sortSpecs && sortSpecs->SpecsDirty) {
                    if (sortSpecs->SpecsCount == 0) {
                        searchSettings.index = SearchSettings::Index::Direct;
                        searchSettings.reverseIndex = false;
                    } else {
                        const ImGuiTableColumnSortSpecs& sortSpec = sortSpecs->Specs[0];
                        int delta = 0;
                        switch (sortSpec.ColumnUserID) {
                        case 0: searchSettings.index = SearchSettings::Index::Name; break;
                        case 2: searchSettings.index = SearchSettings::Index::Size; break;
                        case 3: searchSettings.index = SearchSettings::Index::Date; break;
                        default: break;
                        }
                        searchSettings.reverseIndex = sortSpec.SortDirection == ImGuiSortDirection_Ascending;
                    }
                    if (!firstFrame)
                        notifySearchThread();
                    sortSpecs->SpecsDirty = false;
                }

                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;
                clipper.Begin(shownResults.count);
                while (clipper.Step()) {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                        auto& result = fileList.files[shownResults.indexes[i]];
                        auto fullPath = fullFilePath(result, fileList);

                        ImGui::TableNextRow(ImGuiTableRowFlags_None, float(fontSize));

                        ImGui::TableSetColumnIndex(0);

                        bool isSelected = false;
                        ImGui::PushID(shownResults.indexes[i]);
                        if (ImGui::Selectable("##", isSelected, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, float(fontSize)))) {
                            error = runExplorer(fullPath);
                        }
                        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                            ImGui::OpenPopup("FileOptionsPopup");
                        }
                        if (ImGui::BeginPopup("FileOptionsPopup")) {
                            ImGui::SeparatorText(result.getName(fileList.nameTable));
                            if (ImGui::Selectable("Open")) {
                                system(std::string("\"" + fullPath + "\"").c_str());
                            }
                            if (ImGui::Selectable("Open containing folder")) {
                                error = runExplorer(fullPath);
                            }
                            if (ImGui::Selectable("Copy file name")) {
                                setClipboardText(result.getName(fileList.nameTable));
                            }
                            if (ImGui::Selectable("Copy path")) {
                                setClipboardText(fullPath);
                            }
                            ImGui::EndPopup();
                        }
                        ImGui::PopID();
                        ImGui::SameLine();

                        auto img = getIcon(result, fileList, fullPath);
                        ImGui::Image((void*)img.srv, ImVec2(float(fontSize), float(fontSize)));
                        ImGui::SameLine();
                        ImGui::Text("%s", result.getName(fileList.nameTable));

                        if (ImGui::TableSetColumnIndex(1)) {
                            ImGui::Text(itemFormatStr.c_str(), fullPath.c_str());
                        }

                        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(1, 0.5));
                        if (ImGui::TableSetColumnIndex(2)) {
                            if (result.size >= 1'000'000'000) {
                                ImGui::Selectable((doubleToString(result.size / 1'000'000'000, 1) + " GB").c_str(), false, ImGuiSelectableFlags_None);
                            } else if (result.size >= 1'000'000) {
                                ImGui::Selectable((doubleToString(result.size / 1'000'000, 1) + " MB").c_str(), false, ImGuiSelectableFlags_None);
                            } else {
                                ImGui::Selectable((doubleToString(result.size / 1'000, 1) + " KB").c_str(), false, ImGuiSelectableFlags_None);
                            }
                        }
                        if (ImGui::TableSetColumnIndex(3)) {
                            ImGui::Selectable(dateToStr(result.lastModificationDateInMinutes).c_str(), false, ImGuiSelectableFlags_None);
                        }
                        ImGui::PopStyleVar();
                    }
                }
                clipper.End();
                ImGui::EndTable();
            }
        }

        ImGui::Text("Last search time: %7.3f ms", lastSearchTime.load() * 1'000);
        ImGui::SameLine();
        std::string filesFoundText = std::to_string(shownResults.count) + " files found";
        auto posX = (ImGui::GetWindowWidth() - ImGui::CalcTextSize(filesFoundText.c_str()).x - ImGui::GetStyle().ItemSpacing.x);
        if (posX > ImGui::GetCursorPosX())
            ImGui::SetCursorPosX(posX);
        ImGui::Text("%s", filesFoundText.c_str());

        if (refreshProgress == 0 && lastFileListCreateTime != 0) {
            std::shared_lock lg{ fileListExt.globalMutex };
            std::string text = "Parsed MFT in " + doubleToString(lastFileListCreateTime, 3) + " [s] (speed of " + std::to_string(int(processedRecordCount / 1'000.0 / lastFileListCreateTime)) + " MB / s)";
            ImGui::ProgressBar(0, ImVec2(-1, 0), text.c_str());
        } else {
            ImGui::ProgressBar(float(refreshProgress), ImVec2(-1, 0));
        }

        if (error != ErrorType::None) {
            ImGui::OpenPopup("Error");
            errorPopupOpen = true;
        }
        if (ImGui::BeginPopupModal("Error", &errorPopupOpen)) {
            errorPopupOpen = true;
            if (error == ErrorType::PathNoLongerExists) {
                errorMessage = "This file no longer exists.\nYou need to refresh the file index";
            } else if (error == ErrorType::FailedToRunExplorer) {
                errorMessage = "Failed to run windows explorer";
            } else if (error == ErrorType::FailedToRunAsAdmin) {
                errorMessage = "Failed to run as administrator";
            } else if (error == ErrorType::DeniedPrivileges) {
                errorMessage = "You have to grant admin privileges to create fileList";
            }
            error = ErrorType::None;
            ImGui::Text("%s", errorMessage.c_str());
            ImGui::EndPopup();
        }

        ImGui::End();

        if (showDebugWindow) {
            ImGui::Begin("Debug window");
            ImGui::InputTextMultiline("##", (char*)debugText.c_str(), debugText.size());
            ImGui::End();
        }

        firstFrame = false;
    });
    std::quick_exit(0);
}


