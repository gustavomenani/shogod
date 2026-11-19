#pragma once

// Minimal 3D vector used everywhere the cheat touches world space.

#include <cmath>

namespace shogod::math {

struct vec3 {
    float x{}, y{}, z{};

    constexpr vec3() = default;
    constexpr vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr vec3 operator+(const vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr vec3 operator-(const vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    constexpr vec3& operator+=(const vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    constexpr vec3& operator-=(const vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }

    constexpr float dot(const vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    constexpr vec3 cross(const vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    constexpr float length_sq() const { return dot(*this); }
    float length() const { return std::sqrt(length_sq()); }

    bool finite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }

    vec3 normalised() const {
        const float len = length();
        if (len <= 1e-6f) return {0.0f, 0.0f, 0.0f};
        return *this / len;
    }
};

constexpr vec3 operator*(float s, const vec3& v) { return v * s; }

}  // namespace shogod::math
