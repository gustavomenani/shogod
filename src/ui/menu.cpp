#include "ui/menu.hpp"

#include "brand.hpp"
#include "config/keybinds.hpp"
#include "ui/pages/pages.hpp"
#include "ui/style.hpp"
#include "ui/ui_icons.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace shogod::ui {
namespace {

constexpr float k_sidebar_w = 188.0f;
constexpr float k_header_h = 56.0f;
constexpr ImVec2 k_menu_size(1000.0f, 650.0f);

ImTextureID g_brand_tex = 0;
int g_brand_w = 0;
int g_brand_h = 0;
ImTextureID g_avatar_tex = 0;
int g_avatar_size = 0;

float approach(float value, float target, float rate, float dt) {
    const float t = 1.0f - std::exp(-rate * dt);
    return value + (target - value) * t;
}

bool contains_icase(std::string_view hay, std::string_view needle) noexcept {
    if (needle.empty()) return true;
    if (needle.size() > hay.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool ok = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (std::tolower(static_cast<unsigned char>(hay[i + j])) !=
                std::tolower(static_cast<unsigned char>(needle[j]))) {
                ok = false;
                break;
            }
        }
        if (ok) return true;
    }
    return false;
}

const char* tab_name(tab t) {
    switch (t) {
        case tab::rage: return "Ragebot";
        case tab::antiaim: return "Anti-Aim";
        case tab::players: return "Players";
        case tab::world: return "World";
        case tab::misc: return "Main";
        case tab::configs: return "Presets";
        case tab::keybinds: return "Keybinds";
        default: return "";
    }
}

// Neverlose categorized sidebar layout (AIMBOT / VISUALS / MISCELLANEOUS / SETTINGS).
float tab_y(tab t) {
    switch (t) {
        case tab::rage: return 102.0f;
        case tab::antiaim: return 138.0f;
        case tab::players: return 200.0f;
        case tab::world: return 236.0f;
        case tab::misc: return 298.0f;
        case tab::configs: return 360.0f;
        case tab::keybinds: return 396.0f;
        default: return 102.0f;
    }
}

void draw_tab_icon(tab t, ImVec2 c, ImU32 col, float size = 14.5f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ui_icon_id id = ui_icon_id::crosshair;
    switch (t) {
        case tab::rage: id = ui_icon_id::crosshair; break;
        case tab::antiaim: id = ui_icon_id::shield; break;
        case tab::players: id = ui_icon_id::user; break;
        case tab::world: id = ui_icon_id::eye; break;
        case tab::misc: id = ui_icon_id::zap; break;
        case tab::configs: id = ui_icon_id::folder; break;
        case tab::keybinds: id = ui_icon_id::settings; break;
        default: return;
    }
    draw_ui_icon(*dl, id, c, size, col, 1.4f);
    if (t == tab::rage) {
        dl->AddCircleFilled(c, 1.3f, col, 12);
    }
}

void sidebar(menu_state& state, const status& live, float height, bool animated) {
    const palette& pal = colors();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    dl->AddRectFilled(origin, ImVec2(origin.x + k_sidebar_w, origin.y + height),
                      fade_col(pal.bg_sidebar), 10.0f, ImDrawFlags_RoundCornersLeft);
    dl->AddLine(ImVec2(origin.x + k_sidebar_w, origin.y),
                ImVec2(origin.x + k_sidebar_w, origin.y + height),
                fade_col(mix_col(pal.border_soft, pal.accent, 0.35f)), 1.0f);

    if (g_brand_tex != 0 && g_brand_w > 0 && g_brand_h > 0) {
        const float target_w = 132.0f;
        const float target_h =
            target_w * (static_cast<float>(g_brand_h) / static_cast<float>(g_brand_w));
        const ImVec2 logo_a(std::floor(origin.x + (k_sidebar_w - target_w) * 0.5f), origin.y + 10.0f);
        const ImVec2 logo_b(logo_a.x + target_w, logo_a.y + target_h);

        const bool light_sidebar =
            ((pal.bg_sidebar & 0xFF) + ((pal.bg_sidebar >> 8) & 0xFF) +
             ((pal.bg_sidebar >> 16) & 0xFF)) > 384;
        if (light_sidebar) {
            dl->AddRectFilled(ImVec2(logo_a.x - 9.0f, logo_a.y - 3.0f),
                              ImVec2(logo_b.x + 9.0f, logo_b.y + 3.0f),
                              fade_col(IM_COL32(12, 18, 28, 255)), 7.0f);
        }
        dl->AddImage(g_brand_tex, logo_a, logo_b, ImVec2(0, 0), ImVec2(1, 1),
                     fade_col(IM_COL32(255, 255, 255, 255)));

        ImGui::PushFont(font_small(), 10.5f);
        const ImVec2 sub_sz = ImGui::CalcTextSize("Counter-Strike 2");
        dl->AddText(ImVec2(std::floor(origin.x + (k_sidebar_w - sub_sz.x) * 0.5f),
                           std::floor(logo_b.y + 5.0f)),
                    fade_col(pal.text_faint), "Counter-Strike 2");
        ImGui::PopFont();
        dl->AddLine(ImVec2(origin.x + 14.0f, origin.y + 74.0f),
                    ImVec2(origin.x + k_sidebar_w - 14.0f, origin.y + 74.0f),
                    fade_col(pal.border_soft), 1.0f);
    } else {
        const ImVec2 mark(origin.x + 16.0f, origin.y + 16.0f);
        dl->AddRectFilled(mark, ImVec2(mark.x + 30.0f, mark.y + 30.0f), fade_col(pal.accent_soft), 7.0f);
        dl->AddRect(mark, ImVec2(mark.x + 30.0f, mark.y + 30.0f), fade_col(pal.accent, 0.45f), 7.0f);
        draw_ui_icon(*dl, ui_icon_id::brand_mark, ImVec2(mark.x + 15.0f, mark.y + 15.0f), 20.0f,
                     fade_col(pal.accent), 1.55f);

        ImGui::SetCursorPos(ImVec2(55.0f, 13.0f));
        ImGui::PushFont(font_title(), 18.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, fade_col(pal.text));
        ImGui::TextUnformatted("SHOGOD");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::SetCursorPos(ImVec2(55.0f, 34.0f));
        ImGui::PushFont(font_small(), 11.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, fade_col(pal.text_faint));
        ImGui::TextUnformatted("Counter-Strike 2");
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    auto category_label = [&](float y, const char* text) {
        ImGui::PushFont(font_small(), 10.0f);
        dl->AddText(ImVec2(origin.x + 16.0f, origin.y + y), fade_col(pal.text_faint), text);
        ImGui::PopFont();
    };
    category_label(84.0f, "AIMBOT");
    category_label(182.0f, "VISUALS");
    category_label(280.0f, "MISCELLANEOUS");
    category_label(342.0f, "SETTINGS");

    const float target = tab_y(state.active);
    if (!animated || state.nav_y < 0.0f) state.nav_y = target;
    state.nav_y = approach(state.nav_y, target, 24.0f, ImGui::GetIO().DeltaTime);
    const float selected_y = std::round(origin.y + state.nav_y);
    const ImVec2 pill_a(origin.x + 10.0f, selected_y);
    const ImVec2 pill_b(origin.x + k_sidebar_w - 10.0f, selected_y + 32.0f);
    dl->AddRectFilled(pill_a, pill_b, fade_col(pal.accent_soft, 0.92f), 7.0f);
    dl->AddRect(pill_a, pill_b, fade_col(pal.accent, 0.45f), 7.0f, 0, 1.0f);
    dl->AddRectFilled(ImVec2(pill_a.x + 2.0f, selected_y + 7.0f),
                      ImVec2(pill_a.x + 4.5f, selected_y + 25.0f), fade_col(pal.accent), 2.0f);

    for (int i = 0; i < k_tab_count; ++i) {
        const tab t = static_cast<tab>(i);
        const bool active = state.active == t;
        const float y = tab_y(t);

        ImGui::SetCursorPos(ImVec2(10.0f, y));
        ImGui::PushID(i);
        const bool pressed = ImGui::InvisibleButton("##nav", ImVec2(k_sidebar_w - 20.0f, 32.0f),
                                                    ImGuiButtonFlags_EnableNav);
        const bool hovered = ImGui::IsItemHovered();
        if (pressed) {
            state.active = t;
            state.search_query[0] = '\0';
        }
        ImGui::PopID();

        const ImVec2 item_pos = ImGui::GetItemRectMin();
        const ImVec2 item_max = ImGui::GetItemRectMax();
        if (!active && hovered) {
            dl->AddRectFilled(item_pos, item_max, fade_col(pal.bg_card_hover, 0.7f), 7.0f);
        }

        const ImU32 col = fade_col(active || hovered ? pal.text : pal.text_muted);
        draw_tab_icon(t, ImVec2(item_pos.x + 18.0f, item_pos.y + 16.0f),
                      active ? fade_col(pal.accent) : col);
        dl->AddText(ImVec2(item_pos.x + 34.0f, item_pos.y + (32.0f - ImGui::GetFontSize()) * 0.5f),
                    col, tab_name(t));
    }

    const float footer_y = height - 64.0f;
    dl->AddLine(ImVec2(origin.x + 14.0f, origin.y + footer_y),
                ImVec2(origin.x + k_sidebar_w - 14.0f, origin.y + footer_y),
                fade_col(pal.border_soft));

    const float av_r = 18.0f;
    const ImVec2 avatar(origin.x + 34.0f, origin.y + height - 32.0f);
    if (g_avatar_tex != 0 && g_avatar_size > 0) {
        dl->AddImage(g_avatar_tex, ImVec2(avatar.x - av_r, avatar.y - av_r),
                     ImVec2(avatar.x + av_r, avatar.y + av_r), ImVec2(0, 0), ImVec2(1, 1),
                     fade_col(IM_COL32(255, 255, 255, 255)));
    } else {
        dl->AddCircleFilled(avatar, av_r, fade_col(pal.accent_soft), 32);
        draw_ui_icon(*dl, ui_icon_id::user, avatar, 18.0f, fade_col(pal.accent), 1.5f);
    }
    dl->AddCircle(avatar, av_r, fade_col(pal.accent, 0.85f), 36, 1.5f);
    dl->AddCircleFilled(ImVec2(avatar.x + 13.0f, avatar.y + 13.0f), 4.8f,
                        fade_col(pal.bg_sidebar), 16);
    dl->AddCircleFilled(ImVec2(avatar.x + 13.0f, avatar.y + 13.0f), 3.4f,
                        fade_col(live.anchors_ok > 0 ? pal.success : pal.accent), 16);

    ImGui::PushFont(font_body(), 14.5f);
    dl->AddText(ImVec2(origin.x + 60.0f, origin.y + height - 45.0f), fade_col(pal.text), "shoot");
    ImGui::PopFont();

    ImGui::PushFont(font_small(), font_size_small());
    char ver[32];
    std::snprintf(ver, sizeof ver, "v%.*s • live", static_cast<int>(brand::k_version.size()),
                  brand::k_version.data());
    dl->AddText(ImVec2(origin.x + 60.0f, origin.y + height - 27.0f), fade_col(pal.text_faint), ver);
    ImGui::PopFont();
}

void header(config::store& cfg, menu_state& state, const status& live, float width) {
    const palette& pal = colors();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    // Left: Active section title
    ImGui::SetCursorPos(ImVec2(22.0f, 15.0f));
    ImGui::PushFont(font_title(), 21.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, fade_col(pal.text));
    ImGui::TextUnformatted(tab_name(state.active));
    ImGui::PopStyleColor();
    ImGui::PopFont();

    // Center-left: Neverlose Search bar (filters every bindable control across tabs)
    const float search_w = 216.0f;
    const float search_x = 158.0f;
    const float pill_h = 26.0f;
    const float pill_y = 15.0f;
    ImGui::SetCursorPos(ImVec2(search_x, pill_y));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 4.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, fade_col(pal.bg_input));
    ImGui::PushStyleColor(ImGuiCol_Border, fade_col(pal.border_soft));
    ImGui::SetNextItemWidth(search_w);
    ImGui::InputTextWithHint("##nl_search", "Search settings...", state.search_query,
                             sizeof state.search_query);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    // Top-right pills: [Quick Save] [Binds N] [Config] [FPS]
    ImGui::PushFont(font_small(), font_size_small());
    const std::string fps = std::to_string(static_cast<int>(live.fps)) + " FPS";
    const float fps_w = ImGui::CalcTextSize(fps.c_str()).x + 26.0f;

    char binds_buf[24]{};
    std::snprintf(binds_buf, sizeof binds_buf, "Binds (%zu)", cfg.get().keybinds.size());
    const float binds_w = ImGui::CalcTextSize(binds_buf).x + 22.0f;

    const char* cfg_lbl = live.config_name.empty() ? "default" : live.config_name.c_str();
    const float cfg_w = std::max(86.0f, ImGui::CalcTextSize(cfg_lbl).x + 34.0f);
    const float save_w = 56.0f;

    float rx = width - 22.0f;

    // FPS badge
    rx -= fps_w;
    const ImVec2 fps_a(origin.x + rx, origin.y + pill_y);
    const ImVec2 fps_b(fps_a.x + fps_w, fps_a.y + pill_h);
    dl->AddRectFilled(fps_a, fps_b, fade_col(pal.bg_card), 6.0f);
    dl->AddRect(fps_a, fps_b, fade_col(pal.border_soft), 6.0f);
    dl->AddCircleFilled(ImVec2(fps_a.x + 10.0f, fps_a.y + pill_h * 0.5f), 2.8f,
                        fade_col(pal.success), 12);
    dl->AddText(ImVec2(fps_a.x + 18.0f, fps_a.y + (pill_h - ImGui::GetFontSize()) * 0.5f),
                fade_col(pal.text_muted), fps.c_str());

    // Binds pill
    rx -= (6.0f + binds_w);
    ImGui::SetCursorPos(ImVec2(rx, pill_y));
    const ImVec2 b_a = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##hdr_binds", ImVec2(binds_w, pill_h))) {
        state.active = tab::keybinds;
    }
    const bool b_hov = ImGui::IsItemHovered();
    const ImVec2 b_b(b_a.x + binds_w, b_a.y + pill_h);
    dl->AddRectFilled(b_a, b_b, fade_col(b_hov ? pal.bg_card_hover : pal.bg_card), 6.0f);
    dl->AddRect(b_a, b_b, fade_col(b_hov ? pal.accent : pal.border_soft), 6.0f);
    dl->AddText(ImVec2(b_a.x + 11.0f, b_a.y + (pill_h - ImGui::GetFontSize()) * 0.5f),
                fade_col(b_hov || !cfg.get().keybinds.empty() ? pal.accent : pal.text_muted),
                binds_buf);

    // Config pill
    rx -= (6.0f + cfg_w);
    ImGui::SetCursorPos(ImVec2(rx, pill_y));
    const ImVec2 c_a = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##cfg_pill", ImVec2(cfg_w, pill_h))) {
        state.active = tab::configs;
    }
    const bool c_hov = ImGui::IsItemHovered();
    const ImVec2 c_b(c_a.x + cfg_w, c_a.y + pill_h);
    dl->AddRectFilled(c_a, c_b, fade_col(c_hov ? pal.bg_card_hover : pal.bg_card), 6.0f);
    dl->AddRect(c_a, c_b, fade_col(c_hov ? pal.accent : pal.border_soft), 6.0f);
    draw_tab_icon(tab::configs, ImVec2(c_a.x + 13.0f, c_a.y + pill_h * 0.5f),
                  fade_col(c_hov ? pal.accent : pal.text_muted), 12.5f);
    dl->AddText(ImVec2(c_a.x + 25.0f, c_a.y + (pill_h - ImGui::GetFontSize()) * 0.5f),
                fade_col(pal.text), cfg_lbl);

    // Quick Save pill
    rx -= (6.0f + save_w);
    ImGui::SetCursorPos(ImVec2(rx, pill_y));
    const ImVec2 s_a = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##quick_save", ImVec2(save_w, pill_h))) {
        const auto p = config::store::default_dir() / (std::string(cfg_lbl) + ".json");
        if (cfg.save(p)) state.notify("saved " + std::string(cfg_lbl) + ".json");
        else state.notify("save failed");
    }
    const bool s_hov = ImGui::IsItemHovered();
    const ImVec2 s_b(s_a.x + save_w, s_a.y + pill_h);
    dl->AddRectFilled(s_a, s_b, fade_col(s_hov ? pal.accent : pal.accent_soft), 6.0f);
    dl->AddRect(s_a, s_b, fade_col(pal.accent, 0.8f), 6.0f);
    const ImVec2 sv_sz = ImGui::CalcTextSize("Save");
    dl->AddText(ImVec2(s_a.x + (save_w - sv_sz.x) * 0.5f,
                       s_a.y + (pill_h - sv_sz.y) * 0.5f),
                fade_col(pal.text), "Save");
    ImGui::PopFont();

    dl->AddLine(ImVec2(origin.x, origin.y + k_header_h),
                ImVec2(origin.x + width, origin.y + k_header_h), fade_col(pal.border_soft));
}

void draw_search_results(config::store& cfg, menu_state& state) {
    const palette& pal = colors();
    widgets::section("Search results");
    if (widgets::begin_card("##search_results", ImVec2(0, 0))) {
        const std::string_view q(state.search_query);
        int found = 0;
        for (const auto& spec : config::bind_target_catalog()) {
            if (!contains_icase(spec.label, q) && !contains_icase(spec.category, q) &&
                !contains_icase(spec.id, q)) {
                continue;
            }
            ++found;
            ImGui::PushID(spec.id);
            const float row_w = std::max(160.0f, ImGui::GetContentRegionAvail().x - 14.0f);
            const ImVec2 rpos = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##sr_row", ImVec2(row_w, 28.0f))) {
                state.active = static_cast<tab>(std::clamp(spec.tab_index, 0, k_tab_count - 1));
                state.search_query[0] = '\0';
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                widgets::open_keybind_popup(spec.id);
            }
            const bool hov = ImGui::IsItemHovered();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (hov) {
                dl->AddRectFilled(rpos, ImVec2(rpos.x + row_w, rpos.y + 28.0f),
                                  fade_col(pal.bg_card_hover), 6.0f);
            }
            dl->AddText(ImVec2(rpos.x + 10.0f, rpos.y + 6.0f), fade_col(pal.text), spec.label);
            ImGui::PushFont(font_small(), font_size_small());
            const char* hint = "Click to open  |  Right-click to bind";
            const ImVec2 csz = ImGui::CalcTextSize(spec.category);
            dl->AddText(ImVec2(rpos.x + row_w - csz.x - 12.0f, rpos.y + 7.0f),
                        fade_col(pal.accent), spec.category);
            if (hov) {
                const ImVec2 hsz = ImGui::CalcTextSize(hint);
                dl->AddText(ImVec2(rpos.x + row_w - csz.x - hsz.x - 28.0f, rpos.y + 7.0f),
                            fade_col(pal.text_faint), hint);
            }
            ImGui::PopFont();
            ImGui::PopID();
        }
        if (found == 0) {
            ImGui::TextDisabled("No matching settings for \"%s\"", state.search_query);
        }
        (void)cfg;
    }
    widgets::end_card();
}

}  // namespace

void set_brand_logo(ImTextureID tex, int width, int height) noexcept {
    g_brand_tex = tex;
    g_brand_w = width;
    g_brand_h = height;
}

void set_user_avatar(ImTextureID tex, int size) noexcept {
    g_avatar_tex = tex;
    g_avatar_size = size;
}

void draw_menu(config::store& cfg, menu_state& state, const status& live) {
    const float dt = ImGui::GetIO().DeltaTime;
    const config::config_v1& c = cfg.get();
    const float open_target = state.open ? 1.0f : 0.0f;
    state.open_anim =
        c.ui.animations ? approach(state.open_anim, open_target, 28.0f, dt) : open_target;
    if (!state.open && state.open_anim < 0.01f) {
        state.open_anim = 0.0f;
        widgets::set_active_bind_store(nullptr);
        return;
    }

    apply_style(c.ui.accent, c.ui.dark_theme);
    widgets::set_active_bind_store(&cfg.get());

    for (int i = 0; i < k_tab_count; ++i) {
        const float target = static_cast<int>(state.active) == i ? 1.0f : 0.0f;
        state.tab_anim[static_cast<std::size_t>(i)] =
            c.ui.animations ? approach(state.tab_anim[static_cast<std::size_t>(i)], target, 24.0f, dt)
                            : target;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float alpha = state.open_anim;
    const ImVec2 size(k_menu_size.x, k_menu_size.y);
    const ImVec2 pos(vp->WorkPos.x + (vp->WorkSize.x - size.x) * 0.5f,
                     vp->WorkPos.y + (vp->WorkSize.y - size.y) * 0.5f);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##shogod_menu", nullptr, flags);

    const palette& pal = colors();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 win = ImGui::GetWindowPos();
    dl->AddRectFilled(win, ImVec2(win.x + size.x, win.y + size.y),
                      fade_col(pal.bg_window, c.ui.opacity), 10.0f);

    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    sidebar(state, live, size.y, c.ui.animations);
    ImGui::PopStyleColor();

    const ImU32 frame_border = fade_col(mix_col(pal.border, pal.accent, 0.55f));
    dl->AddRect(win, ImVec2(win.x + size.x, win.y + size.y), frame_border, 10.0f, 0, 1.25f);

    ImGui::SetCursorPos(ImVec2(k_sidebar_w, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##main", ImVec2(size.x - k_sidebar_w, size.y), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    header(cfg, state, live, size.x - k_sidebar_w);

    const float transition = state.tab_anim[static_cast<std::size_t>(state.active)];
    ImGui::SetCursorPos(ImVec2(22.0f, k_header_h + 14.0f));
    ImGui::BeginChild("##content", ImVec2(size.x - k_sidebar_w - 44.0f, size.y - k_header_h - 28.0f),
                      ImGuiChildFlags_None);

    const float tab_alpha = state.open ? alpha * (0.4f + 0.6f * transition) : alpha;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, tab_alpha);

    if (state.search_query[0] != '\0') {
        draw_search_results(cfg, state);
    } else {
        switch (state.active) {
            case tab::rage: pages::rage(cfg, live); break;
            case tab::antiaim: pages::antiaim(cfg); break;
            case tab::players: pages::players(cfg, live); break;
            case tab::world: pages::world(cfg, live); break;
            case tab::misc: pages::misc(cfg); break;
            case tab::configs: pages::configs(cfg, state); break;
            case tab::keybinds: pages::keybinds_and_settings(cfg, live); break;
            default: break;
        }
    }

    ImGui::PopStyleVar();

    if (state.toast_time > 0.0f) {
        state.toast_time -= dt;
        const float a = std::clamp(state.toast_time / 0.4f, 0.0f, 1.0f);
        const ImVec2 text = ImGui::CalcTextSize(state.toast.c_str());
        const ImVec2 avail = ImGui::GetWindowSize();
        const ImVec2 p(avail.x - text.x - 46.0f, avail.y - 40.0f);
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(win.x + k_sidebar_w + p.x, win.y + k_header_h + p.y),
            ImVec2(win.x + k_sidebar_w + p.x + text.x + 24.0f, win.y + k_header_h + p.y + 28.0f),
            fade_col(pal.accent_soft, a), 6.0f);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(win.x + k_sidebar_w + p.x + 12.0f, win.y + k_header_h + p.y + 6.0f),
            fade_col(pal.text, a), state.toast.c_str());
    }

    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Render global Neverlose right-click Keybind editor popup cleanly on top.
    widgets::render_keybind_popup();

    ImGui::End();
    ImGui::PopStyleVar(2);
}

}  // namespace shogod::ui
