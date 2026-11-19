#include "ui/widgets.hpp"

#include "ui/style.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace shogod::ui::widgets {
namespace {

std::unordered_map<ImGuiID, float> g_anim;

constexpr float card_pad = 12.0f;

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

float anim_to(ImGuiID id, bool on, float speed = 14.0f) {
    float& v = g_anim[id];
    const float target = on ? 1.0f : 0.0f;
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
    if (vk >= 'A' && vk <= 'Z') {
        buf[0] = static_cast<char>(vk);
        buf[1] = '\0';
        return buf;
    }
    if (vk >= '0' && vk <= '9') {
        buf[0] = static_cast<char>(vk);
        buf[1] = '\0';
        return buf;
    }
    std::snprintf(buf, sizeof buf, "0x%02X", vk);
    return buf;
}

void label_text(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
}

ImVec2 control_size(float width) { return ImVec2(width, 22.0f); }

bool draw_toggle(const char* label, bool* value, const char* tooltip) {
    ImGui::PushID(label);
    const float width = ImGui::GetContentRegionAvail().x;
    label_text(label);
    ImGui::SameLine(width - 36.0f);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(36.0f, 20.0f);
    ImGui::InvisibleButton("##toggle", size);
    const bool changed = ImGui::IsItemClicked() ? (*value = !*value, true) : false;
    if (tooltip && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushStyleColor(ImGuiCol_Text, colors().text);
        ImGui::TextUnformatted(tooltip);
        ImGui::PopStyleColor();
        ImGui::EndTooltip();
    }

    const float t = anim_to(ImGui::GetID("anim"), *value);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 track = lerp_col(colors().bg_input, colors().accent, t);
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), track, 10.0f);
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), colors().border_soft, 10.0f);

    const float knob_x = (pos.x + 3.0f) + ((pos.x + size.x - 17.0f) - (pos.x + 3.0f)) * t;
    dl->AddCircleFilled(ImVec2(knob_x + 7.0f, pos.y + 10.0f), 7.0f,
                        lerp_col(colors().text_faint, IM_COL32(255, 255, 255, 255), t), 24);
    ImGui::PopID();
    return changed;
}

bool draw_slider(const char* label, float* value, float lo, float hi, const char* tooltip,
                 bool integer) {
    ImGui::PushID(label);
    const float width = ImGui::GetContentRegionAvail().x;
    const float control_w = 150.0f;
    label_text(label);
    ImGui::SameLine(width - control_w);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(control_w, 22.0f);
    ImGui::InvisibleButton("##slider", size);
    const bool active = ImGui::IsItemActive();
    bool changed = false;

    const float t = std::clamp((*value - lo) / (hi - lo), 0.0f, 1.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), colors().bg_input, 5.0f);
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x * t, pos.y + size.y), colors().accent_soft, 5.0f);

    char buf[64];
    if (integer) {
        std::snprintf(buf, sizeof buf, "%d", static_cast<int>(*value));
    } else {
        std::snprintf(buf, sizeof buf, "%.2f", *value);
    }
    const ImVec2 text_size = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(pos.x + size.x - text_size.x - 8.0f, pos.y + (size.y - text_size.y) * 0.5f),
                colors().text, buf);

    if (active) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        if (delta.x != 0.0f) {
            float next = *value + delta.x * (hi - lo) / size.x;
            if (integer) next = std::floor(next + 0.5f);
            next = std::clamp(next, lo, hi);
            if (next != *value) {
                *value = next;
                changed = true;
            }
        }
    }

    if (tooltip && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tooltip);
        ImGui::EndTooltip();
    }
    ImGui::PopID();
    return changed;
}

}  // namespace

void section(const char* title) {
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PushFont(font_small(), font_size_small());
    ImGui::PushStyleColor(ImGuiCol_Text, colors().text_faint);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 2));
}

void tip(const char* text) {
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushStyleColor(ImGuiCol_Text, colors().text);
        ImGui::PushTextWrapPos(280.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::EndTooltip();
    }
}

bool toggle(const char* label, bool* value, const char* tooltip) {
    return draw_toggle(label, value, tooltip);
}

bool slider_float(const char* label, float* value, float lo, float hi, const char* tooltip) {
    return draw_slider(label, value, lo, hi, tooltip, false);
}

bool slider_int(const char* label, int* value, int lo, int hi, const char* tooltip) {
    float v = static_cast<float>(*value);
    const bool changed = draw_slider(label, &v, static_cast<float>(lo), static_cast<float>(hi),
                                     tooltip, true);
    *value = static_cast<int>(v);
    return changed;
}

bool combo(const char* label, int* value, const char* const* items, int count,
           const char* tooltip) {
    ImGui::PushID(label);
    const float width = ImGui::GetContentRegionAvail().x;
    const float control_w = 150.0f;
    label_text(label);
    ImGui::SameLine(width - control_w);
    ImGui::SetNextItemWidth(control_w);

    bool changed = false;
    if (ImGui::BeginCombo("##combo", items[*value])) {
        for (int i = 0; i < count; ++i) {
            const bool selected = i == *value;
            if (ImGui::Selectable(items[i], selected)) {
                if (*value != i) {
                    *value = i;
                    changed = true;
                }
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (tooltip) tip(tooltip);
    ImGui::PopID();
    return changed;
}

bool multi_bits(const char* label, std::uint32_t* bits, const char* const* names,
                const std::uint32_t* masks, int count, const char* tooltip) {
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (tooltip) tip(tooltip);
    ImGui::Spacing();

    bool changed = false;
    const float avail = ImGui::GetContentRegionAvail().x;
    float x = 0.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 6));
    for (int i = 0; i < count; ++i) {
        const float w = ImGui::CalcTextSize(names[i]).x + 18.0f;
        if (x > 0.0f && x + w > avail) {
            ImGui::NewLine();
            x = 0.0f;
        } else if (x > 0.0f) {
            ImGui::SameLine();
        }
        x += w;

        const bool on = (*bits & masks[i]) != 0;
        if (chip(names[i], on)) {
            *bits ^= masks[i];
            changed = true;
        }
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

bool color_row(const char* label, config::rgba* color, const char* tooltip) {
    ImGui::PushID(label);
    const float width = ImGui::GetContentRegionAvail().x;
    label_text(label);
    ImGui::SameLine(width - 46.0f);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(46.0f, 22.0f);
    ImGui::InvisibleButton("##swatch", size);
    const bool open = ImGui::IsItemClicked();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), to_u32(*color), 5.0f);
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), colors().border, 5.0f);

    if (tooltip) tip(tooltip);

    bool changed = false;
    if (open) ImGui::OpenPopup("##picker");
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
    ImGui::PopID();
    return changed;
}

bool keybind_row(const char* label, config::keybind* kb, const char* tooltip) {
    static int capturing = 0;  // widget instance id currently listening

    ImGui::PushID(label);
    const float width = ImGui::GetContentRegionAvail().x;
    const float control_w = 150.0f;
    label_text(label);
    ImGui::SameLine(width - control_w);

    const ImGuiID id = ImGui::GetID("##key");
    const bool is_capturing = capturing == static_cast<int>(id);

    ImGui::PushStyleColor(ImGuiCol_Text, is_capturing ? colors().accent : colors().text);
    if (ImGui::Button(is_capturing ? "press a key..." : key_name(kb->vk), ImVec2(control_w, 0))) {
        capturing = is_capturing ? 0 : static_cast<int>(id);
    }
    ImGui::PopStyleColor();

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        kb->vk = 0;
        capturing = 0;
    }

    if (is_capturing) {
        for (int vk = 1; vk < 256; ++vk) {
            if (vk == VK_ESCAPE) continue;
            if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(vk), false)) {
                kb->vk = vk;
                capturing = 0;
                break;
            }
        }
        for (int mb = 0; mb < 5; ++mb) {
            if (ImGui::IsMouseClicked(mb)) {
                kb->vk = VK_LBUTTON + mb;
                capturing = 0;
                break;
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) capturing = 0;
    }

    if (tooltip) tip(tooltip);
    ImGui::PopID();
    return true;
}

bool action_button(const char* label, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_Button, colors().accent_soft);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors().accent);
    ImGui::PushStyleColor(ImGuiCol_Text, colors().text);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

bool chip(const char* label, bool selected) {
    const float pad = 9.0f;
    const ImVec2 text = ImGui::CalcTextSize(label);
    const ImVec2 size(text.x + pad * 2.0f, text.y + 10.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(label);
    ImGui::InvisibleButton("##chip", size);
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 bg = selected ? colors().accent_soft
                              : (hovered ? colors().bg_card_hover : colors().bg_input);
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg, 5.0f);
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                selected ? colors().accent : colors().border_soft, 5.0f);
    dl->AddText(ImVec2(pos.x + pad, pos.y + 5.0f),
                selected ? colors().text : colors().text_muted, label);
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
    const ImVec2 a(card.min.x - 4.0f, card.min.y);
    const ImVec2 b(card.min.x + card.width, card.min.y + card.height);
    dl->AddRectFilled(a, b, colors().bg_card, 7.0f);
    if (border) dl->AddRect(a, b, colors().border_soft, 7.0f, 0, 1.0f);

    ImGui::Indent(card_pad);
    ImGui::Dummy(ImVec2(0, card_pad * 0.5f));
    return true;
}

void end_card() {
    ImGui::Dummy(ImVec2(0, card_pad * 0.5f));
    ImGui::Unindent(card_pad);

    card_state card = std::move(g_cards.back());
    g_cards.pop_back();

    const ImVec2 max = ImGui::GetItemRectMax();
    // Measure and remember for the next frame's background pass.
    g_card_heights[card.key] = max.y - card.min.y + 2.0f;

    ImGui::PopID();
}

}  // namespace shogod::ui::widgets
