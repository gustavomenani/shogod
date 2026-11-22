#pragma once

// Live CS2 hitbox & transform reader backed by CSkeletonInstance / CModel and
// CHitboxTransformHistory, plus scoped override for lag-compensated traces.

#include "features/math/hitbox_geom.hpp"
#include "sdk/schema.hpp"
#include "sdk/signatures.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace shogod::sdk {

struct matrix2x4 {
    float row0[4];
    float row1[4];
};
static_assert(sizeof(matrix2x4) == 0x20);

struct hitbox_transform_record {
    int tick;
    char pad0[12];
    int count;
    char pad1[12];
    matrix2x4* transforms;
    int capacity;
    std::uint32_t flags;
    matrix2x4 inline_transforms[32];
};
static_assert(sizeof(hitbox_transform_record) == 0x430);

struct hitbox_transform_history {
    void* vtable;
    char pad0[8];
    hitbox_transform_record records[4];
    int first;
    int count;

    const hitbox_transform_record* latest() const noexcept {
        if (first < 0 || first >= 4 || count <= 0 || count > 4) return nullptr;
        return &records[(first + count - 1) % 4];
    }
};
static_assert(sizeof(hitbox_transform_history) == 0x10D8);

struct alignas(16) hitbox_transform_list {
    int count = 0;
    char pad0[12]{};
    const matrix2x4* transforms = nullptr;
    int capacity = 0;
    std::uint32_t flags = 0xC0000000u;
};
static_assert(sizeof(hitbox_transform_list) == 0x20);

struct raw_hitbox {
    const char* name;
    const char* surface_property;
    const char* bone_name;
    math::vec3 mins;
    math::vec3 maxs;
    float shape_radius;
    std::uint32_t bone_hash;
    std::int32_t hitgroup;
    std::uint8_t shape_type;
    bool translation_only;
    std::uint32_t crc;
    std::uint8_t render_color[4];
    std::uint16_t bone_index;
    bool is_forced_transform;
    char pad0[5];
    float forced_transform[8];
};
static_assert(sizeof(raw_hitbox) == 0x70);

struct hitbox_descriptor {
    raw_hitbox* hitbox;
    int mesh_index;
    int padding;
};
static_assert(sizeof(hitbox_descriptor) == 0x10);

struct live_hitbox_set {
    std::vector<math::hitbox_geometry> geometries;
    std::vector<matrix2x4> transforms;
    int simulation_tick = 0;
    float simulation_time = 0.0f;
    bool valid = false;
};

// Reads current hitboxes & transforms from `pawn` (using CHitboxTransformHistory
// if available, or falling back to CModel + CSkeletonInstance::bone_cache).
bool read_pawn_hitboxes(const anchors& a, const netvars& nv, std::uintptr_t pawn,
                        live_hitbox_set& out) noexcept;

// RAII override on CSkeletonInstance +0x440 so ClipTraceToPlayer evaluates
// against a historical lag record's transforms.
class hitbox_transform_scope {
public:
    hitbox_transform_scope(const netvars& nv, std::uintptr_t pawn,
                           const std::vector<matrix2x4>& transforms) noexcept;
    ~hitbox_transform_scope() noexcept;

    hitbox_transform_scope(const hitbox_transform_scope&) = delete;
    hitbox_transform_scope& operator=(const hitbox_transform_scope&) = delete;

    explicit operator bool() const noexcept { return active_; }

private:
    std::uintptr_t skeleton_ = 0;
    void* previous_ = nullptr;
    hitbox_transform_list list_{};
    bool active_ = false;
};

}  // namespace shogod::sdk
