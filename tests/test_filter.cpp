// Kinestop host tests - overlay/source/filter.cpp: Smoothing is a weight per
// 1/60 s, scaled to the real gap between samples, so a 10 Hz sensor converges
// in the same time as a 60 Hz one.
//
// Part of a modified version of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop).

#include <cmath>

#include "check.hpp"
#include "config.hpp"
#include "filter.hpp"

using kinestop::ComplementaryFilter;
using kinestop::Config;
using kinestop::Vec3f;

namespace {

// Gravity for a handheld pitched by `deg` (pitch_acc = atan2(z, -y)).
Vec3f gravity_at_pitch(float deg) {
    const float r = deg * kinestop::DEG_TO_RAD;
    return Vec3f(0.0f, -std::cos(r), std::sin(r));
}

// Starts level, then holds `target` pitch for `seconds` sampled every `dt`.
float pitch_after(float target, float seconds, float dt, const Config& cfg) {
    ComplementaryFilter f;
    f.update(gravity_at_pitch(0.0f), Vec3f(), dt, cfg);
    float pitch = 0.0f;
    const int steps = (int)std::lround(seconds / dt);
    for (int i = 0; i < steps; ++i)
        pitch = f.update(gravity_at_pitch(target), Vec3f(), dt, cfg).pitch_deg;
    return pitch;
}

void test_reference_rate_unchanged() {
    // At 1/60 s per sample the weight is exactly Smoothing, as in the original.
    Config cfg;
    cfg.filter.alpha = 0.85f;
    ComplementaryFilter f;
    f.update(gravity_at_pitch(0.0f), Vec3f(), 1.0f / 60.0f, cfg);
    const float p = f.update(gravity_at_pitch(30.0f), Vec3f(), 1.0f / 60.0f, cfg).pitch_deg;
    CHECK_NEAR(p, 0.15f * 30.0f, 1e-3);
}

void test_rate_independent() {
    for (float alpha : {0.50f, 0.75f, 0.85f, 0.92f, 0.97f}) {
        Config cfg;
        cfg.filter.alpha = alpha;
        for (float t : {0.1f, 0.3f, 0.6f, 1.2f}) {
            const float at60  = pitch_after(30.0f, t, 1.0f / 60.0f, cfg);
            const float at10  = pitch_after(30.0f, t, 1.0f / 10.0f, cfg);
            const float at200 = pitch_after(30.0f, t, 1.0f / 200.0f, cfg);
            CHECK_NEAR(at10, at60, 0.05);
            CHECK_NEAR(at200, at60, 0.05);
        }
    }
}

} // namespace

int main() {
    test_reference_rate_unchanged();
    test_rate_independent();
    return check::summary("filter");
}
