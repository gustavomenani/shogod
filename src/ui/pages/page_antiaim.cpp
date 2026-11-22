#include "ui/pages/pages.hpp"

#include "ui/style.hpp"
#include "ui/widgets.hpp"

namespace shogod::ui::pages {
namespace {

using namespace widgets;

const char* const k_yaw_modes[] = {"Off", "Jitter", "Random", "Static backward"};
const char* const k_pitch_modes[] = {"Off", "Down", "Up", "Zero"};

}  // namespace

void antiaim(config::store& cfg) {
    using namespace config;
    config_v1& c = cfg.get();

    if (!begin_columns("##aa_cols")) return;

    ImGui::TableNextColumn();
    section("Angles");
    if (begin_card("##aa_angles", ImVec2(0, 0))) {
        toggle("Enabled", &c.anti_aim.enabled, "Real view angles isolated from camera via SendMove",
               "aa.enabled");
        toggle("At targets", &c.anti_aim.at_targets,
               "Aims base yaw relative to the nearest enemy instead of your camera",
               "aa.at_targets");
        toggle("Auto adjust", &c.anti_aim.auto_adjust,
               "Automatically trims yaw offset for weapon stance and pose so your head stays centered",
               "aa.auto_adjust");
        toggle("Auto direction", &c.anti_aim.freestanding,
               "Tucks your head toward adjacent wall cover relative to the nearest enemy",
               "aa.freestanding");

        constexpr pitch_mode pitches[] = {pitch_mode::off, pitch_mode::down, pitch_mode::up,
                                          pitch_mode::zero};
        int pitch_sel = 0;
        for (int i = 0; i < 4; ++i) {
            if (pitches[i] == c.anti_aim.base_pitch) pitch_sel = i;
        }
        if (combo("Pitch", &pitch_sel, k_pitch_modes, 4, "Base pitch orientation", nullptr,
                  "aa.base_pitch")) {
            c.anti_aim.base_pitch = pitches[pitch_sel];
        }

        constexpr yaw_mode modes[] = {yaw_mode::off, yaw_mode::jitter, yaw_mode::random,
                                      yaw_mode::fake_static};
        int selected = 0;
        for (int i = 0; i < 4; ++i) {
            if (modes[i] == c.anti_aim.base_yaw) selected = i;
        }
        if (combo("Yaw base", &selected, k_yaw_modes, 4, "Yaw pattern applied each tick", nullptr,
                  "aa.base_yaw")) {
            c.anti_aim.base_yaw = modes[selected];
        }
        slider_float_step("Yaw offset", &c.anti_aim.yaw_offset, -180.0f, 180.0f,
                          "Fine yaw adjustment (Right-click to bind Manual Left -90 / Right +90)",
                          "aa.yaw_offset");
        if (c.anti_aim.base_yaw == yaw_mode::jitter || c.anti_aim.base_yaw == yaw_mode::random) {
            slider_float_step("Jitter range", &c.anti_aim.jitter_range, 0.0f, 180.0f,
                              "Yaw offset magnitude for jitter and random", "aa.jitter_range");
        }
    }
    end_card();

    ImGui::TableNextColumn();
    section("Conditions");
    if (begin_card("##aa_cond", ImVec2(0, 0))) {
        toggle("Safe manual fire", &c.anti_aim.on_shot,
               "Aligns to crosshair strictly on the tick a manual gunshot fires; holds steady between shots",
               "aa.on_shot");
        toggle("Enable in air", &c.anti_aim.on_air, "Keep anti-aim active while airborne",
               "aa.on_air");
        toggle("Enable while moving", &c.anti_aim.on_move, "Keep anti-aim active while moving",
               "aa.on_move");
        separator_y();
        ImGui::PushStyleColor(ImGuiCol_Text, fade_col(colors().text_faint));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 14.0f);
        ImGui::TextWrapped(
            "Tip: Right-click 'Yaw offset' to bind Manual Left (-90), Manual Right (+90), or "
            "Forward (180) as Toggle or Hold overrides.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    end_card();

    end_columns();
}

}  // namespace shogod::ui::pages
