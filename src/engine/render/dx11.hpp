#pragma once

// DX11 + ImGui surface. Owns the ImGui context, the font atlas and the full
// pipeline-state save/restore around the ImGui pass. Callers (the Present hook
// or the standalone host) only open a frame and close it.

#include <cstdint>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <windows.h>

namespace shogod::render {

class context {
public:
    bool init(ID3D11Device* device, ID3D11DeviceContext* device_context, HWND hwnd,
              float ui_scale = 1.0f);
    void shutdown();

    // Returns false until init() succeeded; the caller then skips the frame.
    bool begin();
    void end();

    [[nodiscard]] bool ready() const noexcept { return ready_; }

    // Device loss: drop backend objects; the caller recreates the device and
    // calls invalidate() then a fresh init() path on the new device.
    void invalidate();

    // Writes the swapchain back buffer to a PNG. Used by the visual gate.
    static bool capture_backbuffer(ID3D11Device* device, ID3D11DeviceContext* device_context,
                                   IDXGISwapChain* swapchain, const wchar_t* path);

private:
    void capture_state();
    void restore_state();

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* ctx_ = nullptr;
    HWND hwnd_ = nullptr;
    bool ready_ = false;

    struct saved {
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11DepthStencilView* dsv = nullptr;
        UINT viewport_count = 0;
        D3D11_VIEWPORT viewport{};
        ID3D11BlendState* blend = nullptr;
        FLOAT blend_factor[4]{};
        UINT sample_mask = 0;
        ID3D11DepthStencilState* depth = nullptr;
        UINT stencil_ref = 0;
        ID3D11RasterizerState* raster = nullptr;
    } saved_{};

    float scale_ = 1.0f;
};

}  // namespace shogod::render
