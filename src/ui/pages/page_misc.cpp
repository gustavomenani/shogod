#include "ui/pages/pages.hpp"

#include "ui/style.hpp"
#include "ui/widgets.hpp"

namespace shogod::ui::pages {
namespace {

using namespace widgets;

}  // namespace

void misc(config::store& cfg) {
    config::config_v1& c = cfg.get();

    if (!begin_columns("##misc_cols")) return;

    ImGui::TableNextColumn();
    if (begin_card("##movement", ImVec2(0, 0))) {
        section("MOVEMENT");
        toggle("Bunny hop", &c.misc.movement.bhop);
        toggle("Auto strafe", &c.misc.movement.auto_strafe);
        toggle("Edge jump", &c.misc.movement.edge_jump);
        toggle("Fast ladder", &c.misc.movement.fast_ladder);
        toggle("Slide", &c.misc.movement.slide);
    }
    end_card();

    ImGui::Dummy(ImVec2(0, 8));
    if (begin_card("##utilities", ImVec2(0, 0))) {
        section("UTILITIES");
        toggle("Knife bot", &c.misc.utilities.knife_bot);
        toggle("Auto accept", &c.misc.utilities.auto_accept);
        toggle("Auto pistol", &c.misc.utilities.auto_pistol);
        toggle("Fake duck", &c.misc.utilities.fake_duck);
        separator_y();
        toggle("Third person", &c.misc.utilities.third_person);
        if (c.misc.utilities.third_person)
            slider_float("Distance", &c.misc.utilities.third_person_distance, 0.0f, 500.0f);
        toggle("Free look", &c.misc.utilities.free_look);
        if (c.misc.utilities.free_look) keybind_row("Free look key", &c.misc.utilities.free_look_key);
    }
    end_card();

    ImGui::TableNextColumn();
    if (begin_card("##info", ImVec2(0, 0))) {
        section("INFO");
        toggle("Watermark", &c.misc.info.watermark);
        toggle("Keybind list", &c.misc.info.keybind_list);
        toggle("Hit log", &c.misc.info.hit_log);
        toggle("Match info", &c.misc.info.match_info);
    }
    end_card();

    ImGui::Dummy(ImVec2(0, 8));
    if (begin_card("##detect", ImVec2(0, 0))) {
        section("SERVER DETECTION");
        toggle("Block detected features", &c.misc.block_server_detected,
               "Disables the modules community-server plugins can observe");
        ImGui::PushStyleColor(ImGuiCol_Text, colors().text_faint);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped(
            "Community plugins observe specific behaviours. Turning this on parks the modules "
            "listed in docs/DETECTION.md while keeping the rest live.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    end_card();

    end_columns();
}

}  // namespace shogod::ui::pages
