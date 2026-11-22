#include "ui/pages/pages.hpp"

#include "brand.hpp"
#include "config/keybinds.hpp"
#include "engine/log.hpp"
#include "ui/style.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace shogod::ui::pages {
namespace {

using namespace widgets;

const char* mode_badge(config::bind_mode m) noexcept {
    switch (m) {
        case config::bind_mode::toggle: return "TGL";
        case config::bind_mode::hold:   return "HLD";
        case config::bind_mode::always: return "ALW";
    }
    return "TGL";
}

}  // namespace

void keybinds_and_settings(config::store& cfg, const status& live) {
    config::config_v1& c = cfg.get();
    const palette& pal = colors();

    if (!begin_columns("##keybinds_settings_cols")) return;

    ImGui::TableNextColumn();
    section("Universal Keybinds");
    if (begin_card("##universal_binds", ImVec2(0, 0))) {
        config::keybind menu{c.ui.menu_key, false, false};
        if (keybind_row("Menu key", &menu, "Keyboard only. Right-click resets to Insert.", true)) {
            c.ui.menu_key = menu.bound() ? menu.vk : 0x2d;
        }
        separator_y();

        // Target picker to add a new bind straight from the manager.
        const auto catalog = config::bind_target_catalog();
        static int add_idx = 0;
        if (add_idx < 0 || add_idx >= static_cast<int>(catalog.size())) add_idx = 0;

        static std::vector<std::string> item_labels;
        static std::vector<const char*> item_ptrs;
        if (item_labels.size() != catalog.size()) {
            item_labels.clear();
            item_ptrs.clear();
            item_labels.reserve(catalog.size());
            item_ptrs.reserve(catalog.size());
            for (const auto& sp : catalog) {
                item_labels.push_back(std::string(sp.label) + " (" + sp.category + ")");
            }
            for (const auto& s : item_labels) item_ptrs.push_back(s.c_str());
        }

        if (!item_ptrs.empty()) {
            combo("Add bind for", &add_idx, item_ptrs.data(), static_cast<int>(item_ptrs.size()),
                  "Pick any feature or right-click any control in the menu");
            const float avail_w = (std::max)(100.0f, ImGui::GetContentRegionAvail().x - 14.0f);
            const float add_w = c.keybinds.empty() ? avail_w : std::floor((avail_w - 8.0f) * 0.68f);
            if (action_button("+ Add keybind", ImVec2(add_w, 26.0f)) &&
                c.keybinds.size() < config::k_max_menu_keybinds) {
                const auto& sp = catalog[static_cast<std::size_t>(add_idx)];
                config::menu_keybind kb;
                kb.target = sp.id;
                kb.vk = 0;
                kb.mode = config::bind_mode::toggle;
                kb.show_in_binds = true;
                kb.override_value = sp.default_override;
                c.keybinds.push_back(kb);
                open_keybind_popup(sp.id);
            }
            if (!c.keybinds.empty()) {
                ImGui::SameLine(0.0f, 8.0f);
                const float clr_w = avail_w - add_w - 8.0f;
                if (action_button("Clear all", ImVec2(clr_w, 26.0f))) {
                    c.keybinds.clear();
                }
            }
        }

        separator_y();

        if (c.keybinds.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, fade_col(pal.text_faint));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 14.0f);
            ImGui::TextWrapped(
                "No keybinds configured yet. Right-click any toggle, slider, or combo across "
                "the menu (e.g. Min damage, Force body aim, Yaw offset, Third person, Peek assist) "
                "or click '+ Add keybind' above.");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        } else {
            int erase_idx = -1;
            const float list_w = (std::max)(120.0f, ImGui::GetContentRegionAvail().x - 14.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();

            for (std::size_t i = 0; i < c.keybinds.size(); ++i) {
                config::menu_keybind& kb = c.keybinds[i];
                const config::bind_target_spec* sp = config::find_bind_target(kb.target);
                const char* title = sp ? sp->label : kb.target.c_str();
                const char* cat = sp ? sp->category : "Custom";
                const std::string val_txt =
                    sp ? config::format_bind_override(*sp, kb.override_value) : "on";
                const std::string key_txt =
                    (kb.mode == config::bind_mode::always) ? "ALWAYS" : config::vk_short_name(kb.vk);

                ImGui::PushID(static_cast<int>(i));
                const ImVec2 row_pos = ImGui::GetCursorScreenPos();
                const float row_h = 34.0f;
                const float del_w = 24.0f;
                const float edit_w = (std::max)(60.0f, list_w - del_w - 6.0f);

                const bool edit_clicked = ImGui::InvisibleButton("##kb_edit", ImVec2(edit_w, row_h));
                const bool edit_hov = ImGui::IsItemHovered();
                if (edit_clicked && sp) {
                    open_keybind_popup(sp->id);
                }

                dl->AddRectFilled(
                    row_pos, ImVec2(row_pos.x + edit_w, row_pos.y + row_h),
                    fade_col(edit_hov ? pal.bg_card_hover : pal.bg_input), 6.0f);
                dl->AddRect(row_pos, ImVec2(row_pos.x + edit_w, row_pos.y + row_h),
                            fade_col(edit_hov ? pal.accent : pal.border_soft), 6.0f);

                // Left: Title + Category/Value subtitle.
                dl->AddText(ImVec2(row_pos.x + 9.0f, row_pos.y + 3.0f), fade_col(pal.text), title);
                ImGui::PushFont(font_small(), 10.5f);
                char sub[96]{};
                std::snprintf(sub, sizeof sub, "%s  |  %s  |  %s%s", cat, mode_badge(kb.mode),
                              val_txt.c_str(), kb.show_in_binds ? "" : "  (hidden)");
                dl->AddText(ImVec2(row_pos.x + 9.0f, row_pos.y + 18.0f),
                            fade_col(pal.text_faint), sub);
                ImGui::PopFont();

                // Right pill inside edit button: Key tag.
                ImGui::PushFont(font_small(), 10.5f);
                const ImVec2 ksz = ImGui::CalcTextSize(key_txt.c_str());
                const float pill_w = ksz.x + 12.0f;
                const ImVec2 p0(row_pos.x + edit_w - pill_w - 8.0f, row_pos.y + 8.0f);
                const ImVec2 p1(row_pos.x + edit_w - 8.0f, row_pos.y + row_h - 8.0f);
                dl->AddRectFilled(p0, p1, fade_col(pal.accent_soft), 4.0f);
                dl->AddRect(p0, p1, fade_col(pal.accent), 4.0f);
                dl->AddText(ImVec2(p0.x + 6.0f, p0.y + (p1.y - p0.y - ksz.y) * 0.5f),
                            fade_col(pal.accent), key_txt.c_str());
                ImGui::PopFont();

                // Delete button on far right.
                ImGui::SameLine(0.0f, 6.0f);
                if (action_button("x", ImVec2(del_w, row_h))) {
                    erase_idx = static_cast<int>(i);
                }
                ImGui::PopID();
            }

            if (erase_idx >= 0 && static_cast<std::size_t>(erase_idx) < c.keybinds.size()) {
                c.keybinds.erase(c.keybinds.begin() + erase_idx);
            }
        }
    }
    end_card();

    ImGui::TableNextColumn();
    section("Appearance");
    if (begin_card("##theme", ImVec2(0, 0))) {
        toggle("Dark theme", &c.ui.dark_theme);
        color_row("Accent", &c.ui.accent);

        // Quick Neverlose accent presets.
        struct accent_preset {
            const char* name;
            config::rgba color;
        };
        constexpr accent_preset presets[] = {
            {"NL Blue", {0.31f, 0.49f, 1.00f, 1.0f}},
            {"Cyan",    {0.05f, 0.72f, 0.96f, 1.0f}},
            {"Violet",  {0.58f, 0.41f, 1.00f, 1.0f}},
            {"Crimson", {0.95f, 0.28f, 0.44f, 1.0f}},
        };
        for (int p = 0; p < 4; ++p) {
            if (p > 0) ImGui::SameLine(0.0f, 6.0f);
            const bool active =
                std::abs(c.ui.accent.r - presets[p].color.r) < 0.03f &&
                std::abs(c.ui.accent.g - presets[p].color.g) < 0.03f &&
                std::abs(c.ui.accent.b - presets[p].color.b) < 0.03f;
            if (chip(presets[p].name, active)) {
                c.ui.accent = presets[p].color;
            }
        }
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        slider_float("Opacity", &c.ui.opacity, 0.2f, 1.0f);
        toggle("Animations", &c.ui.animations);
    }
    end_card();

    section("Runtime Diagnostics");
    if (begin_card("##status", ImVec2(0, 0))) {
        auto kv_row = [](const char* k, const char* v) {
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, fade_col(colors().text_muted));
            ImGui::TextUnformatted(k);
            ImGui::PopStyleColor();
            const float vw = ImGui::CalcTextSize(v).x;
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                 (std::max)(8.0f, ImGui::GetContentRegionAvail().x - 14.0f - vw));
            ImGui::PushStyleColor(ImGuiCol_Text, fade_col(colors().text));
            ImGui::TextUnformatted(v);
            ImGui::PopStyleColor();
        };
        char fps_buf[32]{};
        char anc_buf[32]{};
        char binds_buf[32]{};
        std::snprintf(fps_buf, sizeof fps_buf, "%.0f", live.fps);
        std::snprintf(anc_buf, sizeof anc_buf, "%d/%d", live.anchors_ok, live.anchors_total);
        std::snprintf(binds_buf, sizeof binds_buf, "%zu", c.keybinds.size());
        kv_row("Version", std::string(brand::version_tag()).c_str());
        kv_row("FPS", fps_buf);
        kv_row("Anchors", anc_buf);
        kv_row("Keybinds", binds_buf);
        char player_buf[32]{};
        std::snprintf(player_buf, sizeof player_buf, "%d", live.players);
        kv_row("Snapshot players", player_buf);
        kv_row("ESP view matrix", live.view_matrix_ready ? "Ready" : "Unavailable");
        kv_row("Config", live.config_name.c_str());
        separator_y();
        ImGui::PushStyleColor(ImGuiCol_Text, fade_col(colors().text_faint));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 14.0f);
        ImGui::TextWrapped("%s", live.anchor_detail.empty() ? "no anchor report yet"
                                                            : live.anchor_detail.c_str());
        ImGui::TextWrapped("log: %s", log::log_path().c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    end_card();

    end_columns();
}

}  // namespace shogod::ui::pages
