#include "engine/hooks.hpp"

#include "engine/log.hpp"
#include "engine/render/dx11.hpp"
#include "engine/runtime.hpp"
#include "ui/menu.hpp"
#include "ui/style.hpp"

#include <atomic>
#include <mutex>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace shogod::hooks {
namespace {

using present_t = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
using resize_t = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using wndproc_t = LRESULT(CALLBACK*)(HWND, UINT, WPARAM, LPARAM);

present_t g_present = nullptr;
resize_t g_resize = nullptr;
wndproc_t g_wndproc = nullptr;
void** g_present_slot = nullptr;
void** g_resize_slot = nullptr;

render::context g_render;
ID3D11RenderTargetView* g_rtv = nullptr;
UINT g_rtv_w = 0;
UINT g_rtv_h = 0;
HWND g_hwnd = nullptr;
bool g_graphics = false;
bool g_window = false;

bool patch_pointer(void** slot, void* replacement, void** original) {
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
    if (original) *original = *slot;
    *slot = replacement;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

void* vtable_entry(void* object, int index) {
    return (*reinterpret_cast<void***>(object))[index];
}

void release_rtv() {
    if (g_rtv) {
        g_rtv->Release();
        g_rtv = nullptr;
    }
    g_rtv_w = g_rtv_h = 0;
}

bool ensure_rtv(IDXGISwapChain* swapchain) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swapchain->GetDesc(&desc))) return false;
    if (g_rtv && desc.BufferDesc.Width == g_rtv_w && desc.BufferDesc.Height == g_rtv_h) return true;

    release_rtv();
    ID3D11Texture2D* backbuffer = nullptr;
    if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)))) return false;

    ID3D11Device* device = nullptr;
    swapchain->GetDevice(IID_PPV_ARGS(&device));
    if (!device) {
        backbuffer->Release();
        return false;
    }

    const HRESULT hr = device->CreateRenderTargetView(backbuffer, nullptr, &g_rtv);
    if (SUCCEEDED(hr)) {
        g_rtv_w = desc.BufferDesc.Width;
        g_rtv_h = desc.BufferDesc.Height;
    }
    device->Release();
    backbuffer->Release();
    return SUCCEEDED(hr);
}

bool init_surface(IDXGISwapChain* swapchain) {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    swapchain->GetDevice(IID_PPV_ARGS(&device));
    if (!device) return false;
    device->GetImmediateContext(&ctx);

    DXGI_SWAP_CHAIN_DESC desc{};
    swapchain->GetDesc(&desc);
    g_hwnd = desc.OutputWindow;

    ui::load_fonts(1.0f);
    const auto& c = runtime::cfg().get().ui;
    ui::apply_style(c.accent, c.dark_theme);

    const bool ok = g_render.init(device, ctx, g_hwnd);
    ctx->Release();
    device->Release();
    if (ok) log::info("hooks: DX11 surface initialised on {:p}", static_cast<void*>(g_hwnd));
    return ok;
}

void set_menu_open(bool open) {
    runtime::menu().open = open;
    if (open) {
        ClipCursor(nullptr);
        while (ShowCursor(TRUE) < 0) {
        }
    } else {
        while (ShowCursor(FALSE) >= 0) {
        }
    }
}

HRESULT WINAPI hk_present(IDXGISwapChain* swapchain, UINT sync, UINT flags) {
    if (!g_render.ready() && !init_surface(swapchain)) {
        return g_present(swapchain, sync, flags);
    }

    if (ensure_rtv(swapchain)) {
        runtime::frame_tick();
        if (g_render.begin(g_rtv, g_rtv_w, g_rtv_h)) {
            ui::draw_menu(runtime::cfg(), runtime::menu(), runtime::live());
            g_render.end();
        }
    }

    const HRESULT hr = g_present(swapchain, sync, flags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        log::warn("hooks: device lost on Present ({:#x}) — invalidating", static_cast<unsigned>(hr));
        g_render.invalidate();
        release_rtv();
    }
    return hr;
}

HRESULT WINAPI hk_resize(IDXGISwapChain* swapchain, UINT count, UINT w, UINT h, DXGI_FORMAT fmt,
                         UINT flags) {
    // Our view must die before the buffers move or the next Present draws garbage.
    release_rtv();
    return g_resize(swapchain, count, w, h, fmt, flags);
}

LRESULT CALLBACK hk_wndproc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    const int menu_key = runtime::cfg().get().ui.menu_key;
    if (msg == WM_KEYDOWN && static_cast<int>(w) == menu_key && (l & (1 << 30)) == 0) {
        set_menu_open(!runtime::menu().open);
        if (!runtime::menu().open) return 1;  // swallow the toggle key
    }

    if (runtime::menu().open) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, w, l);

        switch (msg) {
            case WM_MOUSEMOVE:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MOUSEWHEEL:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_CHAR:
                return 1;  // the game never sees input aimed at the menu
            case WM_SETCURSOR:
                return 1;
            default:
                break;
        }
    }

    return CallWindowProcW(g_wndproc, hwnd, msg, w, l);
}

struct window_search {
    DWORD pid;
    HWND found;
};

BOOL CALLBACK find_window(HWND hwnd, LPARAM param) {
    auto* search = reinterpret_cast<window_search*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != search->pid || !IsWindowVisible(hwnd)) return TRUE;

    char cls[64]{};
    GetClassNameA(hwnd, cls, sizeof cls);
    if (strstr(cls, "SDL") == nullptr && GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;

    search->found = hwnd;
    return FALSE;
}

bool install_graphics() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"shogod_bootstrap";
    RegisterClassExW(&wc);

    HWND window = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 8, 8,
                                  nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) return false;

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 1;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* swapchain = nullptr;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level{};

    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &desc,
        &swapchain, &device, &level, &ctx);
    if (FAILED(hr)) {
        DestroyWindow(window);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    g_present_slot = reinterpret_cast<void**>(vtable_entry(swapchain, 8));
    g_resize_slot = reinterpret_cast<void**>(vtable_entry(swapchain, 13));
    const bool ok = patch_pointer(g_present_slot, reinterpret_cast<void*>(&hk_present),
                                  reinterpret_cast<void**>(&g_present)) &&
                    patch_pointer(g_resize_slot, reinterpret_cast<void*>(&hk_resize),
                                  reinterpret_cast<void**>(&g_resize));

    swapchain->Release();
    ctx->Release();
    device->Release();
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    if (ok) log::info("hooks: Present/ResizeBuffers hooked");
    else log::error("hooks: swapchain patch failed");
    return ok;
}

bool install_window() {
    window_search search{GetCurrentProcessId(), nullptr};
    EnumWindows(&find_window, reinterpret_cast<LPARAM>(&search));
    if (!search.found) {
        log::error("hooks: game window not found");
        return false;
    }

    g_hwnd = search.found;
    const LONG_PTR previous = SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC,
                                                reinterpret_cast<LONG_PTR>(&hk_wndproc));
    if (previous == 0) {
        log::error("hooks: SetWindowLongPtrW failed");
        return false;
    }
    g_wndproc = reinterpret_cast<wndproc_t>(previous);
    log::info("hooks: WndProc hooked on {:p}", static_cast<void*>(g_hwnd));
    return true;
}

}  // namespace

bool install() {
    if (g_graphics) return true;
    g_graphics = install_graphics();
    if (!g_graphics) return false;
    g_window = install_window();
    return g_graphics;
}

void uninstall() {
    if (g_window) {
        if (g_hwnd && g_wndproc) SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_wndproc));
        g_window = false;
        g_wndproc = nullptr;
    }

    if (g_graphics) {
        if (g_present_slot && g_present) *g_present_slot = reinterpret_cast<void*>(g_present);
        if (g_resize_slot && g_resize) *g_resize_slot = reinterpret_cast<void*>(g_resize);
        g_present_slot = nullptr;
        g_resize_slot = nullptr;
        g_present = nullptr;
        g_resize = nullptr;
        g_graphics = false;
    }

    release_rtv();
    g_render.shutdown();
    if (g_hwnd) while (ShowCursor(TRUE) < 0) {
        }
    log::info("hooks: uninstalled");
}

}  // namespace shogod::hooks
