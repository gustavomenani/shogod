#include "ui/pages/pages.hpp"

#include "ui/preview/model.hpp"
#include "ui/preview/preview.hpp"
#include "ui/style.hpp"
#include "ui/widgets.hpp"

#include <algorithm>

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

const char* const k_cham_targets[] = {"Enemy", "Team", "Local", "Arms", "Weapon"};
const char* const k_cham_shaders[] = {"Shaded", "Flat", "Glow", "Glossy"};

void cham_flags(config::cham_material& m) {
    std::uint32_t bits = static_cast<std::uint32_t>(m.flags);
    if (multi_bits("Flags", &bits, k_cham_flag_names, k_cham_flag_masks, 4, nullptr))
        m.flags = static_cast<int>(bits);
}

}  // namespace

void visuals(config::store& cfg, const status& live) {
    config::config_v1& c = cfg.get();

    if (!begin_columns("##visuals_cols")) return;

    ImGui::TableNextColumn();
    section("Player ESP");
    if (begin_card("##esp", ImVec2(0, 0))) {
        toggle("Enabled", &c.visuals.enabled);
        combo("Bounding box", reinterpret_cast<int*>(&c.visuals.box), k_box_styles, 4);
        color_row("Box colour", &c.visuals.box_color);
        separator_y();
        multi_select("Elements", {
            {"Health bar", &c.visuals.health_bar},
            {"Armor bar", &c.visuals.armor_bar},
            {"Player name", &c.visuals.name},
            {"Flags", &c.visuals.flags},
            {"Distance", &c.visuals.distance},
        });
        multi_select("Weapon display", {
            {"Icon", &c.visuals.weapon_icon},
            {"Name", &c.visuals.weapon},
            {"Ammo", &c.visuals.ammo},
        });
        toggle("Skeleton", &c.visuals.skeleton);
        toggle("Backtrack ghost", &c.visuals.backtrack_ghost);
        toggle("Visible only", &c.visuals.visible_only,
               "Hide players without line of sight; otherwise they draw dimmed");
        toggle("Off-screen arrows", &c.visuals.offscreen,
               "Edge arrows for visible enemies outside the viewport");
        slider_int("ESP opacity", &c.visuals.esp_alpha, 0, 255);
    }
    end_card();

    section("World");
    if (begin_card("##world", ImVec2(0, 0))) {
        toggle("Bomb timer", &c.visuals.bomb_timer, "Shows planted C4 site and countdown");
        toggle("Penetration crosshair", &c.visuals.penetration_crosshair,
               "Shows wallbang penetration dot and damage on aim point");
        separator_y();
        toggle("World modulation", &c.visuals.world_modulate);
        if (c.visuals.world_modulate) color_row("World colour", &c.visuals.world_color);
        toggle("Light modulation", &c.visuals.light_modulate);
        if (c.visuals.light_modulate) color_row("Light colour", &c.visuals.light_color);
        slider_float_step("Exposure %", &c.visuals.exposure, 0.0f, 200.0f, "0 = engine default");
    }
    end_card();

    ImGui::TableNextColumn();
    if (preview::shared_panel().ready()) {
        section("Preview");
        if (begin_card("##preview3d", ImVec2(0, 0))) {
            static int preview_target = 0;
            static constexpr const char* k_preview_labels[3] = {"Enemy", "Team", "Local"};
            static constexpr const char* k_preview_tags[3] = {"SAS", "TEAM", "LOCAL"};
            static constexpr const char* k_stance_names[3] = {
                "Combat stance  1/3", "Standing  2/3", "A-Pose  3/3"};

            // Top row inside preview card: stance hint left, Enemy | Team | Local chips right.
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Drag to orbit");
            float cluster_w = 0.0f;
            for (int t = 0; t < 3; ++t) {
                if (t > 0) cluster_w += 4.0f;
                cluster_w += ImGui::CalcTextSize(k_preview_labels[t]).x + 20.0f;
            }
            const float avail_row_x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            ImGui::SameLine();
            ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), avail_row_x - cluster_w - 2.0f));
            for (int t = 0; t < 3; ++t) {
                if (t > 0) ImGui::SameLine(0.0f, 4.0f);
                if (chip(k_preview_labels[t], preview_target == t)) {
                    preview_target = t;
                }
            }

            const config::cham_material* preview_mats[3] = {
                &c.visuals.player_chams, &c.visuals.team_chams, &c.visuals.local_chams};

            const preview::model_data model = preview::current_model();
            preview::shared_panel().set_skeleton(
                std::span<const math::vec3>(model.joints, static_cast<std::size_t>(model.joint_count)),
                std::span<const std::pair<int, int>>(model.connections,
                                                     static_cast<std::size_t>(model.connection_count)));
            const float avail_w = (std::max)(80.0f, ImGui::GetContentRegionAvail().x - 2.0f);
            preview::shared_panel().draw(ImVec2(avail_w, 304.0f), preview::shared_camera(),
                                         *preview_mats[preview_target], c.visuals.skeleton,
                                         &c.visuals, k_preview_tags[preview_target], &c.visuals);

            // Stance selector footer: [ < ]   Combat stance  1/3   [ > ]  [Reset]
            const int cur_stance = static_cast<int>(preview::shared_panel().current_stance());
            const int stance_count = static_cast<int>(preview::stance::count);
            const float row_w = avail_w;
            const float arrow_w = 34.0f;
            const float reset_w = 62.0f;
            const float row_x = ImGui::GetCursorPosX();
            const float row_y = ImGui::GetCursorPosY() + 2.0f;
            ImGui::SetCursorPos(ImVec2(row_x, row_y));
            ImGui::PushID("stance_prev");
            if (action_button("<", ImVec2(arrow_w, 24.0f))) {
                const int next = (cur_stance + stance_count - 1) % stance_count;
                preview::shared_panel().set_stance(static_cast<preview::stance>(next));
            }
            ImGui::PopID();
            const ImVec2 txt_sz = ImGui::CalcTextSize(k_stance_names[cur_stance]);
            ImGui::SetCursorPos(ImVec2(row_x + (row_w - reset_w - 6.0f - txt_sz.x) * 0.5f, row_y + 4.0f));
            ImGui::TextDisabled("%s", k_stance_names[cur_stance]);
            ImGui::SetCursorPos(ImVec2(row_x + row_w - arrow_w - reset_w - 4.0f, row_y));
            ImGui::PushID("stance_next");
            if (action_button(">", ImVec2(arrow_w, 24.0f))) {
                const int next = (cur_stance + 1) % stance_count;
                preview::shared_panel().set_stance(static_cast<preview::stance>(next));
            }
            ImGui::PopID();
            ImGui::SetCursorPos(ImVec2(row_x + row_w - reset_w, row_y));
            if (action_button("Reset", ImVec2(reset_w, 24.0f))) {
                c.visuals.esp_layout = config::default_esp_layout();
            }
        }
        end_card();
    }

    section("Camera");
    if (begin_card("##camera", ImVec2(0, 0))) {
        ImGui::BeginDisabled(!live.third_person_available);
        toggle("Third person", &c.visuals.third_person);
        if (c.visuals.third_person) {
            slider_int("Camera distance", &c.visuals.third_person_distance, 48, 160);
        }
        slider_int("Camera FOV", &c.visuals.camera_fov, 0, 135,
                   "0 = engine default, 50-135 overrides camera field of view");
        if (c.visuals.camera_fov > 0) {
            toggle("FOV while scoped", &c.visuals.fov_while_scoped,
                   "Keep custom FOV active when zooming with a sniper scope");
        }
        ImGui::EndDisabled();
        if (!live.third_person_available) ImGui::TextUnformatted("Camera hook unavailable");
    }
    end_card();

    section("Removals");
    if (begin_card("##removals", ImVec2(0, 0))) {
        ImGui::BeginDisabled(!live.no_flash_available);
        toggle("No flash overlay", &c.visuals.no_flash);
        ImGui::EndDisabled();
        if (!live.no_flash_available) ImGui::TextUnformatted("Flash overlay field unavailable");
        ImGui::BeginDisabled(!live.no_smoke_available);
        toggle("No smoke", &c.visuals.no_smoke);
        ImGui::EndDisabled();
        if (!live.no_smoke_available) ImGui::TextUnformatted("Smoke render hook unavailable");
        ImGui::BeginDisabled(!live.no_scope_available);
        toggle("No scope overlay", &c.visuals.no_scope);
        ImGui::EndDisabled();
        if (!live.no_scope_available) ImGui::TextUnformatted("Scope render hook unavailable");
        toggle("Remove view punch", &c.visuals.remove_view_punch);
    }
    end_card();

    section("Chams (in-game & preview)");
    if (begin_card("##chams", ImVec2(0, 0))) {
        static int cham_target = 0;
        combo("Target", &cham_target, k_cham_targets, 5);
        config::cham_material* targets[] = {
            &c.visuals.player_chams, &c.visuals.team_chams, &c.visuals.local_chams,
            &c.visuals.arms_chams,   &c.visuals.weapon_chams,
        };
        config::cham_material& mat = *targets[cham_target];
        toggle("Enabled", &mat.enabled);
        combo("Shader", &mat.material, k_cham_shaders, 4);
        color_row("Visible", &mat.visible);
        color_row("Invisible", &mat.invisible);
        cham_flags(mat);
        if (mat.material >= 2) {
            slider_float("Fresnel exponent", &mat.fresnel_exponent, 0.5f, 8.0f);
            slider_float("Fresnel falloff", &mat.fresnel_falloff, 0.5f, 8.0f);
        }
        slider_float("Wire thickness", &mat.wire_thickness, 0.5f, 4.0f);
        if (cham_target == 2) {
            separator_y();
            toggle("Blend while scoped", &c.visuals.scope_blend);
            if (c.visuals.scope_blend) {
                slider_int("Scope blend alpha", &c.visuals.scope_blend_alpha, 0, 255);
            }
        }
    }
    end_card();

    end_columns();
}

}  // namespace shogod::ui::pages
