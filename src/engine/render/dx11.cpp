#include "engine/render/dx11.hpp"

#include "engine/log.hpp"
#include "ui/brand_logo.hpp"
#include "ui/menu.hpp"
#include "ui/preview/preview.hpp"
#include "ui/user_avatar.hpp"

#include <wincodec.h>

#include <algorithm>
#include <filesystem>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#pragma comment(lib, "windowscodecs.lib")

namespace shogod::render {

namespace {

bool upload_rgba_texture(ID3D11Device* device, const std::uint8_t* rgba, int w, int h,
                         ID3D11Texture2D** out_tex, ID3D11ShaderResourceView** out_srv) {
    if (!device || !rgba || w <= 0 || h <= 0 || !out_tex || !out_srv) return false;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(w);
    td.Height = static_cast<UINT>(h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init_data{};
    init_data.pSysMem = rgba;
    init_data.SysMemPitch = static_cast<UINT>(w * 4);
    if (FAILED(device->CreateTexture2D(&td, &init_data, out_tex))) return false;
    if (FAILED(device->CreateShaderResourceView(*out_tex, nullptr, out_srv))) {
        (*out_tex)->Release();
        *out_tex = nullptr;
        return false;
    }
    return true;
}

}  // namespace

bool context::init(ID3D11Device* device, ID3D11DeviceContext* device_context, HWND hwnd,
                   float ui_scale) {
    if (ready_) return true;
    if (!device || !device_context || !hwnd) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // no imgui.ini surprise next to the DLL
    io.LogFilename = nullptr;

    if (!ImGui_ImplWin32_Init(hwnd)) {
        shogod::log::error("ui: ImGui_ImplWin32_Init failed");
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplDX11_Init(device, device_context)) {
        shogod::log::error("ui: ImGui_ImplDX11_Init failed");
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    device_ = device;
    ctx_ = device_context;
    hwnd_ = hwnd;
    scale_ = ui_scale;
    device_->AddRef();
    ctx_->AddRef();

    if (upload_rgba_texture(device_, ui::k_brand_logo_rgba, ui::k_brand_logo_w, ui::k_brand_logo_h,
                            &brand_tex_, &brand_srv_)) {
        ui::set_brand_logo(reinterpret_cast<ImTextureID>(brand_srv_), ui::k_brand_logo_w,
                           ui::k_brand_logo_h);
    }
    if (upload_rgba_texture(device_, ui::k_user_avatar_rgba, ui::k_user_avatar_size,
                            ui::k_user_avatar_size, &avatar_tex_, &avatar_srv_)) {
        ui::set_user_avatar(reinterpret_cast<ImTextureID>(avatar_srv_), ui::k_user_avatar_size);
    }

    ui::preview::shared_panel().init(device, device_context);
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

bool context::begin(ID3D11RenderTargetView* target, UINT width, UINT height) {
    if (!ready_) return false;
    capture_state();  // the game's state, before we bind anything of ours

    if (target && width > 0 && height > 0) {
        ctx_->OMSetRenderTargets(1, &target, nullptr);
        D3D11_VIEWPORT vp{};
        vp.Width = static_cast<float>(width);
        vp.Height = static_cast<float>(height);
        vp.MaxDepth = 1.0f;
        ctx_->RSSetViewports(1, &vp);
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    return true;
}

void context::end() {
    if (!ready_) return;
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    restore_state();
}

void context::shutdown() {
    if (!ready_) return;
    ui::set_brand_logo(0, 0, 0);
    ui::set_user_avatar(0, 0);
    if (avatar_srv_) {
        avatar_srv_->Release();
        avatar_srv_ = nullptr;
    }
    if (avatar_tex_) {
        avatar_tex_->Release();
        avatar_tex_ = nullptr;
    }
    if (brand_srv_) {
        brand_srv_->Release();
        brand_srv_ = nullptr;
    }
    if (brand_tex_) {
        brand_tex_->Release();
        brand_tex_ = nullptr;
    }
    ui::preview::shared_panel().shutdown();
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
