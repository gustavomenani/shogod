// shogod menu host — runs the ImGui menu on a real DX11 swapchain without a
// game. Used for the visual gate (screenshots) and for iterating on the UI.
//
// keys: 1..5 switch tabs, F12 screenshot, ESC quit.

#include "config/config.hpp"
#include "engine/log.hpp"
#include "engine/render/dx11.hpp"
#include "ui/menu.hpp"
#include "ui/style.hpp"

#include <cstring>
#include <filesystem>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

HWND g_hwnd = nullptr;
bool g_running = true;
std::string g_capture_dir;

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, w, l)) return true;
    switch (msg) {
        case WM_DESTROY:
            g_running = false;
            return 0;
        case WM_KEYDOWN:
            if (w == VK_ESCAPE) {
                PostQuitMessage(0);
                g_running = false;
            }
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

bool create_device(HWND hwnd, ID3D11Device** device, ID3D11DeviceContext** ctx,
                   IDXGISwapChain** swapchain) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    // DXGI may typecast an R8G8B8A8 DISCARD backbuffer to BGRA; ask for BGRA up front.
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferDesc.Width = 0;
    desc.BufferDesc.Height = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
    D3D_FEATURE_LEVEL level{};
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    return SUCCEEDED(D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2, D3D11_SDK_VERSION, &desc,
        swapchain, device, &level, ctx));
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--capture" && i + 1 < argc) {
            g_capture_dir = std::filesystem::path(argv[++i]).string();
            std::filesystem::create_directories(g_capture_dir);
        }
    }

    // Fonts live next to the source tree in the host; the DLL ships them.
    _putenv_s("SHOGOD_FONT_DIR", "vendor/fonts");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"shogod_host";
    RegisterClassExW(&wc);

    RECT rect{0, 0, 1100, 720};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"shogod — menu host",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                             wc.hInstance, nullptr);
    if (!g_hwnd) return 1;
    ShowWindow(g_hwnd, SW_SHOW);

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* swapchain = nullptr;
    if (!create_device(g_hwnd, &device, &ctx, &swapchain)) return 1;

    shogod::log::init();
    shogod::render::context renderer;
    if (!renderer.init(device, ctx, g_hwnd)) return 1;

    shogod::config::store cfg;
    cfg.load(shogod::config::store::default_dir() / "default.json");

    shogod::ui::menu_state menu;
    menu.open = true;
    menu.open_anim = 1.0f;
    menu.tab_anim.fill(1.0f);

    shogod::ui::status live;
    live.fps = 240.0f;
    live.tick = 128;
    live.players = 10;
    live.anchors_ok = 8;
    live.anchors_total = 8;
    live.in_game = true;
    live.config_name = "default";
    live.anchor_detail = "entity system · view matrix · CreateMove · trace · schema — all OK";

    std::string pending_capture;
    int frame = 0;

    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11Texture2D* backbuffer = nullptr;
    swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer));
    device->CreateRenderTargetView(backbuffer, nullptr, &rtv);

    int capture_tab = -1;
    int capture_wait = 0;

    MSG msg{};
    while (g_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (GetAsyncKeyState('1') & 1) menu.active = shogod::ui::tab::rage;
        if (GetAsyncKeyState('2') & 1) menu.active = shogod::ui::tab::visuals;
        if (GetAsyncKeyState('3') & 1) menu.active = shogod::ui::tab::misc;
        if (GetAsyncKeyState('4') & 1) menu.active = shogod::ui::tab::configs;
        if (GetAsyncKeyState('5') & 1) menu.active = shogod::ui::tab::settings;

        if (GetAsyncKeyState(VK_F12) & 1 && !g_capture_dir.empty()) {
            const auto path = std::filesystem::path(g_capture_dir) / "manual.png";
            shogod::render::context::capture_backbuffer(device, ctx, swapchain, path.wstring().c_str());
        }

        // Automated gate capture: walk every tab once.
        if (!g_capture_dir.empty()) {
            if (capture_tab < 0 && frame > 4) capture_tab = 0;
            if (capture_tab >= 0 && capture_tab < shogod::ui::k_tab_count) {
                menu.active = static_cast<shogod::ui::tab>(capture_tab);
                if (++capture_wait > 20) {
                    pending_capture = "tab" + std::to_string(capture_tab);
                    capture_wait = 0;
                    ++capture_tab;
                }
            }
        }

        const float clear[4] = {0.035f, 0.045f, 0.07f, 1.0f};
        ctx->ClearRenderTargetView(rtv, clear);
        ctx->OMSetRenderTargets(1, &rtv, nullptr);

        RECT client{};
        GetClientRect(g_hwnd, &client);
        if (renderer.begin(rtv, static_cast<UINT>(client.right), static_cast<UINT>(client.bottom))) {
            shogod::ui::draw_menu(cfg, menu, live);
            renderer.end();
        }

        if (!pending_capture.empty()) {
            const auto path =
                std::filesystem::path(g_capture_dir) / (pending_capture + ".png");
            shogod::render::context::capture_backbuffer(device, ctx, swapchain,
                                                        path.wstring().c_str());
            shogod::log::info("host: captured {}", path.string());
            pending_capture.clear();
            if (capture_tab >= shogod::ui::k_tab_count) {
                g_running = false;
            }
        }

        swapchain->Present(0, 0);
        ++frame;
    }

    renderer.shutdown();
    if (rtv) rtv->Release();
    if (backbuffer) backbuffer->Release();
    if (swapchain) swapchain->Release();
    if (ctx) ctx->Release();
    if (device) device->Release();
    shogod::log::shutdown();
    return 0;
}
