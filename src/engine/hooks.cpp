#include "engine/hooks.hpp"

#include "engine/log.hpp"
#include "engine/render/dx11.hpp"
#include "engine/runtime.hpp"
#include "sdk/signatures.hpp"
#include "ui/menu.hpp"
#include "ui/style.hpp"

#include <atomic>
#include <cstring>
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
void** g_swapchain_vtable = nullptr;
void** g_private_vtable = nullptr;
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

// DXGI dispatch goes through CFG. A detour address that is not in the CFG
// bitmap fails the guard check and the process dies inside dxgi with an
// invalid-target fault; register our pages before the first call lands.
void mark_cfg_valid(void* function) {
    if (function == nullptr) return;

    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const std::uintptr_t page = reinterpret_cast<std::uintptr_t>(function) & ~(info.dwPageSize - 1);
    CFG_CALL_TARGET_INFO entry{};
    entry.Offset = reinterpret_cast<std::uintptr_t>(function) - page;
    entry.Flags = CFG_CALL_TARGET_VALID;

    using set_targets_fn = BOOL(WINAPI*)(HANDLE, PVOID, SIZE_T, ULONG, PCFG_CALL_TARGET_INFO);
    static set_targets_fn set_targets = [] {
        for (const wchar_t* module : {L"kernelbase.dll", L"kernel32.dll"}) {
            const HMODULE handle = GetModuleHandleW(module);
            if (handle != nullptr) {
                auto fn = reinterpret_cast<set_targets_fn>(
                    GetProcAddress(handle, "SetProcessValidCallTargets"));
                if (fn != nullptr) return fn;
            }
        }
        return static_cast<set_targets_fn>(nullptr);
    }();

    if (set_targets == nullptr) {
        log::warn("hooks: SetProcessValidCallTargets unavailable");
        return;
    }

    if (!set_targets(GetCurrentProcess(), reinterpret_cast<void*>(page), info.dwPageSize, 1,
                     &entry)) {
        log::warn("hooks: CFG target registration failed ({}), continuing", GetLastError());
    }
}

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
    log::info("hooks: dummy device");
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

    log::info("hooks: dummy device");
    DXGI_SWAP_CHAIN_DESC desc{};
    swapchain->GetDesc(&desc);
    g_hwnd = desc.OutputWindow;

    const bool ok = g_render.init(device, ctx, g_hwnd);
    if (ok) {
        ui::load_fonts(1.0f);
        const auto& c = runtime::cfg().get().ui;
        ui::apply_style(c.accent, c.dark_theme);
    }
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
    static int present_calls = 0;
    static bool present_logged = false;
    ++present_calls;
    if (!present_logged && present_calls >= 3) {
        present_logged = true;
        log::info("hooks: Present hook live ({} calls)", present_calls);
    }
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
    {
        PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY cfg_policy{};
        if (GetProcessMitigationPolicy(GetCurrentProcess(), ProcessControlFlowGuardPolicy,
                                       &cfg_policy, sizeof cfg_policy)) {
            log::info("hooks: CFG enable={} strict={}",
                      static_cast<bool>(cfg_policy.EnableControlFlowGuard), static_cast<bool>(cfg_policy.StrictMode));
        }
        PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY sig_policy{};
        if (GetProcessMitigationPolicy(GetCurrentProcess(), ProcessSignaturePolicy, &sig_policy,
                                       sizeof sig_policy)) {
            log::info("hooks: signature policy ms-signed-only={}",
                      static_cast<bool>(sig_policy.MicrosoftSignedOnly));
        }
    }
    log::info("hooks: bootstrap window");
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"shogod_bootstrap";
    RegisterClassExW(&wc);

    HWND window = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 8, 8,
                                  nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) return false;

    log::info("hooks: dummy device");
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
    log::info("hooks: dummy device hr={:#x}", static_cast<unsigned>(hr));
    if (FAILED(hr)) {
        DestroyWindow(window);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    const auto vtable = *reinterpret_cast<void***>(swapchain);
    g_swapchain_vtable = vtable;
    g_present = reinterpret_cast<present_t>(vtable[8]);
    g_resize = reinterpret_cast<resize_t>(vtable[13]);
    log::info("hooks: swapchain vtable {:p} (Present {:p}, Resize {:p})",
              static_cast<void*>(vtable), reinterpret_cast<void*>(g_present),
              reinterpret_cast<void*>(g_resize));

    // Shared tables are not touched here: the game's own swapchain gets a
    // private vtable copy in install_swapchain(). Patching the class table
    // while DXGI/overlays own it kills the process before the hook runs.
    const bool ok = g_present != nullptr && g_resize != nullptr;

    if (ok) log::info("hooks: Present/ResizeBuffers hooked");
    else log::error("hooks: swapchain patch failed");
    return ok;
}

using create_move_t = void* (*)(void*, int, int, int);
create_move_t g_create_move = nullptr;
void** g_create_move_slot = nullptr;
int g_create_move_calls = 0;

void* hk_create_move(void* self, int a, int b, int c) {
    if (g_create_move_calls < 3) {
        ++g_create_move_calls;
        const int count = *reinterpret_cast<int*>(static_cast<char*>(self) + 0xb50);
        void* arr = *reinterpret_cast<void**>(static_cast<char*>(self) + 0xb58);
        log::info("create_move: edx={} r8={} r9={} count={} arr={:p}", a, b, c, count, arr);
    }
    return g_create_move(self, a, b, c);
}

bool install_create_move() {
    const auto& anchors = sdk::resolve();
    if (!anchors.input_ok) {
        log::warn("hooks: CreateMove skipped — input anchor failed");
        return false;
    }

    // The methods live in the vtable, not in the object body.
    auto vtable = *reinterpret_cast<void***>(anchors.input);
    g_create_move_slot = &vtable[7];
    g_create_move = reinterpret_cast<create_move_t>(*g_create_move_slot);
    const bool ok = patch_pointer(g_create_move_slot, reinterpret_cast<void*>(&hk_create_move),
                                  reinterpret_cast<void**>(&g_create_move));
    if (ok) log::info("hooks: CreateMove vtable[7] hooked (was {:p})", reinterpret_cast<void*>(g_create_move));
    return ok;
}

struct desc_probe {
    HWND hwnd;
    void* object;
};

bool probe_swapchain(void* object, HWND hwnd) {
    __try {
        const auto vtable = *reinterpret_cast<void***>(object);
        if (vtable != g_swapchain_vtable) return false;

        using get_desc_fn = HRESULT(STDMETHODCALLTYPE*)(void*, DXGI_SWAP_CHAIN_DESC*);
        auto get_desc = reinterpret_cast<get_desc_fn>(vtable[12]);
        if (get_desc == nullptr) return false;

        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(get_desc(object, &desc))) return false;
        return desc.OutputWindow == hwnd && desc.BufferCount > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* find_game_swapchain(HWND hwnd) {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);

    auto* address = static_cast<std::uint8_t*>(info.lpMinimumApplicationAddress);
    auto* maximum = static_cast<std::uint8_t*>(info.lpMaximumApplicationAddress);
    const std::uintptr_t vtable_bits = reinterpret_cast<std::uintptr_t>(g_swapchain_vtable);

    while (address < maximum) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(address, &mbi, sizeof mbi) != sizeof mbi) break;
        auto* next = static_cast<std::uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;

        const bool usable = mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
                            mbi.RegionSize <= 0x4000000 &&
                            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                            PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                            PAGE_EXECUTE_WRITECOPY)) != 0;
        if (usable) {
            const auto* begin = static_cast<const std::uintptr_t*>(mbi.BaseAddress);
            const std::size_t count = mbi.RegionSize / sizeof(std::uintptr_t);
            for (std::size_t i = 0; i < count; ++i) {
                if (begin[i] != vtable_bits) continue;
                auto* candidate = reinterpret_cast<void*>(const_cast<std::uintptr_t*>(begin + i));
                if (probe_swapchain(candidate, hwnd)) return candidate;
            }
        }
        address = next;
    }
    return nullptr;
}

bool install_swapchain(HWND hwnd) {
    if (g_swapchain_vtable == nullptr) return false;

    void* object = nullptr;
    for (int attempt = 0; attempt < 40 && object == nullptr; ++attempt) {
        object = find_game_swapchain(hwnd);
        if (object == nullptr) Sleep(50);
    }
    if (object == nullptr) {
        log::warn("hooks: game swapchain object not found");
        return false;
    }

    constexpr std::size_t k_vtable_slots = 64;
    auto** copy = static_cast<void**>(
        VirtualAlloc(nullptr, k_vtable_slots * sizeof(void*), MEM_COMMIT | MEM_RESERVE,
                     PAGE_READWRITE));
    if (copy == nullptr) return false;
    std::memcpy(copy, g_swapchain_vtable, k_vtable_slots * sizeof(void*));
    copy[8] = reinterpret_cast<void*>(&hk_present);
    copy[13] = reinterpret_cast<void*>(&hk_resize);

    DWORD old = 0;
    VirtualProtect(copy, k_vtable_slots * sizeof(void*), PAGE_READONLY, &old);
    g_private_vtable = copy;
    *reinterpret_cast<void***>(object) = copy;

    log::info("hooks: private vtable {:p} installed on swapchain {:p}", static_cast<void*>(copy),
              object);
    return true;
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
    if (g_window) install_swapchain(g_hwnd);
    install_create_move();
    return g_graphics;
}

void uninstall() {
    if (g_window) {
        if (g_hwnd && g_wndproc) SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_wndproc));
        g_window = false;
        g_wndproc = nullptr;
    }

    if (g_graphics) {
        if (g_private_vtable != nullptr) {
            // The owning object still points at our copy until the process
            // exits; restoring the class table is all that is needed here.
            VirtualFree(g_private_vtable, 0, MEM_RELEASE);
            g_private_vtable = nullptr;
        }
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
