// Kinestop sysmodule - IMU access through HID six-axis sensors.
//
// Original Kinestop by SertAy (https://github.com/sertay1/Kinestop). Reworked
// for the sysmodule/overlay split (a modification of the original project):
// the SevenSixAxisSensor path is gone (it needs 0x80000 bytes of transfer
// memory from our heap) and nothing here can abort the process.

#pragma once

#include <switch.h>
#include "../../common/math_types.hpp"

namespace kinestop {

class SensorManager {
public:
    SensorManager();

    // hid must already be initialized (__appInit). Fails only when no sensor
    // could be started (returning the first failing Result); the manager stays
    // usable either way and retry_start() keeps trying.
    Result init();
    void exit();

    // Retries hidStartSixAxisSensor for handles that failed to start (e.g. a
    // controller that was not connected yet). Call about once per second.
    void retry_start();

    /**
     * @brief Read the newest IMU sample, preferring the console (handheld),
     *        then player 1's Pro Controller / Joy-Cons.
     * @param out_accel Output acceleration vector (in G's)
     * @param out_gyro  Output angular velocity vector, passed through as HID
     *                  reports it (the original overlay did the same).
     * @return true if valid IMU data was sampled, false otherwise.
     */
    bool sample(Vec3f& out_accel, Vec3f& out_gyro);

private:
    static constexpr int MAX_HANDLES = 6;

    bool m_initialized;
    HidSixAxisSensorHandle m_handles[MAX_HANDLES];
    bool m_valid[MAX_HANDLES];
    bool m_started[MAX_HANDLES];
};

} // namespace kinestop
