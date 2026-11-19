#include "ui/menu.hpp"

#include "brand.hpp"
#include "ui/pages/pages.hpp"
#include "ui/style.hpp"

#include <algorithm>
#include <string>

namespace shogod::ui {
namespace {

constexpr float k_sidebar_w = 178.0f;
constexpr float k_header_h = 58.0f;
constexpr ImVec2 k_menu_size(984.0f, 624.0f);

float approach(float value, float target, float rate, float dt) {
    const float t = 1.0f - std::exp(-rate * dt);
    return value + (target - value) * t;
}

const char* tab_name(tab t) {
    switch (t) {
        case tab::rage: return "RageBot";
        case tab::visuals: return "Visuals";
        case tab::misc: return "Misc";
        case tab::configs: return "Config";
        case tab::settings: return "Settings";
        default: return "";
    }
}

const char* tab_subtitle(tab t) {
    switch (t) {
        case tab::rage: return "target selection · aim · antiaim · resolver";
        case tab::visuals: return "esp · chams · world · preview";
        case tab::misc: return "movement · utilities · info";
        case tab::configs: return "profiles · save · load";
        case tab::settings: return "theme · input · anchors";
        default: return "";
    }
}

void icon(tab t, ImVec2 c, ImU32 col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = 5.0f;
    switch (t) {
        case tab::rage: {
            dl->AddCircle(c, r, col, 20, 1.4f);
            dl->AddCircleFilled(c, 1.4f, col, 10);
            dl->AddLine(ImVec2(c.x, c.y - r - 3), ImVec2(c.x, c.y - r - 1), col, 1.4f);
            dl->AddLine(ImVec2(c.x, c.y + r + 1), ImVec2(c.x, c.y + r + 3), col, 1.4f);
            dl->AddLine(ImVec2(c.x - r - 3, c.y), ImVec2(c.x - r - 1, c.y), col, 1.4f);
            dl->AddLine(ImVec2(c.x + r + 1, c.y), ImVec2(c.x + r + 3, c.y), col, 1.4f);
            break;
        }
        case tab::visuals: {
            dl->AddEllipse(c, ImVec2(r + 2, r - 1), col, 0.0f, 24, 1.4f);
            dl->AddCircleFilled(c, 1.8f, col, 12);
            break;
        }
        case tab::misc: {
            for (int i = -1; i <= 1; ++i) {
                const float y = c.y + static_cast<float>(i) * 4.0f;
                dl->AddLine(ImVec2(c.x - r, y), ImVec2(c.x + r, y), col, 1.3f);
                dl->AddCircleFilled(ImVec2(c.x + (i == 0 ? -2.0f : 2.0f), y), 1.6f, col, 8);
            }
            break;
        }
        case tab::configs: {
            const ImVec2 a(c.x - r, c.y - r + 1);
            const ImVec2 b(c.x + r, c.y + r - 1);
            dl->AddRect(a, b, col, 2.0f, 0, 1.4f);
            dl->AddLine(ImVec2(a.x + 1, a.y - 2), ImVec2(a.x + 5, a.y - 2), col, 1.4f);
            break;
        }
        case tab::settings: {
            for (int i = -1; i <= 1; ++i) {
                const float y = c.y + static_cast<float>(i) * 4.0f;
                dl->AddLine(ImVec2(c.x - r, y), ImVec2(c.x + r, y), col, 1.3f);
                dl->AddCircleFilled(ImVec2(c.x + (i == 1 ? -2.0f : 2.0f), y), 1.6f, col, 8);
            }
            break;
        }
        default: break;
    }
}

void chip_text(const char* text, ImU32 dot = 0) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pad(9.0f, 5.0f);
    const ImVec2 text_size = ImGui::CalcTextSize(text);
    const float dot_space = dot ? 12.0f : 0.0f;
    const ImVec2 size(text_size.x + pad.x * 2 + dot_space, text_size.y + pad.y * 2);
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), colors().bg_input, 5.0f);
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), colors().border_soft, 5.0f);
    if (dot) dl->AddCircleFilled(ImVec2(pos.x + pad.x + 3, pos.y + size.y * 0.5f), 3.0f, dot, 10);
    dl->AddText(ImVec2(pos.x + pad.x + dot_space, pos.y + pad.y - 1), colors().text_muted, text);
    ImGui::Dummy(size);
}

void sidebar(config::store& cfg, menu_state& state, const status& live, float height) {
    const palette& pal = colors();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    dl->AddRectFilled(origin, ImVec2(origin.x + k_sidebar_w, origin.y + height), pal.bg_sidebar,
                      9.0f, ImDrawFlags_RoundCornersLeft);

    ImGui::SetCursorPos(ImVec2(18, 16));
    ImGui::PushFont(font_title(), font_size_title());
    ImGui::PushStyleColor(ImGuiCol_Text, pal.text);
    ImGui::TextUnformatted("shogod");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::SetCursorPos(ImVec2(19, 44));
    ImGui::PushFont(font_small(), font_size_small());
    ImGui::PushStyleColor(ImGuiCol_Text, pal.text_faint);
    ImGui::Text("%s", std::string(brand::version_tag()).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    float y = 84.0f;
    for (int i = 0; i < k_tab_count; ++i) {
        const tab t = static_cast<tab>(i);
        const bool active = state.active == t;

        ImGui::SetCursorPos(ImVec2(12, y));
        ImGui::PushID(i);
        ImGui::InvisibleButton("##nav", ImVec2(k_sidebar_w - 24.0f, 38.0f));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) state.active = t;
        ImGui::PopID();

        const ImVec2 item_pos = ImGui::GetItemRectMin();
        const ImVec2 item_max = ImGui::GetItemRectMax();
        const float marker = state.tab_anim[static_cast<std::size_t>(i)];

        if (marker > 0.01f) {
            dl->AddRectFilled(item_pos, item_max, pal.bg_card_hover, 6.0f);
            dl->AddRectFilled(ImVec2(item_pos.x, item_pos.y + 8),
                              ImVec2(item_pos.x + 2.5f, item_max.y - 8), pal.accent, 2.0f);
        } else if (hovered) {
            dl->AddRectFilled(item_pos, item_max, pal.bg_card, 6.0f);
        }

        const ImU32 col = marker > 0.5f ? pal.text : (hovered ? pal.text_muted : pal.text_faint);
        icon(t, ImVec2(item_pos.x + 22.0f, item_pos.y + 19.0f), col);
        dl->AddText(ImVec2(item_pos.x + 38.0f, item_pos.y + 10.0f), col, tab_name(t));
        y += 42.0f;
    }

    // Anchors + config at the sidebar foot.
    ImGui::SetCursorPos(ImVec2(18, height - 66.0f));
    const bool all_ok = live.anchors_total > 0 && live.anchors_ok == live.anchors_total;
    const ImU32 dot = all_ok ? pal.success : (live.anchors_ok > 0 ? pal.warning : pal.error);
    dl->AddCircleFilled(ImVec2(22.0f + ImGui::GetWindowPos().x - ImGui::GetWindowPos().x,
                               ImGui::GetCursorScreenPos().y + 5.0f),
                        3.0f, dot, 10);
    ImGui::SetCursorPosX(30.0f);
    ImGui::PushFont(font_small(), font_size_small());
    ImGui::PushStyleColor(ImGuiCol_Text, pal.text_muted);
    ImGui::Text("anchors %d/%d", live.anchors_ok, live.anchors_total);
    ImGui::SetCursorPos(ImVec2(18, height - 48.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, pal.text_faint);
    ImGui::Text("cfg  %s", live.config_name.c_str());
    ImGui::PopStyleColor(2);
    ImGui::PopFont();

    (void)cfg;
}

void header(const menu_state& state, const status& live, float width) {
    const palette& pal = colors();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    ImGui::PushFont(font_title(), font_size_title());
    dl->AddText(ImVec2(origin.x + 20.0f, origin.y + 12.0f), pal.text, tab_name(state.active));
    ImGui::PopFont();
    ImGui::PushFont(font_small(), font_size_small());
    dl->AddText(ImVec2(origin.x + 21.0f, origin.y + 40.0f), pal.text_faint,
                tab_subtitle(state.active));
    ImGui::PopFont();

    // Right-hand live chips, laid out from the right edge.
    struct entry {
        std::string text;
        ImU32 dot;
    };
    entry entries[3] = {
        {"fps " + std::to_string(static_cast<int>(live.fps)), 0},
        {live.in_game ? "tick " + std::to_string(live.tick) : "menu", 0},
        {live.in_game ? std::to_string(live.players) + " players" : "idle",
         live.anchors_total > 0 && live.anchors_ok == live.anchors_total ? pal.success
                                                                        : pal.warning},
    };

    float x = width - 20.0f;
    ImGui::PushFont(font_small(), font_size_small());
    for (int i = 2; i >= 0; --i) {
        const float w = ImGui::CalcTextSize(entries[i].text.c_str()).x + 30.0f;
        x -= w;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + x, origin.y + 18.0f));
        chip_text(entries[i].text.c_str(), entries[i].dot);
        x -= 8.0f;
    }
    ImGui::PopFont();

    dl->AddLine(ImVec2(origin.x + 20.0f, origin.y + k_header_h),
                ImVec2(origin.x + width - 20.0f, origin.y + k_header_h), pal.border_soft, 1.0f);
}

}  // namespace

void draw_menu(config::store& cfg, menu_state& state, const status& live) {
    const float dt = ImGui::GetIO().DeltaTime;
    state.open_anim = approach(state.open_anim, state.open ? 1.0f : 0.0f, 18.0f, dt);
    if (!state.open && state.open_anim < 0.01f) return;

    const config::config_v1& c = cfg.get();
    apply_style(c.ui.accent, c.ui.dark_theme);

    for (int i = 0; i < k_tab_count; ++i) {
        const float target = static_cast<int>(state.active) == i ? 1.0f : 0.0f;
        state.tab_anim[static_cast<std::size_t>(i)] =
            approach(state.tab_anim[static_cast<std::size_t>(i)], target, 20.0f, dt);
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float alpha = state.open_anim;
    const ImVec2 size(k_menu_size.x, k_menu_size.y);
    const ImVec2 pos(vp->WorkPos.x + (vp->WorkSize.x - size.x) * 0.5f,
                     vp->WorkPos.y + (vp->WorkSize.y - size.y) * 0.5f + (1.0f - alpha) * 10.0f);

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
                      (pal.bg_window & 0x00FFFFFF) | (static_cast<ImU32>(242.0f * alpha) << 24),
                      9.0f);
    dl->AddRect(win, ImVec2(win.x + size.x, win.y + size.y), pal.border, 9.0f, 0, 1.0f);

    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    sidebar(cfg, state, live, size.y);
    ImGui::PopStyleColor();

    ImGui::SetCursorPos(ImVec2(k_sidebar_w, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##main", ImVec2(size.x - k_sidebar_w, size.y), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    header(state, live, size.x - k_sidebar_w);

    ImGui::SetCursorPos(ImVec2(20, k_header_h + 14));
    ImGui::BeginChild("##content", ImVec2(size.x - k_sidebar_w - 40.0f, size.y - k_header_h - 28.0f),
                      ImGuiChildFlags_None);

    const float content_alpha = state.tab_anim[static_cast<std::size_t>(state.active)];
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, content_alpha);
    ImGui::SetCursorPosX((1.0f - content_alpha) * 8.0f);

    switch (state.active) {
        case tab::rage: pages::rage(cfg, live); break;
        case tab::visuals: pages::visuals(cfg); break;
        case tab::misc: pages::misc(cfg); break;
        case tab::configs: pages::configs(cfg, state); break;
        case tab::settings: pages::settings(cfg, live); break;
        default: break;
    }
    ImGui::PopStyleVar();

    // Toast, bottom-right of the content area.
    if (state.toast_time > 0.0f) {
        state.toast_time -= dt;
        const float a = std::clamp(state.toast_time / 0.4f, 0.0f, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a);
        const ImVec2 text = ImGui::CalcTextSize(state.toast.c_str());
        const ImVec2 avail = ImGui::GetWindowSize();
        const ImVec2 p(avail.x - text.x - 46.0f, avail.y - 40.0f);
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(win.x + k_sidebar_w + p.x, win.y + k_header_h + p.y),
            ImVec2(win.x + k_sidebar_w + p.x + text.x + 24.0f, win.y + k_header_h + p.y + 28.0f),
            pal.accent_soft, 6.0f);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(win.x + k_sidebar_w + p.x + 12.0f, win.y + k_header_h + p.y + 6.0f), pal.text,
            state.toast.c_str());
        ImGui::PopStyleVar();
    }

    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
    ImGui::PopStyleVar(2);
}

}  // namespace shogod::ui
