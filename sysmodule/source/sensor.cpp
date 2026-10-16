// Kinestop sysmodule - IMU access through HID six-axis sensors.
//
// Original Kinestop by SertAy (https://github.com/sertay1/Kinestop). Reworked
// for the sysmodule/overlay split (a modification of the original project).

#include "sensor.hpp"
#include <cmath>
#include <cstring>

namespace kinestop {

namespace {

struct HandleSpec {
    HidNpadIdType id;
    HidNpadStyleTag style;
    int count;
};

// Priority order: console IMU first, then player 1's controllers.
constexpr HandleSpec SPECS[] = {
    {HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld, 1},
    {HidNpadIdType_No1,      HidNpadStyleTag_NpadFullKey,  1},
    {HidNpadIdType_No1,      HidNpadStyleTag_NpadJoyDual,  2},  // left, right
    {HidNpadIdType_No1,      HidNpadStyleTag_NpadJoyLeft,  1},
    {HidNpadIdType_No1,      HidNpadStyleTag_NpadJoyRight, 1},
};

float magnitude(const HidVector& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

} // namespace

SensorManager::SensorManager()
    : m_initialized(false)
{
    std::memset(m_handles, 0, sizeof(m_handles));
    std::memset(m_valid, 0, sizeof(m_valid));
    std::memset(m_started, 0, sizeof(m_started));
}

Result SensorManager::init() {
    if (m_initialized) return 0;

    Result first_rc = 0;
    auto note = [&first_rc](Result rc) {
        if (R_FAILED(rc) && R_SUCCEEDED(first_rc)) first_rc = rc;
    };

    // Same npad setup libtesla does from inside nx-ovlloader (also an
    // AppletType_None process): player 1 + handheld, standard styles. Done by
    // hand rather than padConfigureInput(), which aborts on failure.
    hidInitializeNpad();
    const HidNpadIdType ids[] = {HidNpadIdType_No1, HidNpadIdType_Handheld};
    note(hidSetSupportedNpadIdType(ids, 2));
    note(hidSetSupportedNpadStyleSet(HidNpadStyleSet_NpadStandard | HidNpadStyleTag_NpadSystemExt));

    int n = 0;
    for (const HandleSpec& spec : SPECS) {
        Result rc = hidGetSixAxisSensorHandles(&m_handles[n], spec.count, spec.id, spec.style);
        note(rc);
        for (int i = 0; i < spec.count; ++i) m_valid[n + i] = R_SUCCEEDED(rc);
        n += spec.count;
    }

    bool any_started = false;
    for (int i = 0; i < MAX_HANDLES; ++i) {
        if (!m_valid[i]) continue;
        Result rc = hidStartSixAxisSensor(m_handles[i]);
        m_started[i] = R_SUCCEEDED(rc);
        any_started |= m_started[i];
        note(rc);
    }

    m_initialized = true;
    // A controller that is simply not connected is not an error worth
    // reporting; only complain when no sensor at all could be started.
    return any_started ? 0 : (R_FAILED(first_rc) ? first_rc : MAKERESULT(Module_Libnx, LibnxError_NotFound));
}

void SensorManager::retry_start() {
    if (!m_initialized) return;
    for (int i = 0; i < MAX_HANDLES; ++i) {
        if (m_valid[i] && !m_started[i])
            m_started[i] = R_SUCCEEDED(hidStartSixAxisSensor(m_handles[i]));
    }
}

void SensorManager::exit() {
    if (!m_initialized) return;

    for (int i = 0; i < MAX_HANDLES; ++i) {
        if (m_started[i]) {
            hidStopSixAxisSensor(m_handles[i]);
            m_started[i] = false;
        }
    }
    m_initialized = false;
}

bool SensorManager::sample(Vec3f& out_accel, Vec3f& out_gyro) {
    if (!m_initialized) return false;

    // Pass 0 only accepts devices HID reports as connected, so a stale
    // handheld sample cannot win once the console is docked. Pass 1 falls back
    // to the original overlay's rule (any non-zero gravity vector).
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < MAX_HANDLES; ++i) {
            if (!m_started[i]) continue;

            HidSixAxisSensorState state;
            std::memset(&state, 0, sizeof(state));
            if (hidGetSixAxisSensorStates(m_handles[i], &state, 1) == 0) continue;

            if (pass == 0 && !(state.attributes & HidSixAxisSensorAttribute_IsConnected)) continue;
            if (magnitude(state.acceleration) <= 0.05f) continue;

            out_accel = Vec3f(state.acceleration.x, state.acceleration.y, state.acceleration.z);
            out_gyro  = Vec3f(state.angular_velocity.x, state.angular_velocity.y, state.angular_velocity.z);
            return true;
        }
    }

    return false;
}

} // namespace kinestop
