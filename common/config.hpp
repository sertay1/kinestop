#pragma once

#include <cstdint>

namespace kinestop {

struct GeneralConfig {
    int poll_rate_hz = 60;
    int language = 0;           // 0: English, 1: Türkçe
    bool console_sensor = false; // also open the console IMU (SevenSixAxis, 512 KiB): dots move at the HOME menu
};

struct VisualConfig {
    int style = 0;              // Placement Pattern (0: Perimeter, 1: Full Grid, 2: Sides, 3: Corners, 4: Top/Bottom, 5: Surround)
    int color = 0;              // Color Theme Preset (0..11)
    int density = 1;            // Dot Density (0: Low, 1: Medium, 2: High, 3: Ultra)
    float opacity = 0.80f;      // 0.10f to 1.0f
    int line_thickness = 3;     // Dot Size index: 0..9 (1 to 12 pixels), see dots::SIZES
    bool free_floating = true;  // true: Infinite natural horizon float, false: Clamp to screen edges
    bool show_in_captures = false; // also draw the dots into screenshots / video captures
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
    bool auto_calibrate = true;      // the HUD takes its first sample as the zero (ini key "auto_calibrate")
    float sensitivity_pitch = 1.0f;  // 0.2f to 6.0f
    float sensitivity_roll = 1.0f;   // 0.2f to 6.0f
};

struct Config {
    GeneralConfig general;
    VisualConfig visual;
    FilterConfig filter;
    CalibrationConfig calibration;

    static constexpr const char* DEFAULT_DIR  = "sdmc:/config/kinestop";
    static constexpr const char* DEFAULT_PATH = "sdmc:/config/kinestop/config.ini";

    // stdio-based (no iostreams: keeps the overlay small).
    // load() starts from defaults, so keys missing from the file keep their default.
    bool load(const char* filepath = DEFAULT_PATH);
    bool save(const char* filepath = DEFAULT_PATH) const;
    void set_defaults();

    // Clamp every field into its documented range.
    void sanitize();
};

} // namespace kinestop
