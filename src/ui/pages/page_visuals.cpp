#include "ui/pages/pages.hpp"

#include "ui/style.hpp"
#include "ui/widgets.hpp"

namespace shogod::ui::pages {
namespace {

using namespace widgets;

const char* const k_box_styles[] = {"Off", "Flat 2D", "Corner", "Box 3D"};

const char* const k_cham_flag_names[] = {"Ignore Z", "Wireframe", "Flat", "Overlay"};
const std::uint32_t k_cham_flag_masks[] = {
    static_cast<std::uint32_t>(config::chams_flag::ignore_z),
    static_cast<std::uint32_t>(config::chams_flag::wireframe),
    static_cast<std::uint32_t>(config::chams_flag::flat),
    static_cast<std::uint32_t>(config::chams_flag::overlay),
};

void cham_flags(config::cham_material& m) {
    std::uint32_t bits = static_cast<std::uint32_t>(m.flags);
    if (multi_bits("Flags", &bits, k_cham_flag_names, k_cham_flag_masks, 4, nullptr))
        m.flags = static_cast<int>(bits);
}

}  // namespace

void visuals(config::store& cfg) {
    config::config_v1& c = cfg.get();

    if (!begin_columns("##visuals_cols")) return;

    ImGui::TableNextColumn();
    if (begin_card("##esp", ImVec2(0, 0))) {
        section("PLAYER ESP");
        toggle("Enabled", &c.visuals.enabled);
        combo("Box", reinterpret_cast<int*>(&c.visuals.box), k_box_styles, 4);
        color_row("Box colour", &c.visuals.box_color);
        separator_y();
        toggle("Health", &c.visuals.health_bar);
        toggle("Armor", &c.visuals.armor_bar);
        toggle("Name", &c.visuals.name);
        toggle("Weapon", &c.visuals.weapon);
        toggle("Ammo", &c.visuals.ammo);
        toggle("Flags", &c.visuals.flags);
        toggle("Distance", &c.visuals.distance);
        toggle("Skeleton", &c.visuals.skeleton);
        toggle("Glow", &c.visuals.glow);
        toggle("Backtrack ghost", &c.visuals.backtrack_ghost);
        separator_y();
        toggle("Hitmarker", &c.visuals.hitmarker);
        toggle("Damage indicator", &c.visuals.damage_indicator);
        toggle("Bullet tracer", &c.visuals.bullet_tracer);
        toggle("Spectator list", &c.visuals.spectator_list);
        slider_int("ESP opacity", &c.visuals.esp_alpha, 0, 255);
    }
    end_card();

    ImGui::TableNextColumn();
    if (begin_card("##chams", ImVec2(0, 0))) {
        section("CHAMS · PLAYER");
        toggle("Enabled", &c.visuals.player_chams.enabled);
        color_row("Visible", &c.visuals.player_chams.visible);
        color_row("Invisible", &c.visuals.player_chams.invisible);
        cham_flags(c.visuals.player_chams);
        slider_float("Wire thickness", &c.visuals.player_chams.wire_thickness, 0.5f, 4.0f);
        separator_y();
        section("CHAMS · VIEWMODEL / ARMS");
        toggle("Viewmodel", &c.visuals.viewmodel_chams.enabled);
        color_row("Viewmodel colour", &c.visuals.viewmodel_chams.visible);
        toggle("Arms", &c.visuals.arms_chams.enabled);
        color_row("Arms colour", &c.visuals.arms_chams.visible);
    }
    end_card();

    ImGui::Dummy(ImVec2(0, 8));
    if (begin_card("##world", ImVec2(0, 0))) {
        section("WORLD");
        toggle("Bomb timer", &c.visuals.bomb_timer);
        toggle("Grenade helper", &c.visuals.grenade_helper, "Trajectory prediction for nades");
        toggle("Projectile ESP", &c.visuals.projectile_esp);
        toggle("Dropped weapons", &c.visuals.dropped_weapons);
        toggle("Radar hack", &c.visuals.radar_hack);
        separator_y();
        toggle("No flash", &c.visuals.no_flash);
        toggle("No smoke", &c.visuals.no_smoke);
        toggle("No scope overlay", &c.visuals.no_scope_overlay);
        slider_float("World modulation", &c.visuals.world_modulation, 0.0f, 1.0f);
    }
    end_card();

    end_columns();
}

}  // namespace shogod::ui::pages
