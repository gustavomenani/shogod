#pragma once

// In-game Colored Models (Chams) ported from Reverie's SceneSystem +
// RenderSystemDX11 pipeline and driven by shogod::config::visuals_cfg.

#include "config/config.hpp"
#include "sdk/schema.hpp"
#include "sdk/signatures.hpp"

#include <cstdint>
#include <d3d11.h>

namespace shogod::features::chams {

using generate_primitives_fn = void*(__fastcall*)(void* desc, void* object, void* context, void* buffer);
using cull_scene_objects_fn = std::int64_t(__fastcall*)(void* scene_system, void* draw_list,
                                                        void* list, std::uint32_t begin,
                                                        std::uint32_t end, void* output,
                                                        void* pending, void* stats);
using test_scene_pvs_fn = std::uint8_t(__fastcall*)(const void* clusters, const std::uint32_t* bitmap);
using draw_scene_primitive_fn = std::uintptr_t(__fastcall*)(void* draw_data, void* context,
                                                             void* primitive, void* material,
                                                             void* shader_state, void* binding_cache,
                                                             char depth_pass, void* stats,
                                                             void* layer);
using draw_indexed_fn = void(__fastcall*)(void* context, std::uint32_t topology,
                                          std::uint32_t start_index, std::uint32_t index_count,
                                          std::uint32_t instance_count, std::uint32_t unused,
                                          std::int32_t base_vertex, std::uint32_t start_instance);

void update_state(const sdk::anchors& a, const sdk::netvars& nv,
                  const config::visuals_cfg& cfg, std::uintptr_t local_pawn,
                  int local_team, bool local_scoped, bool local_resume_zoom) noexcept;

void clear_cache() noexcept;

void* on_generate_primitives(void* desc, void* object, void* context, void* buffer,
                             generate_primitives_fn original) noexcept;
std::int64_t on_cull_scene_objects(void* scene_system, void* draw_list, void* list,
                                   std::uint32_t begin, std::uint32_t end, void* output,
                                   void* pending, void* stats,
                                   cull_scene_objects_fn original) noexcept;
std::uint8_t on_test_scene_pvs(const void* clusters, const std::uint32_t* bitmap,
                               const void* callsite, test_scene_pvs_fn original) noexcept;
std::uintptr_t on_draw_scene_primitive(void* draw_data, void* context, void* primitive,
                                       void* material, void* shader_state, void* binding_cache,
                                       char depth_pass, void* stats, void* layer,
                                       draw_scene_primitive_fn original) noexcept;
void on_draw_indexed(void* context, std::uint32_t topology, std::uint32_t start_index,
                     std::uint32_t index_count, std::uint32_t instance_count,
                     std::uint32_t unused, std::int32_t base_vertex,
                     std::uint32_t start_instance, draw_indexed_fn original) noexcept;
void on_sort_primitives(void* layer, void* entries, std::uint32_t count) noexcept;
void on_render_layer_draw_list(void* layer, void* page_list) noexcept;

}  // namespace shogod::features::chams
