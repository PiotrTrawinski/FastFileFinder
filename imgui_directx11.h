#pragma once
#define _CRT_SECURE_NO_WARNINGS
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui.cpp"
#include "imgui_demo.cpp"
#include "imgui_draw.cpp"
#include "imgui_widgets.cpp"
#include "imgui_tables.cpp"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.cpp"
#include "backends/imgui_impl_dx11.cpp"
#define STB_IMAGE_IMPLEMENTATION
//#include "stb_image.h"
#include <d3d11.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <tchar.h>
#pragma comment(lib, "d3d11.lib")

#include <tuple>
#include <string>
#include <optional>
#include <chrono>
#include <thread>

namespace MyImGui {
    static ID3D11Device*           g_pd3dDevice = nullptr;
    static ID3D11DeviceContext*    g_pd3dDeviceContext = nullptr;
    static IDXGISwapChain*         g_pSwapChain = nullptr;
    static UINT                    g_ResizeWidth = 0, g_ResizeHeight = 0;
    static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
    static bool                    g_wndMinimized = false;

    void CreateRenderTarget() {
        ID3D11Texture2D* pBackBuffer;
        g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }

    bool CreateDeviceD3D(HWND hWnd, bool useWarp) {
        // Setup swap chain
        DXGI_SWAP_CHAIN_DESC sd;
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = 2;
        sd.BufferDesc.Width = 0;
        sd.BufferDesc.Height = 0;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hWnd;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT createDeviceFlags = 0;
        //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
        D3D_FEATURE_LEVEL featureLevel;
        const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
        HRESULT res = useWarp ? DXGI_ERROR_UNSUPPORTED : D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
        if (res == DXGI_ERROR_UNSUPPORTED) { // Try high-performance WARP software driver if hardware is not available.
            res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
        }
        if (res != S_OK)
            return false;

        CreateRenderTarget();
        return true;
    }

    void CleanupRenderTarget() {
        if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
    }

    void CleanupDeviceD3D() {
        CleanupRenderTarget();
        if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
        if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
        if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
    }

    LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;

        switch (msg) {
        case WM_SIZE:
            g_wndMinimized = (wParam == SIZE_MINIMIZED);
            if (wParam == SIZE_MINIMIZED)
                return 0;
            g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
            g_ResizeHeight = (UINT)HIWORD(lParam);
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
                return 0;
            break;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        }
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    static bool INIT(const std::u16string& windowName, WNDCLASSEXW& wc_out, HWND& hwnd_out, int startX, int startY, int width, int height, bool useWarp) {
        // Create application window
        //ImGui_ImplWin32_EnableDpiAwareness();
        WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui", nullptr };
        ::RegisterClassExW(&wc);

        hwnd_out = ::CreateWindowW(wc.lpszClassName, (LPCWSTR)windowName.c_str(), WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX | WS_SIZEBOX | WS_MAXIMIZEBOX, startX, startY, width, height, nullptr, nullptr, wc.hInstance, nullptr);

        // Initialize Direct3D
        if (!CreateDeviceD3D(hwnd_out, useWarp)) {
            CleanupDeviceD3D();
            ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return false;
        }

        // Show the window
        ::ShowWindow(hwnd_out, SW_NORMAL);
        ::UpdateWindow(hwnd_out);

        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();
        //ImGui::StyleColorsLight();

        ImGui::GetIO().IniFilename = nullptr;

        // Setup Platform/Renderer backends
        ImGui_ImplWin32_Init(hwnd_out);
        ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

        return true;
    }

    struct Image {
        ID3D11ShaderResourceView* srv;
        int width;
        int height;
    };

    static std::optional<Image> createTextureFromRGBA(void* data, int width, int height) {
        Image img;
        img.width = width;
        img.height = height;

        // Create texture
        D3D11_TEXTURE2D_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Width = img.width;
        desc.Height = img.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;

        ID3D11Texture2D* pTexture = NULL;
        D3D11_SUBRESOURCE_DATA subResource;
        subResource.pSysMem = data;
        subResource.SysMemPitch = desc.Width * 4;
        subResource.SysMemSlicePitch = 0;
        ImGui_ImplDX11_GetBackendData()->pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);

        // Create texture view
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;
        ImGui_ImplDX11_GetBackendData()->pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, &img.srv);
        pTexture->Release();

        return img;
    }

    //static std::optional<Image> loadTextureFromFile(std::string filename) {
    //    int width, height;

    //    auto imageData = stbi_load(filename.c_str(), &width, &height, NULL, STBI_rgb_alpha);
    //    if (imageData == NULL)
    //        return std::nullopt;

    //    auto img = createTextureFromRGBA(imageData, width, height);

    //    stbi_image_free(imageData);

    //    return img;
    //}

    static WNDCLASSEXW wc;
    static HWND hwnd;

    static bool Init(const std::u16string& windowName, int startX=100, int startY=100, int width=800, int height=600, bool useWarp=true) {
        if (!MyImGui::INIT(windowName, wc, hwnd, startX, startY, width, height, useWarp))
            return false;
        return true;
    }

    static HWND Hwnd() {
        return hwnd;
    }

    static void _dummyInitFunction(HWND hwnd) {}
    template<typename Function, typename InitFunction=void(HWND)> static bool Run(Function function, InitFunction initFunction= _dummyInitFunction) {
        initFunction(hwnd);

        // Main loop
        //ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
        ImVec4 transparent_color = ImVec4(0, 0, 0, 0.5);
        MSG msg;
        ZeroMemory(&msg, sizeof(msg));
        //MyImGui::g_pSwapChain->SetFullscreenState(true, nullptr);
        while (msg.message != WM_QUIT) {
            // Poll and handle messages (inputs, window resize, etc.)
            // See the WndProc() function below for our to dispatch events to the Win32 backend.
            if (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
                continue;
            }

            if (g_wndMinimized) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            // Handle window resize (we don't resize directly in the WM_SIZE handler)
            if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
                CleanupRenderTarget();
                g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
                g_ResizeWidth = g_ResizeHeight = 0;
                CreateRenderTarget();
            }

            // Start the Dear ImGui frame
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            function();

            // Rendering
            ImGui::Render();
            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
            //const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
            //g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
            g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, (float*)&transparent_color);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

            g_pSwapChain->Present(1, 0); // Present with vsync
            //g_pSwapChain->Present(0, 0); // Present without vsync
        }

        // Cleanup
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        CleanupDeviceD3D();
        ::DestroyWindow(hwnd);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

        return true;
    }
}

