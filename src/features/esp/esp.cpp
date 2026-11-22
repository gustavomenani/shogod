#include "features/esp/esp.hpp"

#include "config/bind_state.hpp"

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

void draw_keybinds(ImDrawList* dl, const config::config_v1& cfg, const ui::status& live) {
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
    float y = 260.0f;
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
    if (cfg.misc.info.keybind_list) draw_keybinds(dl, cfg, live);

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
                          scale_alpha(IM_COL32(240, 245, 255, 210), shade));
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

        if (cfg.visuals.health_bar) {
            const float hp_frac = std::clamp(static_cast<float>(p.health) / 100.0f, 0.0f, 1.0f);
            const float bx = std::clamp(box.x >= 9.0f ? box.x - 8.0f : box.x + 2.0f,
                                        1.0f, std::max(1.0f, static_cast<float>(screen_w) - 6.0f));
            const float fill_h = box.h * hp_frac;
            const ImU32 hp_col = scale_alpha(
                IM_COL32(static_cast<int>((1.0f - hp_frac) * 230.0f + 25.0f),
                         static_cast<int>(hp_frac * 220.0f + 25.0f), 60, 255),
                alpha);
            dl->AddRectFilled(ImVec2(bx - 1, box.y - 1), ImVec2(bx + 5, box.y + box.h + 1), outline);
            dl->AddRectFilled(ImVec2(bx, box.y + box.h - fill_h), ImVec2(bx + 4, box.y + box.h),
                              hp_col);
            ++stats.health_bars;
            if (p.health < 100) {
                char hp_txt[16]{};
                std::snprintf(hp_txt, sizeof hp_txt, "%d", p.health);
                const float hp_x = bx >= 23.0f ? bx - 22.0f : bx + 6.0f;
                dl->AddText(ImVec2(hp_x, std::max(0.0f, box.y + box.h - fill_h - 6.0f)),
                            scale_alpha(IM_COL32(255, 255, 255, 230), alpha), hp_txt);
            }
        }

        if (cfg.visuals.armor_bar && p.armor > 0) {
            const float ar_frac = std::clamp(static_cast<float>(p.armor) / 100.0f, 0.0f, 1.0f);
            const float ay = box.y + box.h + 3.0f;
            dl->AddRectFilled(ImVec2(box.x - 1, ay - 1), ImVec2(box.x + box.w + 1, ay + 3), outline);
            dl->AddRectFilled(ImVec2(box.x, ay), ImVec2(box.x + box.w * ar_frac, ay + 2),
                              scale_alpha(IM_COL32(95, 175, 255, 245), alpha));
            ++stats.armor_bars;
        }

        if (cfg.visuals.name && p.name[0] != '\0') {
            const ImVec2 tsz = ImGui::CalcTextSize(p.name);
            dl->AddText(ImVec2(box.x + (box.w - tsz.x) * 0.5f, box.y - tsz.y - 2.0f),
                        scale_alpha(IM_COL32(242, 245, 250, 240), alpha), p.name);
        }

        float bottom_y = box.y + box.h + (cfg.visuals.armor_bar && p.armor > 0 ? 6.0f : 3.0f);
        const float center_x = box.x + box.w * 0.5f;
        const bool has_icon = cfg.visuals.weapon_icon && (p.weapon_id != 0 || p.weapon_name[0] != '\0');
        const bool has_name = cfg.visuals.weapon && p.weapon_name[0] != '\0';
        const bool has_ammo = cfg.visuals.ammo && p.ammo >= 0;
        const bool has_dist = cfg.visuals.distance;
        const float weapon_height = (has_icon ? 15.0f : 0.0f) +
            ImGui::GetTextLineHeightWithSpacing() * ((has_name ? 1 : 0) + (has_ammo ? 1 : 0) + (has_dist ? 1 : 0));
        if (weapon_height > 0.0f) {
            bottom_y = std::clamp(bottom_y, 0.0f,
                                  std::max(0.0f, static_cast<float>(screen_h) - weapon_height));
        }
        const auto weapon_color = scale_alpha(IM_COL32(215, 222, 235, 230), alpha);
        if (has_icon && draw_weapon_icon(dl, p, std::clamp(center_x, 22.0f,
                                   std::max(22.0f, static_cast<float>(screen_w) - 22.0f)),
                                        bottom_y, weapon_color)) {
            bottom_y += 15.0f;
            ++stats.weapon_icons;
        }
        const auto weapon_text = [&](const char* value) {
            const auto size = ImGui::CalcTextSize(value);
            const float x = std::clamp(center_x - size.x * 0.5f, 0.0f,
                                       std::max(0.0f, static_cast<float>(screen_w) - size.x));
            dl->AddText(ImVec2(x, bottom_y), weapon_color, value);
            bottom_y += ImGui::GetTextLineHeightWithSpacing();
        };
        if (has_name) {
            const auto* spec = resolve_weapon_spec(p);
            weapon_text(spec ? spec->name : p.weapon_name);
            ++stats.weapon_names;
        }
        if (has_ammo) {
            char ammo[16]{};
            std::snprintf(ammo, sizeof ammo, "[%d]", p.ammo);
            weapon_text(ammo);
            ++stats.ammo_labels;
        }

        if (cfg.visuals.distance) {
            const float dist_m = (p.origin - frame->local_origin).length() * 0.01905f;
            char dbuf[24]{};
            std::snprintf(dbuf, sizeof dbuf, "%.0fm", dist_m);
            const ImVec2 dsz = ImGui::CalcTextSize(dbuf);
            dl->AddText(ImVec2(box.x + (box.w - dsz.x) * 0.5f, bottom_y),
                        scale_alpha(IM_COL32(175, 185, 200, 210), alpha), dbuf);
        }

        if (cfg.visuals.flags) {
            float fy = box.y;
            const float fx = box.x + box.w + 4.0f;
            auto flag = [&](const char* txt, ImU32 col) {
                dl->AddText(ImVec2(fx, fy), scale_alpha(col, alpha), txt);
                fy += 13.0f;
            };
            if (p.scoped) flag("ZOOM", IM_COL32(125, 210, 255, 235));
            if (p.defusing) flag("DEFUSE", IM_COL32(255, 110, 95, 245));
            if (p.flashing) flag("FLASH", IM_COL32(255, 225, 120, 235));
            if (p.has_kit) flag("KIT", IM_COL32(160, 235, 160, 220));
        }
    }
    return stats;
}

}  // namespace shogod::features::esp
