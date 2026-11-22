#include "ui/widgets.hpp"

#include "config/keybinds.hpp"
#include "ui/style.hpp"
#include "ui/ui_icons.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace shogod::ui::widgets {
namespace {

std::unordered_map<ImGuiID, float> g_anim;

config::config_v1* g_bind_cfg = nullptr;
std::string g_bind_popup_target;
bool g_bind_popup_open_req = false;
int g_bind_capture_index = -1;
bool g_bind_capture_armed = false;

constexpr float card_pad = 14.0f;
constexpr float row_height = 26.0f;
constexpr float field_width = 148.0f;

int count_binds_for(const char* bind_id, int& first_vk) noexcept {
    first_vk = 0;
    if (!g_bind_cfg || !bind_id || bind_id[0] == '\0') return 0;
    int count = 0;
    for (const auto& kb : g_bind_cfg->keybinds) {
        if (kb.target == bind_id) {
            if (count == 0) first_vk = kb.vk;
            ++count;
        }
    }
    return count;
}

void draw_bind_badge_and_rc(const char* bind_id, ImVec2 row_min, ImVec2 row_max) {
    if (!g_bind_cfg || !bind_id || bind_id[0] == '\0') return;
    int first_vk = 0;
    const int n = count_binds_for(bind_id, first_vk);

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool over_row = mouse.x >= row_min.x && mouse.x <= row_max.x &&
                          mouse.y >= row_min.y && mouse.y <= row_max.y &&
                          ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (over_row && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        g_bind_popup_target = bind_id;
        g_bind_popup_open_req = true;
        g_bind_capture_index = -1;
    }

    if (n <= 0) return;

    char badge[24]{};
    if (n == 1) {
        const std::string k = config::vk_short_name(first_vk);
        std::snprintf(badge, sizeof badge, "%s", k.c_str());
    } else {
        std::snprintf(badge, sizeof badge, "%d binds", n);
    }

    const float badge_x = ImGui::GetItemRectMax().x + 6.0f;
    ImGui::PushFont(font_small(), 10.5f);
    const ImVec2 bsz = ImGui::CalcTextSize(badge);
    const float pw = bsz.x + 10.0f;
    const float ph = 15.0f;
    const ImVec2 p0(badge_x, row_min.y + (row_height - ph) * 0.5f);
    const ImVec2 p1(p0.x + pw, p0.y + ph);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, fade_col(colors().accent_soft), 4.0f);
    dl->AddRect(p0, p1, fade_col(colors().accent, 0.7f), 4.0f);
    dl->AddText(ImVec2(p0.x + 5.0f, p0.y + (ph - bsz.y) * 0.5f), fade_col(colors().accent), badge);
    ImGui::PopFont();
}

int poll_any_vk_press() noexcept {
    if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) return -1;
    constexpr int k_mouse_vks[] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
    for (int vk : k_mouse_vks) {
        if ((GetAsyncKeyState(vk) & 0x8000) != 0) return vk;
    }
    for (int vk = 0x08; vk <= 0xFE; ++vk) {
        if (vk == VK_ESCAPE || vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) continue;
        if ((GetAsyncKeyState(vk) & 0x8000) != 0) return vk;
    }
    return 0;
}

void align_control(float width) {
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                  ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                                      card_pad - width));
}

struct card_state {
    ImVec2 min{};
    float width = 0.0f;
    float height = 0.0f;
    ImGuiID key = 0;
};

std::unordered_map<ImGuiID, float> g_card_heights;
std::vector<card_state> g_cards;

ImU32 lerp_col(ImU32 a, ImU32 b, float t) {
    auto ch = [t](ImU32 x, ImU32 y, int shift) {
        const float a0 = static_cast<float>((x >> shift) & 0xFF);
        const float b0 = static_cast<float>((y >> shift) & 0xFF);
        return static_cast<int>(a0 + (b0 - a0) * t);
    };
    return IM_COL32(ch(a, b, IM_COL32_R_SHIFT), ch(a, b, IM_COL32_G_SHIFT),
                    ch(a, b, IM_COL32_B_SHIFT), ch(a, b, IM_COL32_A_SHIFT));
}

float anim_to(ImGuiID id, bool on, float speed = 15.0f) {
    const float target = on ? 1.0f : 0.0f;
    float& v = g_anim.try_emplace(id, target).first->second;
    v += (target - v) * std::min(1.0f, ImGui::GetIO().DeltaTime * speed);
    if (v < 0.001f) v = 0.0f;
    if (v > 0.999f) v = 1.0f;
    return v;
}

const char* key_name(int vk) {
    static char buf[32];
    if (vk == 0) return "None";
    switch (vk) {
        case VK_LBUTTON: return "Mouse1";
        case VK_RBUTTON: return "Mouse2";
        case VK_MBUTTON: return "Mouse3";
        case VK_XBUTTON1: return "Mouse4";
        case VK_XBUTTON2: return "Mouse5";
        case VK_SPACE: return "Space";
        case VK_LSHIFT: return "LShift";
        case VK_RSHIFT: return "RShift";
        case VK_LCONTROL: return "LCtrl";
        case VK_RCONTROL: return "RCtrl";
        case VK_LMENU: return "LAlt";
        case VK_RMENU: return "RAlt";
        case VK_TAB: return "Tab";
        case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete";
        case VK_HOME: return "Home";
        case VK_END: return "End";
        default: break;
    }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        buf[0] = static_cast<char>(vk);
        buf[1] = '\0';
        return buf;
    }
    std::snprintf(buf, sizeof buf, "0x%02X", vk);
    return buf;
}

void label_text(const char* label, ImU32 col = 0) {
    ImGui::AlignTextToFramePadding();
    if (col) ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextUnformatted(label);
    if (col) ImGui::PopStyleColor();
}

void draw_chevron(ImDrawList* dl, ImVec2 center, float open_t, ImU32 col) {
    // Smoothly rotates Lucide chevron-down between down (open_t = 0) and up (open_t = 1).
    const float y_off = 1.9f * (1.0f - 2.0f * open_t);
    const ImVec2 pts[3] = {
        {center.x - 3.8f, center.y - y_off},
        {center.x, center.y + y_off},
        {center.x + 3.8f, center.y - y_off},
    };
    dl->AddPolyline(pts, 3, col, ImDrawFlags_None, 1.45f);
    dl->AddCircleFilled(pts[0], 0.65f, col, 8);
    dl->AddCircleFilled(pts[2], 0.65f, col, 8);
}

void draw_checkmark(ImDrawList* dl, ImVec2 center, ImU32 col) {
    draw_ui_icon(*dl, ui_icon_id::check, center, 13.0f, col, 1.55f);
}

bool draw_toggle(const char* label, bool* value, const char* tooltip, const char* bind_id) {
    ImGui::PushID(label);
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    const float row_w = ImGui::GetContentRegionAvail().x;
    label_text(label);
    draw_bind_badge_and_rc(bind_id, row_min, ImVec2(row_min.x + row_w, row_min.y + row_height));
    align_control(34.0f);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(34.0f, row_height);
    const bool pressed = ImGui::InvisibleButton("##toggle", size, ImGuiButtonFlags_EnableNav);
    if (pressed) *value = !*value;
    if (tooltip) tip(tooltip);

    const float t = anim_to(ImGui::GetID("anim"), *value);
    const float h = anim_to(ImGui::GetID("hov"), ImGui::IsItemHovered(), 20.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pill_a(pos.x, pos.y + 4.0f);
    const ImVec2 pill_b(pos.x + size.x, pos.y + 22.0f);
    const ImU32 off_fill = lerp_col(colors().bg_input, colors().bg_card_hover, h * 0.6f);
    const ImU32 track = fade_col(lerp_col(off_fill, colors().accent, t));
    dl->AddRectFilled(pill_a, pill_b, track, 9.0f);
    dl->AddRect(pill_a, pill_b,
                fade_col(lerp_col(lerp_col(colors().border_soft, colors().border, h),
                                  colors().accent, t)),
                9.0f, 0, 1.0f);

    if (ImGui::GetIO().NavVisible && ImGui::IsItemFocused()) {
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), fade_col(colors().accent), 6.0f);
    }

    const float knob_x = (pos.x + 9.0f) + (pos.x + size.x - 9.0f - (pos.x + 9.0f)) * t;
    dl->AddCircleFilled(
        ImVec2(knob_x, pos.y + row_height * 0.5f), 6.0f,
        fade_col(lerp_col(colors().text_faint, IM_COL32(255, 255, 255, 255), t)), 24);
    ImGui::PopID();
    return pressed;
}

bool draw_slider(const char* label, float* value, float lo, float hi, const char* tooltip,
                 bool integer, bool hp_over_100 = false, const char* bind_id = nullptr) {
    ImGui::PushID(label);
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    const float row_w = ImGui::GetContentRegionAvail().x;
    label_text(label);
    draw_bind_badge_and_rc(bind_id, row_min, ImVec2(row_min.x + row_w, row_min.y + row_height));
    align_control(field_width);

    if (integer) {
        const float rounded = std::clamp(std::floor(*value + 0.5f), lo, hi);
        if (std::fabs(*value - rounded) > 1e-4f) *value = rounded;
    }

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(field_width, row_height);
    const float pill_w = 54.0f;
    const float track_start = pos.x + 4.0f;
    const float track_end = pos.x + size.x - pill_w - 10.0f;

    ImGui::InvisibleButton("##slider", size);
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    bool changed = false;

    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float span = std::max(1.0f, track_end - track_start);
        const float rel = std::clamp((ImGui::GetIO().MousePos.x - track_start) / span, 0.0f, 1.0f);
        float next = lo + rel * (hi - lo);
        if (integer) next = std::floor(next + 0.5f);
        next = std::clamp(next, lo, hi);
        if (next != *value) {
            *value = next;
            changed = true;
        }
    }

    const float t = std::clamp((*value - lo) / (hi - lo), 0.0f, 1.0f);
    const float focus_t = anim_to(ImGui::GetID("focus"), active || hovered, 20.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float center = pos.y + size.y * 0.5f;
    const float thumb_x = track_start + (track_end - track_start) * t;
    dl->AddRectFilled(ImVec2(track_start, center - 1.5f), ImVec2(track_end, center + 1.5f),
                      fade_col(colors().border), 2.0f);
    if (thumb_x > track_start + 1.0f) {
        dl->AddRectFilled(ImVec2(track_start, center - 1.5f), ImVec2(thumb_x, center + 1.5f),
                          fade_col(colors().accent), 2.0f);
    }
    dl->AddCircleFilled(ImVec2(thumb_x, center), 5.0f + 0.8f * focus_t,
                        fade_col(IM_COL32(255, 255, 255, 255)), 20);
    dl->AddCircle(ImVec2(thumb_x, center), 5.0f + 0.8f * focus_t, fade_col(colors().accent), 20,
                  1.5f);

    // Compact numeric readout pill on the right.
    const ImVec2 pill_a(pos.x + size.x - pill_w, pos.y + 3.0f);
    const ImVec2 pill_b(pos.x + size.x, pos.y + size.y - 3.0f);
    dl->AddRectFilled(pill_a, pill_b, fade_col(colors().bg_input), 5.0f);
    dl->AddRect(pill_a, pill_b,
                fade_col(lerp_col(colors().border_soft, colors().accent, active ? 1.0f : focus_t * 0.6f)),
                5.0f);

    char buf[64];
    if (hp_over_100) {
        const int iv = std::clamp(static_cast<int>(std::floor(*value + 0.5f)), 1, 120);
        if (iv > 100) std::snprintf(buf, sizeof buf, "HP+%d", iv - 100);
        else std::snprintf(buf, sizeof buf, "%d", iv);
    } else if (integer) {
        std::snprintf(buf, sizeof buf, "%d", static_cast<int>(std::floor(*value + 0.5f)));
    } else {
        std::snprintf(buf, sizeof buf, "%.2f", *value);
    }
    const ImVec2 text_size = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(pill_a.x + (pill_w - text_size.x) * 0.5f,
                       pill_a.y + ((pill_b.y - pill_a.y) - text_size.y) * 0.5f),
                fade_col(colors().text), buf);

    if (tooltip) tip(tooltip);
    ImGui::PopID();
    return changed;
}

bool select_trigger(const char* label, const char* preview, bool muted_preview, const char* tooltip,
                    ImVec2* out_pos, float* out_w, float* out_h, const char* bind_id = nullptr) {
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    const float row_w = ImGui::GetContentRegionAvail().x;
    label_text(label);
    if (tooltip) tip(tooltip);
    draw_bind_badge_and_rc(bind_id, row_min, ImVec2(row_min.x + row_w, row_min.y + row_height));
    align_control(field_width);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = 24.0f;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
    const ImVec2 btn_pos = ImGui::GetCursorScreenPos();
    const bool clicked =
        ImGui::InvisibleButton("##trigger", ImVec2(field_width, h), ImGuiButtonFlags_EnableNav);
    const bool hovered = ImGui::IsItemHovered();
    const bool open = ImGui::IsPopupOpen("##nl_popup");

    const float hov_t = anim_to(ImGui::GetID("trig_hov"), hovered || open, 18.0f);
    const float open_t = anim_to(ImGui::GetID("trig_open"), open, 18.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 rect_max(btn_pos.x + field_width, btn_pos.y + h);
    dl->AddRectFilled(btn_pos, rect_max,
                      fade_col(lerp_col(colors().bg_input, colors().bg_card_hover, hov_t * 0.8f)),
                      6.0f);
    dl->AddRect(btn_pos, rect_max,
                fade_col(lerp_col(colors().border_soft,
                                  open ? colors().accent : colors().border, hov_t)),
                6.0f);

    const ImVec2 clip_min(btn_pos.x + 8.0f, btn_pos.y);
    const ImVec2 clip_max(rect_max.x - 22.0f, rect_max.y);
    dl->PushClipRect(clip_min, clip_max, true);
    const ImU32 text_col = fade_col(muted_preview ? colors().text_muted : colors().text);
    dl->AddText(ImVec2(btn_pos.x + 9.0f, btn_pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                text_col, preview);
    dl->PopClipRect();

    draw_chevron(dl, ImVec2(rect_max.x - 12.0f, btn_pos.y + h * 0.5f), open_t,
                 fade_col(lerp_col(colors().text_faint, colors().text, open_t)));

    *out_pos = btn_pos;
    *out_w = field_width;
    *out_h = h;
    if (clicked) ImGui::OpenPopup("##nl_popup");
    return true;
}

bool begin_nl_popup(ImVec2 trigger_pos, float trigger_w, float trigger_h) {
    const float popup_w = std::max(168.0f, trigger_w);
    ImGui::SetNextWindowPos(ImVec2(trigger_pos.x - (popup_w - trigger_w), trigger_pos.y + trigger_h + 4.0f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(popup_w, 0.0f), ImVec2(240.0f, 320.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);

    if (!ImGui::BeginPopup("##nl_popup", ImGuiWindowFlags_NoMove)) {
        ImGui::PopStyleVar(4);
        return false;
    }

    // Subtle Neverlose inner top bevel on the floating popup card.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wpos = ImGui::GetWindowPos();
    const ImVec2 wsize = ImGui::GetWindowSize();
    dl->AddLine(ImVec2(wpos.x + 8.0f, wpos.y + 1.0f),
                ImVec2(wpos.x + wsize.x - 8.0f, wpos.y + 1.0f),
                fade_col(IM_COL32(255, 255, 255, 18)), 1.0f);
    return true;
}

void end_nl_popup() {
    ImGui::EndPopup();
    ImGui::PopStyleVar(4);
}

bool popup_option_row(const char* name, bool selected, bool multi, float left_pad = 0.0f,
                      ImVec2* out_row_pos = nullptr) {
    ImGui::PushID(name);
    const float w = std::max(156.0f, ImGui::GetContentRegionAvail().x);
    const float h = 28.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    if (out_row_pos) *out_row_pos = pos;
    const bool clicked = ImGui::InvisibleButton("##opt", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const float sel_t = anim_to(ImGui::GetID("sel"), selected, 18.0f);
    const float hov_t = anim_to(ImGui::GetID("hov"), hovered, 22.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + w, pos.y + h);
    if (sel_t > 0.01f || hov_t > 0.01f) {
        const ImU32 sel_fill = lerp_col(colors().bg_card_hover, colors().accent_soft, 0.72f);
        const ImU32 fill = lerp_col(colors().bg_card_hover, sel_fill, sel_t);
        const float alpha_mul = std::max(hov_t * 0.85f, sel_t);
        dl->AddRectFilled(pos, max, fade_col(fill, alpha_mul), 6.0f);
        if (sel_t > 0.05f) {
            dl->AddRect(pos, max, fade_col(colors().accent, 0.28f * sel_t), 6.0f, 0, 1.0f);
        }
    }

    if (selected) {
        // Signature vertical accent indicator bar on the left inside active option pill.
        dl->AddRectFilled(ImVec2(pos.x + 2.0f, pos.y + 6.0f), ImVec2(pos.x + 4.5f, pos.y + h - 6.0f),
                          fade_col(colors().accent, sel_t), 2.0f);
    }

    const ImU32 text_col = fade_col(lerp_col(colors().text_muted, colors().text, std::max(sel_t, hov_t)));
    dl->AddText(ImVec2(pos.x + 11.0f + left_pad, pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                text_col, name);

    if (multi && sel_t > 0.05f) {
        draw_checkmark(dl, ImVec2(max.x - 14.0f, pos.y + h * 0.5f), fade_col(colors().accent, sel_t));
    }

    ImGui::PopID();
    return clicked;
}

}  // namespace

void section(const char* title) {
    char upper[64]{};
    std::size_t i = 0;
    for (; title[i] != '\0' && i + 1 < sizeof upper; ++i) {
        const unsigned char ch = static_cast<unsigned char>(title[i]);
        upper[i] = (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - 'a' + 'A') : static_cast<char>(ch);
    }
    upper[i] = '\0';

    ImGui::PushFont(font_small(), 11.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 2.0f, p.y), fade_col(colors().text_faint),
                                        upper);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 10.0f));
}

void tip(const char* text) {
    if (ImGui::IsItemHovered()) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(9, 6));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0f);
        ImGui::BeginTooltip();
        ImGui::PushStyleColor(ImGuiCol_Text, colors().text);
        ImGui::PushTextWrapPos(280.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::EndTooltip();
        ImGui::PopStyleVar(2);
    }
}

bool toggle(const char* label, bool* value, const char* tooltip, const char* bind_id) {
    return draw_toggle(label, value, tooltip, bind_id);
}

bool slider_float(const char* label, float* value, float lo, float hi, const char* tooltip,
                  const char* bind_id) {
    return draw_slider(label, value, lo, hi, tooltip, false, false, bind_id);
}

bool slider_float_step(const char* label, float* value, float lo, float hi, const char* tooltip,
                       const char* bind_id) {
    return draw_slider(label, value, lo, hi, tooltip, true, false, bind_id);
}

bool slider_min_damage(const char* label, float* value, const char* tooltip, const char* bind_id) {
    return draw_slider(label, value, 1.0f, 120.0f, tooltip, true, true, bind_id);
}

bool slider_int(const char* label, int* value, int lo, int hi, const char* tooltip,
                const char* bind_id) {
    float v = static_cast<float>(*value);
    const bool changed = draw_slider(label, &v, static_cast<float>(lo), static_cast<float>(hi),
                                     tooltip, true, false, bind_id);
    *value = static_cast<int>(v);
    return changed;
}

bool combo(const char* label, int* value, const char* const* items, int count,
           const char* tooltip, combo_icon draw_icon, const char* bind_id) {
    ImGui::PushID(label);
    const std::string preview =
        draw_icon ? std::string("       ") + items[*value] : items[*value];
    ImVec2 trig_pos{};
    float trig_w = 0.0f;
    float trig_h = 0.0f;
    select_trigger(label, preview.c_str(), false, tooltip, &trig_pos, &trig_w, &trig_h, bind_id);
    if (draw_icon) {
        draw_icon(*ImGui::GetWindowDrawList(),
                  ImVec2(trig_pos.x + 17.0f, trig_pos.y + trig_h * 0.5f), *value,
                  fade_col(colors().text_muted));
    }

    bool changed = false;
    if (begin_nl_popup(trig_pos, trig_w, trig_h)) {
        for (int i = 0; i < count; ++i) {
            const bool selected = (i == *value);
            ImVec2 row_pos{};
            if (popup_option_row(items[i], selected, false, draw_icon ? 20.0f : 0.0f, &row_pos)) {
                if (*value != i) {
                    *value = i;
                    changed = true;
                }
                ImGui::CloseCurrentPopup();
            }
            if (draw_icon) {
                draw_icon(*ImGui::GetWindowDrawList(), ImVec2(row_pos.x + 19.0f, row_pos.y + 14.0f),
                          i, fade_col(selected ? colors().accent : colors().text_muted));
            }
        }
        end_nl_popup();
    }
    ImGui::PopID();
    return changed;
}

void multi_select(const char* label, std::initializer_list<list_option> options) {
    int selected = 0;
    const char* single = "";
    for (const auto& option : options) {
        if (*option.value) {
            ++selected;
            single = option.name;
        }
    }
    const std::string summary = selected == 0 ? "None"
                                : selected == 1 ? single
                                                : std::to_string(selected) + " selected";
    ImGui::PushID(label);
    ImVec2 trig_pos{};
    float trig_w = 0.0f;
    float trig_h = 0.0f;
    select_trigger(label, summary.c_str(), selected == 0, nullptr, &trig_pos, &trig_w, &trig_h);

    if (begin_nl_popup(trig_pos, trig_w, trig_h)) {
        for (const auto& option : options) {
            if (popup_option_row(option.name, *option.value, true)) {
                *option.value = !*option.value;
            }
            if (option.tooltip) tip(option.tooltip);
        }
        end_nl_popup();
    }
    ImGui::PopID();
}

bool multi_bits(const char* label, std::uint32_t* bits, const char* const* names,
                const std::uint32_t* masks, int count, const char* tooltip) {
    int selected = 0;
    const char* single = "";
    for (int i = 0; i < count; ++i) {
        if (*bits & masks[i]) {
            ++selected;
            single = names[i];
        }
    }
    const std::string summary = selected == 0 ? "None"
                                : selected == 1 ? single
                                                : std::to_string(selected) + " selected";
    bool changed = false;
    ImGui::PushID(label);
    ImVec2 trig_pos{};
    float trig_w = 0.0f;
    float trig_h = 0.0f;
    select_trigger(label, summary.c_str(), selected == 0, tooltip, &trig_pos, &trig_w, &trig_h);

    if (begin_nl_popup(trig_pos, trig_w, trig_h)) {
        for (int i = 0; i < count; ++i) {
            const bool active = (*bits & masks[i]) != 0;
            if (popup_option_row(names[i], active, true)) {
                *bits ^= masks[i];
                changed = true;
            }
        }
        end_nl_popup();
    }
    ImGui::PopID();
    return changed;
}

bool color_row(const char* label, config::rgba* color, const char* tooltip) {
    ImGui::PushID(label);
    label_text(label);
    const ImVec2 swatch(30.0f, 16.0f);
    align_control(swatch.x);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (row_height - swatch.y) * 0.5f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##swatch", swatch);
    const bool open = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + swatch.x, pos.y + swatch.y), fade_col(colors().bg_input),
                      4.0f);
    dl->AddRectFilled(pos, ImVec2(pos.x + swatch.x, pos.y + swatch.y), fade_col(to_u32(*color)),
                      4.0f);
    dl->AddRect(pos, ImVec2(pos.x + swatch.x, pos.y + swatch.y),
                fade_col(hovered ? colors().accent : colors().border), 4.0f);

    if (tooltip) tip(tooltip);

    bool changed = false;
    if (open) ImGui::OpenPopup("##picker");
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
    if (ImGui::BeginPopup("##picker")) {
        ImGui::SetNextItemWidth(220.0f);
        ImVec4 v = to_vec4(*color);
        if (ImGui::ColorPicker4("##color", &v.x,
                                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoSidePreview |
                                    ImGuiColorEditFlags_PickerHueBar)) {
            *color = from_vec4(v);
            changed = true;
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return changed;
}

bool keybind_row(const char* label, config::keybind* kb, const char* tooltip, bool keyboard_only) {
    static ImGuiID capturing = 0;
    static bool wait_release = false;
    static int last_frame = -1;
    if (ImGui::GetFrameCount() > last_frame + 1) capturing = 0;
    bool changed = false;

    ImGui::PushID(label);
    label_text(label);
    align_control(field_width);

    const ImGuiID id = ImGui::GetID("##key");
    const bool is_capturing = capturing == id;
    if (is_capturing) last_frame = ImGui::GetFrameCount();
    const float h = 24.0f;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##key_btn", ImVec2(field_width, h));
    const bool hovered = ImGui::IsItemHovered();

    if (pressed) {
        capturing = is_capturing ? 0 : id;
        last_frame = ImGui::GetFrameCount();
        wait_release = true;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        kb->vk = 0;
        changed = true;
        capturing = 0;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + field_width, pos.y + h);
    dl->AddRectFilled(
        pos, max,
        fade_col(is_capturing ? colors().accent_soft
                              : (hovered ? colors().bg_card_hover : colors().bg_input)),
        6.0f);
    dl->AddRect(pos, max,
                fade_col(is_capturing ? colors().accent
                                      : (hovered ? colors().border : colors().border_soft)),
                6.0f);

    const char* caption = is_capturing ? "Press any key..." : key_name(kb->vk);
    const ImVec2 tsz = ImGui::CalcTextSize(caption);
    dl->AddText(ImVec2(pos.x + (field_width - tsz.x) * 0.5f, pos.y + (h - tsz.y) * 0.5f),
                fade_col(is_capturing ? colors().accent : colors().text), caption);

    if (is_capturing) {
        DWORD owner = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &owner);
        if (owner != GetCurrentProcessId() || (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
            capturing = 0;
        } else if (capturing == id) {
            int down = 0;
            for (int vk = 1; vk < 255; ++vk) {
                if (keyboard_only && vk <= VK_XBUTTON2) continue;
                if (vk == VK_ESCAPE || vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
                    (vk >= VK_LSHIFT && vk <= VK_RMENU)) continue;
                if ((GetAsyncKeyState(vk) & 0x8000) != 0) {
                    down = vk;
                    break;
                }
            }
            if (down == 0) wait_release = false;
            else if (!wait_release) {
                kb->vk = down;
                capturing = 0;
                changed = true;
            }
        }
    }

    if (tooltip) tip(tooltip);
    ImGui::PopID();
    return changed;
}

bool action_button(const char* label, const ImVec2& size) {
    const float w = size.x > 0.0f ? size.x : std::max(84.0f, ImGui::CalcTextSize(label).x + 24.0f);
    const float h = size.y > 0.0f ? size.y : 28.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + w, pos.y + h);
    const ImU32 fill = active ? colors().accent
                              : (hovered ? colors().accent_soft : colors().bg_card_hover);
    dl->AddRectFilled(pos, max, fade_col(fill), 6.0f);
    dl->AddRect(pos, max, fade_col(hovered || active ? colors().accent : colors().border), 6.0f);
    const ImVec2 tsz = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(pos.x + (w - tsz.x) * 0.5f, pos.y + (h - tsz.y) * 0.5f),
                fade_col(colors().text), label);
    return pressed;
}

bool chip(const char* label, bool selected) {
    const float pad = 10.0f;
    const ImVec2 text = ImGui::CalcTextSize(label);
    const ImVec2 size(text.x + pad * 2.0f, text.y + 10.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(label);
    ImGui::InvisibleButton("##chip", size);
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 bg =
        fade_col(selected ? colors().accent_soft
                          : (hovered ? colors().bg_card_hover : colors().bg_input));
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg, 6.0f);
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                fade_col(selected ? colors().accent : colors().border_soft), 6.0f);
    dl->AddText(ImVec2(pos.x + pad, pos.y + 5.0f),
                fade_col(selected ? colors().text : colors().text_muted), label);
    return clicked;
}

bool begin_card(const char* id, const ImVec2& size, bool border) {
    (void)size;
    ImGui::PushID(id);
    g_cards.emplace_back();
    card_state& card = g_cards.back();
    card.width = ImGui::GetContentRegionAvail().x;
    card.min = ImGui::GetCursorScreenPos();
    card.key = ImGui::GetID("##card_height");
    card.height = g_card_heights[card.key];

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a(card.min.x, card.min.y);
    const ImVec2 b(card.min.x + card.width, card.min.y + card.height);
    dl->AddRectFilled(a, b, fade_col(colors().bg_card), 10.0f);
    if (border) {
        dl->AddRect(a, b, fade_col(colors().border_soft), 10.0f, 0, 1.0f);
        // Crisp 1px Neverlose top bevel highlight.
        if (card.width > 20.0f) {
            dl->AddLine(ImVec2(a.x + 10.0f, a.y + 1.0f), ImVec2(b.x - 10.0f, a.y + 1.0f),
                        fade_col(IM_COL32(255, 255, 255, 10)), 1.0f);
        }
    }

    ImGui::Indent(card_pad);
    ImGui::Dummy(ImVec2(0, 8.0f));
    return true;
}

void end_card() {
    ImGui::Dummy(ImVec2(0, 8.0f));
    ImGui::Unindent(card_pad);

    card_state card = std::move(g_cards.back());
    g_cards.pop_back();

    const ImVec2 max = ImGui::GetItemRectMax();
    g_card_heights[card.key] = max.y - card.min.y + 2.0f;

    ImGui::PopID();
    ImGui::Dummy(ImVec2(0, 6.0f));
}

void set_active_bind_store(config::config_v1* cfg) noexcept { g_bind_cfg = cfg; }

void open_keybind_popup(const char* bind_id) {
    if (!bind_id || bind_id[0] == '\0') return;
    g_bind_popup_target = bind_id;
    g_bind_popup_open_req = true;
    g_bind_capture_index = -1;
}

void render_keybind_popup() {
    if (!g_bind_cfg) return;
    if (g_bind_popup_open_req) {
        ImGui::OpenPopup("##nl_bind_popup");
        g_bind_popup_open_req = false;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, fade_col(colors().bg_window));
    ImGui::PushStyleColor(ImGuiCol_Border, fade_col(colors().border));
    ImGui::SetNextWindowSizeConstraints(ImVec2(276.0f, 0.0f), ImVec2(320.0f, 420.0f));

    if (ImGui::BeginPopup("##nl_bind_popup")) {
        const config::bind_target_spec* spec = config::find_bind_target(g_bind_popup_target);
        if (!spec) {
            ImGui::TextDisabled("Target unavailable");
            ImGui::EndPopup();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
            return;
        }

        ImGui::PushFont(font_body(), font_size_body());
        ImGui::TextUnformatted(spec->label);
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::PushFont(font_small(), font_size_small());
        ImGui::TextDisabled("(%s)", spec->category);
        ImGui::PopFont();
        ImGui::Separator();

        int remove_idx = -1;
        int match_count = 0;
        const char* k_modes[] = {"Toggle", "Hold", "Always"};

        for (std::size_t i = 0; i < g_bind_cfg->keybinds.size(); ++i) {
            auto& kb = g_bind_cfg->keybinds[i];
            if (kb.target != g_bind_popup_target) continue;
            ++match_count;
            ImGui::PushID(static_cast<int>(i));

            const bool capturing = (g_bind_capture_index == static_cast<int>(i));
            if (capturing) {
                const int vk = poll_any_vk_press();
                if (!g_bind_capture_armed) {
                    if (vk == 0) g_bind_capture_armed = true;
                } else if (vk == -1) {
                    kb.vk = 0;
                    g_bind_capture_index = -1;
                } else if (vk > 0) {
                    if (vk != g_bind_cfg->ui.menu_key) kb.vk = vk;
                    g_bind_capture_index = -1;
                }
            }

            const std::string key_lbl =
                capturing ? "Press a key..." : ("Key: " + config::vk_name(kb.vk));
            if (ImGui::Button(key_lbl.c_str(), ImVec2(142.0f, 24.0f))) {
                g_bind_capture_index = static_cast<int>(i);
                g_bind_capture_armed = false;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(82.0f);
            int mode_i = static_cast<int>(kb.mode);
            if (ImGui::Combo("##mode", &mode_i, k_modes, 3)) {
                kb.mode = static_cast<config::bind_mode>(mode_i);
            }
            ImGui::SameLine();
            if (ImGui::Button("X", ImVec2(24.0f, 24.0f))) {
                remove_idx = static_cast<int>(i);
            }

            // Override value control matching target type
            switch (spec->kind) {
                case config::bind_value_kind::boolean: {
                    bool ov_on = kb.override_value >= 0.5f;
                    if (ImGui::Checkbox("Override -> Enable", &ov_on)) {
                        kb.override_value = ov_on ? 1.0f : 0.0f;
                    }
                    break;
                }
                case config::bind_value_kind::min_damage: {
                    int iv = std::clamp(static_cast<int>(std::lround(kb.override_value)), 1, 120);
                    char fmt[24]{};
                    if (iv > 100) std::snprintf(fmt, sizeof fmt, "HP+%d", iv - 100);
                    else std::snprintf(fmt, sizeof fmt, "%d dmg", iv);
                    ImGui::SetNextItemWidth(258.0f);
                    if (ImGui::SliderInt("##ov_dmg", &iv, 1, 120, fmt)) {
                        kb.override_value = static_cast<float>(iv);
                    }
                    break;
                }
                case config::bind_value_kind::integer: {
                    int iv = std::clamp(static_cast<int>(std::lround(kb.override_value)),
                                        static_cast<int>(spec->min_val),
                                        static_cast<int>(spec->max_val));
                    ImGui::SetNextItemWidth(258.0f);
                    if (ImGui::SliderInt("##ov_int", &iv, static_cast<int>(spec->min_val),
                                         static_cast<int>(spec->max_val))) {
                        kb.override_value = static_cast<float>(iv);
                    }
                    break;
                }
                case config::bind_value_kind::choice: {
                    int cv = std::clamp(static_cast<int>(std::lround(kb.override_value)), 0,
                                        std::max(0, spec->choice_count - 1));
                    ImGui::SetNextItemWidth(258.0f);
                    if (spec->choices && ImGui::Combo("##ov_ch", &cv, spec->choices, spec->choice_count)) {
                        kb.override_value = static_cast<float>(cv);
                    }
                    break;
                }
                case config::bind_value_kind::floating:
                default: {
                    ImGui::SetNextItemWidth(258.0f);
                    ImGui::SliderFloat("##ov_fl", &kb.override_value, spec->min_val, spec->max_val,
                                       "%.0f");
                    break;
                }
            }

            ImGui::Checkbox("Show on Keybinds", &kb.show_in_binds);
            ImGui::Separator();
            ImGui::PopID();
        }

        if (remove_idx >= 0) {
            g_bind_cfg->keybinds.erase(g_bind_cfg->keybinds.begin() + remove_idx);
            g_bind_capture_index = -1;
        }
        if (match_count == 0) {
            ImGui::TextDisabled("No keybinds attached yet.");
        }

        if (g_bind_cfg->keybinds.size() < config::k_max_menu_keybinds &&
            ImGui::Button("+ Add bind", ImVec2(258.0f, 26.0f))) {
            config::menu_keybind fresh{};
            fresh.target = spec->id;
            fresh.vk = 0;
            fresh.mode = config::bind_mode::toggle;
            fresh.show_in_binds = true;
            fresh.override_value = spec->default_override;
            g_bind_cfg->keybinds.push_back(std::move(fresh));
            g_bind_capture_index = static_cast<int>(g_bind_cfg->keybinds.size()) - 1;
            g_bind_capture_armed = false;
        }

        ImGui::EndPopup();
    } else {
        g_bind_capture_index = -1;
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

}  // namespace shogod::ui::widgets
