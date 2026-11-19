#include "engine/render/dx11.hpp"

#include "engine/log.hpp"

#include <wincodec.h>

#include <algorithm>
#include <filesystem>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#pragma comment(lib, "windowscodecs.lib")

namespace shogod::render {

bool context::init(ID3D11Device* device, ID3D11DeviceContext* device_context, HWND hwnd,
                   float ui_scale) {
    if (ready_) return true;
    if (!device || !device_context || !hwnd) return false;

    device_ = device;
    ctx_ = device_context;
    hwnd_ = hwnd;
    scale_ = ui_scale;
    device_->AddRef();
    ctx_->AddRef();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // no imgui.ini surprise next to the DLL
    io.LogFilename = nullptr;

    if (!ImGui_ImplWin32_Init(hwnd)) {
        shogod::log::error("ui: ImGui_ImplWin32_Init failed");
        return false;
    }
    if (!ImGui_ImplDX11_Init(device, device_context)) {
        shogod::log::error("ui: ImGui_ImplDX11_Init failed");
        ImGui_ImplWin32_Shutdown();
        return false;
    }

    ready_ = true;
    shogod::log::info("ui: DX11 surface up (scale {:.2f})", ui_scale);
    return true;
}

void context::capture_state() {
    saved_ = {};
    ctx_->OMGetRenderTargets(1, &saved_.rtv, &saved_.dsv);
    saved_.viewport_count = 1;
    ctx_->RSGetViewports(&saved_.viewport_count, &saved_.viewport);
    ctx_->OMGetBlendState(&saved_.blend, saved_.blend_factor, &saved_.sample_mask);
    ctx_->OMGetDepthStencilState(&saved_.depth, &saved_.stencil_ref);
    ctx_->RSGetState(&saved_.raster);
}

void context::restore_state() {
    ctx_->OMSetRenderTargets(1, &saved_.rtv, saved_.dsv);
    if (saved_.viewport_count > 0) ctx_->RSSetViewports(saved_.viewport_count, &saved_.viewport);
    ctx_->OMSetBlendState(saved_.blend, saved_.blend_factor, saved_.sample_mask);
    ctx_->OMSetDepthStencilState(saved_.depth, saved_.stencil_ref);
    ctx_->RSSetState(saved_.raster);

    if (saved_.rtv) saved_.rtv->Release();
    if (saved_.dsv) saved_.dsv->Release();
    if (saved_.blend) saved_.blend->Release();
    if (saved_.depth) saved_.depth->Release();
    if (saved_.raster) saved_.raster->Release();
    saved_ = {};
}

bool context::begin() {
    if (!ready_) return false;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    capture_state();
    return true;
}

void context::end() {
    if (!ready_) return;
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    restore_state();
}

void context::invalidate() {
    if (!ready_) return;
    ImGui_ImplDX11_InvalidateDeviceObjects();
}

void context::shutdown() {
    if (!ready_) return;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    ready_ = false;

    if (ctx_) {
        ctx_->Release();
        ctx_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
    hwnd_ = nullptr;
}

bool context::capture_backbuffer(ID3D11Device* device, ID3D11DeviceContext* device_context,
                                 IDXGISwapChain* swapchain, const wchar_t* path) {
    if (!device || !device_context || !swapchain || !path) return false;

    ID3D11Texture2D* back = nullptr;
    if (FAILED(swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) {
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC staged = desc;
    staged.BindFlags = 0;
    staged.Usage = D3D11_USAGE_STAGING;
    staged.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staged.MiscFlags = 0;

    ID3D11Texture2D* staging = nullptr;
    if (FAILED(device->CreateTexture2D(&staged, nullptr, &staging))) {
        back->Release();
        return false;
    }
    device_context->CopyResource(staging, back);
    back->Release();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(device_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        staging->Release();
        return false;
    }

    IWICImagingFactory* factory = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    bool ok = false;

    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninit = SUCCEEDED(co);

    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE)) &&
        SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
        SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->SetSize(desc.Width, desc.Height) ) ) {
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
            desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
            format = GUID_WICPixelFormat32bppRGBA;
        }
        if (SUCCEEDED(frame->SetPixelFormat(&format))) {
            // Staging rows are pitched; a tight width*4 stride shears the image.
            const UINT pitch = mapped.RowPitch;
            ok = SUCCEEDED(frame->WritePixels(desc.Height, pitch, pitch * desc.Height,
                                              static_cast<BYTE*>(mapped.pData)));
        }
        if (ok) {
            ok = SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
        }
    }

    device_context->Unmap(staging, 0);
    staging->Release();
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    if (uninit) CoUninitialize();
    return ok;
}

}  // namespace shogod::render
