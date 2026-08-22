#pragma once

#include <string>
#include <cstdint>

namespace kinestop {

struct GeneralConfig {
    bool enabled = true;
    int poll_rate_hz = 60;
    int language = 0;           // 0: English, 1: Türkçe
};

struct VisualConfig {
    int style = 0;              // Placement Pattern (0: Perimeter, 1: Full Grid, 2: Sides, 3: Corners, 4: Top/Bottom, 5: Surround)
    int color = 0;              // Color Theme Preset (0..11)
    int density = 1;            // Dot Density (0: Low, 1: Medium, 2: High, 3: Ultra)
    float opacity = 0.80f;      // 0.10f to 1.0f
    int line_thickness = 3;     // Dot Size: 0..9 (1 to 12 pixels)
    bool free_floating = true;  // true: Infinite natural horizon float, false: Clamp to screen edges
    bool draw_center_reticle = false;
    bool draw_roll_indicators = false;
};

struct FilterConfig {
    float alpha = 0.85f;        // 0.50f to 0.97f (smoothing weight)
    float deadzone_deg = 0.5f;  // 0.0f to 5.0f (degrees)
};

struct CalibrationConfig {
    float pitch_offset = 0.0f;       // Zero reference offset (deg)
    float roll_offset = 0.0f;        // Zero reference offset (deg)
    bool invert_pitch = false;
    bool invert_roll = false;
    float sensitivity_pitch = 1.0f;  // 0.2f to 3.0f
    float sensitivity_roll = 1.0f;   // 0.2f to 3.0f
};

struct Config {
    GeneralConfig general;
    VisualConfig visual;
    FilterConfig filter;
    CalibrationConfig calibration;

    static const char* DEFAULT_PATH;

    bool load(const std::string& filepath = DEFAULT_PATH);
    bool save(const std::string& filepath = DEFAULT_PATH) const;
    void set_defaults();
};

} // namespace kinestop
