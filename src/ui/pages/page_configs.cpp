#include "ui/pages/pages.hpp"

#include "brand.hpp"
#include "ui/style.hpp"
#include "ui/widgets.hpp"

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
    if (begin_card("##session", ImVec2(0, 0))) {
        section("PROFILE");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputText("##name", g_name, sizeof g_name);
        ImGui::Dummy(ImVec2(0, 4));

        const auto path = config::store::default_dir() / (std::string(g_name) + ".json");

        if (action_button("Save", ImVec2(120, 0))) {
            if (cfg.save(path)) state.notify("saved " + std::string(g_name) + ".json");
            else state.notify("save failed");
        }
        ImGui::SameLine();
        if (action_button("Load", ImVec2(120, 0))) {
            if (cfg.load(path)) state.notify("loaded " + std::string(g_name) + ".json");
            else state.notify("no such config");
        }

        separator_y();
        section("PRESETS");
        if (action_button("Safe", ImVec2(120, 0))) {
            config::apply_safe_profile(cfg.get());
            state.notify("safe profile applied");
        }
        ImGui::SameLine();
        if (action_button("Rage", ImVec2(120, 0))) {
            config::apply_rage_profile(cfg.get());
            state.notify("rage profile applied");
        }
        ImGui::SameLine();
        if (action_button("Reset", ImVec2(120, 0))) {
            cfg.get() = config::config_v1{};
            config::apply_safe_profile(cfg.get());
            state.notify("defaults restored");
        }

        separator_y();
        ImGui::PushStyleColor(ImGuiCol_Text, colors().text_faint);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("%s", config::store::default_dir().string().c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    end_card();

    ImGui::TableNextColumn();
    if (begin_card("##existing", ImVec2(0, 0))) {
        section("ON DISK");
        const auto profiles = list_profiles();
        if (profiles.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, colors().text_faint);
            ImGui::TextUnformatted("no profiles yet");
            ImGui::PopStyleColor();
        }
        for (const auto& name : profiles) {
            ImGui::PushID(name.c_str());
            if (ImGui::Selectable(name.c_str())) {
                std::strncpy(g_name, name.c_str(), sizeof g_name - 1);
                cfg.load(config::store::default_dir() / (name + ".json"));
                state.notify("loaded " + name + ".json");
            }
            ImGui::PopID();
        }
    }
    end_card();

    end_columns();
}

}  // namespace shogod::ui::pages
