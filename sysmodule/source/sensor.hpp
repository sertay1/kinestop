#pragma once

#include <switch.h>
#include "../../common/math_types.hpp"

namespace kinestop {

class SensorManager {
public:
    SensorManager();
    ~SensorManager();

    bool init();
    void exit();

    /**
     * @brief Poll the active IMU sensor and retrieve acceleration & angular velocity.
     * @param out_accel Output acceleration vector (in G's)
     * @param out_gyro  Output angular velocity vector (in deg/s)
     * @return true if valid IMU data was sampled, false otherwise.
     */
    bool sample(Vec3f& out_accel, Vec3f& out_gyro);

private:
    bool m_initialized;
    bool m_hidsys_initialized;
    PadState m_pad;
    HidSixAxisSensorHandle m_handles[16];
    bool m_sensor_started[16];
    int m_num_handles;

    void refresh_handles();
};

} // namespace kinestop
