#include "ui/pages/pages.hpp"

#include "brand.hpp"
#include "engine/log.hpp"
#include "ui/style.hpp"
#include "ui/widgets.hpp"

namespace shogod::ui::pages {
namespace {

using namespace widgets;

}  // namespace

void settings(config::store& cfg, const status& live) {
    config::config_v1& c = cfg.get();

    if (!begin_columns("##settings_cols")) return;

    ImGui::TableNextColumn();
    if (begin_card("##theme", ImVec2(0, 0))) {
        section("APPEARANCE");
        toggle("Dark theme", &c.ui.dark_theme);
        color_row("Accent", &c.ui.accent);
        slider_float("Opacity", &c.ui.opacity, 0.2f, 1.0f);
        toggle("Animations", &c.ui.animations);
    }
    end_card();

    ImGui::Dummy(ImVec2(0, 8));
    if (begin_card("##input", ImVec2(0, 0))) {
        section("INPUT");
        config::keybind menu{c.ui.menu_key, false, false};
        keybind_row("Menu key", &menu);
        c.ui.menu_key = menu.vk;
        ImGui::PushStyleColor(ImGuiCol_Text, colors().text_faint);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("The menu swallows game input while it is open on the WndProc path.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    end_card();

    ImGui::TableNextColumn();
    if (begin_card("##status", ImVec2(0, 0))) {
        section("RUNTIME");
        ImGui::Text("version   %s", std::string(brand::version_tag()).c_str());
        ImGui::Text("fps       %.0f", live.fps);
        ImGui::Text("anchors   %d/%d", live.anchors_ok, live.anchors_total);
        ImGui::Text("config    %s", live.config_name.c_str());
        separator_y();
        ImGui::PushStyleColor(ImGuiCol_Text, colors().text_faint);
        ImGui::PushTextWrapPos(0.0f);
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
