#include "features/chams/chams.hpp"

#include "sdk/entities.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace shogod::features::chams {
namespace {

struct rgba8 {
    std::uint8_t r, g, b, a;
};
static_assert(sizeof(rgba8) == 4);

rgba8 to_rgba8(const config::rgba& c, int base_alpha = 255) noexcept {
    const auto clamp_u8 = [](float v) -> std::uint8_t {
        return static_cast<std::uint8_t>(std::clamp(static_cast<int>(v * 255.0f + 0.5f), 0, 255));
    };
    const std::uint8_t a = static_cast<std::uint8_t>(
        std::clamp((static_cast<int>(c.a * 255.0f + 0.5f) * base_alpha) / 255, 0, 255));
    return {clamp_u8(c.r), clamp_u8(c.g), clamp_u8(c.b), a};
}

struct mesh_primitive {
    void* data;
    char pad0[0x10];
    void* scene_animatable_object;
    void* material;
    void* material2;
    char pad1[0x10];
    void* some_data;
    char pad2[0x8];
    rgba8 color;
    char pad3[0x1C];
};
static_assert(sizeof(mesh_primitive) == 0x70);
static_assert(offsetof(mesh_primitive, scene_animatable_object) == 0x18);
static_assert(offsetof(mesh_primitive, material) == 0x20);
static_assert(offsetof(mesh_primitive, material2) == 0x28);
static_assert(offsetof(mesh_primitive, color) == 0x50);

struct mesh_primitive_page {
    int count;
    char pad[4];
    mesh_primitive entries[256];
};

struct mesh_primitive_page_list {
    int count;
    char pad[4];
    mesh_primitive_page** pages;
};

struct mesh_primitive_vec {
    int count;
    char pad[4];
    mesh_primitive* data;
    int capacity;
    std::uint32_t allocation_flags;
};

struct mesh_primitive_output_buffer {
    mesh_primitive* array;
    int capacity;
    int count;
    mesh_primitive_vec overflow;
    mesh_primitive overflow_inline[20];
    mesh_primitive_vec* duplicate_dispatch;
    int duplicate_dispatch_start_count;

    struct cursor {
        const mesh_primitive_output_buffer* owner{};
        mesh_primitive* primary_data{};
        int primary_capacity{};
        int primary_count{};
        int overflow_count{};
        mesh_primitive_vec* duplicate_vec{};
        int duplicate_count{};
        bool valid{};
    };

    static bool valid_range(const mesh_primitive* d, int cnt, int cap) noexcept {
        return cnt >= 0 && cap >= 0 && cnt <= cap && (cap == 0 || d != nullptr);
    }

    cursor snapshot() const noexcept {
        cursor c{};
        c.owner = this;
        c.primary_data = array;
        c.primary_capacity = capacity;
        c.primary_count = count;
        c.overflow_count = overflow.count;
        c.duplicate_vec = duplicate_dispatch;
        c.duplicate_count = duplicate_dispatch ? duplicate_dispatch->count : 0;
        c.valid = valid_range(array, count, capacity) &&
                  valid_range(overflow.data, overflow.count, overflow.capacity) &&
                  (!duplicate_dispatch ||
                   valid_range(duplicate_dispatch->data, duplicate_dispatch->count,
                               duplicate_dispatch->capacity));
        return c;
    }

    template <typename Visitor>
    void visit_since(const cursor& c, Visitor&& fn) const noexcept {
        if (!c.valid || c.owner != this || c.primary_data != array ||
            c.primary_capacity != capacity || c.duplicate_vec != duplicate_dispatch ||
            c.primary_count < 0 || c.primary_count > count ||
            c.overflow_count < 0 || c.overflow_count > overflow.count ||
            !valid_range(array, count, capacity) ||
            !valid_range(overflow.data, overflow.count, overflow.capacity)) {
            return;
        }
        const int dup_cnt = duplicate_dispatch ? duplicate_dispatch->count : 0;
        if (c.duplicate_count < 0 || c.duplicate_count > dup_cnt) return;
        for (int i = c.primary_count; i < count; ++i) fn(&array[i]);
        for (int i = c.overflow_count; i < overflow.count; ++i) fn(&overflow.data[i]);
        for (int i = c.duplicate_count; i < dup_cnt; ++i) fn(&duplicate_dispatch->data[i]);
    }
};
static_assert(sizeof(mesh_primitive_output_buffer) == 0x8F8);

struct scene_vis_record {
    float bmin[4];
    float bmax[4];
    std::uint64_t flags;
    void* object;
};
struct scene_vis_list {
    std::uint32_t count;
    char pad[4];
    scene_vis_record* records;
    const void* pvs;
    const std::uint32_t* pvs_bitmap() const noexcept {
        return pvs ? reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(pvs) + 8)
                   : nullptr;
    }
};

enum class pass_role : int { occluded = 0, original = 1, visible = 2 };

struct shared_state {
    sdk::anchors anchors{};
    sdk::netvars nv{};
    config::visuals_cfg cfg{};
    std::uintptr_t local_pawn = 0;
    int local_team = 0;
    bool local_scoped = false;
    bool local_resume_zoom = false;
};

std::mutex g_state_mutex;
shared_state g_state{};

std::shared_mutex g_materials_mutex;
std::unordered_map<std::string, void*> g_material_cache;
std::unordered_map<void*, pass_role> g_material_roles;
std::uint64_t g_material_gen = 0;

std::mutex g_trans_mutex;
std::unordered_map<void*, void*> g_transparent_cache;
std::uint64_t g_trans_count = 0;

struct pvs_scope {
    pvs_scope* prev = nullptr;
    const void* callsite = nullptr;
    const std::uint32_t* bitmap = nullptr;
    std::array<const void*, 96> clusters{};
    std::size_t count = 0;
};
thread_local pvs_scope* tl_pvs_scope = nullptr;

struct draw_marker {
    void* context = nullptr;
    bool occluded = false;
    bool in_override = false;
};
thread_local draw_marker tl_draw_marker{};

template <typename T>
bool safe_read(std::uintptr_t addr, T& out) noexcept {
    if (addr < 0x10000 || addr > 0x7FFFFFFFFFFFULL) return false;
    __try {
        out = *reinterpret_cast<const T*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

const char* entity_class_name(std::uintptr_t entity) noexcept {
    if (entity == 0) return nullptr;
    __try {
        auto** vt = *reinterpret_cast<void***>(entity);
        if (!vt || !vt[47]) return nullptr;
        struct cls_info {
            void* self;
            const char* name;
        };
        cls_info* info = nullptr;
        using fn_t = void (*)(std::uintptr_t, cls_info**);
        reinterpret_cast<fn_t>(vt[47])(entity, &info);
        return info ? info->name : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool entity_is_weapon(std::uintptr_t entity) noexcept {
    if (entity == 0) return false;
    __try {
        auto** vt = *reinterpret_cast<void***>(entity);
        using fn_t = bool (*)(std::uintptr_t);
        return reinterpret_cast<fn_t>(vt[165])(entity);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::uintptr_t scene_object_entity(const sdk::anchors& a, void* scene_obj) noexcept {
    if (!scene_obj || a.entity_system == 0) return 0;
    std::uint32_t owner_handle = 0;
    if (!safe_read(reinterpret_cast<std::uintptr_t>(scene_obj) + 0xC0, owner_handle)) return 0;
    return sdk::resolve_handle(a.entity_system, owner_handle);
}

constexpr const char* k_shaded_vmat = R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
    shader = "csgo_complex.vfx"
    F_TRANSLUCENT = 1
    F_DISABLE_Z_BUFFERING = %d
    F_PAINT_VERTEX_COLORS = 1
    F_RENDER_BACKFACES = 0
    F_ADDITIVE_BLEND = %d
    g_vColorTint = [1.000000, 1.000000, 1.000000, 1.000000]
    g_vReflectanceRange = [0.200000, 0.600000, 0.000000, 0.000000]
    g_vGlossinessRange = [0.100000, 0.500000, 0.000000, 0.000000]
    g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
    g_tNormal = resource:"materials/default/default_normal_tga_7be61377.vtex"
    g_tAmbientOcclusion = resource:"materials/default/default_ao_tga_559f1ac6.vtex"
})";

constexpr const char* k_flat_vmat = R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
    shader = "csgo_unlitgeneric.vfx"
    F_RENDER_BACKFACES = 0
    F_VERTEX_COLOR = 0
    F_DISABLE_Z_PREPASS = 1
    F_DISABLE_Z_BUFFERING = %d
    F_BLEND_MODE = %d
    g_flModelTintAmount = 1
    g_vColorTint = [1.000000, 1.000000, 1.000000, 1.000000]
    g_bFogEnabled = 0
    g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
})";

constexpr const char* k_glow_vmat = R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
    shader = "csgo_effects.vfx"
    g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
    g_tMask1 = resource:"materials/default/default_mask_tga_344101f8.vtex"
    g_tMask2 = resource:"materials/default/default_mask_tga_344101f8.vtex"
    g_tMask3 = resource:"materials/default/default_mask_tga_344101f8.vtex"
    g_flColorBoost = 20
    g_flOpacityScale = 0.7
    g_flFresnelExponent = %f
    g_flFresnelFalloff = %f
    g_flFresnelMax = 0
    g_flFresnelMin = 1
    F_ADDITIVE_BLEND = 1
    F_DISABLE_Z_BUFFERING = %d
    F_RENDER_BACKFACES = 0
    g_vColorTint = [1.00000, 1.00000, 1.00000, 1.00000]
})";

constexpr const char* k_glossy_vmat = R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
    shader = "csgo_effects.vfx"
    F_ADDITIVE_BLEND = %d
    F_DISABLE_Z_BUFFERING = %d
    g_flOpacityScale = 1.0
    g_flFresnelExponent = %f
    g_flFresnelFalloff = %f
    g_flFresnelMax = 1.0
    g_flFresnelMin = 0.0
    g_flColorBoost = 3.0
    g_flToolsVisCubemapReflectionRoughness = 0.1
    g_flBeginMixingRoughness = 0.1
    g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
    g_tMask1 = resource:"materials/default/default_mask_tga_fde710a5.vtex"
    g_tMask2 = resource:"materials/default/default_mask_tga_fde710a5.vtex"
    g_tMask3 = resource:"materials/default/default_mask_tga_fde710a5.vtex"
    g_vColorTint = [1.000000, 1.000000, 1.000000, 1.0]
})";

void* create_material_from_vmat(const sdk::anchors& a, const char* mat_name, const char* vmat) noexcept {
    if (a.material_system == 0 || !a.kv3_load) return nullptr;
    struct kv3_id {
        const char* name;
        std::uint64_t h1;
        std::uint64_t h2;
    };
    struct key_values3 {
        std::uint64_t key;
        void* value;
        std::byte pad[0x8];
    };
    struct strong_handle {
        void* binding = nullptr;
        operator void*() const noexcept {
            return binding ? *reinterpret_cast<void* const*>(binding) : nullptr;
        }
    };

    alignas(16) std::byte storage[0x100 + sizeof(key_values3)]{};
    auto* kv3 = reinterpret_cast<key_values3*>(storage + 0x100);
    const kv3_id id{mat_name, 0x469806E97412167CULL, 0xE73790B53EE6F2AFULL};

    using load_fn = bool(__fastcall*)(key_values3*, void*, const char*, const kv3_id*, const char*, int);
    using create_fn = void(__fastcall*)(void*, strong_handle*, const char*, key_values3*,
                                        int, int, int, int, int, int);

    __try {
        if (!reinterpret_cast<load_fn>(a.kv3_load)(kv3, nullptr, vmat, &id, nullptr, 0)) {
            return nullptr;
        }
        strong_handle out{};
        auto* mat_sys = reinterpret_cast<void*>(a.material_system);
        auto** vt = *reinterpret_cast<void***>(mat_sys);
        reinterpret_cast<create_fn>(vt[29])(mat_sys, &out, mat_name, kv3, 0, 0, 0, 0, 0, 1);
        return static_cast<void*>(out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* get_custom_material(const sdk::anchors& a, int mat_id, bool ignore_depth, bool additive,
                          float exp_v, float falloff_v, pass_role role) {
    std::lock_guard lock(g_materials_mutex);
    mat_id = std::clamp(mat_id, 0, 3);
    const bool uses_fresnel = (mat_id == 2 || mat_id == 3);
    exp_v = uses_fresnel && std::isfinite(exp_v) ? std::round(std::clamp(exp_v, 0.f, 5.f) * 100.f) / 100.f : 0.f;
    falloff_v = uses_fresnel && std::isfinite(falloff_v) ? std::round(std::clamp(falloff_v, 0.f, 20.f) * 100.f) / 100.f : 0.f;
    additive = additive || mat_id == 2;

    char key[128]{};
    std::snprintf(key, sizeof key, "sg_mat_%d_%d_%d_%d_%.2f_%.2f",
                  mat_id, ignore_depth ? 1 : 0, additive ? 1 : 0,
                  static_cast<int>(role), exp_v, falloff_v);
    if (auto it = g_material_cache.find(key); it != g_material_cache.end()) {
        return it->second;
    }

    char vmat[2048]{};
    const int z_dis = ignore_depth ? 1 : 0;
    const int add_f = additive ? 1 : 0;
    switch (mat_id) {
        case 0: std::snprintf(vmat, sizeof vmat, k_shaded_vmat, z_dis, add_f); break;
        case 1: std::snprintf(vmat, sizeof vmat, k_flat_vmat, z_dis, additive ? 4 : 1); break;
        case 2: std::snprintf(vmat, sizeof vmat, k_glow_vmat, exp_v, falloff_v, z_dis); break;
        case 3: std::snprintf(vmat, sizeof vmat, k_glossy_vmat, add_f, z_dis, exp_v, falloff_v); break;
    }

    char name[192]{};
    std::snprintf(name, sizeof name, "%s_%llu.vmat", key,
                  static_cast<unsigned long long>(g_material_gen));
    void* mat = create_material_from_vmat(a, name, vmat);
    if (mat) {
        g_material_cache.emplace(key, mat);
        g_material_roles.emplace(mat, role);
    }
    return mat;
}

void* layer_material(void* mat, void* scene_layer) noexcept {
    if (!mat || !scene_layer) return mat;
    __try {
        auto** vt = *reinterpret_cast<void***>(mat);
        const void* mode_data = reinterpret_cast<const std::byte*>(scene_layer) + 0x30;
        using fn_t = void* (*)(void*, const void*);
        return reinterpret_cast<fn_t>(vt[6])(mat, mode_data);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return mat;
    }
}

void* transparent_proxy(const sdk::anchors& a, void* orig_mat) {
    if (!orig_mat || !a.find_material_param) return nullptr;
    std::lock_guard lock(g_trans_mutex);
    if (auto it = g_transparent_cache.find(orig_mat); it != g_transparent_cache.end()) {
        return it->second;
    }

    auto tex_path = [&](const char* param_name) -> const char* {
        struct mat_param {
            float vec[4];
            void* texture_value;
        };
        using find_fn = mat_param* (*)(void*, const char*);
        __try {
            auto* p = reinterpret_cast<find_fn>(a.find_material_param)(orig_mat, param_name);
            if (!p || !p->texture_value) return nullptr;
            auto* binding = *reinterpret_cast<void* const*>(p->texture_value);
            if (!binding) return nullptr;
            return *reinterpret_cast<const char* const*>(binding);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    };

    const char* c_path = tex_path("g_tColor");
    const char* n_path = tex_path("g_tNormal");
    const char* ao_path = tex_path("g_tAmbientOcclusion");
    char vmat[2048]{};
    std::snprintf(vmat, sizeof vmat,
                  R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} --> {
shader = "csgo_complex.vfx"
F_TRANSLUCENT = 1
F_PAINT_VERTEX_COLORS = 1
F_RENDER_BACKFACES = 0
g_vColorTint = [1.000000, 1.000000, 1.000000, 1.000000]
g_tColor = resource:"%s"
g_tNormal = resource:"%s"
g_tAmbientOcclusion = resource:"%s"
})",
                  c_path ? c_path : "materials/default/default_color_tga_72dcfbfd.vtex",
                  n_path ? n_path : "materials/default/default_normal_tga_7652cb.vtex",
                  ao_path ? ao_path : "materials/default/default_mask_tga_fde710a5.vtex");

    char name[64]{};
    std::snprintf(name, sizeof name, "sg_scope_%llu.vmat",
                  static_cast<unsigned long long>(g_trans_count++));
    void* proxy = create_material_from_vmat(a, name, vmat);
    if (proxy) {
        g_transparent_cache[orig_mat] = proxy;
        std::lock_guard mlock(g_materials_mutex);
        g_material_roles.insert_or_assign(proxy, pass_role::original);
    }
    return proxy;
}

int pass_role_of(void* mat) noexcept {
    if (!mat) return -1;
    auto it = g_material_roles.find(mat);
    return it != g_material_roles.end() ? static_cast<int>(it->second) : -1;
}

bool should_order_layer(void* layer) noexcept {
    if (!layer) return false;
    std::uint64_t flags = 0;
    std::memcpy(&flags, reinterpret_cast<const std::byte*>(layer) + 0x48, sizeof flags);
    return (flags & (1ull << 24)) == 0 && ((flags & 2ull) == 0 || (flags & 1ull) != 0);
}

void order_passes(const std::vector<mesh_primitive*>& entries) noexcept {
    if (entries.size() <= 1) return;
    std::shared_lock lock(g_materials_mutex);
    if (g_material_roles.empty()) return;

    int tagged = 0;
    for (const mesh_primitive* p : entries) {
        if (p && p->scene_animatable_object && pass_role_of(p->material) >= 0) {
            if (++tagged >= 2) break;
        }
    }
    if (tagged < 2) return;

    struct slot_t {
        std::size_t idx;
        int rank;
    };
    std::unordered_map<void*, std::vector<slot_t>> groups;
    for (mesh_primitive* p : entries) {
        if (!p || !p->scene_animatable_object) continue;
        const int r = pass_role_of(p->material);
        if (r >= 0 && r <= 2) groups.try_emplace(p->scene_animatable_object);
    }
    if (groups.empty()) return;

    for (std::size_t i = 0; i < entries.size(); ++i) {
        mesh_primitive* p = entries[i];
        if (!p) continue;
        auto it = groups.find(p->scene_animatable_object);
        if (it == groups.end()) continue;
        const int r = pass_role_of(p->material);
        it->second.push_back({i, (r >= 0 && r <= 2) ? r : 1});
    }

    for (auto& [obj, grp] : groups) {
        if (grp.size() <= 1) continue;
        auto less = [](const slot_t& a, const slot_t& b) { return a.rank < b.rank; };
        if (std::is_sorted(grp.begin(), grp.end(), less)) continue;
        auto sorted = grp;
        std::stable_sort(sorted.begin(), sorted.end(), less);
        std::vector<mesh_primitive> copy;
        copy.reserve(sorted.size());
        for (const auto& s : sorted) copy.push_back(*entries[s.idx]);
        for (std::size_t i = 0; i < grp.size(); ++i) *entries[grp[i].idx] = copy[i];
    }
}

bool supports_depth(const sdk::anchors& a, void* ctx) noexcept {
    if (!ctx || !a.set_depth_state || !a.set_depth_override) return false;
    __try {
        auto** vt = *reinterpret_cast<void***>(ctx);
        if (vt[43] != a.set_depth_state || vt[44] != a.set_depth_override) return false;
        return vt[94] == a.draw_occluded_dx11 || vt[94] == a.draw_occluded_software;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool build_occluded_desc(const D3D11_DEPTH_STENCIL_DESC& src, std::uint64_t& packed) noexcept {
    if (!src.DepthEnable ||
        (src.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ZERO &&
         src.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ALL)) {
        return false;
    }
    D3D11_COMPARISON_FUNC inv{};
    switch (src.DepthFunc) {
        case D3D11_COMPARISON_LESS:
        case D3D11_COMPARISON_LESS_EQUAL: inv = D3D11_COMPARISON_GREATER; break;
        case D3D11_COMPARISON_GREATER:
        case D3D11_COMPARISON_GREATER_EQUAL: inv = D3D11_COMPARISON_LESS; break;
        default: return false;
    }
    auto val = [](std::uint64_t v) { return v - 1; };
    packed = 1ull | (val(inv) << 2)
        | (val(src.FrontFace.StencilFunc) << 16)
        | (val(src.BackFace.StencilFunc) << 20)
        | (static_cast<std::uint64_t>(src.StencilEnable != FALSE) << 24)
        | (val(src.FrontFace.StencilFailOp) << 25)
        | (val(src.FrontFace.StencilDepthFailOp) << 28)
        | (val(src.FrontFace.StencilPassOp) << 32)
        | (val(src.BackFace.StencilFailOp) << 35)
        | (val(src.BackFace.StencilDepthFailOp) << 38)
        | (val(src.BackFace.StencilPassOp) << 41)
        | (static_cast<std::uint64_t>(src.StencilReadMask) << 48);
    return true;
}

}  // namespace

void update_state(const sdk::anchors& a, const sdk::netvars& nv,
                  const config::visuals_cfg& cfg, std::uintptr_t local_pawn,
                  int local_team, bool local_scoped, bool local_resume_zoom) noexcept {
    std::lock_guard lock(g_state_mutex);
    g_state.anchors = a;
    g_state.nv = nv;
    g_state.cfg = cfg;
    g_state.local_pawn = local_pawn;
    g_state.local_team = local_team;
    g_state.local_scoped = local_scoped;
    g_state.local_resume_zoom = local_resume_zoom;
}

void clear_cache() noexcept {
    {
        std::lock_guard lock(g_trans_mutex);
        g_transparent_cache.clear();
    }
    std::lock_guard lock(g_materials_mutex);
    g_material_roles.clear();
    g_material_cache.clear();
    ++g_material_gen;
}

void* on_generate_primitives(void* desc, void* object, void* context, void* buffer,
                             generate_primitives_fn original) noexcept {
    shared_state st{};
    {
        std::lock_guard lock(g_state_mutex);
        st = g_state;
    }
    if (!object || !buffer || st.anchors.entity_system == 0 || st.local_pawn == 0) {
        return original(desc, object, context, buffer);
    }

    auto* out_buf = static_cast<mesh_primitive_output_buffer*>(buffer);
    if (!out_buf->snapshot().valid) return original(desc, object, context, buffer);

    const std::uintptr_t ent = scene_object_entity(st.anchors, object);
    if (ent == 0) return original(desc, object, context, buffer);
    const char* cls = entity_class_name(ent);
    if (!cls) return original(desc, object, context, buffer);

    struct layer_entry {
        void* mat = nullptr;
        rgba8 col{};
    };
    std::array<layer_entry, 2> layers{};
    std::size_t layer_count = 0;
    bool preserve_orig = true;
    int base_alpha = 255;

    auto push_cham = [&](const config::cham_material& ch, bool is_player, bool allow_occluded) {
        if (!ch.enabled) return;
        const bool overlay = (ch.flags & static_cast<int>(config::chams_flag::overlay)) != 0;
        const bool flat = (ch.flags & static_cast<int>(config::chams_flag::flat)) != 0;
        const bool ignore_z = (ch.flags & static_cast<int>(config::chams_flag::ignore_z)) != 0;
        const int mat_id = flat ? 1 : ch.material;

        preserve_orig = overlay;
        if (allow_occluded && ignore_z && ch.invisible.a > 0.01f) {
            void* m = get_custom_material(st.anchors, mat_id, false, overlay,
                                          ch.fresnel_exponent, ch.fresnel_falloff,
                                          pass_role::occluded);
            if (m && layer_count < layers.size()) {
                layers[layer_count++] = {m, to_rgba8(ch.invisible, base_alpha)};
            }
        }
        if (ch.visible.a > 0.01f) {
            void* m = get_custom_material(st.anchors, mat_id, !is_player && ignore_z, overlay,
                                          ch.fresnel_exponent, ch.fresnel_falloff,
                                          pass_role::visible);
            if (m && layer_count < layers.size()) {
                layers[layer_count++] = {m, to_rgba8(ch.visible, base_alpha)};
            }
        }
    };

    const std::string_view cname(cls);
    const bool scope_blend = st.cfg.scope_blend && (st.local_scoped || st.local_resume_zoom);
    if (cname == "C_CS2HudModelArms") {
        push_cham(st.cfg.arms_chams, false, false);
    } else if (cname == "C_CS2HudModelWeapon") {
        push_cham(st.cfg.weapon_chams, false, false);
    } else if (entity_is_weapon(ent)) {
        std::uint32_t owner_h = 0;
        if (st.nv.ent_owner != 0 && safe_read(ent + st.nv.ent_owner, owner_h) &&
            sdk::resolve_handle(st.anchors.entity_system, owner_h) == st.local_pawn) {
            if (scope_blend) base_alpha = std::clamp(st.cfg.scope_blend_alpha, 0, 255);
            push_cham(st.cfg.weapon_chams, false, false);
        } else {
            return original(desc, object, context, buffer);
        }
    } else if (cname == "C_CSPlayerPawn") {
        int hp = 0;
        int team = 0;
        if (!safe_read(ent + st.nv.ent_health, hp) || hp <= 0) {
            return original(desc, object, context, buffer);
        }
        safe_read(ent + st.nv.ent_team, team);
        if (ent == st.local_pawn) {
            if (scope_blend) base_alpha = std::clamp(st.cfg.scope_blend_alpha, 0, 255);
            push_cham(st.cfg.local_chams, false, false);
        } else if (st.local_team == 0 || team != st.local_team) {
            push_cham(st.cfg.player_chams, true, true);
        } else {
            push_cham(st.cfg.team_chams, true, true);
        }
    } else {
        return original(desc, object, context, buffer);
    }

    if (layer_count == 0 && base_alpha == 255) {
        return original(desc, object, context, buffer);
    }

    void* scene_layer = context ? *reinterpret_cast<void**>(static_cast<std::byte*>(context) + 0x10) : nullptr;
    std::uint64_t native_flags = 0;
    if (scene_layer) {
        std::memcpy(&native_flags, static_cast<std::byte*>(scene_layer) + 0x48, sizeof native_flags);
    }
    const bool shadow_pass = (native_flags & (1ull << 24)) != 0;

    if (preserve_orig || (layer_count == 0 && base_alpha < 255)) {
        if (!(base_alpha < 255 && shadow_pass)) {
            const auto before = out_buf->snapshot();
            original(desc, object, context, buffer);
            if (base_alpha < 255) {
                out_buf->visit_since(before, [&](mesh_primitive* p) {
                    if (!p->material) return;
                    void* proxy = transparent_proxy(st.anchors, p->material);
                    if (!proxy) return;
                    p->material = proxy;
                    p->material2 = layer_material(proxy, scene_layer);
                    p->color.a = static_cast<std::uint8_t>((p->color.a * base_alpha) / 255);
                });
            }
        }
    }

    if (!shadow_pass) {
        for (std::size_t i = 0; i < layer_count; ++i) {
            if (!layers[i].mat || layers[i].col.a == 0) continue;
            const auto before = out_buf->snapshot();
            void* secondary = layer_material(layers[i].mat, scene_layer);
            original(desc, object, context, buffer);
            out_buf->visit_since(before, [&](mesh_primitive* p) {
                p->material = layers[i].mat;
                p->material2 = secondary;
                p->color = layers[i].col;
            });
        }
    }
    return nullptr;
}

std::int64_t on_cull_scene_objects(void* scene_system, void* draw_list, void* list,
                                   std::uint32_t begin, std::uint32_t end, void* output,
                                   void* pending, void* stats,
                                   cull_scene_objects_fn original) noexcept {
    shared_state st{};
    {
        std::lock_guard lock(g_state_mutex);
        st = g_state;
    }

    pvs_scope scope{};
    scope.prev = tl_pvs_scope;
    scope.callsite = st.anchors.player_pvs_callsite;
    const auto* vis_list = static_cast<const scene_vis_list*>(list);
    scope.bitmap = vis_list ? vis_list->pvs_bitmap() : nullptr;
    tl_pvs_scope = &scope;

    const bool enemy_occ = st.cfg.player_chams.enabled &&
                           (st.cfg.player_chams.flags & static_cast<int>(config::chams_flag::ignore_z)) != 0;
    const bool team_occ = st.cfg.team_chams.enabled &&
                          (st.cfg.team_chams.flags & static_cast<int>(config::chams_flag::ignore_z)) != 0;

    if ((enemy_occ || team_occ) && st.anchors.animatable_desc && vis_list && vis_list->records &&
        begin < end && end <= vis_list->count && (end - begin) <= scope.clusters.size() &&
        st.local_pawn != 0 && scope.callsite && scope.bitmap) {
        for (std::uint32_t i = begin; i < end; ++i) {
            auto* obj = vis_list->records[i].object;
            if (!obj) continue;
            const auto* bytes = static_cast<const std::uint8_t*>(obj);
            const std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(bytes + 0x78);
            const void* obj_desc = *reinterpret_cast<void* const*>(bytes + 0x18);
            if ((flags & 8u) == 0 || obj_desc != st.anchors.animatable_desc) continue;
            const void* clusters = *reinterpret_cast<const void* const*>(bytes + 0xA0);
            if (!clusters) continue;

            const std::uintptr_t ent = scene_object_entity(st.anchors, obj);
            if (ent == 0 || ent == st.local_pawn) continue;
            const char* cls = entity_class_name(ent);
            if (!cls || std::strcmp(cls, "C_CSPlayerPawn") != 0) continue;

            int hp = 0;
            int team = 0;
            if (!safe_read(ent + st.nv.ent_health, hp) || hp <= 0) continue;
            safe_read(ent + st.nv.ent_team, team);
            const bool is_enemy = (st.local_team == 0 || team != st.local_team);
            if ((is_enemy && !enemy_occ) || (!is_enemy && !team_occ)) continue;

            bool exists = false;
            for (std::size_t k = 0; k < scope.count; ++k) {
                if (scope.clusters[k] == clusters) {
                    exists = true;
                    break;
                }
            }
            if (!exists && scope.count < scope.clusters.size()) {
                scope.clusters[scope.count++] = clusters;
            }
        }
    }

    const auto res = original(scene_system, draw_list, list, begin, end, output, pending, stats);
    tl_pvs_scope = scope.prev;
    return res;
}

std::uint8_t on_test_scene_pvs(const void* clusters, const std::uint32_t* bitmap,
                               const void* callsite, test_scene_pvs_fn original) noexcept {
    const pvs_scope* scope = tl_pvs_scope;
    if (scope && clusters && bitmap && callsite && scope->bitmap == bitmap &&
        scope->callsite == callsite) {
        for (std::size_t i = 0; i < scope->count; ++i) {
            if (scope->clusters[i] == clusters) return 1;
        }
    }
    return original(clusters, bitmap);
}

std::uintptr_t on_draw_scene_primitive(void* draw_data, void* context, void* primitive,
                                       void* material, void* shader_state, void* binding_cache,
                                       char depth_pass, void* stats, void* layer,
                                       draw_scene_primitive_fn original) noexcept {
    shared_state st{};
    {
        std::lock_guard lock(g_state_mutex);
        st = g_state;
    }

    bool hidden = false;
    if (!depth_pass && primitive && layer) {
        std::uint64_t layer_flags = 0;
        std::memcpy(&layer_flags, static_cast<const std::byte*>(layer) + 0x48, sizeof layer_flags);
        if ((layer_flags & (1ull << 24)) == 0) {
            void* prim_mat = static_cast<const mesh_primitive*>(primitive)->material;
            std::shared_lock lock(g_materials_mutex);
            if (auto it = g_material_roles.find(prim_mat); it != g_material_roles.end()) {
                hidden = (it->second == pass_role::occluded);
            }
        }
    }
    if (hidden && !supports_depth(st.anchors, context)) return 0;

    const draw_marker prev = tl_draw_marker;
    tl_draw_marker = {context, hidden, prev.in_override};
    const auto res = original(draw_data, context, primitive, material, shader_state,
                              binding_cache, depth_pass, stats, layer);
    tl_draw_marker = prev;
    return res;
}

bool verify_depth_device(void* dev, void* expected_create) noexcept {
    if (!dev || !expected_create) return false;
    __try {
        return (*reinterpret_cast<void***>(dev))[31] == expected_create;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void on_draw_indexed(void* context, std::uint32_t topology, std::uint32_t start_index,
                     std::uint32_t index_count, std::uint32_t instance_count,
                     std::uint32_t unused, std::int32_t base_vertex,
                     std::uint32_t start_instance, draw_indexed_fn original) noexcept {
    if (!tl_draw_marker.occluded || tl_draw_marker.context != context || tl_draw_marker.in_override) {
        original(context, topology, start_index, index_count, instance_count, unused, base_vertex,
                 start_instance);
        return;
    }

    void* depth_factory = nullptr;
    void* create_depth_state = nullptr;
    void* set_depth_state = nullptr;
    void* set_depth_override = nullptr;
    {
        std::lock_guard lock(g_state_mutex);
        const auto& anch = g_state.anchors;
        if (!supports_depth(anch, context)) {
            original(context, topology, start_index, index_count, instance_count, unused,
                     base_vertex, start_instance);
            return;
        }
        depth_factory = anch.depth_factory;
        create_depth_state = anch.create_depth_state;
        set_depth_state = anch.set_depth_state;
        set_depth_override = anch.set_depth_override;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(context);
    ID3D11DepthStencilState* actual = nullptr;
    std::uint32_t actual_ref = 0;
    ID3D11DepthStencilState* override_state = nullptr;
    std::uint32_t override_ref = 0;
    std::memcpy(&actual, bytes + 0xA20, sizeof actual);
    std::memcpy(&actual_ref, bytes + 0xA28, sizeof actual_ref);
    std::memcpy(&override_state, bytes + 0xA58, sizeof override_state);
    std::memcpy(&override_ref, bytes + 0xA60, sizeof override_ref);

    D3D11_DEPTH_STENCIL_DESC desc{};
    std::uint64_t packed = 0;
    if (!actual) return;
    actual->GetDesc(&desc);
    if (!build_occluded_desc(desc, packed)) return;

    using factory_fn = void*(__fastcall*)(const char*, int*);
    using create_fn = ID3D11DepthStencilState*(__fastcall*)(void*, const std::uint64_t*);
    using set_fn = void(__fastcall*)(void*, ID3D11DepthStencilState*, std::uint32_t);

    void* dev = depth_factory ? reinterpret_cast<factory_fn>(depth_factory)("RenderDevice003", nullptr)
                              : nullptr;
    if (!verify_depth_device(dev, create_depth_state)) return;
    ID3D11DepthStencilState* inv = reinterpret_cast<create_fn>(create_depth_state)(dev, &packed);
    if (!inv) return;

    const draw_marker prev = tl_draw_marker;
    tl_draw_marker.in_override = true;
    auto set_state = reinterpret_cast<set_fn>(set_depth_state);
    auto set_over = reinterpret_cast<set_fn>(set_depth_override);
    set_over(context, inv, actual_ref);
    set_state(context, inv, actual_ref);
    original(context, topology, start_index, index_count, instance_count, unused, base_vertex,
             start_instance);
    set_over(context, nullptr, 0);
    set_state(context, actual, actual_ref);
    set_over(context, override_state, override_ref);
    tl_draw_marker = prev;
}

void on_sort_primitives(void* layer, void* entries, std::uint32_t count) noexcept {
    if (!should_order_layer(layer) || !entries || count < 2 || count > 65536) return;
    {
        std::shared_lock lock(g_materials_mutex);
        if (g_material_roles.empty()) return;
    }
    auto* arr = static_cast<mesh_primitive*>(entries);
    std::vector<mesh_primitive*> vec;
    vec.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) vec.push_back(&arr[i]);
    order_passes(vec);
}

void on_render_layer_draw_list(void* layer, void* page_list) noexcept {
    if (!should_order_layer(layer) || !page_list) return;
    {
        std::shared_lock lock(g_materials_mutex);
        if (g_material_roles.empty()) return;
    }
    const auto* lists = static_cast<const mesh_primitive_page_list*>(page_list);
    if (lists->count <= 1 || !lists->pages) return;
    std::vector<mesh_primitive*> vec;
    for (int i = 0; i < lists->count; ++i) {
        mesh_primitive_page* page = lists->pages[i];
        if (!page || page->count < 0 || page->count > 256) return;
        for (int j = 0; j < page->count; ++j) vec.push_back(&page->entries[j]);
    }
    if (vec.size() >= 2) order_passes(vec);
}

}  // namespace shogod::features::chams
