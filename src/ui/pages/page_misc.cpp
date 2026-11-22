#include "ui/pages/pages.hpp"

#include "features/audio/hitsounds.hpp"
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
    section("Movement");
    if (begin_card("##movement", ImVec2(0, 0))) {
        toggle("Bunny hop", &c.misc.movement.bhop,
               "Subtick landing prediction jump while holding space", "misc.bhop");
        toggle("Edge jump", &c.misc.movement.edge_jump,
               "Jumps when you run off a ledge without pressing space", "misc.edge_jump");
        toggle("Auto strafe", &c.misc.movement.auto_strafe,
               "Multi-subtick air strafe acceleration with zero camera jitter",
               "misc.auto_strafe");
        toggle("Peek assist", &c.misc.movement.peek_assist,
               "Automatically returns to peek origin after firing (Right-click to bind Hold/Toggle)",
               "misc.peek_assist");
        toggle("Slide walk", &c.misc.movement.leg_movement,
               "Alternates slide-walk flags in CUserCommand", "misc.leg_movement");
    }
    end_card();

    section("Weapons");
    if (begin_card("##utilities", ImVec2(0, 0))) {
        toggle("Auto pistol", &c.misc.utilities.auto_pistol, "Pulses attack on semi-auto pistols",
               "misc.auto_pistol");
    }
    end_card();

    section("Server detection");
    if (begin_card("##detect", ImVec2(0, 0))) {
        toggle("Block detected features", &c.misc.block_server_detected,
               "Parks modules detectable by community plugins", "misc.safe_mode");
        ImGui::PushStyleColor(ImGuiCol_Text, fade_col(colors().text_faint));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 14.0f);
        ImGui::TextWrapped(
            "Community plugins inspect angle snaps and air strafe. Enabling this parks anti-aim, "
            "auto strafe and auto pistol, and raises ragebot damage and safe-point minimums.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    end_card();

    ImGui::TableNextColumn();
    section("Hitmarker & Audio");
    if (begin_card("##hitsounds", ImVec2(0, 0))) {
        toggle("Crosshair hitmarker", &c.misc.info.hitmarker,
               "Draws fading crosshair X marker on enemy damage", "misc.hitmarker");
        if (c.misc.info.hitmarker) {
            color_row("Hitmarker color", &c.misc.info.hitmarker_color);
        }
        toggle("Enable hitsounds", &c.misc.hitsound.enabled,
               "Plays embedded WAV or CS2 native sound on hit/kill", "misc.hitsound");
        if (c.misc.hitsound.enabled) {
            slider_float("Volume", &c.misc.hitsound.volume, 0.0f, 2.0f,
                         "0.0 = mute, 1.0 = 100%, 2.0 = 200% boost");
            const auto* names = features::audio::sound_names();
            const int count = features::audio::sound_count();
            if (combo("Body hit", &c.misc.hitsound.body_hit, names, count)) {
                features::audio::preview_sound(c.misc.hitsound.body_hit, c.misc.hitsound.volume);
            }
            if (combo("Body kill", &c.misc.hitsound.body_kill, names, count)) {
                features::audio::preview_sound(c.misc.hitsound.body_kill, c.misc.hitsound.volume);
            }
            if (combo("Head hit", &c.misc.hitsound.head_hit, names, count)) {
                features::audio::preview_sound(c.misc.hitsound.head_hit, c.misc.hitsound.volume);
            }
            if (combo("Head kill", &c.misc.hitsound.head_kill, names, count)) {
                features::audio::preview_sound(c.misc.hitsound.head_kill, c.misc.hitsound.volume);
            }
            if (action_button("Preview hitsound", ImVec2(-1, 26.0f))) {
                features::audio::preview_sound(
                    c.misc.hitsound.head_kill > 0 ? c.misc.hitsound.head_kill
                                                  : c.misc.hitsound.body_hit,
                    c.misc.hitsound.volume);
            }
        }
    }
    end_card();

    section("Overlay HUD");
    if (begin_card("##info", ImVec2(0, 0))) {
        toggle("Watermark", &c.misc.info.watermark, "Top-right Shogod telemetry bar");
        toggle("Keybind list", &c.misc.info.keybind_list,
               "Displays active keybinds & value overrides on screen");
        toggle("Spectator list", &c.misc.info.spectator_list,
               "Shows players currently spectating you");
    }
    end_card();

    end_columns();
}

}  // namespace shogod::ui::pages
