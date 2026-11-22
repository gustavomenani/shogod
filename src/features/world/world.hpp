#pragma once

// World & perspective visuals ported from Reverie: world/light modulation,
// exposure/tonemap control, view-punch suppression, and penetration crosshair.

#include "config/config.hpp"
#include "sdk/entities.hpp"
#include "sdk/schema.hpp"
#include "sdk/signatures.hpp"
#include "sdk/snapshot.hpp"

#include <cstdint>

namespace shogod::features::world {

using base_draw_array_fn = void(__fastcall*)(void* ptr, void* context, void* render_list,
                                            int count, void* view, void* layer, void* a6);
using setup_light_fn = void*(__fastcall*)(void* ptr, void* light_obj, void* unk);
using setup_tonemap_fn = void(__fastcall*)(void* a1, float a2, void* a3);
using view_punch_fn = void*(__fastcall*)(void* cam_svc, float* a2, float* a3, float* a4);

void update_config(const config::visuals_cfg& cfg, const sdk::netvars& nv) noexcept;
void clear_backups() noexcept;

void on_base_draw_array(void* ptr, void* context, void* render_list, int count,
                        void* view, void* layer, void* a6,
                        base_draw_array_fn original) noexcept;
void* on_setup_light(void* ptr, void* light_obj, void* unk, setup_light_fn original) noexcept;
void on_setup_tonemap(void* a1, float a2, void* a3, setup_tonemap_fn original) noexcept;
void* on_view_punch(void* cam_svc, float* a2, float* a3, float* a4,
                    view_punch_fn original) noexcept;

void update_penetration_crosshair(const config::config_v1& cfg, const sdk::anchors& anchors,
                                  const sdk::netvars& nv, const sdk::frame_snapshot* frame) noexcept;

void draw_penetration_crosshair(const config::config_v1& cfg, const sdk::anchors& anchors,
                                const sdk::netvars& nv, const sdk::frame_snapshot* frame,
                                int screen_w, int screen_h) noexcept;

}  // namespace shogod::features::world
