#pragma once

#include "windowsInclude.h"
#include "commonFileReading.h"
#include "imgui_directx11.h"

#include <string>
#include <optional>
#include <filesystem>
#include <thread>

static void saveIconInFile(std::string fileName, HICON hicon) {
    Gdiplus::Bitmap bitmap(hicon);
    const CLSID pngEncoderClsId = { 0x557cf406, 0x1a04, 0x11d3,{ 0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e } };
    bitmap.Save(std::wstring(fileName.begin(), fileName.end()).c_str(), &pngEncoderClsId);
}

static void convertBGRAtoRGBAandChangeBlackToAlpha(char* data, int pixelCount) {
    for (int i = 0; i < pixelCount; ++i) {
        if (data[i * 4] == 0 && data[i * 4 + 1] == 0 && data[i * 4 + 2] == 0)
            data[i * 4 + 3] = 0;
        std::swap(data[i * 4], data[i * 4 + 2]);
    }
}

static std::optional<MyImGui::Image> hiconToImage(HICON hicon) {
    char tmpDataBuf[32 * 32 * 4];

    Gdiplus::BitmapData bmData;
    bmData.Width = 32;
    bmData.Height = 32;
    bmData.Stride = 128;
    bmData.PixelFormat = PixelFormat32bppARGB;
    bmData.Scan0 = tmpDataBuf;
    Gdiplus::Rect rect(0, 0, 32, 32);

    // TODO: It seems to actually be BGRA format with alpha not working (it's black instead)
    Gdiplus::Bitmap bitmap(hicon);
    bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeUserInputBuf, PixelFormat32bppARGB, &bmData);
    convertBGRAtoRGBAandChangeBlackToAlpha((char*)bmData.Scan0, 32 * 32);

    return MyImGui::createTextureFromRGBA(bmData.Scan0, 32, 32);
}

static MyImGui::Image getIcon(const FileInfo& fileInfo, FileList& fileList, const std::string& fullPath) {
    //std::filesystem::path path(fileInfo.name);
    std::filesystem::path path(fileInfo.getName(fileList.nameTable));
    auto ext = std::string((char*)path.extension().u8string().c_str());
    static std::optional<MyImGui::Image> defaultFileImg;
    static std::optional<MyImGui::Image> defaultExeImg;
    static int defaultFileIicon = -1;

    if (!defaultFileImg) {
        SHFILEINFOA sfi;
        auto hImageList = SHGetFileInfoA(".fastFileFinder_123456789", 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_USEFILEATTRIBUTES);
        defaultFileImg = hiconToImage(sfi.hIcon);
        defaultFileIicon = sfi.iIcon;
    }
    if (!defaultExeImg) {
        SHSTOCKICONINFO out;
        out.cbSize = sizeof(out);
        SHGetStockIconInfo(SIID_APPLICATION, SHGSI_ICON, &out);
        defaultExeImg = hiconToImage(out.hIcon);
    }

    if (fileInfo.isDir()) {
        static std::optional<MyImGui::Image> img;
        if (!img) {
            SHSTOCKICONINFO out;
            out.cbSize = sizeof(out);
            SHGetStockIconInfo(SIID_FOLDER, SHGSI_ICON, &out);
            img = hiconToImage(out.hIcon);
        }
        return *img;
    } else if (ext.empty()) {
        return *defaultFileImg;
    } else {
        static std::unordered_map<std::string, std::optional<MyImGui::Image>> imgMap;
        if (ext == ".exe") {
            auto& img = imgMap[fullPath];
            if (img)
                return *img;
            img = defaultExeImg;
            std::thread([&, fullPath = fullPath]() {
                SHFILEINFOA sfi;
                auto hImageList = SHGetFileInfoA(fullPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_USEFILEATTRIBUTES);
                auto hicon = sfi.hIcon;
                if (hicon) {
                    // TODO: technically not thread-safe - other thread might read that img while this is assigned
                    // however in practice it boils down to pointer assignemnt which will be done atomically and thus always work correctly
                    img = hiconToImage(hicon);
                }
                }).detach();
                return *img;
        } else {
            auto& img = imgMap[ext];
            if (img)
                return *img;
            img = defaultFileImg;
            std::thread([&, ext = ext]() {
                SHFILEINFOA sfi;
                auto hImageList = SHGetFileInfoA((char*)ext.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_USEFILEATTRIBUTES);
                if (hImageList && sfi.iIcon != defaultFileIicon) {
                    // TODO: technically not thread-safe - other thread might read that img while this is assigned
                    // however in practice it boils down to pointer assignemnt which will be done atomically and thus always work correctly
                    img = hiconToImage(sfi.hIcon);
                }
                }).detach();
                return *img;
        }
    }
}
