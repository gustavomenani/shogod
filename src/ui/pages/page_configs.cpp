#include "ui/pages/pages.hpp"

#include "brand.hpp"
#include "ui/style.hpp"
#include "ui/ui_icons.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace shogod::ui::pages {
namespace {

using namespace widgets;

char g_name[48] = "default";

std::vector<std::string> list_profiles() {
    std::vector<std::string> out;
    std::error_code ec;
    const auto dir = config::store::default_dir();
    if (!std::filesystem::is_directory(dir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            out.push_back(entry.path().stem().string());
        }
    }
    return out;
}

}  // namespace

void configs(config::store& cfg, menu_state& state) {
    if (!begin_columns("##config_cols")) return;

    ImGui::TableNextColumn();
    section("Profile");
    if (begin_card("##session", ImVec2(0, 0))) {
        const float inner_w = std::max(120.0f, ImGui::GetContentRegionAvail().x - 14.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::SetNextItemWidth(inner_w);
        ImGui::InputText("##name", g_name, sizeof g_name);
        ImGui::PopStyleVar(2);
        ImGui::Dummy(ImVec2(0, 4));

        const auto path = config::store::default_dir() / (std::string(g_name) + ".json");
        const float gap = 8.0f;
        const float act_w = std::floor((inner_w - gap * 2.0f) / 3.0f);
        if (action_button("Save", ImVec2(act_w, 0))) {
            if (cfg.save(path)) state.notify("saved " + std::string(g_name) + ".json");
            else state.notify("save failed");
        }
        ImGui::SameLine(0.0f, gap);
        if (action_button("Load", ImVec2(act_w, 0))) {
            if (cfg.load(path)) state.notify("loaded " + std::string(g_name) + ".json");
            else state.notify("could not load " + std::string(g_name) + ".json");
        }
        ImGui::SameLine(0.0f, gap);
        if (action_button("Delete", ImVec2(act_w, 0))) {
            std::error_code ec;
            if (std::filesystem::remove(path, ec) && !ec) {
                state.notify("deleted " + std::string(g_name) + ".json");
            } else {
                state.notify("could not delete " + std::string(g_name) + ".json");
            }
        }

        separator_y();
        const float three_w = std::floor((inner_w - gap * 2.0f) / 3.0f);
        if (action_button("Safe", ImVec2(three_w, 0))) {
            config::apply_safe_profile(cfg.get());
            state.notify("safe profile applied");
        }
        ImGui::SameLine(0.0f, gap);
        if (action_button("Rage", ImVec2(three_w, 0))) {
            config::apply_rage_profile(cfg.get());
            state.notify("rage profile applied");
        }
        ImGui::SameLine(0.0f, gap);
        if (action_button("Reset", ImVec2(three_w, 0))) {
            cfg.get() = config::config_v1{};
            config::apply_safe_profile(cfg.get());
            state.notify("defaults restored");
        }

        separator_y();
        ImGui::PushStyleColor(ImGuiCol_Text, fade_col(colors().text_faint));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner_w);
        ImGui::TextWrapped("%s", config::store::default_dir().string().c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    end_card();

    ImGui::TableNextColumn();
    section("Saved profiles");
    if (begin_card("##existing", ImVec2(0, 0))) {
        const auto profiles = list_profiles();
        if (profiles.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, fade_col(colors().text_faint));
            ImGui::TextUnformatted("no profiles yet");
            ImGui::PopStyleColor();
        }
        for (const auto& name : profiles) {
            ImGui::PushID(name.c_str());
            const float row_w = std::max(100.0f, ImGui::GetContentRegionAvail().x - 14.0f);
            const ImVec2 rpos = ImGui::GetCursorScreenPos();
            const bool active_profile = (std::strcmp(g_name, name.c_str()) == 0);
            const bool clicked = ImGui::InvisibleButton("##prof", ImVec2(row_w, 28.0f));
            const bool hovered = ImGui::IsItemHovered();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (active_profile || hovered) {
                dl->AddRectFilled(
                    rpos, ImVec2(rpos.x + row_w, rpos.y + 28.0f),
                    fade_col(active_profile ? colors().accent_soft : colors().bg_card_hover), 6.0f);
            }
            if (active_profile) {
                dl->AddRectFilled(ImVec2(rpos.x + 2.0f, rpos.y + 6.0f),
                                  ImVec2(rpos.x + 4.5f, rpos.y + 22.0f), fade_col(colors().accent),
                                  2.0f);
            }
            const ImU32 row_col = fade_col(active_profile || hovered ? colors().text : colors().text_muted);
            draw_ui_icon(*dl, ui_icon_id::folder, ImVec2(rpos.x + 17.0f, rpos.y + 14.0f), 13.0f,
                         active_profile ? fade_col(colors().accent) : row_col, 1.4f);
            dl->AddText(ImVec2(rpos.x + 30.0f, rpos.y + (28.0f - ImGui::GetTextLineHeight()) * 0.5f),
                        row_col, name.c_str());
            if (clicked) {
                if (cfg.load(config::store::default_dir() / (name + ".json"))) {
                    std::snprintf(g_name, sizeof g_name, "%s", name.c_str());
                    state.notify("loaded " + name + ".json");
                } else {
                    state.notify("could not load " + name + ".json");
                }
            }
            ImGui::PopID();
        }
    }
    end_card();

    end_columns();
}

}  // namespace shogod::ui::pages
