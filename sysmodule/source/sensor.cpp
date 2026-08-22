#include "sensor.hpp"
#include <cstring>
#include <cmath>

namespace kinestop {

SensorManager::SensorManager()
    : m_initialized(false)
    , m_hidsys_initialized(false)
    , m_num_handles(0)
{
    std::memset(m_handles, 0, sizeof(m_handles));
    std::memset(m_sensor_started, 0, sizeof(m_sensor_started));
}

SensorManager::~SensorManager() {
    exit();
}

bool SensorManager::init() {
    if (m_initialized) return true;

    // 1. Initialize SevenSixAxisSensor (Console Hardware IMU DSP)
    hidInitializeSevenSixAxisSensor();
    hidStartSevenSixAxisSensor();
    hidSetSevenSixAxisSensorFusionStrength(1.0f);
    hidResetSevenSixAxisSensorTimestamp();

    // 2. Configure Npad Input Styles
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeAny(&m_pad);

    refresh_handles();

    m_initialized = true;
    return true;
}

void SensorManager::refresh_handles() {
    for (int i = 0; i < m_num_handles; ++i) {
        if (m_sensor_started[i]) {
            hidStopSixAxisSensor(m_handles[i]);
            m_sensor_started[i] = false;
        }
    }

    m_num_handles = 0;
    std::memset(m_handles, 0, sizeof(m_handles));
    std::memset(m_sensor_started, 0, sizeof(m_sensor_started));

    Result rc;

    // 1. Handheld Console (1 handle)
    rc = hidGetSixAxisSensorHandles(&m_handles[m_num_handles], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
    if (R_SUCCEEDED(rc)) {
        m_num_handles += 1;
    }

    // 2. Pro Controller (Player 1) (1 handle)
    rc = hidGetSixAxisSensorHandles(&m_handles[m_num_handles], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey);
    if (R_SUCCEEDED(rc)) {
        m_num_handles += 1;
    }

    // 3. Joy-Con Dual (Player 1) (2 handles: Left & Right)
    rc = hidGetSixAxisSensorHandles(&m_handles[m_num_handles], 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual);
    if (R_SUCCEEDED(rc)) {
        m_num_handles += 2;
    }

    // 4. Joy-Con Left Solo (Player 1) (1 handle)
    rc = hidGetSixAxisSensorHandles(&m_handles[m_num_handles], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyLeft);
    if (R_SUCCEEDED(rc)) {
        m_num_handles += 1;
    }

    // 5. Joy-Con Right Solo (Player 1) (1 handle)
    rc = hidGetSixAxisSensorHandles(&m_handles[m_num_handles], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyRight);
    if (R_SUCCEEDED(rc)) {
        m_num_handles += 1;
    }

    // 6. Pro Controller (Player 2) (1 handle)
    rc = hidGetSixAxisSensorHandles(&m_handles[m_num_handles], 1, HidNpadIdType_No2, HidNpadStyleTag_NpadFullKey);
    if (R_SUCCEEDED(rc)) {
        m_num_handles += 1;
    }

    // 7. Joy-Con Dual (Player 2) (2 handles)
    rc = hidGetSixAxisSensorHandles(&m_handles[m_num_handles], 2, HidNpadIdType_No2, HidNpadStyleTag_NpadJoyDual);
    if (R_SUCCEEDED(rc)) {
        m_num_handles += 2;
    }

    // Start all registered sensor handles
    for (int i = 0; i < m_num_handles; ++i) {
        if (m_handles[i].type_value != 0) {
            rc = hidStartSixAxisSensor(m_handles[i]);
            if (R_SUCCEEDED(rc)) {
                m_sensor_started[i] = true;
            }
        }
    }
}

void SensorManager::exit() {
    if (!m_initialized) return;

    for (int i = 0; i < m_num_handles; ++i) {
        if (m_sensor_started[i]) {
            hidStopSixAxisSensor(m_handles[i]);
            m_sensor_started[i] = false;
        }
    }

    hidStopSevenSixAxisSensor();
    hidFinalizeSevenSixAxisSensor();

    m_initialized = false;
}

bool SensorManager::sample(Vec3f& out_accel, Vec3f& out_gyro) {
    if (!m_initialized) {
        if (!init()) return false;
    }

    // 1. Primary: Check Console Hardware SevenSixAxisSensor (Continuous Hardware IMU)
    {
        HidSevenSixAxisSensorState sevenState = {0};
        size_t total_out = 0;
        Result rc = hidGetSevenSixAxisSensorStates(&sevenState, 1, &total_out);
        if (R_SUCCEEDED(rc) && total_out > 0) {
            float ax = sevenState.unk_x18[0];
            float ay = sevenState.unk_x18[1];
            float az = sevenState.unk_x18[2];
            float gx = sevenState.unk_x18[3];
            float gy = sevenState.unk_x18[4];
            float gz = sevenState.unk_x18[5];

            float mag = std::sqrt(ax * ax + ay * ay + az * az);
            if (mag > 0.05f) {
                // If reported in m/s^2 (~9.8G), normalize to standard G's
                if (mag > 4.5f) {
                    ax /= 9.80665f;
                    ay /= 9.80665f;
                    az /= 9.80665f;
                }
                out_accel = Vec3f(ax, ay, az);
                out_gyro  = Vec3f(gx, gy, gz);
                return true;
            }
        }
    }

    // 2. Secondary: Check Npad SixAxis Controller Handles
    padUpdate(&m_pad);

    for (int i = 0; i < m_num_handles; ++i) {
        if (m_handles[i].type_value == 0) continue;

        if (!m_sensor_started[i]) {
            Result rc = hidStartSixAxisSensor(m_handles[i]);
            if (R_SUCCEEDED(rc)) m_sensor_started[i] = true;
        }

        HidSixAxisSensorState state = {0};
        size_t count = hidGetSixAxisSensorStates(m_handles[i], &state, 1);
        if (count > 0) {
            float mag = std::sqrt(state.acceleration.x * state.acceleration.x +
                                  state.acceleration.y * state.acceleration.y +
                                  state.acceleration.z * state.acceleration.z);
            if (mag > 0.05f) {
                out_accel = Vec3f(state.acceleration.x, state.acceleration.y, state.acceleration.z);
                out_gyro  = Vec3f(state.angular_velocity.x, state.angular_velocity.y, state.angular_velocity.z);
                return true;
            }
        }
    }

    return false;
}

} // namespace kinestop
