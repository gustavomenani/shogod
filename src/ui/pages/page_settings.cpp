#include "ui/pages/pages.hpp"

#include "brand.hpp"
#include "engine/log.hpp"
#include "ui/style.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cstdio>

namespace shogod::ui::pages {
namespace {

using namespace widgets;

void feature_bind_row(const char* label, config::keybind& bind, int reserved_key) {
    ImGui::PushID(label);
    keybind_row(label, &bind, "Click to bind. Right-click to clear. Escape cancels.");
    if (bind.bound()) {
        const char* const modes[] = {"Toggle", "Hold"};
        int mode = bind.hold ? 1 : 0;
        if (combo("Mode", &mode, modes, 2)) bind.hold = mode == 1;
        if (bind.vk == reserved_key) ImGui::TextUnformatted("This key is reserved for the menu");
    }
    ImGui::PopID();
}

}  // namespace

void settings(config::store& cfg, const status& live) {
    config::config_v1& c = cfg.get();

    if (!begin_columns("##settings_cols")) return;

    ImGui::TableNextColumn();
    section("Appearance");
    if (begin_card("##theme", ImVec2(0, 0))) {
        toggle("Dark theme", &c.ui.dark_theme);
        color_row("Accent", &c.ui.accent);
        slider_float("Opacity", &c.ui.opacity, 0.2f, 1.0f);
        toggle("Animations", &c.ui.animations);
    }
    end_card();

    section("Input");
    if (begin_card("##input", ImVec2(0, 0))) {
        config::keybind menu{c.ui.menu_key, false, false};
        if (keybind_row("Menu key", &menu, "Keyboard only. Right-click resets to Insert.", true)) {
            c.ui.menu_key = menu.bound() ? menu.vk : 0x2d;
        }
        feature_bind_row("Ragebot", c.rage.toggle, c.ui.menu_key);
        feature_bind_row("Anti-Aim", c.anti_aim.toggle, c.ui.menu_key);
        feature_bind_row("Player ESP", c.visuals.toggle, c.ui.menu_key);
        feature_bind_row("Third person", c.visuals.third_person_bind, c.ui.menu_key);
        feature_bind_row("Bunny hop", c.misc.movement.bhop_bind, c.ui.menu_key);
        feature_bind_row("Auto strafe", c.misc.movement.auto_strafe_bind, c.ui.menu_key);
        feature_bind_row("Peek assist", c.misc.movement.peek_assist_bind, c.ui.menu_key);
        ImGui::PushStyleColor(ImGuiCol_Text, fade_col(colors().text_faint));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 14.0f);
        ImGui::TextWrapped("The menu swallows game input while open on the WndProc path.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    end_card();

    ImGui::TableNextColumn();
    section("Runtime");
    if (begin_card("##status", ImVec2(0, 0))) {
        auto kv_row = [](const char* k, const char* v) {
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, fade_col(colors().text_muted));
            ImGui::TextUnformatted(k);
            ImGui::PopStyleColor();
            const float vw = ImGui::CalcTextSize(v).x;
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                 std::max(8.0f, ImGui::GetContentRegionAvail().x - 14.0f - vw));
            ImGui::PushStyleColor(ImGuiCol_Text, fade_col(colors().text));
            ImGui::TextUnformatted(v);
            ImGui::PopStyleColor();
        };
        char fps_buf[32]{};
        char anc_buf[32]{};
        std::snprintf(fps_buf, sizeof fps_buf, "%.0f", live.fps);
        std::snprintf(anc_buf, sizeof anc_buf, "%d/%d", live.anchors_ok, live.anchors_total);
        kv_row("Version", std::string(brand::version_tag()).c_str());
        kv_row("FPS", fps_buf);
        kv_row("Anchors", anc_buf);
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
