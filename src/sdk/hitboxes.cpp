#include "sdk/hitboxes.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <windows.h>

namespace shogod::sdk {
namespace {

constexpr std::uintptr_t k_skel_model_handle = 0x1E0;
constexpr std::uintptr_t k_skel_proc_handle = 0x200;
constexpr std::uintptr_t k_skel_bone_cache = 0x1C0;
constexpr std::uintptr_t k_skel_transform_alloc = 0x1D0;
constexpr std::uintptr_t k_skel_transform_count = 0x1D4;
constexpr std::uintptr_t k_skel_hitbox_context = 0x348;
constexpr std::uintptr_t k_skel_transform_override = 0x440;
constexpr std::uintptr_t k_default_pawn_hitbox_history = 0x2000;

std::uintptr_t resolve_hitbox_history_offset_seh(std::uintptr_t update_fn) noexcept {
    static std::uintptr_t cached_fn = 0;
    static std::uintptr_t cached_offset = k_default_pawn_hitbox_history;
    if (update_fn == 0) return k_default_pawn_hitbox_history;
    if (update_fn == cached_fn) return cached_offset;
    __try {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(update_fn);
        for (std::size_t i = 0; i + 7 <= 256; ++i) {
            if (bytes[i] == 0x48 && bytes[i + 1] == 0x8D &&
                (bytes[i + 2] == 0x9F || bytes[i + 2] == 0xB7)) {
                std::uint32_t disp = 0;
                std::memcpy(&disp, bytes + i + 3, sizeof disp);
                if (disp >= 0x1800 && disp <= 0x3000) {
                    cached_fn = update_fn;
                    cached_offset = static_cast<std::uintptr_t>(disp);
                    return cached_offset;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    cached_fn = update_fn;
    cached_offset = k_default_pawn_hitbox_history;
    return k_default_pawn_hitbox_history;
}

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

std::uintptr_t get_skeleton(const netvars& nv, std::uintptr_t pawn) noexcept {
    if (pawn == 0 || nv.ent_scene_node == 0) return 0;
    std::uintptr_t node = 0;
    if (!safe_read(pawn + nv.ent_scene_node, node) || node == 0) return 0;
    __try {
        auto** vt = *reinterpret_cast<void***>(node);
        using fn_t = std::uintptr_t (*)(std::uintptr_t);
        return reinterpret_cast<fn_t>(vt[14])(node);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

std::uintptr_t get_model(std::uintptr_t skel) noexcept {
    std::uintptr_t h1 = 0, h2 = 0;
    if (!safe_read(skel + k_skel_model_handle, h1) ||
        !safe_read(skel + k_skel_proc_handle, h2) || h1 == 0 || h1 != h2) {
        return 0;
    }
    std::uintptr_t model = 0;
    safe_read(h1, model);
    return model;
}

struct utl_vec {
    int size;
    hitbox_descriptor* elements;
    unsigned int capacity;
    int flags;
};

struct hitbox_set_raw {
    char pad[40];
    std::int32_t count;
    char pad2[4];
    raw_hitbox* hitboxes;
};

struct render_mesh_raw {
    char pad[360];
    hitbox_set_raw* sets;
    std::int32_t set_count;
};

const hitbox_transform_record* read_latest_record_seh(std::uintptr_t pawn,
                                                      std::uintptr_t history_offset) noexcept {
    __try {
        const auto* hist =
            reinterpret_cast<const hitbox_transform_history*>(pawn + history_offset);
        return hist->latest();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool query_hitbox_list_seh(std::uintptr_t get_set_name, std::uintptr_t hash_name,
                           std::uintptr_t get_list, std::uintptr_t skel, std::uintptr_t model,
                           std::uintptr_t ctx, hitbox_descriptor* descs, int max_count,
                           int& out_count) noexcept {
    out_count = 0;
    __try {
        utl_vec vec{0, descs, static_cast<unsigned int>(max_count), static_cast<int>(0x80000000u)};
        using set_name_fn = const char* (*)(std::uintptr_t);
        using hash_fn = std::uint32_t (*)(const char*, std::uint32_t);
        using list_fn = void (*)(std::uintptr_t, utl_vec*, std::uintptr_t, std::uint32_t);

        const char* set_name = reinterpret_cast<set_name_fn>(get_set_name)(skel);
        const std::uint32_t hash =
            (set_name && *set_name) ? reinterpret_cast<hash_fn>(hash_name)(set_name, 0x31415926u) : 0u;
        reinterpret_cast<list_fn>(get_list)(model, &vec, ctx, hash);
        if (vec.size <= 0 || vec.elements != descs) return false;
        out_count = std::min(vec.size, max_count);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out_count = 0;
        return false;
    }
}

bool read_ref_mesh_set_seh(std::uintptr_t model, std::uintptr_t skel, const raw_hitbox*& out_hitboxes,
                           int& out_hb_count, const matrix2x4*& out_bones,
                           int& out_bone_count) noexcept {
    out_hitboxes = nullptr;
    out_hb_count = 0;
    out_bones = nullptr;
    out_bone_count = 0;
    __try {
        const auto mesh_count = *reinterpret_cast<const int*>(model + 0x70);
        const auto* const* meshes = *reinterpret_cast<render_mesh_raw* const* const*>(model + 0x78);
        const matrix2x4* bone_cache = *reinterpret_cast<const matrix2x4* const*>(skel + k_skel_bone_cache);
        const auto alloc_flags = *reinterpret_cast<const std::uint32_t*>(skel + k_skel_transform_alloc);
        const auto raw_count = *reinterpret_cast<const std::uint32_t*>(skel + k_skel_transform_count);
        const std::uint32_t capacity = alloc_flags & 0x3FFFFFFFu;
        if (mesh_count > 0 && meshes && meshes[0] && meshes[0]->set_count > 0 &&
            meshes[0]->sets && bone_cache && raw_count > 0 && raw_count <= capacity &&
            raw_count <= 256u) {
            const auto& set = meshes[0]->sets[0];
            out_hitboxes = set.hitboxes;
            out_hb_count = std::clamp(set.count, 0, 64);
            out_bones = bone_cache;
            out_bone_count = static_cast<int>(raw_count);
            return out_hitboxes != nullptr && out_hb_count > 0;
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out_hitboxes = nullptr;
        out_hb_count = 0;
        out_bones = nullptr;
        out_bone_count = 0;
        return false;
    }
}

bool read_hitbox_entry_seh(const raw_hitbox* hb, math::vec3& mins, math::vec3& maxs, float& radius,
                           int& shape_type, int& hitgroup, std::uint16_t& bone_index,
                           bool& is_foot) noexcept {
    if (!hb) return false;
    __try {
        mins = hb->mins;
        maxs = hb->maxs;
        radius = hb->shape_radius;
        shape_type = hb->shape_type;
        hitgroup = hb->hitgroup;
        bone_index = hb->bone_index;
        const char* n1 = hb->name;
        const char* n2 = hb->bone_name;
        auto has_kw = [](const char* s) {
            if (!s) return false;
            for (int i = 0; s[i] && i < 64; ++i) {
                const char c0 = static_cast<char>(s[i] | 0x20);
                if (c0 == 'f' && (s[i + 1] | 0x20) == 'o' && (s[i + 2] | 0x20) == 'o' &&
                    (s[i + 3] | 0x20) == 't') return true;
                if (c0 == 'a' && (s[i + 1] | 0x20) == 'n' && (s[i + 2] | 0x20) == 'k' &&
                    (s[i + 3] | 0x20) == 'l' && (s[i + 4] | 0x20) == 'e') return true;
                if (c0 == 't' && (s[i + 1] | 0x20) == 'o' && (s[i + 2] | 0x20) == 'e') return true;
            }
            return false;
        };
        is_foot = has_kw(n1) || has_kw(n2);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

bool read_pawn_hitboxes(const anchors& a, const netvars& nv, std::uintptr_t pawn,
                        live_hitbox_set& out) noexcept {
    out.valid = false;
    out.geometries.clear();
    out.transforms.clear();

    const std::uintptr_t skel = get_skeleton(nv, pawn);
    if (skel == 0) return false;

    const std::uintptr_t model = get_model(skel);
    if (model == 0) return false;

    if (nv.ent_sim_time != 0) safe_read(pawn + nv.ent_sim_time, out.simulation_time);
    if (std::isfinite(out.simulation_time) && out.simulation_time > 0.0f &&
        out.simulation_time < 30000000.0f) {
        out.simulation_tick = static_cast<int>(out.simulation_time * 64.0f + 0.5f);
    } else if (nv.ent_sim_tick != 0) {
        safe_read(pawn + nv.ent_sim_tick, out.simulation_tick);
    }

    // 1. Try CHitboxTransformHistory + GetHitboxList (exact CS2 animation hitboxes).
    const std::uintptr_t hist_off = resolve_hitbox_history_offset_seh(a.update_hitbox_history);
    const hitbox_transform_record* rec = read_latest_record_seh(pawn, hist_off);
    if (rec && rec->transforms && rec->count > 0 && rec->count <= 64 &&
        a.get_hitbox_set_name != 0 && a.hash_hitbox_name != 0 && a.get_hitbox_list != 0) {
        hitbox_descriptor descs[64]{};
        std::uintptr_t ctx = 0;
        safe_read(skel + k_skel_hitbox_context, ctx);
        int list_count = 0;
        if (query_hitbox_list_seh(a.get_hitbox_set_name, a.hash_hitbox_name, a.get_hitbox_list,
                                  skel, model, ctx, descs, rec->count, list_count) &&
            list_count == rec->count) {
            out.transforms.resize(static_cast<std::size_t>(rec->count));
            for (int i = 0; i < rec->count; ++i) {
                safe_read(reinterpret_cast<std::uintptr_t>(&rec->transforms[i]),
                          out.transforms[static_cast<std::size_t>(i)]);
            }
            out.geometries.reserve(static_cast<std::size_t>(rec->count));
            for (int i = 0; i < rec->count; ++i) {
                math::vec3 mins{}, maxs{};
                float radius = 0.0f;
                int shape_type = 0, hitgroup = 0;
                std::uint16_t bone_idx = 0;
                bool foot = false;
                if (!read_hitbox_entry_seh(descs[i].hitbox, mins, maxs, radius, shape_type, hitgroup,
                                           bone_idx, foot)) {
                    continue;
                }
                const auto& raw_tm = out.transforms[static_cast<std::size_t>(i)];
                float packed[8]{raw_tm.row0[0], raw_tm.row0[1], raw_tm.row0[2], raw_tm.row0[3],
                                raw_tm.row1[0], raw_tm.row1[1], raw_tm.row1[2], raw_tm.row1[3]};
                const auto tm = math::matrix_from_2x4(packed);
                auto geom = math::build_hitbox_geometry(mins, maxs, radius, shape_type, hitgroup,
                                                        foot, tm, raw_tm.row0[3]);
                if (geom.valid) out.geometries.push_back(geom);
            }
            if (!out.geometries.empty()) {
                out.valid = true;
                return true;
            }
            out.transforms.clear();
        }
    }

    // 2. Fallback: CModel ref_meshes[0]->hitbox_sets[0] + CSkeletonInstance::bone_cache.
    const raw_hitbox* hitboxes = nullptr;
    const matrix2x4* bone_cache = nullptr;
    int hb_count = 0, transform_count = 0;
    if (read_ref_mesh_set_seh(model, skel, hitboxes, hb_count, bone_cache, transform_count)) {
        out.transforms.resize(static_cast<std::size_t>(hb_count));
        out.geometries.reserve(static_cast<std::size_t>(hb_count));
        for (int i = 0; i < hb_count; ++i) {
            math::vec3 mins{}, maxs{};
            float radius = 0.0f;
            int shape_type = 0, hitgroup = 0;
            std::uint16_t bone_idx = 0;
            bool foot = false;
            if (!read_hitbox_entry_seh(&hitboxes[i], mins, maxs, radius, shape_type, hitgroup,
                                       bone_idx, foot) ||
                bone_idx >= static_cast<std::uint16_t>(transform_count)) {
                out.geometries.clear();
                out.transforms.clear();
                out.valid = false;
                return false;
            }
            matrix2x4 raw_tm{};
            if (!safe_read(reinterpret_cast<std::uintptr_t>(&bone_cache[bone_idx]), raw_tm)) {
                out.geometries.clear();
                out.transforms.clear();
                out.valid = false;
                return false;
            }
            float packed[8]{raw_tm.row0[0], raw_tm.row0[1], raw_tm.row0[2], raw_tm.row0[3],
                            raw_tm.row1[0], raw_tm.row1[1], raw_tm.row1[2], raw_tm.row1[3]};
            const auto tm = math::matrix_from_2x4(packed);
            auto geom = math::build_hitbox_geometry(mins, maxs, radius, shape_type, hitgroup, foot,
                                                    tm, raw_tm.row0[3]);
            if (!geom.valid) {
                out.geometries.clear();
                out.transforms.clear();
                out.valid = false;
                return false;
            }
            out.transforms[static_cast<std::size_t>(i)] = raw_tm;
            out.geometries.push_back(geom);
        }
        out.valid = !out.geometries.empty() &&
                    out.geometries.size() == out.transforms.size() &&
                    out.geometries.size() == static_cast<std::size_t>(hb_count);
        if (!out.valid) {
            out.geometries.clear();
            out.transforms.clear();
        }
        return out.valid;
    }

    return false;
}

hitbox_transform_scope::hitbox_transform_scope(const netvars& nv, std::uintptr_t pawn,
                                               const std::vector<matrix2x4>& transforms) noexcept {
    if (transforms.empty()) return;
    skeleton_ = get_skeleton(nv, pawn);
    if (skeleton_ == 0) return;
    __try {
        auto** slot = reinterpret_cast<void**>(skeleton_ + k_skel_transform_override);
        previous_ = *slot;
        list_.count = static_cast<int>(transforms.size());
        list_.capacity = list_.count;
        list_.transforms = transforms.data();
        *slot = &list_;
        active_ = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        skeleton_ = 0;
        active_ = false;
    }
}

hitbox_transform_scope::~hitbox_transform_scope() noexcept {
    if (!active_ || skeleton_ == 0) return;
    __try {
        *reinterpret_cast<void**>(skeleton_ + k_skel_transform_override) = previous_;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

}  // namespace shogod::sdk
