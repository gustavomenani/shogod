#pragma once

// Shared box-relative ESP element layout solver (top, right, bottom, left).
// Adapted from Reverie's esp_layout so the 3D Preview and in-game ESP mirror
// side placement, full-span bars, stacking order, and drag-and-drop 1:1.

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace shogod::features::esp {

enum layout_side : int { side_top = 0, side_right = 1, side_bottom = 2, side_left = 3 };
enum layout_align : int { align_start = 0, align_center = 1, align_end = 2 };

struct layout_dims {
    float width = 0.0f;
    float height = 0.0f;
    bool full_span = false;
    float extra_reserve = 0.0f;
};

struct layout_rect {
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
};

struct layout_item {
    int id = 0;
    int side = side_top;
    int align = align_center;
    int order = 0;
    layout_dims sz{};
    layout_rect box{};
};

inline bool is_horizontal_side(int side) noexcept {
    return side == side_top || side == side_bottom;
}

inline void resolve_dims(int side, const layout_dims& sz, float box_min_x, float box_min_y,
                         float box_max_x, float box_max_y, float& perp, float& along) noexcept {
    const float box_w = box_max_x - box_min_x;
    const float box_h = box_max_y - box_min_y;
    if (is_horizontal_side(side)) {
        perp = sz.height;
        along = sz.full_span ? box_w : sz.width;
    } else {
        perp = sz.width;
        along = sz.full_span ? box_h : sz.height;
    }
}

inline layout_rect place_item(int side, int align, float offset, const layout_dims& sz,
                              float box_min_x, float box_min_y, float box_max_x,
                              float box_max_y) noexcept {
    const float box_w = box_max_x - box_min_x;
    const float box_h = box_max_y - box_min_y;

    float perp = 0.0f;
    float along = 0.0f;
    resolve_dims(side, sz, box_min_x, box_min_y, box_max_x, box_max_y, perp, along);

    layout_rect r{};
    if (is_horizontal_side(side)) {
        float ax_min = box_min_x;
        float ax_max = box_max_x;
        if (align == align_start) {
            ax_min = box_min_x;
            ax_max = ax_min + along;
        } else if (align == align_end) {
            ax_max = box_max_x;
            ax_min = ax_max - along;
        } else {
            const float mid = box_min_x + box_w * 0.5f;
            ax_min = mid - along * 0.5f;
            ax_max = mid + along * 0.5f;
        }
        r.min_x = ax_min;
        r.max_x = ax_max;
        if (side == side_top) {
            r.max_y = box_min_y - offset;
            r.min_y = r.max_y - perp;
        } else {
            r.min_y = box_max_y + offset;
            r.max_y = r.min_y + perp;
        }
    } else {
        float ax_min = box_min_y;
        float ax_max = box_max_y;
        if (align == align_start) {
            ax_min = box_min_y;
            ax_max = ax_min + along;
        } else if (align == align_end) {
            ax_max = box_max_y;
            ax_min = ax_max - along;
        } else {
            const float mid = box_min_y + box_h * 0.5f;
            ax_min = mid - along * 0.5f;
            ax_max = mid + along * 0.5f;
        }
        r.min_y = ax_min;
        r.max_y = ax_max;
        if (side == side_left) {
            r.max_x = box_min_x - offset;
            r.min_x = r.max_x - perp;
        } else {
            r.min_x = box_max_x + offset;
            r.max_x = r.min_x + perp;
        }
    }
    return r;
}

inline void compute_layout(std::vector<layout_item>& items, float box_min_x, float box_min_y,
                           float box_max_x, float box_max_y, float base_offset = 4.0f,
                           float gap = 2.0f, float list_gap = 1.0f) {
    for (int side : {side_top, side_bottom}) {
        std::vector<int> full_idxs, norm_idxs;
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (items[i].side != side) continue;
            if (items[i].sz.full_span) full_idxs.push_back(i);
            else norm_idxs.push_back(i);
        }
        std::stable_sort(full_idxs.begin(), full_idxs.end(),
                         [&](int a, int b) { return items[a].order < items[b].order; });
        std::stable_sort(norm_idxs.begin(), norm_idxs.end(),
                         [&](int a, int b) { return items[a].order < items[b].order; });

        float offset = base_offset;
        auto place_group = [&](const std::vector<int>& idxs) {
            for (int idx : idxs) {
                items[idx].align = align_center;
                items[idx].box = place_item(side, align_center, offset, items[idx].sz, box_min_x,
                                            box_min_y, box_max_x, box_max_y);
                float perp = 0.0f, along = 0.0f;
                resolve_dims(side, items[idx].sz, box_min_x, box_min_y, box_max_x, box_max_y, perp,
                             along);
                offset += perp + items[idx].sz.extra_reserve + gap;
            }
        };
        // Full-span bars sit closest to the bounding box, followed by text/icon rows.
        place_group(full_idxs);
        place_group(norm_idxs);
    }

    for (int side : {side_left, side_right}) {
        std::vector<int> full_idxs, norm_idxs;
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (items[i].side != side) continue;
            items[i].align = align_start;
            if (items[i].sz.full_span) full_idxs.push_back(i);
            else norm_idxs.push_back(i);
        }
        std::stable_sort(full_idxs.begin(), full_idxs.end(),
                         [&](int a, int b) { return items[a].order < items[b].order; });
        std::stable_sort(norm_idxs.begin(), norm_idxs.end(),
                         [&](int a, int b) { return items[a].order < items[b].order; });

        float perp_offset = base_offset;
        for (int idx : full_idxs) {
            items[idx].box = place_item(side, align_start, perp_offset, items[idx].sz, box_min_x,
                                        box_min_y, box_max_x, box_max_y);
            float perp = 0.0f, along = 0.0f;
            resolve_dims(side, items[idx].sz, box_min_x, box_min_y, box_max_x, box_max_y, perp,
                         along);
            perp_offset += perp + items[idx].sz.extra_reserve + gap;
        }

        const float list_offset = perp_offset;
        float y_cursor = box_min_y;
        for (int idx : norm_idxs) {
            const layout_dims& sz = items[idx].sz;
            layout_rect r{};
            r.min_y = y_cursor;
            r.max_y = y_cursor + sz.height;
            if (side == side_left) {
                r.max_x = box_min_x - list_offset;
                r.min_x = r.max_x - sz.width;
            } else {
                r.min_x = box_max_x + list_offset;
                r.max_x = r.min_x + sz.width;
            }
            items[idx].box = r;
            y_cursor += sz.height + sz.extra_reserve + list_gap;
        }
    }
}

inline int compute_side(float box_min_x, float box_min_y, float box_max_x, float box_max_y,
                        float px, float py) noexcept {
    const float mid_x = (box_min_x + box_max_x) * 0.5f;
    const float mid_y = (box_min_y + box_max_y) * 0.5f;
    const float hw = std::max((box_max_x - box_min_x) * 0.5f, 1.0f);
    const float hh = std::max((box_max_y - box_min_y) * 0.5f, 1.0f);
    const float rx = (px - mid_x) / hw;
    const float ry = (py - mid_y) / hh;
    if (std::fabs(rx) > std::fabs(ry)) {
        return rx < 0.0f ? side_left : side_right;
    }
    return ry < 0.0f ? side_top : side_bottom;
}

inline std::vector<layout_item> build_layout_with_ghost(
    const std::vector<layout_item>& items, int drag_id, const layout_dims& drag_sz,
    int new_side, float box_min_x, float box_min_y, float box_max_x, float box_max_y,
    float mouse_x, float mouse_y, int& out_order) {
    (void)mouse_x;
    std::vector<layout_item> list;
    list.reserve(items.size() + 1);
    for (const auto& it : items) {
        if (it.id != drag_id) list.push_back(it);
    }
    compute_layout(list, box_min_x, box_min_y, box_max_x, box_max_y);

    struct side_item {
        int index;
        float metric;
    };
    std::vector<side_item> group;
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        if (list[i].side != new_side) continue;
        const float cy = (list[i].box.min_y + list[i].box.max_y) * 0.5f;
        float metric = 0.0f;
        if (new_side == side_top) metric = box_min_y - cy;
        else if (new_side == side_bottom) metric = cy - box_max_y;
        else metric = cy - box_min_y;
        group.push_back({i, metric});
    }
    std::sort(group.begin(), group.end(),
              [](const side_item& a, const side_item& b) { return a.metric < b.metric; });

    float mouse_metric = 0.0f;
    if (new_side == side_top) mouse_metric = box_min_y - mouse_y;
    else if (new_side == side_bottom) mouse_metric = mouse_y - box_max_y;
    else mouse_metric = mouse_y - box_min_y;

    int insert_idx = 0;
    for (std::size_t i = 0; i < group.size(); ++i) {
        if (mouse_metric > group[i].metric) insert_idx = static_cast<int>(i) + 1;
    }
    for (int seq = 0; seq < static_cast<int>(group.size()); ++seq) {
        list[group[seq].index].order = (seq < insert_idx) ? seq : seq + 1;
    }

    layout_item ghost{};
    ghost.id = drag_id;
    ghost.side = new_side;
    ghost.align = is_horizontal_side(new_side) ? align_center : align_start;
    ghost.order = insert_idx;
    ghost.sz = drag_sz;
    list.push_back(ghost);

    compute_layout(list, box_min_x, box_min_y, box_max_x, box_max_y);
    out_order = insert_idx;
    return list;
}

}  // namespace shogod::features::esp
