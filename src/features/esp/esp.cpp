#include "features/esp/esp.hpp"

#include "config/bind_state.hpp"
#include "features/audio/hitsounds.hpp"
#include "features/esp/esp_layout.hpp"

#include "brand.hpp"
#include "imgui.h"
#include "sdk/bones.hpp"
#include "ui/cs2_weapon_icons.hpp"
#include "ui/style.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string_view>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace shogod::features::esp {
namespace {

ImU32 with_alpha(const config::rgba& c, int alpha_0_255) {
    const int a = std::clamp(static_cast<int>(c.a * static_cast<float>(alpha_0_255)), 0, 255);
    return IM_COL32(static_cast<int>(c.r * 255.0f), static_cast<int>(c.g * 255.0f),
                    static_cast<int>(c.b * 255.0f), a);
}

ImU32 scale_alpha(ImU32 col, int alpha_0_255) {
    const int base_a = (col >> IM_COL32_A_SHIFT) & 0xFF;
    const int a = (base_a * std::clamp(alpha_0_255, 0, 255)) / 255;
    return (col & ~(0xFFu << IM_COL32_A_SHIFT)) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
}

void draw_corner_box(ImDrawList* dl, float x, float y, float w, float h, ImU32 col, ImU32 outline) {
    const float cw = std::max(4.0f, w * 0.24f);
    const float ch = std::max(4.0f, h * 0.20f);
    const float x2 = x + w;
    const float y2 = y + h;

    auto corner = [&](float px, float py, float dx, float dy) {
        dl->AddLine(ImVec2(px, py), ImVec2(px + dx, py), outline, 3.0f);
        dl->AddLine(ImVec2(px, py), ImVec2(px, py + dy), outline, 3.0f);
        dl->AddLine(ImVec2(px, py), ImVec2(px + dx, py), col, 1.0f);
        dl->AddLine(ImVec2(px, py), ImVec2(px, py + dy), col, 1.0f);
    };
    corner(x, y, cw, ch);
    corner(x2, y, -cw, ch);
    corner(x, y2, cw, -ch);
    corner(x2, y2, -cw, -ch);
}

std::array<math::vec3, 8> player_box_corners(const sdk::player_snapshot& p) {
    const float height = p.eye.z > p.origin.z + 10.0f ? p.eye.z - p.origin.z + 10.0f : 72.0f;
    const auto mins = p.origin + (p.bounds_valid ? p.bounds_min : math::vec3{-16.0f, -16.0f, 0.0f});
    const auto maxs = p.origin + (p.bounds_valid ? p.bounds_max : math::vec3{16.0f, 16.0f, height});
    return {{{mins.x, mins.y, mins.z}, {maxs.x, mins.y, mins.z},
             {maxs.x, maxs.y, mins.z}, {mins.x, maxs.y, mins.z},
             {mins.x, mins.y, maxs.z}, {maxs.x, mins.y, maxs.z},
             {maxs.x, maxs.y, maxs.z}, {mins.x, maxs.y, maxs.z}}};
}

bool draw_3d_box(ImDrawList* dl, const sdk::player_snapshot& p, const math::mat4& vm, int sw, int sh,
                 ImU32 col) {
    const auto pts = player_box_corners(p);
    ImVec2 scr[8]{};
    for (int i = 0; i < 8; ++i) {
        math::vec2 s{};
        if (!vm.world_to_screen(pts[i], sw, sh, s)) return false;
        scr[i] = ImVec2(s.x, s.y);
    }
    for (int i = 0; i < 4; ++i) {
        const int next = (i + 1) & 3;
        dl->AddLine(scr[i], scr[next], col, 1.0f);
        dl->AddLine(scr[i + 4], scr[next + 4], col, 1.0f);
        dl->AddLine(scr[i], scr[i + 4], col, 1.0f);
    }
    return true;
}

void draw_skeleton(ImDrawList* dl, const math::vec3* bones, int bone_count, const math::mat4& vm,
                   int sw, int sh, ImU32 col) {
    for (const auto& [a, b] : sdk::k_skeleton_links) {
        if (a >= bone_count || b >= bone_count) continue;
        math::vec2 sa{}, sb{};
        if (vm.world_to_screen(bones[a], sw, sh, sa) &&
            vm.world_to_screen(bones[b], sw, sh, sb)) {
            dl->AddLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y), col, 1.2f);
        }
    }
}

void draw_watermark(ImDrawList* dl, const ui::status& live, int sw) {
    char buf[128]{};
    if (live.in_game) {
        std::snprintf(buf, sizeof buf, "%s | %d fps | %d tick | %d players",
                      brand::k_name.data(), static_cast<int>(live.fps), live.tick, live.players);
    } else {
        std::snprintf(buf, sizeof buf, "%s | %d fps | anchors %d/%d",
                      brand::k_name.data(), static_cast<int>(live.fps), live.anchors_ok,
                      live.anchors_total);
    }
    const ImVec2 sz = ImGui::CalcTextSize(buf);
    const float pad_x = 10.0f;
    const float pad_y = 5.0f;
    const float x = static_cast<float>(sw) - sz.x - pad_x * 2.0f - 14.0f;
    const float y = 14.0f;
    const auto& pal = ui::colors();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + sz.x + pad_x * 2.0f, y + sz.y + pad_y * 2.0f),
                      IM_COL32(16, 18, 24, 210), 4.0f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + sz.x + pad_x * 2.0f, y + 2.0f), pal.accent, 2.0f);
    dl->AddText(ImVec2(x + pad_x, y + pad_y), IM_COL32(238, 242, 248, 245), buf);
}

void draw_offscreen_arrow(ImDrawList* dl, const sdk::player_snapshot& p,
                          const sdk::frame_snapshot& frame, const config::config_v1& cfg,
                          int screen_w, int screen_h) {
    // Camera forward is the negated w-row of worldToProjection (row 3 holds
    // -forward for the Source view-projection layout).
    const float fx = -frame.view_matrix.m[3][0];
    const float fy = -frame.view_matrix.m[3][1];
    if (std::fabs(fx) + std::fabs(fy) < 1e-4f) return;

    const float cam_yaw = std::atan2(fy, fx);
    const float dx = p.origin.x - frame.local_origin.x;
    const float dy = p.origin.y - frame.local_origin.y;
    if (std::fabs(dx) + std::fabs(dy) < 1e-3f) return;
    const float target_yaw = std::atan2(dy, dx);

    // Screen-space angle: 0 = straight ahead (top), positive = right.
    float rel = target_yaw - cam_yaw;
    while (rel > 3.14159265f) rel -= 6.28318531f;
    while (rel < -3.14159265f) rel += 6.28318531f;

    const float cx = static_cast<float>(screen_w) * 0.5f;
    const float cy = static_cast<float>(screen_h) * 0.5f;
    const float radius = std::min(static_cast<float>(screen_w), static_cast<float>(screen_h)) * 0.42f;
    const float ax = cx + std::sin(rel) * radius;
    const float ay = cy - std::cos(rel) * radius;

    const float alpha = static_cast<float>(cfg.visuals.esp_alpha) / 255.0f;
    const ImU32 col = scale_alpha(with_alpha(cfg.visuals.box_color, 255), cfg.visuals.esp_alpha);

    const float dir_x = std::sin(rel);
    const float dir_y = -std::cos(rel);
    const float perp_x = std::cos(rel);
    const float perp_y = std::sin(rel);
    const float tip = 14.0f;
    const float base = 7.0f;
    const ImVec2 a(ax + dir_x * tip, ay + dir_y * tip);
    const ImVec2 b(ax - dir_x * base + perp_x * base, ay - dir_y * base + perp_y * base);
    const ImVec2 c(ax - dir_x * base - perp_x * base, ay - dir_y * base - perp_y * base);
    dl->AddTriangleFilled(a, b, c, col);

    const float dist_m = (p.origin - frame.local_origin).length() * 0.01905f;
    char label[16]{};
    std::snprintf(label, sizeof label, "%.0fm", dist_m);
    dl->AddText(ImVec2(ax - 14.0f, ay + 16.0f), scale_alpha(IM_COL32(235, 240, 250, 255),
                                                             static_cast<int>(alpha * 255.0f)),
                label);
}

const config::rage_weapon_spec* resolve_weapon_spec(const sdk::player_snapshot& p) noexcept {
    if (p.weapon_id > 0) {
        if (const auto* by_def = config::find_rage_weapon_by_def(p.weapon_id)) return by_def;
    }
    if (p.weapon_name[0] != '\0') {
        if (const auto* by_key = config::find_rage_weapon_by_key(p.weapon_name)) return by_key;
    }
    return nullptr;
}

bool draw_weapon_icon(ImDrawList* dl, const sdk::player_snapshot& p, float center, float y,
                      ImU32 color) {
    if (!dl || (p.weapon_id == 0 && p.weapon_name[0] == '\0')) return false;
    int icon_idx = -1;
    if (const auto* spec = resolve_weapon_spec(p)) {
        icon_idx = spec->icon_index;
    } else {
        switch (static_cast<config::weapon_group>(p.weapon_family)) {
            case config::weapon_group::pistol:
                icon_idx = static_cast<int>(ui::cs2_weapon_icon::usp_silencer);
                break;
            case config::weapon_group::heavy_pistol:
                icon_idx = static_cast<int>(ui::cs2_weapon_icon::deagle);
                break;
            case config::weapon_group::smg:
                icon_idx = static_cast<int>(ui::cs2_weapon_icon::mp9);
                break;
            case config::weapon_group::scout:
                icon_idx = static_cast<int>(ui::cs2_weapon_icon::ssg08);
                break;
            case config::weapon_group::auto_sniper:
                icon_idx = static_cast<int>(ui::cs2_weapon_icon::scar20);
                break;
            case config::weapon_group::awp:
                icon_idx = static_cast<int>(ui::cs2_weapon_icon::awp);
                break;
            case config::weapon_group::heavy:
                icon_idx = static_cast<int>(ui::cs2_weapon_icon::m249);
                break;
            default:
                icon_idx = static_cast<int>(ui::cs2_weapon_icon::ak47);
                break;
        }
    }
    ui::draw_cs2_weapon_icon(*dl, ImVec2(center, y + 6.5f), 34.0f, 12.0f, icon_idx, color);
    return true;
}

void draw_keybinds(ImDrawList* dl, const config::config_v1& cfg, const ui::status& live,
                   float min_y = 260.0f) {
    using config::bound_feature;
    const auto active = [&](bound_feature feature, bool enabled) {
        return live.binds_sampled ? (live.active_binds & config::feature_bit(feature)) != 0 : enabled;
    };
    struct entry {
        const char* name;
        bool active;
    };
    const entry items[] = {
        {"Ragebot", active(bound_feature::rage, cfg.rage.enabled)},
        {"Anti-Aim", active(bound_feature::anti_aim, cfg.anti_aim.enabled) && !cfg.misc.block_server_detected},
        {"Player ESP", active(bound_feature::esp, cfg.visuals.enabled)},
        {"Third person", live.third_person_available && active(bound_feature::third_person, cfg.visuals.third_person)},
        {"Pose history", cfg.visuals.backtrack_ghost && cfg.rage.backtrack},
        {"Bunny hop", active(bound_feature::bhop, cfg.misc.movement.bhop)},
        {"Auto strafe", active(bound_feature::auto_strafe, cfg.misc.movement.auto_strafe) && !cfg.misc.block_server_detected},
        {"Peek assist", active(bound_feature::peek_assist, cfg.misc.movement.peek_assist)},
        {"Auto pistol", cfg.misc.utilities.auto_pistol && !cfg.misc.block_server_detected},
        {"Safe mode", cfg.misc.block_server_detected},
    };
    int active_count = 0;
    for (const auto& it : items) {
        if (it.active) ++active_count;
    }
    if (active_count == 0) return;

    const float x = 14.0f;
    float y = std::max(260.0f, min_y);
    const float w = 160.0f;
    const float h = 22.0f + active_count * 18.0f + 6.0f;
    const float row_h = 18.0f;
    const auto& pal = ui::colors();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(14, 17, 24, 240), 5.0f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), pal.border, 5.0f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + 2.0f), pal.accent, 2.0f);
    dl->AddText(ImVec2(x + 8.0f, y + 5.0f), IM_COL32(238, 242, 248, 240), "Keybinds");
    y += 24.0f;
    for (const auto& it : items) {
        if (!it.active) continue;
        dl->AddText(ImVec2(x + 8.0f, y), IM_COL32(200, 206, 218, 230), it.name);
        dl->AddText(ImVec2(x + w - 28.0f, y), pal.accent, "[on]");
        y += row_h;
    }
}

float draw_spectators(ImDrawList* dl, const sdk::frame_snapshot* frame) {
    if (!frame || !frame->in_game || frame->spectator_count <= 0) return 0.0f;

    const int n = std::min(frame->spectator_count, sdk::k_max_spectators);
    float max_name_w = ImGui::CalcTextSize("Spectators").x + 36.0f;
    for (int i = 0; i < n; ++i) {
        max_name_w = std::max(max_name_w, ImGui::CalcTextSize(frame->spectators[i].name).x + 34.0f);
    }
    const float x = 14.0f;
    const float top_y = 64.0f;
    float y = top_y;
    const float w = std::clamp(max_name_w, 160.0f, 250.0f);
    const float row_h = 18.0f;
    const float h = 24.0f + static_cast<float>(n) * row_h + 6.0f;
    const auto& pal = ui::colors();

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(14, 17, 24, 236), 5.0f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), pal.border, 5.0f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + 2.0f), pal.accent, 2.0f);
    dl->AddText(ImVec2(x + 9.0f, y + 5.0f), IM_COL32(238, 242, 248, 242), "Spectators");

    y += 24.0f;
    for (int i = 0; i < n; ++i) {
        const auto& sp = frame->spectators[i];
        const ImU32 team_dot = (sp.team == 2)   ? IM_COL32(235, 200, 120, 235)
                               : (sp.team == 3) ? IM_COL32(140, 195, 245, 235)
                                                : IM_COL32(210, 214, 224, 220);
        dl->AddCircleFilled(ImVec2(x + 12.0f, y + 7.5f), 2.5f, team_dot);
        dl->AddText(ImVec2(x + 21.0f, y), IM_COL32(210, 216, 228, 235), sp.name);
        y += row_h;
    }
    return top_y + h;
}

void draw_hitmarker(ImDrawList* dl, const config::config_v1& cfg, const sdk::frame_snapshot* frame,
                    int sw, int sh) {
    if (!cfg.misc.info.hitmarker || !frame || !frame->in_game || !frame->local_alive) return;
    bool headshot = false;
    const float alpha = features::audio::hitmarker_alpha(GetTickCount64(), headshot);
    if (alpha <= 0.0f) return;

    const ImVec2 c(static_cast<float>(sw) * 0.5f, static_cast<float>(sh) * 0.5f);
    const config::rgba base_col = headshot
        ? config::rgba{1.0f, 0.35f, 0.35f, cfg.misc.info.hitmarker_color.a}
        : cfg.misc.info.hitmarker_color;
    const ImU32 col = IM_COL32(static_cast<int>(base_col.r * 255.0f),
                               static_cast<int>(base_col.g * 255.0f),
                               static_cast<int>(base_col.b * 255.0f),
                               std::clamp(static_cast<int>(base_col.a * alpha * 255.0f), 0, 255));
    const ImU32 shadow = IM_COL32(0, 0, 0, std::clamp(static_cast<int>(alpha * 180.0f), 0, 255));
    const float inner = 4.0f;
    const float outer = headshot ? 11.0f : 9.0f;
    for (int sx : {-1, 1}) {
        for (int sy : {-1, 1}) {
            const ImVec2 p0(c.x + static_cast<float>(sx) * inner, c.y + static_cast<float>(sy) * inner);
            const ImVec2 p1(c.x + static_cast<float>(sx) * outer, c.y + static_cast<float>(sy) * outer);
            dl->AddLine(ImVec2(p0.x + 1.0f, p0.y + 1.0f), ImVec2(p1.x + 1.0f, p1.y + 1.0f),
                        shadow, 2.0f);
            dl->AddLine(p0, p1, col, 1.5f);
        }
    }
}

void draw_bar_glow(ImDrawList* dl, ImVec2 min_p, ImVec2 max_p, ImU32 col) {
    if (!dl || max_p.x <= min_p.x || max_p.y <= min_p.y) return;
    const int r = (col >> IM_COL32_R_SHIFT) & 0xFF;
    const int g = (col >> IM_COL32_G_SHIFT) & 0xFF;
    const int b = (col >> IM_COL32_B_SHIFT) & 0xFF;
    const int a = (col >> IM_COL32_A_SHIFT) & 0xFF;
    for (int step = 1; step <= 3; ++step) {
        const float pad = static_cast<float>(step) * 1.4f;
        const int ga = (a * (4 - step)) / 14;
        dl->AddRectFilled(ImVec2(min_p.x - pad, min_p.y - pad),
                          ImVec2(max_p.x + pad, max_p.y + pad), IM_COL32(r, g, b, ga), 1.5f);
    }
}

}  // namespace

screen_box compute_player_box(const sdk::player_snapshot& player, const math::mat4& vm,
                              int screen_w, int screen_h) noexcept {
    screen_box out{};
    if (!player.alive || screen_w <= 0 || screen_h <= 0) return out;

    if (player.bounds_valid) {
        const float limit_x = static_cast<float>(screen_w);
        const float limit_y = static_cast<float>(screen_h);
        float left = limit_x, right = 0.0f;
        float top = limit_y, bottom = 0.0f;
        int projected = 0;
        for (const auto& corner : player_box_corners(player)) {
            math::vec2 point{};
            if (!vm.world_to_screen(corner, screen_w, screen_h, point)) continue;
            ++projected;
            const float x = std::clamp(point.x, 0.0f, limit_x);
            const float y = std::clamp(point.y, 0.0f, limit_y);
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }
        if (projected >= 2 && right - left >= 2.0f && bottom - top >= 4.0f) {
            return {left, top, right - left, bottom - top, true};
        }
    }

    math::vec2 feet{}, head{};
    const math::vec3 top_world{player.origin.x, player.origin.y,
                               std::max(player.eye.z + 8.0f, player.origin.z + 72.0f)};
    if (!vm.world_to_screen(player.origin, screen_w, screen_h, feet) ||
        !vm.world_to_screen(top_world, screen_w, screen_h, head)) {
        return out;
    }

    const float h = feet.y - head.y;
    if (!std::isfinite(h) || h < 4.0f || h > static_cast<float>(screen_h) * 3.0f) return out;
    const float w = h * 0.52f;
    out.x = head.x - w * 0.5f;
    out.y = head.y;
    out.w = w;
    out.h = h;
    out.valid = true;
    return out;
}

draw_stats draw(const config::config_v1& cfg, const sdk::frame_snapshot* frame,
          const backtrack_history& bt, const ui::status& live, int screen_w, int screen_h) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (dl == nullptr || screen_w <= 0 || screen_h <= 0) return {};

    if (cfg.misc.info.watermark) draw_watermark(dl, live, screen_w);
    const float spec_bottom = cfg.misc.info.spectator_list ? draw_spectators(dl, frame) : 0.0f;
    if (cfg.misc.info.keybind_list) {
        draw_keybinds(dl, cfg, live, spec_bottom > 0.0f ? (spec_bottom + 8.0f) : 260.0f);
    }
    draw_hitmarker(dl, cfg, frame, screen_w, screen_h);

    if (frame == nullptr || !frame->in_game || !frame->view_matrix.usable_projection()) return {};

    // Bomb timer
    if (cfg.visuals.bomb_timer && frame->bomb_planted) {
        math::vec2 bpos{};
        if (frame->view_matrix.world_to_screen(frame->bomb_pos, screen_w, screen_h, bpos)) {
            char label[48]{};
            const char site = frame->bomb_site == 1 ? 'B' : 'A';
            const float rem = std::max(0.0f, frame->bomb_blow_time - frame->curtime);
            std::snprintf(label, sizeof label, "C4 [%c] %.1fs", site, rem);
            dl->AddCircleFilled(ImVec2(bpos.x, bpos.y), 4.0f, IM_COL32(255, 80, 70, 240));
            dl->AddText(ImVec2(bpos.x + 8.0f, bpos.y - 7.0f), IM_COL32(255, 190, 90, 250), label);
        }
    }

    const bool esp_active = live.binds_sampled
        ? (live.active_binds & config::feature_bit(config::bound_feature::esp)) != 0
        : cfg.visuals.enabled;
    if (!esp_active || cfg.visuals.esp_alpha <= 0) return {};

    draw_stats stats{};

    const int alpha = cfg.visuals.esp_alpha;
    const ImU32 outline = scale_alpha(IM_COL32(10, 12, 16, 200), alpha);

    for (int i = 0; i < frame->count; ++i) {
        const sdk::player_snapshot& p = frame->players[i];
        if (!p.alive || !p.enemy || p.pawn == frame->local_pawn) continue;
        ++stats.enemies;
        if (p.visible) ++stats.visible;
        if (cfg.visuals.visible_only && !p.visible) continue;

        // Occluded players stay visible (ESP) but dimmed so line of sight is immediately clear.
        const int shade = p.visible ? alpha : std::max(40, alpha * 45 / 100);

        if (cfg.visuals.backtrack_ghost && cfg.rage.backtrack) {
            const auto rec = bt.best(p.index, p.pawn_handle, cfg.rage.backtrack_ms, frame->curtime);
            if (rec && rec->bone_count > 0) {
                draw_skeleton(dl, rec->bones, rec->bone_count, frame->view_matrix, screen_w,
                              screen_h, scale_alpha(IM_COL32(140, 180, 255, 110), shade));
            }
        }

        if (cfg.visuals.skeleton && p.bone_count > 0) {
            draw_skeleton(dl, p.bones, p.bone_count, frame->view_matrix, screen_w, screen_h,
                          with_alpha(cfg.visuals.skeleton_color, shade));
        }

        const screen_box box = compute_player_box(p, frame->view_matrix, screen_w, screen_h);
        if (!box.valid) {
            if (cfg.visuals.offscreen && p.visible) {
                draw_offscreen_arrow(dl, p, *frame, cfg, screen_w, screen_h);
                ++stats.arrows;
            }
            continue;
        }
        ++stats.projected;
        const ImU32 player_box = with_alpha(cfg.visuals.box_color, shade);
        const ImU32 player_outline = scale_alpha(IM_COL32(10, 12, 16, 200), shade);
        const int vertices_before_box = dl->VtxBuffer.Size;

        switch (cfg.visuals.box) {
            case config::box_style::flat_2d:
                dl->AddRect(ImVec2(box.x - 1, box.y - 1),
                            ImVec2(box.x + box.w + 1, box.y + box.h + 1), player_outline, 0.0f,
                            0, 1.0f);
                dl->AddRect(ImVec2(box.x, box.y), ImVec2(box.x + box.w, box.y + box.h), player_box,
                            0.0f, 0, 1.0f);
                break;
            case config::box_style::corner:
                draw_corner_box(dl, box.x, box.y, box.w, box.h, player_box, player_outline);
                break;
            case config::box_style::box_3d:
                if (!draw_3d_box(dl, p, frame->view_matrix, screen_w, screen_h, player_box)) {
                    draw_corner_box(dl, box.x, box.y, box.w, box.h, player_box, player_outline);
                }
                break;
            default:
                break;
        }

        if (dl->VtxBuffer.Size > vertices_before_box) ++stats.boxes;

        // Shared layout pass matching interactive 3D Preview placement & ordering.
        std::vector<layout_item> layout_items;
        layout_items.reserve(config::k_esp_elem_count);

        const auto slot_for = [&](config::esp_elem_id eid) -> const config::esp_elem_slot& {
            return cfg.visuals.esp_layout[static_cast<std::size_t>(eid)];
        };
        const auto add_elem = [&](config::esp_elem_id eid, float w, float h, bool full_span) {
            const auto& slot = slot_for(eid);
            layout_item it{};
            it.id = static_cast<int>(eid);
            it.side = std::clamp(slot.side, 0, 3);
            it.order = slot.order;
            it.sz = {w, h, full_span, 0.0f};
            layout_items.push_back(it);
        };

        if (cfg.visuals.health_bar) {
            const int s = slot_for(config::esp_elem_id::health_bar).side;
            add_elem(config::esp_elem_id::health_bar, is_horizontal_side(s) ? box.w : 4.0f,
                     is_horizontal_side(s) ? 4.0f : box.h, true);
        }
        if (cfg.visuals.armor_bar && p.armor > 0) {
            const int s = slot_for(config::esp_elem_id::armor_bar).side;
            add_elem(config::esp_elem_id::armor_bar, is_horizontal_side(s) ? box.w : 3.0f,
                     is_horizontal_side(s) ? 3.0f : box.h, true);
        }
        if (cfg.visuals.name && p.name[0] != '\0') {
            const ImVec2 tsz = ImGui::CalcTextSize(p.name);
            add_elem(config::esp_elem_id::name, tsz.x, tsz.y, false);
        }
        const bool has_icon =
            cfg.visuals.weapon_icon && (p.weapon_id != 0 || p.weapon_name[0] != '\0');
        if (has_icon) {
            add_elem(config::esp_elem_id::weapon_icon, 36.0f, 13.0f, false);
        }
        const auto* spec = resolve_weapon_spec(p);
        const char* wep_label = (spec && spec->name[0] != '\0') ? spec->name : p.weapon_name;
        const bool has_name = cfg.visuals.weapon && wep_label[0] != '\0';
        if (has_name) {
            const ImVec2 tsz = ImGui::CalcTextSize(wep_label);
            add_elem(config::esp_elem_id::weapon_name, tsz.x, tsz.y, false);
        }
        char ammo_buf[16]{};
        const bool has_ammo = cfg.visuals.ammo && p.ammo >= 0;
        if (has_ammo) {
            std::snprintf(ammo_buf, sizeof ammo_buf, "[%d]", p.ammo);
            const ImVec2 tsz = ImGui::CalcTextSize(ammo_buf);
            add_elem(config::esp_elem_id::ammo, tsz.x, tsz.y, false);
        }
        char dist_buf[24]{};
        if (cfg.visuals.distance) {
            const float dist_m = (p.origin - frame->local_origin).length() * 0.01905f;
            std::snprintf(dist_buf, sizeof dist_buf, "%.0fm", dist_m);
            const ImVec2 tsz = ImGui::CalcTextSize(dist_buf);
            add_elem(config::esp_elem_id::distance, tsz.x, tsz.y, false);
        }

        char money_buf[24]{};
        char ping_buf[16]{};
        if (cfg.visuals.flags) {
            auto add_flag_txt = [&](config::esp_elem_id fid, const char* txt) {
                const ImVec2 tsz = ImGui::CalcTextSize(txt);
                add_elem(fid, tsz.x, tsz.y, false);
            };
            if (cfg.visuals.flag_scoped && p.scoped) add_flag_txt(config::esp_elem_id::flag_scoped, "ZOOM");
            if (cfg.visuals.flag_defuse && p.defusing) add_flag_txt(config::esp_elem_id::flag_defuse, "DEFUSE");
            if (cfg.visuals.flag_flash && p.flashing) add_flag_txt(config::esp_elem_id::flag_flash, "FLASH");
            if (cfg.visuals.flag_kit && p.has_kit) add_flag_txt(config::esp_elem_id::flag_kit, "KIT");
            if (cfg.visuals.flag_reload && p.reloading) add_flag_txt(config::esp_elem_id::flag_reload, "RELOAD");
            if (cfg.visuals.flag_armor && p.armor > 0) {
                add_flag_txt(config::esp_elem_id::flag_armor, p.helmet ? "HK" : "K");
            }
            if (cfg.visuals.flag_money && p.money > 0) {
                std::snprintf(money_buf, sizeof money_buf, "$%d", p.money);
                add_flag_txt(config::esp_elem_id::flag_money, money_buf);
            }
            if (cfg.visuals.flag_ping && p.ping > 0) {
                std::snprintf(ping_buf, sizeof ping_buf, "%dms", p.ping);
                add_flag_txt(config::esp_elem_id::flag_ping, ping_buf);
            }
        }

        compute_layout(layout_items, box.x, box.y, box.x + box.w, box.y + box.h, 4.0f, 3.0f, 1.0f);

        for (const auto& it : layout_items) {
            const float x0 = it.box.min_x;
            const float y0 = it.box.min_y;
            const float x1 = it.box.max_x;
            const float y1 = it.box.max_y;
            const float cx = (x0 + x1) * 0.5f;
            const bool horiz = is_horizontal_side(it.side);

            switch (static_cast<config::esp_elem_id>(it.id)) {
                case config::esp_elem_id::health_bar: {
                    const float hp_frac = std::clamp(static_cast<float>(p.health) / 100.0f, 0.0f, 1.0f);
                    const ImU32 hp_top = cfg.visuals.health_bar_gradient
                        ? with_alpha(cfg.visuals.health_bar_grad_color, alpha)
                        : with_alpha(cfg.visuals.health_bar_color, alpha);
                    const ImU32 hp_bot = with_alpha(cfg.visuals.health_bar_color, alpha);
                    if (cfg.visuals.health_bar_glow) {
                        draw_bar_glow(dl, ImVec2(x0, y0), ImVec2(x1, y1), hp_bot);
                    }
                    dl->AddRectFilled(ImVec2(x0 - 1, y0 - 1), ImVec2(x1 + 1, y1 + 1), outline);
                    if (horiz) {
                        const float fill_w = (x1 - x0) * hp_frac;
                        dl->AddRectFilledMultiColor(ImVec2(x0, y0), ImVec2(x0 + fill_w, y1),
                                                    hp_top, hp_bot, hp_bot, hp_top);
                    } else {
                        const float fill_h = (y1 - y0) * hp_frac;
                        dl->AddRectFilledMultiColor(ImVec2(x0, y1 - fill_h), ImVec2(x1, y1),
                                                    hp_top, hp_top, hp_bot, hp_bot);
                    }
                    ++stats.health_bars;
                    if (p.health < 100) {
                        char hp_txt[16]{};
                        std::snprintf(hp_txt, sizeof hp_txt, "%d", p.health);
                        const float fill_h = (y1 - y0) * hp_frac;
                        const ImVec2 hsz = ImGui::CalcTextSize(hp_txt);
                        const float hx = horiz ? (x0 + (x1 - x0) * hp_frac - hsz.x * 0.5f)
                                               : (it.side == side_right ? x1 + 2.0f : x0 - hsz.x - 2.0f);
                        const float hy = horiz ? (y0 - hsz.y * 0.35f)
                                               : std::max(0.0f, y1 - fill_h - hsz.y * 0.5f);
                        dl->AddText(ImVec2(hx, hy), scale_alpha(IM_COL32(255, 255, 255, 235), alpha),
                                    hp_txt);
                    }
                    break;
                }
                case config::esp_elem_id::armor_bar: {
                    const float ar_frac = std::clamp(static_cast<float>(p.armor) / 100.0f, 0.0f, 1.0f);
                    const ImU32 ar_col = with_alpha(cfg.visuals.armor_bar_color, alpha);
                    if (cfg.visuals.armor_bar_glow) {
                        draw_bar_glow(dl, ImVec2(x0, y0), ImVec2(x1, y1), ar_col);
                    }
                    dl->AddRectFilled(ImVec2(x0 - 1, y0 - 1), ImVec2(x1 + 1, y1 + 1), outline);
                    if (horiz) {
                        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + (x1 - x0) * ar_frac, y1), ar_col);
                    } else {
                        dl->AddRectFilled(ImVec2(x0, y1 - (y1 - y0) * ar_frac), ImVec2(x1, y1), ar_col);
                    }
                    ++stats.armor_bars;
                    break;
                }
                case config::esp_elem_id::name:
                    dl->AddText(ImVec2(x0, y0), with_alpha(cfg.visuals.name_color, alpha), p.name);
                    break;
                case config::esp_elem_id::weapon_icon:
                    if (draw_weapon_icon(
                            dl, p,
                            std::clamp(cx, 22.0f, std::max(22.0f, static_cast<float>(screen_w) - 22.0f)),
                            y0, with_alpha(cfg.visuals.weapon_icon_color, alpha))) {
                        ++stats.weapon_icons;
                    }
                    break;
                case config::esp_elem_id::weapon_name:
                    dl->AddText(ImVec2(x0, y0), with_alpha(cfg.visuals.weapon_color, alpha), wep_label);
                    ++stats.weapon_names;
                    break;
                case config::esp_elem_id::ammo:
                    dl->AddText(ImVec2(x0, y0), with_alpha(cfg.visuals.ammo_color, alpha), ammo_buf);
                    ++stats.ammo_labels;
                    break;
                case config::esp_elem_id::distance:
                    dl->AddText(ImVec2(x0, y0), with_alpha(cfg.visuals.distance_color, alpha), dist_buf);
                    break;
                case config::esp_elem_id::flag_scoped:
                    dl->AddText(ImVec2(x0, y0), scale_alpha(IM_COL32(125, 210, 255, 235), alpha), "ZOOM");
                    break;
                case config::esp_elem_id::flag_defuse:
                    dl->AddText(ImVec2(x0, y0), scale_alpha(IM_COL32(255, 110, 95, 245), alpha), "DEFUSE");
                    break;
                case config::esp_elem_id::flag_flash:
                    dl->AddText(ImVec2(x0, y0), scale_alpha(IM_COL32(255, 225, 120, 235), alpha), "FLASH");
                    break;
                case config::esp_elem_id::flag_kit:
                    dl->AddText(ImVec2(x0, y0), scale_alpha(IM_COL32(160, 235, 160, 220), alpha), "KIT");
                    break;
                case config::esp_elem_id::flag_reload:
                    dl->AddText(ImVec2(x0, y0), scale_alpha(IM_COL32(255, 185, 95, 235), alpha), "RELOAD");
                    break;
                case config::esp_elem_id::flag_armor:
                    dl->AddText(ImVec2(x0, y0), scale_alpha(IM_COL32(165, 205, 255, 225), alpha),
                                p.helmet ? "HK" : "K");
                    break;
                case config::esp_elem_id::flag_money:
                    dl->AddText(ImVec2(x0, y0), scale_alpha(IM_COL32(140, 235, 140, 225), alpha), money_buf);
                    break;
                case config::esp_elem_id::flag_ping:
                    dl->AddText(ImVec2(x0, y0), scale_alpha(IM_COL32(205, 212, 226, 215), alpha), ping_buf);
                    break;
                default:
                    break;
            }
        }
    }
    return stats;
}

float draw_spectators(ImDrawList* dl, const sdk::frame_snapshot* frame) {
    if (!frame || !frame->in_game || frame->spectator_count <= 0) return 0.0f;

    const int n = std::min(frame->spectator_count, sdk::k_max_spectators);
    float max_name_w = ImGui::CalcTextSize("Spectators").x + 36.0f;
    for (int i = 0; i < n; ++i) {
        max_name_w = std::max(max_name_w, ImGui::CalcTextSize(frame->spectators[i].name).x + 34.0f);
    }
    const float x = 14.0f;
    const float top_y = 64.0f;
    float y = top_y;
    const float w = std::clamp(max_name_w, 160.0f, 250.0f);
    const float row_h = 18.0f;
    const float h = 24.0f + static_cast<float>(n) * row_h + 6.0f;
    const auto& pal = ui::colors();

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(14, 17, 24, 236), 5.0f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), pal.border, 5.0f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + 2.0f), pal.accent, 2.0f);
    dl->AddText(ImVec2(x + 9.0f, y + 5.0f), IM_COL32(238, 242, 248, 242), "Spectators");

    y += 24.0f;
    for (int i = 0; i < n; ++i) {
        const auto& sp = frame->spectators[i];
        const ImU32 team_dot = (sp.team == 2)   ? IM_COL32(235, 200, 120, 235)
                               : (sp.team == 3) ? IM_COL32(140, 195, 245, 235)
                                                : IM_COL32(210, 214, 224, 220);
        dl->AddCircleFilled(ImVec2(x + 12.0f, y + 7.5f), 2.5f, team_dot);
        dl->AddText(ImVec2(x + 21.0f, y), IM_COL32(210, 216, 228, 235), sp.name);
        y += row_h;
    }
    return top_y + h;
}

void draw_hitmarker(ImDrawList* dl, const config::config_v1& cfg, const sdk::frame_snapshot* frame,
                    int sw, int sh) {
    if (!cfg.misc.info.hitmarker || !frame || !frame->in_game || !frame->local_alive) return;
    bool headshot = false;
    const float alpha = features::audio::hitmarker_alpha(GetTickCount64(), headshot);
    if (alpha <= 0.0f) return;

    const ImVec2 c(static_cast<float>(sw) * 0.5f, static_cast<float>(sh) * 0.5f);
    const config::rgba base_col = headshot
        ? config::rgba{1.0f, 0.35f, 0.35f, cfg.misc.info.hitmarker_color.a}
        : cfg.misc.info.hitmarker_color;
    const ImU32 col = IM_COL32(static_cast<int>(base_col.r * 255.0f),
                               static_cast<int>(base_col.g * 255.0f),
                               static_cast<int>(base_col.b * 255.0f),
                               std::clamp(static_cast<int>(base_col.a * alpha * 255.0f), 0, 255));
    const ImU32 shadow = IM_COL32(0, 0, 0, std::clamp(static_cast<int>(alpha * 180.0f), 0, 255));
    const float inner = 4.0f;
    const float outer = headshot ? 11.0f : 9.0f;
    for (int sx : {-1, 1}) {
        for (int sy : {-1, 1}) {
            const ImVec2 p0(c.x + static_cast<float>(sx) * inner, c.y + static_cast<float>(sy) * inner);
            const ImVec2 p1(c.x + static_cast<float>(sx) * outer, c.y + static_cast<float>(sy) * outer);
            dl->AddLine(ImVec2(p0.x + 1.0f, p0.y + 1.0f), ImVec2(p1.x + 1.0f, p1.y + 1.0f),
                        shadow, 2.0f);
            dl->AddLine(p0, p1, col, 1.5f);
        }
    }
}

void draw_bar_glow(ImDrawList* dl, ImVec2 min_p, ImVec2 max_p, ImU32 col) {
    if (!dl || max_p.x <= min_p.x || max_p.y <= min_p.y) return;
    const int r = (col >> IM_COL32_R_SHIFT) & 0xFF;
    const int g = (col >> IM_COL32_G_SHIFT) & 0xFF;
    const int b = (col >> IM_COL32_B_SHIFT) & 0xFF;
    const int a = (col >> IM_COL32_A_SHIFT) & 0xFF;
    for (int step = 1; step <= 3; ++step) {
        const float pad = static_cast<float>(step) * 1.4f;
        const int ga = (a * (4 - step)) / 14;
        dl->AddRectFilled(ImVec2(min_p.x - pad, min_p.y - pad),
                          ImVec2(max_p.x + pad, max_p.y + pad), IM_COL32(r, g, b, ga), 1.5f);
    }
}

}  // namespace shogod::features::esp
