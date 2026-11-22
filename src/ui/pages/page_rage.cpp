#include "ui/pages/pages.hpp"

#include "ui/cs2_weapon_icons.hpp"
#include "ui/style.hpp"
#include "ui/ui_icons.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace shogod::ui::pages {
namespace {

using namespace widgets;

const char* const k_group_names[] = {
    "Pistol", "Heavy Pistol", "SMG", "Rifle", "Scout", "Auto Sniper", "AWP", "Heavy",
};
const int k_group_icons[] = {
    static_cast<int>(cs2_weapon_icon::usp_silencer),
    static_cast<int>(cs2_weapon_icon::deagle),
    static_cast<int>(cs2_weapon_icon::mp9),
    static_cast<int>(cs2_weapon_icon::ak47),
    static_cast<int>(cs2_weapon_icon::ssg08),
    static_cast<int>(cs2_weapon_icon::scar20),
    static_cast<int>(cs2_weapon_icon::awp),
    static_cast<int>(cs2_weapon_icon::m249),
};

// Fast-access HvH weapons pinned beside the full weapon selector.
constexpr int k_quick_defs[] = {1, 64, 40, 38, 9, 7};

bool same_weapon_cfg(const config::rage_weapon& a, const config::rage_weapon& b) noexcept {
    return a.fov == b.fov && a.min_damage == b.min_damage && a.hitchance == b.hitchance &&
           a.aim_points == b.aim_points && a.multipoint == b.multipoint &&
           a.safe_point == b.safe_point && a.auto_fire == b.auto_fire &&
           a.auto_scope == b.auto_scope && a.auto_stop == b.auto_stop &&
           a.auto_stop_flags == b.auto_stop_flags && a.auto_stop_speed == b.auto_stop_speed &&
           a.auto_stop_ticks == b.auto_stop_ticks && a.auto_stop_wait == b.auto_stop_wait &&
           a.auto_crouch == b.auto_crouch && a.penetration == b.penetration &&
           a.delay_shot == b.delay_shot && a.delay_ticks == b.delay_ticks &&
           a.ignore_limbs == b.ignore_limbs && a.force_body_aim == b.force_body_aim &&
           a.prefer_body_aim == b.prefer_body_aim && a.strict_hitchance == b.strict_hitchance;
}

const char* const k_priorities[] = {"Distance", "Lowest HP", "Threat", "Cycle"};
const char* const k_point_names[] = {"Head", "Neck", "Chest", "Stomach", "Pelvis", "Legs"};
const std::uint32_t k_point_masks[] = {
    config::aim_point::head,    config::aim_point::neck,   config::aim_point::chest,
    config::aim_point::stomach, config::aim_point::pelvis, config::aim_point::legs,
};

}  // namespace

void rage(config::store& cfg, const status& /*live*/) {
    using namespace config;
    config_v1& c = cfg.get();

    // selected_def > 0 targets a specific CS2 weapon override; selected_def == 0 edits a group default.
    static int selected_def = 7;  // Default to AK-47 so specific selector is immediately clear.
    static int group = static_cast<int>(weapon_group::rifle);
    if (selected_def > 0) {
        if (const rage_weapon_spec* sp = find_rage_weapon_by_def(selected_def)) {
            group = static_cast<int>(sp->group);
        } else {
            selected_def = 0;
        }
    }

    const palette& pal = colors();
    const float total_w = ImGui::GetContentRegionAvail().x;
    const float bar_h = 54.0f;
    const ImVec2 bar_pos = ImGui::GetCursorScreenPos();
    ImDrawList& draw = *ImGui::GetWindowDrawList();

    // ---- Left: Active Weapon Selector Trigger + Reset ----------------------
    const float picker_w = std::min(226.0f, total_w * 0.34f);
    const ImVec2 pick_pos = bar_pos;
    const ImVec2 pick_end(pick_pos.x + picker_w, pick_pos.y + bar_h);

    const rage_weapon_spec* active_spec =
        selected_def > 0 ? find_rage_weapon_by_def(selected_def) : nullptr;
    const bool is_custom =
        active_spec && c.rage.weapon_overrides.find(active_spec->def_index) != c.rage.weapon_overrides.end();

    const float reset_btn_w = is_custom ? 24.0f : 0.0f;
    ImGui::SetCursorScreenPos(pick_pos);
    const bool open_clicked = ImGui::InvisibleButton(
        "##weapon_picker_btn", ImVec2(picker_w - reset_btn_w - 4.0f, bar_h), ImGuiButtonFlags_EnableNav);
    const bool pick_hovered = ImGui::IsItemHovered();
    const bool picker_open = ImGui::IsPopupOpen("##weapon_picker_popup");
    if (open_clicked) {
        ImGui::SetNextWindowPos(ImVec2(pick_pos.x, pick_end.y + 6.0f));
        ImGui::OpenPopup("##weapon_picker_popup");
    }

    draw.AddRectFilled(pick_pos, pick_end,
                       fade_col(pick_hovered || picker_open ? pal.bg_card_hover : pal.bg_card), 8.0f);
    draw.AddRect(pick_pos, pick_end,
                 fade_col(picker_open ? pal.accent : (pick_hovered ? pal.border : pal.border_soft)),
                 8.0f);

    const int disp_icon = active_spec ? active_spec->icon_index : k_group_icons[group];
    const char* disp_title = active_spec ? active_spec->name : k_group_names[group];
    char sub_buf[48]{};
    if (!active_spec) {
        std::snprintf(sub_buf, sizeof sub_buf, "Group default");
    } else if (is_custom) {
        std::snprintf(sub_buf, sizeof sub_buf, "Custom (%s)", k_group_names[group]);
    } else {
        std::snprintf(sub_buf, sizeof sub_buf, "Uses %s group", k_group_names[group]);
    }

    draw_cs2_weapon_icon(draw, ImVec2(pick_pos.x + 32.0f, pick_pos.y + bar_h * 0.5f), 42.0f, 18.0f,
                         disp_icon, fade_col(pal.accent));

    const float text_x = pick_pos.x + 62.0f;
    draw.AddText(ImVec2(text_x, pick_pos.y + 10.0f), fade_col(pal.text), disp_title);
    ImGui::PushFont(font_small(), font_size_small());
    draw.AddText(ImVec2(text_x, pick_pos.y + 29.0f),
                 fade_col(is_custom ? pal.accent : pal.text_faint), sub_buf);
    ImGui::PopFont();

    if (is_custom) {
        const ImVec2 rst_pos(pick_end.x - 42.0f, pick_pos.y + (bar_h - 22.0f) * 0.5f);
        ImGui::SetCursorScreenPos(rst_pos);
        if (ImGui::InvisibleButton("##reset_wep_override", ImVec2(22.0f, 22.0f))) {
            c.rage.weapon_overrides.erase(active_spec->def_index);
        }
        const bool rst_hov = ImGui::IsItemHovered();
        if (rst_hov) {
            draw.AddRectFilled(rst_pos, ImVec2(rst_pos.x + 22.0f, rst_pos.y + 22.0f),
                               fade_col(pal.accent_soft), 5.0f);
            ImGui::SetTooltip("Reset %s to %s group defaults", active_spec->name, k_group_names[group]);
        }
        draw_ui_icon(draw, ui_icon_id::rotate_ccw,
                     ImVec2(rst_pos.x + 11.0f, rst_pos.y + 11.0f), 12.5f,
                     fade_col(rst_hov ? pal.accent : pal.text_muted), 1.4f);
    }

    draw_ui_icon(draw, ui_icon_id::chevron_down,
                 ImVec2(pick_end.x - 14.0f, pick_pos.y + bar_h * 0.5f), 12.5f,
                 fade_col(picker_open || pick_hovered ? pal.accent : pal.text_muted), 1.45f);

    // ---- Right: Quick-Pick HvH Strip (Deagle, R8, Scout, SCAR-20, AWP, AK) --
    const float gap = 8.0f;
    const ImVec2 quick_pos(pick_end.x + gap, bar_pos.y);
    const ImVec2 quick_end(bar_pos.x + total_w, bar_pos.y + bar_h);
    const float quick_w = quick_end.x - quick_pos.x;
    draw.AddRectFilled(quick_pos, quick_end, fade_col(pal.bg_card), 8.0f);
    draw.AddRect(quick_pos, quick_end, fade_col(pal.border_soft), 8.0f);

    constexpr int k_quick_count = static_cast<int>(std::size(k_quick_defs));
    const float slot_w = quick_w / static_cast<float>(k_quick_count);
    for (int i = 0; i < k_quick_count; ++i) {
        const rage_weapon_spec* qspec = find_rage_weapon_by_def(k_quick_defs[i]);
        if (!qspec) continue;
        const float x0 = std::floor(quick_pos.x + slot_w * static_cast<float>(i));
        const float x1 = (i == k_quick_count - 1)
                             ? quick_end.x
                             : std::floor(quick_pos.x + slot_w * static_cast<float>(i + 1));
        const float item_w = x1 - x0;
        ImGui::SetCursorScreenPos(ImVec2(x0, quick_pos.y));
        ImGui::PushID(qspec->def_index);
        if (ImGui::InvisibleButton("##quick_wep", ImVec2(item_w, bar_h), ImGuiButtonFlags_EnableNav)) {
            selected_def = qspec->def_index;
            group = static_cast<int>(qspec->group);
        }
        ImGui::PopID();

        const bool selected = (selected_def == qspec->def_index);
        const bool hovered = ImGui::IsItemHovered();
        const bool q_custom =
            c.rage.weapon_overrides.find(qspec->def_index) != c.rage.weapon_overrides.end();
        if (hovered) ImGui::SetTooltip("%s", qspec->name);

        if (i > 0 && !selected && selected_def != k_quick_defs[i - 1]) {
            draw.AddLine(ImVec2(x0, quick_pos.y + 10.0f), ImVec2(x0, quick_end.y - 10.0f),
                         fade_col(pal.border_soft), 1.0f);
        }

        if (selected || hovered) {
            const ImVec2 pad0(x0 + 3.0f, quick_pos.y + 3.0f);
            const ImVec2 pad1(x1 - 3.0f, quick_end.y - 3.0f);
            draw.AddRectFilled(pad0, pad1,
                               fade_col(selected ? pal.accent_soft : pal.bg_card_hover), 6.0f);
            if (selected) {
                draw.AddRect(pad0, pad1, fade_col(pal.accent), 6.0f);
                draw.AddRectFilled(ImVec2(pad0.x + 10.0f, pad1.y - 2.0f),
                                   ImVec2(pad1.x - 10.0f, pad1.y), fade_col(pal.accent), 1.0f);
            }
        }

        const float cx = (x0 + x1) * 0.5f;
        draw_cs2_weapon_icon(draw, ImVec2(cx, quick_pos.y + 19.0f),
                             std::min(44.0f, item_w - 14.0f), 16.0f, qspec->icon_index,
                             fade_col(selected ? pal.accent : (hovered ? pal.text : pal.text_muted)));

        if (q_custom) {
            draw.AddCircleFilled(ImVec2(x1 - 9.0f, quick_pos.y + 9.0f), 2.5f, fade_col(pal.accent), 10);
        }

        ImGui::PushFont(font_small(), font_size_small());
        const ImVec2 label = ImGui::CalcTextSize(qspec->short_name);
        draw.AddText(ImVec2(std::floor(cx - label.x * 0.5f), quick_pos.y + 33.0f),
                     fade_col(selected ? pal.text : (hovered ? pal.text : pal.text_muted)),
                     qspec->short_name);
        ImGui::PopFont();
    }

    // ---- Weapon Picker Popup (All 35 CS2 Weapons + 8 Group Defaults) -------
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, fade_col(pal.bg_window));
    ImGui::PushStyleColor(ImGuiCol_Border, fade_col(pal.border));
    ImGui::SetNextWindowSizeConstraints(ImVec2(total_w, 0.0f), ImVec2(total_w, 520.0f));
    if (ImGui::BeginPopup("##weapon_picker_popup")) {
        ImDrawList& pdl = *ImGui::GetWindowDrawList();
        const float pw = std::max(200.0f, total_w - 24.0f);

        ImGui::PushFont(font_small(), 10.5f);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(fade_col(pal.text_faint)), "GROUP DEFAULTS");
        ImGui::PopFont();

        constexpr int gcols = 8;
        const float ggap = 4.0f;
        const float gw = std::floor((pw - ggap * (gcols - 1)) / static_cast<float>(gcols));
        const float gh = 24.0f;
        const ImVec2 gorigin = ImGui::GetCursorScreenPos();
        for (int g = 0; g < k_weapon_groups; ++g) {
            const ImVec2 p0(gorigin.x + static_cast<float>(g) * (gw + ggap), gorigin.y);
            const ImVec2 p1(p0.x + gw, p0.y + gh);
            ImGui::SetCursorScreenPos(p0);
            ImGui::PushID(1000 + g);
            if (ImGui::InvisibleButton("##grp", ImVec2(gw, gh))) {
                selected_def = 0;
                group = g;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
            const bool sel = (selected_def == 0 && group == g);
            const bool hov = ImGui::IsItemHovered();
            pdl.AddRectFilled(p0, p1, fade_col(sel ? pal.accent_soft : (hov ? pal.bg_card_hover : pal.bg_card)), 5.0f);
            pdl.AddRect(p0, p1, fade_col(sel ? pal.accent : (hov ? pal.border : pal.border_soft)), 5.0f);
            ImGui::PushFont(font_small(), 11.0f);
            const ImVec2 ts = ImGui::CalcTextSize(k_group_names[g]);
            pdl.AddText(ImVec2(std::floor(p0.x + (gw - ts.x) * 0.5f), std::floor(p0.y + (gh - ts.y) * 0.5f)),
                        fade_col(sel ? pal.accent : (hov ? pal.text : pal.text_muted)), k_group_names[g]);
            ImGui::PopFont();
        }
        ImGui::SetCursorScreenPos(gorigin);
        ImGui::Dummy(ImVec2(pw, gh + 6.0f));

        std::size_t cat_count = 0;
        const rage_weapon_spec* cat = rage_weapon_catalog(cat_count);

        struct cat_section {
            const char* title;
            int start_idx;
            int end_idx;
        };
        const cat_section sections[] = {
            {"PISTOLS", 0, 11},
            {"SMGS", 11, 18},
            {"RIFLES", 18, 25},
            {"SNIPERS", 25, 29},
            {"HEAVY & SHOTGUNS", 29, static_cast<int>(cat_count)},
        };

        constexpr int wcols = 6;
        const float wgap = 4.0f;
        const float tile_w = std::floor((pw - wgap * (wcols - 1)) / static_cast<float>(wcols));
        const float tile_h = 31.0f;

        for (std::size_t si = 0; si < std::size(sections); ++si) {
            const cat_section& sec = sections[si];
            ImGui::PushFont(font_small(), 10.5f);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(fade_col(pal.text_faint)), "%s", sec.title);
            ImGui::PopFont();

            const ImVec2 base = ImGui::GetCursorScreenPos();
            int local_i = 0;
            int max_row = 0;
            for (int idx = sec.start_idx; idx < sec.end_idx; ++idx, ++local_i) {
                const rage_weapon_spec& sp = cat[idx];
                const int row = local_i / wcols;
                const int col = local_i % wcols;
                max_row = std::max(max_row, row);
                const ImVec2 p0(base.x + static_cast<float>(col) * (tile_w + wgap),
                                base.y + static_cast<float>(row) * (tile_h + wgap));
                const ImVec2 p1(p0.x + tile_w, p0.y + tile_h);
                ImGui::SetCursorScreenPos(p0);
                ImGui::PushID(sp.def_index);
                if (ImGui::InvisibleButton("##wep_tile", ImVec2(tile_w, tile_h))) {
                    selected_def = sp.def_index;
                    group = static_cast<int>(sp.group);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();

                const bool sel = (selected_def == sp.def_index);
                const bool hov = ImGui::IsItemHovered();
                const bool has_ov = c.rage.weapon_overrides.find(sp.def_index) != c.rage.weapon_overrides.end();

                pdl.AddRectFilled(p0, p1, fade_col(sel ? pal.accent_soft : (hov ? pal.bg_card_hover : pal.bg_card)), 5.0f);
                pdl.AddRect(p0, p1, fade_col(sel ? pal.accent : (hov ? pal.border : pal.border_soft)), 5.0f);

                const float tcx = (p0.x + p1.x) * 0.5f;
                draw_cs2_weapon_icon(pdl, ImVec2(tcx, p0.y + 10.5f), tile_w - 20.0f, 11.0f, sp.icon_index,
                                     fade_col(sel ? pal.accent : (hov ? pal.text : pal.text_muted)));
                if (has_ov) {
                    pdl.AddCircleFilled(ImVec2(p1.x - 5.5f, p0.y + 5.5f), 2.0f, fade_col(pal.accent), 10);
                }

                ImGui::PushFont(font_small(), 10.0f);
                const ImVec2 lbl = ImGui::CalcTextSize(sp.name);
                pdl.AddText(ImVec2(std::floor(tcx - lbl.x * 0.5f), p0.y + 18.0f),
                            fade_col(sel ? pal.text : (hov ? pal.text : pal.text_muted)), sp.name);
                ImGui::PopFont();
            }
            const float block_h = static_cast<float>(max_row + 1) * tile_h +
                                  static_cast<float>(max_row) * wgap +
                                  ((si + 1 < std::size(sections)) ? 4.0f : 0.0f);
            ImGui::SetCursorScreenPos(base);
            ImGui::Dummy(ImVec2(pw, block_h));
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    ImGui::SetCursorScreenPos(bar_pos);
    ImGui::Dummy(ImVec2(total_w, bar_h + 8.0f));

    // Resolve edited weapon target cleanly: specific edits auto-promote on change.
    rage_weapon working =
        (active_spec) ? c.rage.resolve_weapon(active_spec->def_index, group)
                      : c.rage.weapons[static_cast<std::size_t>(group)];
    const rage_weapon before_edit = working;
    rage_weapon& w = active_spec ? working : c.rage.weapons[static_cast<std::size_t>(group)];

    if (!begin_columns("##rage_cols")) return;

    ImGui::TableNextColumn();
    section("General");
    if (begin_card("##target", ImVec2(0, 0))) {
        toggle("Enabled", &c.rage.enabled, "Master switch for the ragebot");
        combo("Target priority", reinterpret_cast<int*>(&c.rage.priority), k_priorities, 4);
        toggle("Silent aim", &c.rage.silent_aim, "Aims inside CUserCommand without moving local camera");
        toggle("Auto revolver", &c.rage.auto_revolver, "Pre-cocks the R8 Revolver");
    }
    end_card();

    section("Accuracy");
    if (begin_card("##fire", ImVec2(0, 0))) {
        multi_select("Automatic actions", {
            {"Auto fire", &w.auto_fire},
            {"Auto scope", &w.auto_scope},
            {"Auto crouch", &w.auto_crouch},
        });
        toggle("Auto Stop", &w.auto_stop, "Counter-strafes automatically until shot accuracy is reached");
        if (w.auto_stop) {
            constexpr const char* stop_names[] = {"Early", "In air", "Between shots"};
            constexpr std::uint32_t stop_masks[] = {
                auto_stop_flag::early,
                auto_stop_flag::in_air,
                auto_stop_flag::between_shots,
            };
            multi_bits("Auto Stop options", &w.auto_stop_flags, stop_names, stop_masks, 3,
                       "Early pre-brakes before ready; In air brakes jumpshots; Between shots holds accuracy while spraying/tapping");
        }
        constexpr const char* delay_names[] = {"Damage", "Hitchance"};
        constexpr std::uint32_t delay_masks[] = {
            static_cast<std::uint32_t>(delay_shot_mode::damage),
            static_cast<std::uint32_t>(delay_shot_mode::hitchance),
        };
        auto delay_bits = static_cast<std::uint32_t>(w.delay_shot);
        if (multi_bits("Delay Shot", &delay_bits, delay_names, delay_masks, 2,
                       "Requires consecutive passing ticks before firing for higher precision")) {
            w.delay_shot = static_cast<delay_shot_mode>(delay_bits);
        }
        if (w.delay_shot != delay_shot_mode::off) {
            slider_int("Delay ticks", &w.delay_ticks, 2, 6,
                       "Consecutive stable ticks required before pulling the trigger");
        }
        toggle("Prefer body aim", &w.prefer_body_aim,
               "Takes clean body shots when enemy heads are hidden or pitch-down");
        toggle("Force body aim", &w.force_body_aim, "Excludes head hitbox on this weapon");
        toggle("Wallbang", &w.penetration,
               "Use simulated damage after penetration for the Min damage threshold");
    }
    end_card();

    ImGui::TableNextColumn();
    section("Aimbot");
    if (begin_card("##aim", ImVec2(0, 0))) {
        slider_float_step("Field of view", &w.fov, 0.0f, 180.0f,
                          "Maximum angular distance to a target");
        slider_min_damage("Min damage", &w.min_damage,
                          "1-100 flat damage, 101-120 scales as HP+1..HP+20");
        slider_float_step("Hitchance", &w.hitchance, 0.0f, 100.0f,
                          "Probability required before firing");
        separator_y();
        multi_bits("Hitboxes", &w.aim_points, k_point_names, k_point_masks, 6);
        slider_float_step("Multi-point", &w.multipoint, 0.0f, 100.0f,
                          "Hitbox edge spread scale; 0 aims center, higher peeks tight corners");
    }
    end_card();

    section("Prediction");
    if (begin_card("##prediction", ImVec2(0, 0))) {
        toggle("Lag compensation", &c.rage.backtrack,
               "Record historical poses for backtracking and ESP ghost");
        if (c.rage.backtrack) {
            slider_float_step("Window (ms)", &c.rage.backtrack_ms, 0.0f, 200.0f,
                              "Lower window reduces CPU cost when scanning lag records");
        }
        slider_float_step("Extrapolation (ms)", &c.rage.extrapolation_ms, 0.0f, 50.0f,
                          "Projects hitboxes along wall-clamped enemy velocity (0 = off)");
    }
    end_card();

    end_columns();

    if (active_spec && !same_weapon_cfg(before_edit, working)) {
        c.rage.weapon_overrides[active_spec->def_index] = working;
    }
}

}  // namespace shogod::ui::pages
