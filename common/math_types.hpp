#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace kinestop {

constexpr float PI = 3.14159265358979323846f;
constexpr float RAD_TO_DEG = 180.0f / PI;
constexpr float DEG_TO_RAD = PI / 180.0f;

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;

    Vec2f() = default;
    constexpr Vec2f(float x_, float y_) : x(x_), y(y_) {}

    Vec2f operator+(const Vec2f& o) const { return Vec2f(x + o.x, y + o.y); }
    Vec2f operator-(const Vec2f& o) const { return Vec2f(x - o.x, y - o.y); }
    Vec2f operator*(float s) const { return Vec2f(x * s, y * s); }
};

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3f() = default;
    constexpr Vec3f(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    float length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    Vec3f normalized() const {
        float len = length();
        if (len > 1e-6f) {
            return Vec3f(x / len, y / len, z / len);
        }
        return Vec3f(0.0f, 0.0f, 0.0f);
    }
};

struct Orientation {
    float pitch_deg = 0.0f; // Tilt up/down in degrees
    float roll_deg  = 0.0f; // Tilt left/right in degrees
    float yaw_deg   = 0.0f; // Heading in degrees (optional)
};

inline float clamp(float v, float min_val, float max_val) {
    return (v < min_val) ? min_val : ((v > max_val) ? max_val : v);
}

inline float apply_deadzone(float value, float deadzone) {
    if (std::abs(value) < deadzone) {
        return 0.0f;
    }
    if (value > 0.0f) {
        return value - deadzone;
    } else {
        return value + deadzone;
    }
}

} // namespace kinestop
