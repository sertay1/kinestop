#pragma once

#include "../../common/math_types.hpp"
#include "../../common/config.hpp"

namespace kinestop {

class ComplementaryFilter {
public:
    ComplementaryFilter();

    void reset();

    /**
     * @brief Update filter with IMU sensor readings.
     * @param accel Accelerometer data in G's (x: right, y: up, z: toward user)
     * @param gyro  Gyroscope data in deg/s (x: pitch rate, y: yaw rate, z: roll rate)
     * @param dt    Delta time in seconds (e.g. 1.0f / 60.0f)
     * @param cfg   Filter and calibration configuration
     * @return Filtered orientation (pitch, roll, yaw in degrees)
     */
    Orientation update(const Vec3f& accel, const Vec3f& gyro, float dt, const Config& cfg);

    /**
     * @brief Gets current un-offset orientation (useful for calibration snapshots).
     */
    Orientation get_raw_filtered_orientation() const {
        return m_orientation;
    }

private:
    Orientation m_orientation;
    bool m_initialized;
};

} // namespace kinestop
