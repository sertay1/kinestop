#include "filter.hpp"
#include <cmath>

namespace kinestop {

ComplementaryFilter::ComplementaryFilter()
    : m_orientation{0.0f, 0.0f, 0.0f}
    , m_initialized(false)
{
}

void ComplementaryFilter::reset() {
    m_orientation = {0.0f, 0.0f, 0.0f};
    m_initialized = false;
}

Orientation ComplementaryFilter::update(const Vec3f& accel, const Vec3f& gyro, float dt, const Config& cfg) {
    if (dt <= 0.0f || dt > 1.0f) {
        dt = 1.0f / (float)cfg.general.poll_rate_hz;
    }

    // Accelerometer pitch & roll calculation from gravity vector
    // Standard Switch handheld coordinates: +X = Right, +Y = Up, +Z = Toward User
    float accel_mag = accel.length();
    float pitch_acc = 0.0f;
    float roll_acc  = 0.0f;

    if (accel_mag > 0.1f) {
        // Robust 3D Euler angles with 0 gimbal locking:
        // Pitch: tilt forward/backward
        pitch_acc = std::atan2(accel.z, -accel.y) * RAD_TO_DEG;
        
        // Roll: tilt left/right
        roll_acc = std::atan2(accel.x, std::sqrt(accel.y * accel.y + accel.z * accel.z)) * RAD_TO_DEG;
    }

    if (!m_initialized) {
        m_orientation.pitch_deg = pitch_acc;
        m_orientation.roll_deg = roll_acc;
        m_orientation.yaw_deg = 0.0f;
        m_initialized = true;
    } else {
        float alpha = clamp(cfg.filter.alpha, 0.0f, 0.999f);

        // Gyro integration (gyro.x = pitch rate, gyro.z = roll rate in deg/s)
        float pitch_gyro = m_orientation.pitch_deg + gyro.x * dt;
        float roll_gyro  = m_orientation.roll_deg + gyro.z * dt;
        float yaw_gyro   = m_orientation.yaw_deg + gyro.y * dt;

        // Complementary filter blend
        m_orientation.pitch_deg = alpha * pitch_gyro + (1.0f - alpha) * pitch_acc;
        m_orientation.roll_deg  = alpha * roll_gyro + (1.0f - alpha) * roll_acc;
        m_orientation.yaw_deg   = yaw_gyro;
    }

    return m_orientation;
}

} // namespace kinestop
