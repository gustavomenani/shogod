#include "engine/hooks.hpp"

#include "config/bind_state.hpp"
#include "config/keybinds.hpp"

#include "engine/log.hpp"
#include "engine/module.hpp"
#include "engine/render/dx11.hpp"
#include "engine/runtime.hpp"
#include "features/audio/hitsounds.hpp"
#include "features/chams/chams.hpp"
#include "features/createmove.hpp"
#include "features/esp/esp.hpp"
#include "features/math/angles.hpp"
#include "features/world/world.hpp"
#include "sdk/bones.hpp"
#include "sdk/entities.hpp"
#include "sdk/signatures.hpp"
#include "sdk/shot_events.hpp"
#include "sdk/trace.hpp"
#include "sdk/usercmd.hpp"
#include "ui/menu.hpp"
#include "ui/style.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <MinHook.h>
#include <d3d11.h>
#include <dxgi.h>
#include <intrin.h>
#include <windows.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

extern ImGuiKey ImGui_ImplWin32_KeyEventToImGuiKey(WPARAM, LPARAM);

namespace shogod::hooks {
namespace {

using present_t = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
using resize_t = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using wndproc_t = LRESULT(CALLBACK*)(HWND, UINT, WPARAM, LPARAM);

present_t g_present = nullptr;
resize_t g_resize = nullptr;
void** g_swapchain_vtable = nullptr;
wndproc_t g_wndproc = nullptr;
std::atomic<bool> g_menu_open{false};
std::atomic<int> g_menu_key{VK_INSERT};

struct game_settings {
    config::config_v1 config;
    std::uint32_t active_binds = 0;
};

std::mutex g_settings_mutex;
game_settings g_settings;

bool game_has_focus(HWND game_window) {
    const HWND foreground = GetForegroundWindow();
    return game_window && foreground &&
           GetAncestor(game_window, GA_ROOTOWNER) == GetAncestor(foreground, GA_ROOTOWNER);
}
std::atomic<DWORD> g_game_thread{0};
std::atomic<int> g_third_person_distance{0};
std::atomic<int> g_camera_fov{0};
std::atomic<bool> g_fov_while_scoped{false};
std::atomic<bool> g_local_scoped{false};
std::atomic<bool> g_no_smoke{false};
std::atomic<bool> g_no_scope{false};

using setup_view_t = void (*)(void*, void*);
using draw_scope_t = void (*)(void*, void*);
using draw_smoke_t = void* (*)(void*, void*, std::uintptr_t, int, void*, void*);
setup_view_t g_setup_view = nullptr;
draw_smoke_t g_draw_smoke = nullptr;
draw_scope_t g_draw_scope = nullptr;

using sort_primitives_t = void(__fastcall*)(void*, void*, void*, std::uint32_t);
using render_layer_draw_list_t = void(__fastcall*)(void*, void*, void*, void*, int, void*);

features::chams::generate_primitives_fn g_generate_primitives = nullptr;
features::chams::cull_scene_objects_fn g_cull_scene_objects = nullptr;
features::chams::test_scene_pvs_fn g_test_scene_pvs = nullptr;
features::chams::draw_scene_primitive_fn g_draw_scene_primitive = nullptr;
features::chams::draw_indexed_fn g_draw_occluded_dx11 = nullptr;
features::chams::draw_indexed_fn g_draw_occluded_software = nullptr;
sort_primitives_t g_sort_primitives = nullptr;
render_layer_draw_list_t g_render_layer_draw_list = nullptr;

features::world::base_draw_array_fn g_base_draw_array = nullptr;
features::world::setup_light_fn g_setup_light = nullptr;
features::world::setup_tonemap_fn g_setup_tonemap = nullptr;
features::world::view_punch_fn g_view_punch = nullptr;

// CViewSetup offsets verified in SetupView (client.dll 0xb92a20: m_flFov at 0x498,
// origin at 0x4a0, angles at 0x4b8).
constexpr std::size_t k_camera_fov = 0x498;
constexpr std::size_t k_camera_origin = 0x4a0;
constexpr std::size_t k_camera_angles = 0x4b8;
// Scope callback at client.dll RVA 0x8dd900 writes its output visibility here.
constexpr std::size_t k_scope_visible = 4;

render::context g_render;
ID3D11RenderTargetView* g_rtv = nullptr;
UINT g_rtv_w = 0;
UINT g_rtv_h = 0;
HWND g_hwnd = nullptr;
bool g_graphics = false;

// DXGI dispatch goes through CFG. A detour address that is not in the CFG
// bitmap fails the guard check and the process dies inside dxgi with an
// invalid-target fault; register our pages before the first call lands.
void mark_cfg_valid(void* function) {
    if (function == nullptr) return;

    PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY policy{};
    if (!GetProcessMitigationPolicy(GetCurrentProcess(), ProcessControlFlowGuardPolicy, &policy,
                                    sizeof policy) ||
        policy.EnableControlFlowGuard == 0) {
        return;  // CFG off: indirect calls are not checked, nothing to register
    }

    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const std::uintptr_t page_mask = ~(static_cast<std::uintptr_t>(info.dwPageSize) - 1);
    const std::uintptr_t page = reinterpret_cast<std::uintptr_t>(function) & page_mask;
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

bool read_camera_view(void* setup, math::vec3& origin, math::angles& angles) noexcept {
    if (setup == nullptr) return false;
    auto* bytes = static_cast<std::uint8_t*>(setup);
    __try {
        origin = *reinterpret_cast<const math::vec3*>(bytes + k_camera_origin);
        angles = *reinterpret_cast<const math::angles*>(bytes + k_camera_angles);
        return origin.finite() && std::isfinite(angles.pitch) && std::isfinite(angles.yaw) &&
               std::fabs(angles.pitch) <= 90.0f && std::fabs(angles.yaw) <= 360.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool write_camera_origin(void* setup, const math::vec3& origin) noexcept {
    __try {
        *reinterpret_cast<math::vec3*>(static_cast<std::uint8_t*>(setup) + k_camera_origin) = origin;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool write_camera_fov(void* setup, float fov) noexcept {
    if (setup == nullptr) return false;
    auto* ptr = reinterpret_cast<float*>(static_cast<std::uint8_t*>(setup) + k_camera_fov);
    __try {
        const float current = *ptr;
        if (!std::isfinite(current) || current < 10.0f || current > 160.0f) return false;
        *ptr = fov;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct camera_query {
    std::uintptr_t pawn = 0;
    math::vec3 origin{};
    math::vec3 forward{};
    int distance = 0;
    ULONGLONG requested = 0;
};

std::mutex g_camera_mutex;
camera_query g_camera_request{};
camera_query g_camera_completed{};
std::optional<float> g_camera_clearance;

std::optional<float> trace_camera(const camera_query& query) {
    const auto desired = query.origin - query.forward * static_cast<float>(query.distance);
    const float start[3] = {query.origin.x, query.origin.y, query.origin.z};
    const float end[3] = {desired.x, desired.y, desired.z};
    float fraction = 0.0f;
    if (!sdk::trace_fraction_skip(start, end, reinterpret_cast<void*>(query.pawn), fraction) ||
        !std::isfinite(fraction) || fraction < 0.0f || fraction > 1.0f) return std::nullopt;
    const float clear = static_cast<float>(query.distance) * fraction;
    return fraction == 1.0f ? clear : std::max(0.0f, clear - 6.0f);
}

void process_camera_request(const sdk::frame_snapshot& frame) {
    camera_query request{};
    {
        std::lock_guard guard(g_camera_mutex);
        if (!frame.in_game || !frame.local_alive || !frame.local_eye_valid ||
            g_third_person_distance.load(std::memory_order_relaxed) <= 0) {
            g_camera_request = {};
            g_camera_completed = {};
            g_camera_clearance.reset();
            return;
        }
        request = g_camera_request;
        g_camera_request = {};
    }
    if (request.pawn == 0 || request.pawn != frame.local_pawn ||
        GetTickCount64() - request.requested > 100) return;

    const auto result = trace_camera(request);
    std::lock_guard guard(g_camera_mutex);
    g_camera_completed = request;
    g_camera_clearance = result;
}

void hk_setup_view(void* client, void* setup) {
    g_setup_view(client, setup);
    static std::atomic<bool> entered{false};
    if (!entered.exchange(true, std::memory_order_relaxed)) log::info("visual hooks: SetupView callback active");

    const int fov_req = g_camera_fov.load(std::memory_order_relaxed);
    if (fov_req > 0 && (!g_local_scoped.load(std::memory_order_relaxed) ||
                        g_fov_while_scoped.load(std::memory_order_relaxed))) {
        write_camera_fov(setup, static_cast<float>(std::clamp(fov_req, 50, 135)));
    }

    const int distance = g_third_person_distance.load(std::memory_order_relaxed);
    if (distance <= 0) return;

    const auto snap = runtime::snapshot_publisher().acquire();
    if (!snap || !snap->in_game || !snap->local_alive || !snap->local_eye_valid || !snap->local_pawn) {
        return;
    }

    math::vec3 origin{};
    math::angles angles{};
    if (!read_camera_view(setup, origin, angles) ||
        (origin - snap->local_eye).length_sq() > 64.0f * 64.0f) return;
    const camera_query query{snap->local_pawn, origin, math::forward_from_angles(angles),
                             distance, GetTickCount64()};
    std::optional<float> clearance;
    if (GetCurrentThreadId() == g_game_thread.load(std::memory_order_relaxed)) {
        clearance = trace_camera(query);
    } else {
        std::lock_guard guard(g_camera_mutex);
        g_camera_request = query;
        const auto& done = g_camera_completed;
        if (g_camera_clearance && done.pawn == query.pawn && done.distance == query.distance &&
            query.requested >= done.requested && query.requested - done.requested <= 100) {
            const float eye_shift = (done.origin - origin).length();
            const float turn_shift = (done.forward - query.forward).length() * *g_camera_clearance;
            if (eye_shift <= 16.0f && done.forward.dot(query.forward) >= 0.94f) {
                clearance = std::max(0.0f, *g_camera_clearance - eye_shift - turn_shift);
            }
        }
    }

    if (clearance && *clearance > 0.1f &&
        write_camera_origin(setup, origin - query.forward * *clearance)) {
        static std::atomic<bool> reported{false};
        if (!reported.exchange(true, std::memory_order_relaxed)) {
            log::info("visual hooks: third-person camera origin written");
        }
    }
}

void* hk_draw_smoke(void* descriptor, void* object, std::uintptr_t a3, int a4, void* a5, void* a6) {
    if (g_no_smoke.load(std::memory_order_relaxed)) {
        static std::atomic<bool> reported{false};
        if (!reported.exchange(true, std::memory_order_relaxed)) log::info("visual hooks: smoke suppressed");
        return nullptr;
    }
    return g_draw_smoke(descriptor, object, a3, a4, a5, a6);
}

void hk_draw_scope(void* renderer, void* result) {
    g_draw_scope(renderer, result);
    if (result && g_no_scope.load(std::memory_order_relaxed)) {
        __try {
            *(static_cast<std::uint8_t*>(result) + k_scope_visible) = 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

void* __fastcall hk_generate_primitives(void* desc, void* object, void* context, void* buffer) {
    return features::chams::on_generate_primitives(desc, object, context, buffer,
                                                   g_generate_primitives);
}

std::int64_t __fastcall hk_cull_scene_objects(void* scene_system, void* draw_list, void* list,
                                              std::uint32_t begin, std::uint32_t end, void* output,
                                              void* pending, void* stats) {
    return features::chams::on_cull_scene_objects(scene_system, draw_list, list, begin, end, output,
                                                  pending, stats, g_cull_scene_objects);
}

std::uint8_t __fastcall hk_test_scene_pvs(const void* clusters, const std::uint32_t* bitmap) {
    return features::chams::on_test_scene_pvs(clusters, bitmap, _ReturnAddress(), g_test_scene_pvs);
}

std::uintptr_t __fastcall hk_draw_scene_primitive(void* draw_data, void* context, void* primitive,
                                                  void* material, void* shader_state,
                                                  void* binding_cache, char depth_pass, void* stats,
                                                  void* layer) {
    return features::chams::on_draw_scene_primitive(draw_data, context, primitive, material,
                                                    shader_state, binding_cache, depth_pass, stats,
                                                    layer, g_draw_scene_primitive);
}

void __fastcall hk_draw_occluded_dx11(void* context, std::uint32_t topology,
                                      std::uint32_t start_index, std::uint32_t index_count,
                                      std::uint32_t instance_count, std::uint32_t unused,
                                      std::int32_t base_vertex, std::uint32_t start_instance) {
    features::chams::on_draw_indexed(context, topology, start_index, index_count, instance_count,
                                     unused, base_vertex, start_instance, g_draw_occluded_dx11);
}

void __fastcall hk_draw_occluded_software(void* context, std::uint32_t topology,
                                          std::uint32_t start_index, std::uint32_t index_count,
                                          std::uint32_t instance_count, std::uint32_t unused,
                                          std::int32_t base_vertex, std::uint32_t start_instance) {
    features::chams::on_draw_indexed(context, topology, start_index, index_count, instance_count,
                                     unused, base_vertex, start_instance, g_draw_occluded_software);
}

void __fastcall hk_sort_primitives(void* context, void* layer, void* entries, std::uint32_t count) {
    g_sort_primitives(context, layer, entries, count);
    features::chams::on_sort_primitives(layer, entries, count);
}

void __fastcall hk_render_layer_draw_list(void* scene_system, void* draw_list, void* layer,
                                          void* batch_list, int start_flags, void* pages) {
    features::chams::on_render_layer_draw_list(layer, pages);
    g_render_layer_draw_list(scene_system, draw_list, layer, batch_list, start_flags, pages);
}

void __fastcall hk_base_draw_array(void* ptr, void* context, void* render_list, int count,
                                   void* view, void* layer, void* a6) {
    features::world::on_base_draw_array(ptr, context, render_list, count, view, layer, a6,
                                        g_base_draw_array);
}

void* __fastcall hk_setup_light(void* ptr, void* light_obj, void* unk) {
    return features::world::on_setup_light(ptr, light_obj, unk, g_setup_light);
}

void __fastcall hk_setup_tonemap(void* a1, float a2, void* a3) {
    features::world::on_setup_tonemap(a1, a2, a3, g_setup_tonemap);
}

void* __fastcall hk_view_punch(void* cam_svc, float* a2, float* a3, float* a4) {
    return features::world::on_view_punch(cam_svc, a2, a3, a4, g_view_punch);
}

bool ensure_minhook() {
    const auto st = MH_Initialize();
    if (st == MH_OK || st == MH_ERROR_ALREADY_INITIALIZED) return true;
    log::warn("hooks: MH_Initialize failed ({})", MH_StatusToString(st));
    return false;
}

template <typename Fn>
bool hook_fn(void* target, void* detour, Fn& original, const char* label) {
    if (!target) return false;
    void* orig = nullptr;
    const auto created = MH_CreateHook(target, detour, &orig);
    if (created != MH_OK || orig == nullptr) {
        log::warn("hooks: create {} failed ({})", label, MH_StatusToString(created));
        return false;
    }
    // Publish the trampoline before activating the detour so render/scene threads
    // cannot enter the hook and observe a null original pointer.
    original = reinterpret_cast<Fn>(orig);
    std::atomic_thread_fence(std::memory_order_release);
    const auto enabled = MH_EnableHook(target);
    if (enabled != MH_OK) {
        original = nullptr;
        log::warn("hooks: enable {} failed ({})", label, MH_StatusToString(enabled));
        return false;
    }
    return true;
}

void* vtable_slot_safe(void* object, std::size_t index) {
    if (!object) return nullptr;
    __try {
        return (*reinterpret_cast<void***>(object))[index];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void install_visual_callbacks() {
    const auto& anchors = runtime::anchors();
    if (!ensure_minhook()) return;

    if (anchors.setup_view_fn) {
        runtime::live().third_person_available =
            hook_fn(anchors.setup_view_fn, reinterpret_cast<void*>(&hk_setup_view), g_setup_view,
                    "SetupView");
    } else if (anchors.setup_view_shared_slot && anchors.setup_view_normal_slot &&
               *anchors.setup_view_shared_slot == *anchors.setup_view_normal_slot) {
        g_setup_view = reinterpret_cast<setup_view_t>(*anchors.setup_view_normal_slot);
        mark_cfg_valid(reinterpret_cast<void*>(&hk_setup_view));
        if (patch_pointer(anchors.setup_view_normal_slot, reinterpret_cast<void*>(&hk_setup_view), nullptr)) {
            if (patch_pointer(anchors.setup_view_shared_slot, reinterpret_cast<void*>(&hk_setup_view), nullptr)) {
                runtime::live().third_person_available = true;
            } else {
                patch_pointer(anchors.setup_view_normal_slot, reinterpret_cast<void*>(g_setup_view), nullptr);
            }
        }
    }

    if (anchors.draw_smoke_fn) {
        runtime::live().no_smoke_available =
            hook_fn(anchors.draw_smoke_fn, reinterpret_cast<void*>(&hk_draw_smoke), g_draw_smoke,
                    "DrawSmoke");
    } else if (anchors.draw_smoke_slot) {
        g_draw_smoke = reinterpret_cast<draw_smoke_t>(*anchors.draw_smoke_slot);
        mark_cfg_valid(reinterpret_cast<void*>(&hk_draw_smoke));
        if (patch_pointer(anchors.draw_smoke_slot, reinterpret_cast<void*>(&hk_draw_smoke), nullptr)) {
            runtime::live().no_smoke_available = true;
        }
    }

    if (anchors.draw_scope) {
        runtime::live().no_scope_available =
            hook_fn(anchors.draw_scope, reinterpret_cast<void*>(&hk_draw_scope), g_draw_scope,
                    "DrawScopeOverlay");
    }

    hook_fn(anchors.view_punch_fn, reinterpret_cast<void*>(&hk_view_punch), g_view_punch, "ViewPunch");

    // Chams (SceneSystem + RenderSystemDX11)
    void* gen_prims = vtable_slot_safe(anchors.animatable_desc, 4);
    const bool ch_gen = hook_fn(gen_prims, reinterpret_cast<void*>(&hk_generate_primitives),
                                g_generate_primitives, "GeneratePrimitives");
    const bool ch_sort = hook_fn(anchors.sort_primitives, reinterpret_cast<void*>(&hk_sort_primitives),
                                 g_sort_primitives, "SortPrimitives");
    const bool ch_rldl = hook_fn(anchors.render_layer_draw_list,
                                 reinterpret_cast<void*>(&hk_render_layer_draw_list),
                                 g_render_layer_draw_list, "RenderLayerDrawList");
    const bool ch_draw = hook_fn(anchors.draw_scene_primitive,
                                 reinterpret_cast<void*>(&hk_draw_scene_primitive),
                                 g_draw_scene_primitive, "DrawScenePrimitive");
    const bool ch_cull = hook_fn(anchors.cull_scene_objects,
                                 reinterpret_cast<void*>(&hk_cull_scene_objects),
                                 g_cull_scene_objects, "CullSceneObjects");
    const bool ch_pvs = hook_fn(anchors.test_scene_pvs, reinterpret_cast<void*>(&hk_test_scene_pvs),
                                g_test_scene_pvs, "TestScenePvs");
    const bool ch_dx11 = hook_fn(anchors.draw_occluded_dx11,
                                 reinterpret_cast<void*>(&hk_draw_occluded_dx11),
                                 g_draw_occluded_dx11, "DrawOccludedDx11");
    const bool ch_sw = hook_fn(anchors.draw_occluded_software,
                               reinterpret_cast<void*>(&hk_draw_occluded_software),
                               g_draw_occluded_software, "DrawOccludedSoftware");

    // World modulation
    const bool w_base = hook_fn(anchors.base_draw_array, reinterpret_cast<void*>(&hk_base_draw_array),
                                g_base_draw_array, "BaseDrawArray");
    const bool w_light = hook_fn(anchors.setup_light, reinterpret_cast<void*>(&hk_setup_light),
                                 g_setup_light, "SetupLight");
    const bool w_tone = hook_fn(anchors.setup_tonemap, reinterpret_cast<void*>(&hk_setup_tonemap),
                                g_setup_tonemap, "SetupTonemap");

    log::info("visual hooks: camera={} smoke={} scope={} chams(gen={} sort={} rldl={} draw={} cull={} pvs={} dx11={} sw={}) world(base={} light={} tone={})",
              runtime::live().third_person_available, runtime::live().no_smoke_available,
              runtime::live().no_scope_available, ch_gen, ch_sort, ch_rldl, ch_draw, ch_cull,
              ch_pvs, ch_dx11, ch_sw, w_base, w_light, w_tone);
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
    if (FAILED(swapchain->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr) return false;
    if (FAILED(device->GetDeviceRemovedReason())) {
        device->Release();
        return false;
    }
    device->GetImmediateContext(&ctx);
    if (ctx == nullptr) {
        device->Release();
        return false;
    }

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

struct SDL_Window;

struct relative_mouse {
    using focus_fn = SDL_Window* (*)();
    using get_fn = bool (*)(SDL_Window*);
    using set_fn = bool (*)(SDL_Window*, bool);
    using main_thread_fn = bool (*)();

    focus_fn focus = nullptr;
    get_fn get = nullptr;
    set_fn set = nullptr;
    main_thread_fn is_main_thread = nullptr;
    SDL_Window* suspended = nullptr;
};

relative_mouse g_mouse;

void resolve_mouse_api() {
    const auto sdl = GetModuleHandleW(L"SDL3.dll");
    if (sdl == nullptr) return;
    g_mouse.focus = reinterpret_cast<relative_mouse::focus_fn>(GetProcAddress(sdl, "SDL_GetKeyboardFocus"));
    g_mouse.get = reinterpret_cast<relative_mouse::get_fn>(GetProcAddress(sdl, "SDL_GetWindowRelativeMouseMode"));
    g_mouse.set = reinterpret_cast<relative_mouse::set_fn>(GetProcAddress(sdl, "SDL_SetWindowRelativeMouseMode"));
    g_mouse.is_main_thread = reinterpret_cast<relative_mouse::main_thread_fn>(GetProcAddress(sdl, "SDL_IsMainThread"));
}

void suspend_relative_mouse() {
    if (!g_mouse.focus || !g_mouse.get || !g_mouse.set || !g_mouse.is_main_thread ||
        !g_mouse.is_main_thread()) return;
    SDL_Window* window = g_mouse.focus();
    if (window != nullptr && g_mouse.get(window) && g_mouse.set(window, false)) {
        g_mouse.suspended = window;
    }
}

struct menu_input {
    UINT message = 0;
    WPARAM w = 0;
    LPARAM l = 0;
    unsigned modifiers = 0;
};

std::mutex g_menu_input_mutex;
std::array<menu_input, 256> g_menu_inputs{};
std::size_t g_menu_input_count = 0;
bool g_reset_menu_input = false;
unsigned g_capture_buttons = 0;

void reset_menu_input() {
    std::lock_guard guard(g_menu_input_mutex);
    g_menu_input_count = 0;
    g_reset_menu_input = true;
}

void queue_menu_input(UINT msg, WPARAM w, LPARAM l) {
    unsigned mods = 0;
    if (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) {
        if (GetKeyState(VK_CONTROL) < 0) mods |= 1;
        if (GetKeyState(VK_SHIFT) < 0) mods |= 2;
        if (GetKeyState(VK_MENU) < 0) mods |= 4;
        if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) mods |= 8;
    }
    const menu_input input{msg, w, l, mods};
    std::lock_guard guard(g_menu_input_mutex);
    if (msg == WM_MOUSEMOVE && g_menu_input_count != 0 &&
        g_menu_inputs[g_menu_input_count - 1].message == WM_MOUSEMOVE) {
        g_menu_inputs[g_menu_input_count - 1] = input;
        return;
    }
    if (g_menu_input_count == g_menu_inputs.size()) {
        g_menu_input_count = 0;
        g_reset_menu_input = true;
        return;
    }
    g_menu_inputs[g_menu_input_count++] = input;
}

int mouse_button(UINT msg, WPARAM w) {
    switch (msg) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: return 0;
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK: return 1;
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK: return 2;
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
            return GET_XBUTTON_WPARAM(w) == XBUTTON1 ? 3 : 4;
        default: return -1;
    }
}

bool mouse_button_down(UINT msg) {
    return msg != WM_LBUTTONUP && msg != WM_RBUTTONUP && msg != WM_MBUTTONUP &&
           msg != WM_XBUTTONUP;
}

void drain_menu_input(ImGuiIO& io) {
    std::array<menu_input, 256> pending{};
    std::size_t count = 0;
    bool reset = false;
    {
        std::lock_guard guard(g_menu_input_mutex);
        count = g_menu_input_count;
        std::copy_n(g_menu_inputs.begin(), count, pending.begin());
        g_menu_input_count = 0;
        reset = g_reset_menu_input;
        g_reset_menu_input = false;
    }
    if (reset) {
        io.ClearEventsQueue();
        io.ClearInputKeys();
        io.ClearInputMouse();
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto& input = pending[i];
        const int button = mouse_button(input.message, input.w);
        if (button >= 0 || input.message == WM_MOUSEMOVE) {
            io.AddMousePosEvent(static_cast<float>(static_cast<short>(LOWORD(input.l))),
                                static_cast<float>(static_cast<short>(HIWORD(input.l))));
            if (button >= 0) io.AddMouseButtonEvent(button, mouse_button_down(input.message));
            continue;
        }
        switch (input.message) {
            case WM_MOUSEWHEEL:
                io.AddMouseWheelEvent(0.0f, static_cast<float>(GET_WHEEL_DELTA_WPARAM(input.w)) / WHEEL_DELTA);
                break;
            case WM_MOUSEHWHEEL:
                io.AddMouseWheelEvent(-static_cast<float>(GET_WHEEL_DELTA_WPARAM(input.w)) / WHEEL_DELTA, 0.0f);
                break;
            case WM_CHAR:
                if (input.w <= 0xffff) io.AddInputCharacterUTF16(static_cast<ImWchar16>(input.w));
                break;
            case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: {
                io.AddKeyEvent(ImGuiMod_Ctrl, (input.modifiers & 1) != 0);
                io.AddKeyEvent(ImGuiMod_Shift, (input.modifiers & 2) != 0);
                io.AddKeyEvent(ImGuiMod_Alt, (input.modifiers & 4) != 0);
                io.AddKeyEvent(ImGuiMod_Super, (input.modifiers & 8) != 0);
                const auto key = ImGui_ImplWin32_KeyEventToImGuiKey(input.w, input.l);
                if (key != ImGuiKey_None) {
                    io.AddKeyEvent(key, input.message == WM_KEYDOWN || input.message == WM_SYSKEYDOWN);
                }
                break;
            }
            default:
                break;
        }
    }
}

void set_menu_open(HWND hwnd, bool open) {
    if (g_menu_open.exchange(open) == open) return;
    reset_menu_input();
    if (open) {
        suspend_relative_mouse();
        ClipCursor(nullptr);
    } else {
        g_capture_buttons = 0;
        if (GetCapture() == hwnd) ReleaseCapture();
        if (g_mouse.suspended && g_mouse.is_main_thread && g_mouse.is_main_thread() &&
            g_mouse.focus() == g_mouse.suspended) {
            g_mouse.set(g_mouse.suspended, true);
        }
        g_mouse.suspended = nullptr;
    }
    log::info("hooks: menu {}", open ? "opened" : "closed");
}

std::uint32_t sample_feature_binds(const config::config_v1& cfg, bool accepts_input) {
    using config::bound_feature;
    static std::array<config::bind_state, static_cast<unsigned>(bound_feature::count)> states{};
    struct setting {
        bound_feature feature;
        const config::keybind& bind;
        bool enabled;
    };
    const setting settings[] = {
        {bound_feature::rage, cfg.rage.toggle, cfg.rage.enabled},
        {bound_feature::anti_aim, cfg.anti_aim.toggle, cfg.anti_aim.enabled},
        {bound_feature::esp, cfg.visuals.toggle, cfg.visuals.enabled},
        {bound_feature::third_person, cfg.visuals.third_person_bind, cfg.visuals.third_person},
        {bound_feature::bhop, cfg.misc.movement.bhop_bind, cfg.misc.movement.bhop},
        {bound_feature::auto_strafe, cfg.misc.movement.auto_strafe_bind, cfg.misc.movement.auto_strafe},
        {bound_feature::peek_assist, cfg.misc.movement.peek_assist_bind, cfg.misc.movement.peek_assist},
    };
    const int menu_key = cfg.ui.menu_key > 0 ? cfg.ui.menu_key : VK_INSERT;
    std::uint32_t active = 0;
    for (const auto& setting : settings) {
        auto& state = states[static_cast<unsigned>(setting.feature)];
        const bool enabled = setting.enabled && setting.bind.vk != menu_key;
        const bool down = accepts_input && enabled && setting.bind.bound() &&
                          (GetAsyncKeyState(setting.bind.vk) & 0x8000) != 0;
        if (state.sample(setting.bind, enabled, accepts_input, down)) {
            active |= config::feature_bit(setting.feature);
        }
    }
    return active;
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
        auto& menu = runtime::menu();
        const bool was_open = menu.open;
        menu.open = g_menu_open.load();
        if (was_open && !menu.open) {
            runtime::cfg().save(config::store::default_dir() / "default.json");
        }
        ImGuiIO& io = ImGui::GetIO();
        drain_menu_input(io);
        io.MouseDrawCursor = menu.open;
        if (menu.open) io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
        else io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        runtime::frame_tick();
        if (g_render.begin(g_rtv, g_rtv_w, g_rtv_h)) {
            const auto snap = runtime::snapshot_publisher().acquire();
            const ImVec2 display = ImGui::GetIO().DisplaySize;
            const auto& raw_cfg = runtime::cfg().get();
            const bool accepts_input = !menu.open && game_has_focus(g_hwnd);
            static config::keybind_runtime g_keybind_runtime;
            config::config_v1 cfg = raw_cfg;
            const int menu_key = raw_cfg.ui.menu_key > 0 ? raw_cfg.ui.menu_key : VK_INSERT;
            g_keybind_runtime.poll(
                raw_cfg.keybinds,
                [](int vk) noexcept { return (GetAsyncKeyState(vk) & 0x8000) != 0; },
                accepts_input, menu_key);
            g_keybind_runtime.apply_overrides(cfg, raw_cfg.keybinds,
                                              &runtime::live().active_keybinds);
            const auto active = sample_feature_binds(cfg, accepts_input);
            {
                std::lock_guard guard(g_settings_mutex);
                g_settings = {cfg, active};
            }
            g_menu_key.store(cfg.ui.menu_key, std::memory_order_relaxed);
            runtime::live().active_binds = active;
            runtime::live().binds_sampled = true;
            g_third_person_distance.store(runtime::live().third_person_available &&
                                              (active & config::feature_bit(config::bound_feature::third_person))
                                              ? cfg.visuals.third_person_distance : 0,
                                          std::memory_order_relaxed);
            g_camera_fov.store(runtime::live().third_person_available ? cfg.visuals.camera_fov : 0,
                               std::memory_order_relaxed);
            g_fov_while_scoped.store(cfg.visuals.fov_while_scoped, std::memory_order_relaxed);
            g_no_smoke.store(runtime::live().no_smoke_available && cfg.visuals.no_smoke,
                             std::memory_order_relaxed);
            g_no_scope.store(runtime::live().no_scope_available && cfg.visuals.no_scope,
                             std::memory_order_relaxed);
            if (!snap || !snap->in_game) {
                features::chams::update_state(runtime::anchors(), runtime::netvars(), cfg.visuals, 0,
                                              0, false, false);
                features::world::update_config(cfg.visuals, runtime::netvars());
                features::audio::pump_hitsounds(nullptr, 0);
            }
            const auto stats = features::esp::draw(cfg, snap ? &*snap : nullptr,
                                                  runtime::backtrack(), runtime::live(),
                                                  static_cast<int>(display.x), static_cast<int>(display.y));
            features::world::draw_penetration_crosshair(cfg, runtime::anchors(), runtime::netvars(),
                                                        snap ? &*snap : nullptr,
                                                        static_cast<int>(display.x),
                                                        static_cast<int>(display.y));
            struct esp_sample {
                int players;
                bool view_matrix_ready;
                bool enabled;
                int box;
                int opacity;
                features::esp::draw_stats draw;
                bool operator==(const esp_sample&) const = default;
            };
            static std::optional<esp_sample> previous;
            static ULONGLONG next_report = 0;
            const ULONGLONG now = GetTickCount64();
            if (now >= next_report) {
                next_report = now + 2000;
                const esp_sample sample{snap ? snap->count : 0,
                                        snap && snap->view_matrix.usable_projection(),
                                        (active & config::feature_bit(config::bound_feature::esp)) != 0,
                                        static_cast<int>(cfg.visuals.box),
                                        cfg.visuals.esp_alpha, stats};
                if (sample != previous) {
                    log::info("esp: players={} matrix={} enabled={} box={} opacity={} enemies={} visible={} projected={} boxes={} arrows={} hp={} armor={} icons={} names={} ammo={}",
                              sample.players, sample.view_matrix_ready, sample.enabled, sample.box,
                              sample.opacity, stats.enemies, stats.visible, stats.projected,
                              stats.boxes, stats.arrows, stats.health_bars, stats.armor_bars,
                              stats.weapon_icons, stats.weapon_names, stats.ammo_labels);
                    previous = sample;
                }
                if (snap && !snap->view_matrix.usable_projection()) {
                    const auto& m = snap->view_matrix.m;
                    log::info("esp: matrix unusable [{:.3f} {:.3f} {:.3f} {:.3f}] "
                              "[{:.3f} {:.3f} {:.3f} {:.3f}] "
                              "[{:.3f} {:.3f} {:.3f} {:.3f}] "
                              "[{:.3f} {:.3f} {:.3f} {:.3f}]",
                              m[0][0], m[0][1], m[0][2], m[0][3], m[1][0], m[1][1], m[1][2],
                              m[1][3], m[2][0], m[2][1], m[2][2], m[2][3], m[3][0], m[3][1],
                              m[3][2], m[3][3]);
                }
                if (snap && snap->count > 0 && stats.projected == 0) {
                    log::info("esp: players present but nothing projected local={} eye=({:.0f},{:.0f},{:.0f})",
                              snap->local_index, snap->local_eye.x, snap->local_eye.y,
                              snap->local_eye.z);
                }
            }
            ui::draw_menu(runtime::cfg(), runtime::menu(), runtime::live());
            g_render.end();
        }
    }

    const HRESULT hr = g_present(swapchain, sync, flags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        log::warn("hooks: device lost on Present ({:#x}) — invalidating", static_cast<unsigned>(hr));
        release_rtv();
        g_render.shutdown();
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
    const int menu_key = g_menu_key.load(std::memory_order_relaxed);
    if ((msg == WM_KEYDOWN || msg == WM_KEYUP) && static_cast<int>(w) == menu_key) {
        if (msg == WM_KEYDOWN && (l & (1 << 30)) == 0) {
            set_menu_open(hwnd, !g_menu_open.load());
        }
        return 0;
    }

    if (g_menu_open.load()) {
        if (msg == WM_KILLFOCUS) set_menu_open(hwnd, false);
        if (msg == WM_CAPTURECHANGED && g_capture_buttons != 0) {
            g_capture_buttons = 0;
            reset_menu_input();
        }
        const int button = mouse_button(msg, w);
        if (button >= 0) {
            const unsigned mask = 1u << button;
            if (mouse_button_down(msg)) {
                if (g_capture_buttons == 0 && GetCapture() == nullptr) SetCapture(hwnd);
                g_capture_buttons |= mask;
            } else {
                g_capture_buttons &= ~mask;
                if (g_capture_buttons == 0 && GetCapture() == hwnd) ReleaseCapture();
            }
        }

        switch (msg) {
            case WM_INPUT:
                suspend_relative_mouse();
                return DefWindowProcW(hwnd, msg, w, l);
            case WM_MOUSEMOVE:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MBUTTONDBLCLK:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_XBUTTONDBLCLK:
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_CHAR:
                queue_menu_input(msg, w, l);
                return 1;
            case WM_SETCURSOR:
                if (LOWORD(l) == HTCLIENT) {
                    SetCursor(nullptr);
                    return TRUE;
                }
                break;
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

    // The class vtable is static image data; the bootstrap objects can go.
    swapchain->Release();
    ctx->Release();
    device->Release();
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    if (ok) log::info("hooks: Present/ResizeBuffers hooked");
    else log::error("hooks: swapchain patch failed");
    return ok;
}

using input_update_t = void* (*)(void*, int, float, bool);
using create_move_t = void(__fastcall*)(void*, int, bool);
using send_move_t = void(__fastcall*)(void*, int);
input_update_t g_input_update = nullptr;
create_move_t g_create_move = nullptr;
send_move_t g_send_move = nullptr;
features::createmove_state g_cm_state{};

template <typename T>
bool safe_read_mem(std::uintptr_t addr, T& out) noexcept {
    if (addr < 0x10000 || addr > 0x7FFFFFFFFFFFULL) return false;
    __try {
        out = *reinterpret_cast<const T*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool read_cmd_diag_seh(const sdk::anchors& anch, std::uintptr_t controller,
                       math::angles& out_angles, std::uint64_t& out_held) noexcept {
    out_angles = {};
    out_held = 0;
    __try {
        auto* mgr = sdk::get_command_manager(anch, controller);
        if (!mgr) return false;
        auto* cmd = mgr->get_user_command();
        if (!cmd || !cmd->pb.base || !cmd->pb.base->viewangles) return false;
        const float px = cmd->pb.base->viewangles->x;
        const float py = cmd->pb.base->viewangles->y;
        out_held = cmd->button_state.held;
        if (!std::isfinite(px) || !std::isfinite(py)) return false;
        out_angles = {px, py};
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void run_tick_pipeline(void* self) {
    static int calls = 0;
    static math::angles last_written{};
    static bool last_valid = false;
    static ULONGLONG next_report = 0;
    static bool was_in_game = false;

    ++calls;
    g_game_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    const sdk::anchors& anch = runtime::anchors();
    const sdk::netvars& nv = runtime::netvars();
    sdk::frame_snapshot& write = runtime::snapshot_publisher().write_buffer();
    LARGE_INTEGER perf_freq{}, perf_a{}, perf_b{};
    QueryPerformanceFrequency(&perf_freq);
    QueryPerformanceCounter(&perf_a);
    sdk::collect_frame(anch, nv, write);
    process_camera_request(write);
    game_settings settings{};
    {
        std::lock_guard guard(g_settings_mutex);
        settings = g_settings;
    }
    sdk::update_shot_events(anch.game_event_manager_cell, anch.client_base, anch.client_size,
                            write.local_controller, settings.config.misc.info.hitmarker,
                            &settings.config.misc.hitsound);
    features::audio::pump_hitsounds(anch.play_vsnd, anch.cvar);

    if (was_in_game && !write.in_game) {
        features::chams::clear_cache();
        features::world::clear_backups();
        features::audio::reset_hitmarker();
        runtime::backtrack().reset();
        g_cm_state = {};
    }
    was_in_game = write.in_game;

    int local_team = 0;
    bool local_scoped = false;
    bool local_resume_zoom = false;
    if (write.local_pawn != 0) {
        if (nv.ent_team != 0) safe_read_mem(write.local_pawn + nv.ent_team, local_team);
        std::uint8_t sc = 0, rz = 0;
        if (nv.cs_pawn_scoped != 0 && safe_read_mem(write.local_pawn + nv.cs_pawn_scoped, sc)) {
            local_scoped = sc != 0;
        }
        if (nv.cs_pawn_resume_zoom != 0 &&
            safe_read_mem(write.local_pawn + nv.cs_pawn_resume_zoom, rz)) {
            local_resume_zoom = rz != 0;
        }
    }
    g_local_scoped.store(local_scoped, std::memory_order_relaxed);
    features::chams::update_state(anch, nv, settings.config.visuals, write.local_pawn, local_team,
                                  local_scoped, local_resume_zoom);
    features::world::update_config(settings.config.visuals, nv);
    features::world::update_penetration_crosshair(settings.config, anch, nv, &write);

    if (write.in_game && settings.config.visuals.no_flash) {
        sdk::suppress_flash_overlay(nv, write.local_pawn);
    }

    if (write.in_game && sdk::trace_ready()) {
        static bool trace_selftest_done = false;
        if (!trace_selftest_done) {
            const float s[3] = {write.local_eye.x, write.local_eye.y, write.local_eye.z};
            const float e[3] = {write.local_eye.x, write.local_eye.y, write.local_eye.z - 500.0f};
            const float e2[3] = {write.local_eye.x, write.local_eye.y, write.local_eye.z + 500.0f};
            float down = 0.0f, up = 0.0f;
            if (write.local_eye_valid && sdk::trace_fraction(s, e, down) &&
                sdk::trace_fraction(s, e2, up)) {
                trace_selftest_done = true;
                log::info("trace: self-test down_blocked={} up_blocked={}", down < 1.0f, up < 1.0f);
            }
        }
    }

    math::angles entry_angles{};
    std::uint64_t btn0 = 0;
    const bool entry_ok = read_cmd_diag_seh(anch, write.local_controller, entry_angles, btn0);

    if (g_menu_open.load() || !game_has_focus(g_hwnd)) {
        g_cm_state.delay = {};
        g_cm_state.brake_commands = 0;
        g_cm_state.bhop = {};
        g_cm_state.peek_assist = {};
        g_cm_state.ground_move = {};
        g_cm_state.freestanding = {};
        features::reset_strafe_state(g_cm_state);
        g_cm_state.bhop_space_latched = false;
        g_cm_state.tick_latch = {};
        g_cm_state.auto_pistol_wait = 0;
        g_cm_state.auto_pistol_release = false;
        g_cm_state.last_auto_pistol_cmd = -1;
        g_cm_state.rage_cache = {};
    } else {
        features::run_createmove(reinterpret_cast<std::uintptr_t>(self), anch, nv, settings.config,
                                 write, runtime::backtrack(), g_cm_state, settings.active_binds);
    }
    runtime::snapshot_publisher().publish();
    QueryPerformanceCounter(&perf_b);
    {
        static double accum_us = 0.0;
        static int samples = 0;
        if (perf_freq.QuadPart > 0) {
            accum_us += static_cast<double>(perf_b.QuadPart - perf_a.QuadPart) * 1e6 /
                        static_cast<double>(perf_freq.QuadPart);
            ++samples;
        }
        if (samples >= 256) {
            log::info("perf: pipeline avg {:.1f} us ({} samples)", accum_us / samples, samples);
            accum_us = 0.0;
            samples = 0;
        }
    }

    const ULONGLONG now = GetTickCount64();
    if (now >= next_report) {
        next_report = now + 2000;
        math::angles exit_angles{};
        std::uint64_t btn0_after = 0;
        const bool exit_ok = read_cmd_diag_seh(anch, write.local_controller, exit_angles, btn0_after);
        const bool persisted =
            last_valid && entry_ok &&
            std::fabs(entry_angles.pitch - last_written.pitch) < 0.01f &&
            std::fabs(math::yaw_delta(entry_angles.yaw, last_written.yaw)) < 0.01f;
        log::info("cm: calls={} in_game={} local={} players={} entry=({:.1f},{:.1f}) "
                  "exit=({:.1f},{:.1f}) persisted={} btn0={:x}->{:x}",
                  calls, write.in_game ? 1 : 0, write.local_index, write.count,
                  entry_angles.pitch, entry_angles.yaw, exit_angles.pitch, exit_angles.yaw,
                  persisted ? 1 : 0, btn0, btn0_after);
        last_written = exit_angles;
        last_valid = exit_ok;
    }
}

void __fastcall hk_create_move(void* self, int slot, bool active) {
    g_create_move(self, slot, active);
    if (slot == 0) run_tick_pipeline(self);
}

void __fastcall hk_send_move(void* self, int slot) {
    math::angles saved{};
    bool ok = false;
    __try {
        saved = reinterpret_cast<sdk::csgo_input_view*>(self)->viewangles();
        ok = std::isfinite(saved.pitch) && std::isfinite(saved.yaw);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    g_send_move(self, slot);
    if (ok) {
        __try {
            reinterpret_cast<sdk::csgo_input_view*>(self)->set_viewangles(saved);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

void* hk_input_update(void* self, int a, float frame_time, bool active) {
    void* result = g_input_update(self, a, frame_time, active);
    if (a == 0) run_tick_pipeline(self);
    return result;
}

bool install_input_hooks() {
    const auto& anchors = runtime::anchors();
    if (!anchors.input_ok) {
        log::warn("hooks: input hooks skipped — input anchor failed");
        return false;
    }

    ensure_minhook();

    bool cm_ok = false;
    if (anchors.create_move != 0) {
        cm_ok = hook_fn(reinterpret_cast<void*>(anchors.create_move),
                        reinterpret_cast<void*>(&hk_create_move), g_create_move,
                        "CCSGOInput::CreateMove[5]");
    }
    if (!cm_ok && anchors.input_update != 0) {
        auto vtable = *reinterpret_cast<void***>(anchors.input);
        g_input_update = reinterpret_cast<input_update_t>(anchors.input_update);
        cm_ok = patch_pointer(&vtable[4], reinterpret_cast<void*>(&hk_input_update), nullptr);
        if (cm_ok) {
            log::info("hooks: fallback input update vtable[4] hooked (was {:p})",
                      reinterpret_cast<void*>(g_input_update));
        }
    }

    bool sm_ok = false;
    if (anchors.send_move != 0) {
        sm_ok = hook_fn(reinterpret_cast<void*>(anchors.send_move),
                        reinterpret_cast<void*>(&hk_send_move), g_send_move,
                        "CCSGOInput::SendMove[8]");
    }
    log::info("hooks: input create_move={} send_move={}", cm_ok, sm_ok);
    return cm_ok;
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
                            (mbi.Protect & PAGE_GUARD) == 0 &&
                            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                            PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                            PAGE_EXECUTE_WRITECOPY)) != 0;
        if (usable) {
            // A guard page or a race with a freeing thread faults mid-scan;
            // keep the walk inside SEH so one bad region cannot kill the game.
            __try {
                const auto* begin = static_cast<const std::uintptr_t*>(mbi.BaseAddress);
                const std::size_t count = mbi.RegionSize / sizeof(std::uintptr_t);
                for (std::size_t i = 0; i < count; ++i) {
                    if (begin[i] != vtable_bits) continue;
                    auto* candidate =
                        reinterpret_cast<void*>(const_cast<std::uintptr_t*>(begin + i));
                    if (probe_swapchain(candidate, hwnd)) return candidate;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                // region went away or is partially unmapped: skip it
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
    g_menu_key.store(runtime::cfg().get().ui.menu_key, std::memory_order_relaxed);
    resolve_mouse_api();
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
    install_visual_callbacks();
    if (install_window()) install_swapchain(g_hwnd);
    install_input_hooks();
    return g_graphics;
}

}  // namespace shogod::hooks
