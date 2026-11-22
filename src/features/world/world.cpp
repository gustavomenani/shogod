#include "features/world/world.hpp"

#include "features/math/angles.hpp"
#include "imgui.h"
#include "sdk/trace.hpp"
#include "sdk/usercmd.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string_view>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace shogod::features::world {
namespace {

struct rgba8 {
    std::uint8_t r, g, b, a;
};

struct mesh_primitive {
    void* data;
    char pad0[0x10];
    void* scene_animatable_object;
    void* material;
    void* material2;
    char pad1[0x10];
    void* some_data;
    char pad2[0x8];
    rgba8 color;
    char pad3[0x1C];
};
static_assert(sizeof(mesh_primitive) == 0x70);

struct light_backup {
    std::uintptr_t address = 0;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    bool valid = false;
};

std::mutex g_mutex;
config::visuals_cfg g_cfg{};
sdk::netvars g_nv{};
std::atomic<bool> g_world_mod{false};
std::atomic<std::uint32_t> g_world_col_packed{0xFFFFFFFFu};
std::atomic<float> g_exposure{0.0f};
std::atomic<bool> g_remove_punch{false};
std::atomic<std::int32_t> g_off_punch_vec{0};
std::atomic<std::int32_t> g_off_punch_tick{0};
std::atomic<std::int32_t> g_off_punch_ratio{0};
std::array<light_backup, 256> g_light_backups{};
int g_light_count = 0;
bool g_was_light = false;

rgba8 to_rgba8(const config::rgba& c) noexcept {
    auto u = [](float v) {
        return static_cast<std::uint8_t>(std::clamp(static_cast<int>(v * 255.0f + 0.5f), 0, 255));
    };
    return {u(c.r), u(c.g), u(c.b), u(c.a)};
}

const char* read_material_name_seh(void* material) noexcept {
    if (!material) return nullptr;
    __try {
        auto** vt = *reinterpret_cast<void***>(material);
        using name_fn = const char* (*)(void*);
        return reinterpret_cast<name_fn>(vt[0])(material);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void apply_tonemap_brightness_seh(void* a1, float brightness) noexcept {
    if (!a1) return;
    __try {
        auto** vt = *reinterpret_cast<void***>(a1);
        using fn_t = void (*)(void*, float);
        reinterpret_cast<fn_t>(vt[5])(a1, brightness);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void zero_view_punch_seh(void* cam_svc, std::int32_t off_vec, std::int32_t off_tick,
                         std::int32_t off_ratio) noexcept {
    if (!cam_svc || off_vec == 0) return;
    auto* base = static_cast<std::byte*>(cam_svc);
    __try {
        float zero[3]{};
        std::memcpy(base + off_vec, zero, sizeof zero);
        if (off_tick != 0) *reinterpret_cast<std::int32_t*>(base + off_tick) = -1;
        if (off_ratio != 0) *reinterpret_cast<float*>(base + off_ratio) = 0.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

}  // namespace

void update_config(const config::visuals_cfg& cfg, const sdk::netvars& nv) noexcept {
    const rgba8 packed = to_rgba8(cfg.world_color);
    const std::uint32_t bits = static_cast<std::uint32_t>(packed.r) |
                               (static_cast<std::uint32_t>(packed.g) << 8) |
                               (static_cast<std::uint32_t>(packed.b) << 16) |
                               (static_cast<std::uint32_t>(packed.a) << 24);
    g_world_col_packed.store(bits, std::memory_order_relaxed);
    g_world_mod.store(cfg.world_modulate, std::memory_order_relaxed);
    g_exposure.store(cfg.exposure, std::memory_order_relaxed);
    g_off_punch_vec.store(nv.cam_view_punch, std::memory_order_relaxed);
    g_off_punch_tick.store(nv.cam_view_punch_tick, std::memory_order_relaxed);
    g_off_punch_ratio.store(nv.cam_view_punch_ratio, std::memory_order_relaxed);
    g_remove_punch.store(cfg.remove_view_punch, std::memory_order_relaxed);

    std::lock_guard lock(g_mutex);
    g_cfg = cfg;
    g_nv = nv;
}

void clear_backups() noexcept {
    std::lock_guard lock(g_mutex);
    g_light_count = 0;
    g_light_backups = {};
    g_was_light = false;
}

void on_base_draw_array(void* ptr, void* context, void* render_list, int count,
                        void* view, void* layer, void* a6,
                        base_draw_array_fn original) noexcept {
    if (!g_world_mod.load(std::memory_order_relaxed) || !render_list || count <= 0) {
        original(ptr, context, render_list, count, view, layer, a6);
        return;
    }

    auto* list = static_cast<mesh_primitive*>(render_list);
    const std::uint32_t bits = g_world_col_packed.load(std::memory_order_relaxed);
    const rgba8 col{static_cast<std::uint8_t>(bits & 0xFFu),
                    static_cast<std::uint8_t>((bits >> 8) & 0xFFu),
                    static_cast<std::uint8_t>((bits >> 16) & 0xFFu),
                    static_cast<std::uint8_t>((bits >> 24) & 0xFFu)};
    for (int i = 0; i < count; ++i) {
        if (!list[i].material) continue;
        const char* raw_name = read_material_name_seh(list[i].material);
        if (!raw_name) continue;
        const std::string_view name(raw_name);
        if (name.find("sprays") != std::string_view::npos ||
            name.find("stickers") != std::string_view::npos ||
            name.find("patches") != std::string_view::npos ||
            name.find("glass") != std::string_view::npos ||
            name.find("blood") != std::string_view::npos ||
            name.find("materials/models/weapons") != std::string_view::npos ||
            name.find("weapons/models") != std::string_view::npos ||
            name.find("characters/models") != std::string_view::npos ||
            name.find("compmat_") != std::string_view::npos ||
            name.find("_ignorez") != std::string_view::npos) {
            continue;
        }
        list[i].color = col;
    }

    original(ptr, context, render_list, count, view, layer, a6);
}

void* on_setup_light(void* ptr, void* light_obj, void* unk, setup_light_fn original) noexcept {
    if (!light_obj) return original(ptr, light_obj, unk);

    config::visuals_cfg cfg{};
    {
        std::lock_guard lock(g_mutex);
        cfg = g_cfg;
    }

    auto* color = reinterpret_cast<float*>(static_cast<std::byte*>(light_obj) + 0xE4);
    const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(light_obj);

    if (!cfg.light_modulate && g_was_light) {
        for (int i = 0; i < g_light_count; ++i) {
            if (g_light_backups[i].valid && g_light_backups[i].address == addr) {
                color[0] = g_light_backups[i].r;
                color[1] = g_light_backups[i].g;
                color[2] = g_light_backups[i].b;
                color[3] = g_light_backups[i].a;
            }
        }
        g_light_count = 0;
        g_light_backups = {};
    }
    g_was_light = cfg.light_modulate;

    if (cfg.light_modulate) {
        int idx = -1;
        for (int i = 0; i < g_light_count; ++i) {
            if (g_light_backups[i].valid && g_light_backups[i].address == addr) {
                idx = i;
                break;
            }
        }
        if (idx == -1 && g_light_count < static_cast<int>(g_light_backups.size())) {
            idx = g_light_count++;
            g_light_backups[idx] = {addr, color[0], color[1], color[2], color[3], true};
        }
        if (idx >= 0) {
            color[0] = g_light_backups[idx].r * cfg.light_color.r;
            color[1] = g_light_backups[idx].g * cfg.light_color.g;
            color[2] = g_light_backups[idx].b * cfg.light_color.b;
            color[3] = g_light_backups[idx].a * cfg.light_color.a;
        }
    }

    return original(ptr, light_obj, unk);
}

void on_setup_tonemap(void* a1, float a2, void* a3, setup_tonemap_fn original) noexcept {
    original(a1, a2, a3);
    const float exp = g_exposure.load(std::memory_order_relaxed);
    const float brightness = (exp > 0.5f) ? (exp / 100.0f) : 1.0f;
    apply_tonemap_brightness_seh(a1, brightness);
}

void* on_view_punch(void* cam_svc, float* a2, float* a3, float* a4,
                    view_punch_fn original) noexcept {
    const std::int32_t off_vec = g_off_punch_vec.load(std::memory_order_relaxed);
    if (g_remove_punch.load(std::memory_order_relaxed) && cam_svc && off_vec != 0) {
        zero_view_punch_seh(cam_svc, off_vec, g_off_punch_tick.load(std::memory_order_relaxed),
                            g_off_punch_ratio.load(std::memory_order_relaxed));
    }
    return original(cam_svc, a2, a3, a4);
}

namespace {

std::atomic<bool> g_pen_valid{false};
std::atomic<bool> g_pen_ok{false};
std::atomic<float> g_pen_dmg{0.0f};

}  // namespace

void update_penetration_crosshair(const config::config_v1& cfg, const sdk::anchors& anchors,
                                  const sdk::netvars& nv, const sdk::frame_snapshot* frame) noexcept {
    if (!cfg.visuals.penetration_crosshair || !frame || !frame->in_game ||
        !frame->local_alive || !frame->local_eye_valid || !sdk::bullet_trace_ready() ||
        !anchors.input_ok || anchors.input == 0) {
        g_pen_valid.store(false, std::memory_order_relaxed);
        return;
    }

    sdk::weapon_info wep{};
    if (!sdk::read_active_weapon(anchors.entity_system, nv, frame->local_pawn, wep) ||
        wep.damage <= 0 || wep.range <= 10.0f) {
        g_pen_valid.store(false, std::memory_order_relaxed);
        return;
    }

    const auto* input_view = reinterpret_cast<const sdk::csgo_input_view*>(anchors.input);
    const math::vec3 dir = math::forward_from_angles(input_view->viewangles()).normalised();
    if (dir.length_sq() < 0.5f) {
        g_pen_valid.store(false, std::memory_order_relaxed);
        return;
    }

    const float s[3] = {frame->local_eye.x, frame->local_eye.y, frame->local_eye.z};
    const float d[3] = {dir.x, dir.y, dir.z};
    float dmg = 0.0f;
    const bool pen = sdk::trace_wall_damage(s, d, frame->local_pawn, frame->local_team,
                                            static_cast<float>(wep.damage), wep.penetration,
                                            wep.range_modifier, wep.range, dmg);
    g_pen_ok.store(pen, std::memory_order_relaxed);
    g_pen_dmg.store(dmg, std::memory_order_relaxed);
    g_pen_valid.store(true, std::memory_order_release);
}

void draw_penetration_crosshair(const config::config_v1& cfg, const sdk::anchors&,
                                const sdk::netvars&, const sdk::frame_snapshot* frame,
                                int screen_w, int screen_h) noexcept {
    if (!cfg.visuals.penetration_crosshair || !frame || !frame->in_game || !frame->local_alive ||
        !g_pen_valid.load(std::memory_order_acquire)) {
        return;
    }
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    const bool pen = g_pen_ok.load(std::memory_order_relaxed);
    const float dmg = g_pen_dmg.load(std::memory_order_relaxed);
    const ImVec2 center(static_cast<float>(screen_w) * 0.5f, static_cast<float>(screen_h) * 0.5f);
    const ImU32 col = pen ? IM_COL32(85, 235, 130, 235) : IM_COL32(235, 85, 85, 195);
    dl->AddCircleFilled(center, 3.0f, IM_COL32(10, 12, 16, 175));
    dl->AddCircleFilled(center, 2.2f, col);
    if (pen && dmg >= 1.0f) {
        char buf[16]{};
        std::snprintf(buf, sizeof buf, "%.0f", dmg);
        dl->AddText(ImVec2(center.x + 8.0f, center.y + 5.0f), col, buf);
    }
}

}  // namespace shogod::features::world
