// Kinestop configuration (config.ini) load/save.
//
// Original Kinestop by SertAy (https://github.com/sertay1/Kinestop).
// This stdio rewrite of the original ifstream-based parser is part of a
// modified version of the original project: iostreams would cost the overlay
// far more memory than the parser itself, and it must fit Ultrahand's
// default 4 MB overlay heap.

#include "config.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define MKDIR(dir) _mkdir(dir)
#else
#include <unistd.h>
#define MKDIR(dir) mkdir(dir, 0777)
#endif

namespace kinestop {

namespace {

constexpr size_t LINE_MAX_LEN = 256;
constexpr size_t PATH_MAX_LEN = 512;

// In-place trim; returns the first non-space character.
char* trim(char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') ++s;
    size_t len = std::strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n'))
        s[--len] = '\0';
    return s;
}

void to_lower(char* s) {
    for (; *s; ++s) *s = (char)std::tolower((unsigned char)*s);
}

bool parse_bool(const char* val, bool default_val) {
    char buf[8];
    size_t n = std::strlen(val);
    if (n == 0 || n >= sizeof(buf)) return default_val;
    std::memcpy(buf, val, n + 1);
    to_lower(buf);
    if (!std::strcmp(buf, "true") || !std::strcmp(buf, "1") || !std::strcmp(buf, "yes") || !std::strcmp(buf, "on")) return true;
    if (!std::strcmp(buf, "false") || !std::strcmp(buf, "0") || !std::strcmp(buf, "no") || !std::strcmp(buf, "off")) return false;
    return default_val;
}

int parse_int(const char* val, int default_val) {
    if (!*val) return default_val;
    char* end = nullptr;
    long result = std::strtol(val, &end, 10);
    if (end == val) return default_val;
    if (result > 1000000L) result = 1000000L;   // keep the int cast well-defined;
    if (result < -1000000L) result = -1000000L; // sanitize() clamps further
    return (int)result;
}

float parse_float(const char* val, float default_val) {
    if (!*val) return default_val;
    char* end = nullptr;
    float result = std::strtof(val, &end);
    if (end == val || std::isnan(result)) return default_val;
    return result;
}

int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// NaN maps to `fallback` (normally the field's default).
float clampf(float v, float lo, float hi, float fallback) {
    if (std::isnan(v)) return fallback;
    return v < lo ? lo : (v > hi ? hi : v);
}

// Creates every parent directory of `filepath` (not the file itself).
void ensure_parent_dirs(const char* filepath) {
    char current[PATH_MAX_LEN];
    size_t len = std::strlen(filepath);
    if (len >= sizeof(current)) return;

    for (size_t i = 0; i < len; ++i) {
        char c = filepath[i];
        if ((c == '/' || c == '\\') && i > 0) {
            current[i] = '\0';
            // Skip the device root ("sdmc:") and empty components.
            if (current[i - 1] != ':' && current[i - 1] != '/' && current[i - 1] != '\\')
                MKDIR(current);
        }
        current[i] = c;
    }
}

// "sdmc:/foo" -> "/foo". Used as a fallback when the sdmc: device prefix is
// not understood (host builds).
const char* strip_device(const char* filepath) {
    return std::strncmp(filepath, "sdmc:", 5) == 0 ? filepath + 5 : nullptr;
}

const char* bool_str(bool b) { return b ? "true" : "false"; }

} // namespace

void Config::set_defaults() {
    general.poll_rate_hz = 60;
    general.language = 0; // 0: English, 1: Türkçe
    general.console_sensor = false;

    visual.style = 0;
    visual.color = 0;
    visual.density = 1;
    visual.opacity = 0.80f;
    visual.line_thickness = 3;
    visual.free_floating = true;
    visual.show_in_captures = false;

    filter.alpha = 0.85f;
    filter.deadzone_deg = 0.5f;

    calibration.pitch_offset = 0.0f;
    calibration.roll_offset = 0.0f;
    calibration.invert_pitch = false;
    calibration.invert_roll = false;
    calibration.auto_calibrate = true;
    calibration.sensitivity_pitch = 1.0f;
    calibration.sensitivity_roll = 1.0f;
    calibration.console_sensitivity_offset = 4;
}

void Config::sanitize() {
    general.poll_rate_hz = clampi(general.poll_rate_hz, 10, 120);
    general.language = clampi(general.language, 0, 1);

    // Unknown styles fall back to Perimeter (0), as the original
    // get_anchors() did with `default: case 0:`.
    if (visual.style < 0 || visual.style > 5) visual.style = 0;
    visual.color = clampi(visual.color, 0, 11);
    visual.density = clampi(visual.density, 0, 3);
    visual.opacity = clampf(visual.opacity, 0.10f, 1.0f, 0.80f);
    visual.line_thickness = clampi(visual.line_thickness, 0, 9);

    // The UI only offers 0.50..0.97 (dots::SMOOTHINGS); the filter itself is
    // well-defined for anything below 1.0, so hand-edited values are allowed.
    filter.alpha = clampf(filter.alpha, 0.0f, 0.999f, 0.85f);
    filter.deadzone_deg = clampf(filter.deadzone_deg, 0.0f, 5.0f, 0.5f);

    // Calibration offsets are angles from the filter: any finite value is valid.
    if (!std::isfinite(calibration.pitch_offset)) calibration.pitch_offset = 0.0f;
    if (!std::isfinite(calibration.roll_offset)) calibration.roll_offset = 0.0f;
    calibration.sensitivity_pitch = clampf(calibration.sensitivity_pitch, 0.2f, 6.0f, 1.0f);
    calibration.sensitivity_roll = clampf(calibration.sensitivity_roll, 0.2f, 6.0f, 1.0f);
    calibration.console_sensitivity_offset = clampi(calibration.console_sensitivity_offset, 0, 9);
}

bool Config::load(const char* filepath) {
    set_defaults();

    FILE* file = std::fopen(filepath, "r");
    if (!file) {
        const char* alt = strip_device(filepath);
        if (alt) file = std::fopen(alt, "r");
    }
    if (!file) return false;

    enum Section { None, General, Visual, Filter, Calibration } section = None;
    char raw[LINE_MAX_LEN];

    while (std::fgets(raw, sizeof(raw), file)) {
        // An over-long line arrives in several chunks; drop the remainder so
        // its tail is not parsed as a line of its own.
        size_t raw_len = std::strlen(raw);
        if (raw_len == sizeof(raw) - 1 && raw[raw_len - 1] != '\n') {
            int c;
            while ((c = std::fgetc(file)) != EOF && c != '\n') {}
        }

        char* line = trim(raw);
        if (!*line || *line == ';' || *line == '#') continue;

        size_t len = std::strlen(line);
        if (line[0] == '[' && line[len - 1] == ']') {
            line[len - 1] = '\0';
            char* name = trim(line + 1);
            to_lower(name);
            if (!std::strcmp(name, "general")) section = General;
            else if (!std::strcmp(name, "visual")) section = Visual;
            else if (!std::strcmp(name, "filter")) section = Filter;
            else if (!std::strcmp(name, "calibration")) section = Calibration;
            else section = None;
            continue;
        }

        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = trim(line);
        const char* val = trim(eq + 1);
        to_lower(key);

        // Unknown keys (including legacy ones such as draw_center_reticle,
        // draw_roll_indicators, the 2.0.0 test builds' debug_flags and the
        // original's general.enabled: the dots now start from the menu's
        // launch item) are ignored silently.
        switch (section) {
            case General:
                if (!std::strcmp(key, "poll_rate_hz")) general.poll_rate_hz = parse_int(val, general.poll_rate_hz);
                else if (!std::strcmp(key, "language")) general.language = parse_int(val, general.language);
                else if (!std::strcmp(key, "console_sensor")) general.console_sensor = parse_bool(val, general.console_sensor);
                break;
            case Visual:
                if (!std::strcmp(key, "style")) visual.style = parse_int(val, visual.style);
                else if (!std::strcmp(key, "color")) visual.color = parse_int(val, visual.color);
                else if (!std::strcmp(key, "density")) visual.density = parse_int(val, visual.density);
                else if (!std::strcmp(key, "opacity")) visual.opacity = parse_float(val, visual.opacity);
                else if (!std::strcmp(key, "line_thickness")) visual.line_thickness = parse_int(val, visual.line_thickness);
                else if (!std::strcmp(key, "free_floating")) visual.free_floating = parse_bool(val, visual.free_floating);
                else if (!std::strcmp(key, "show_in_captures")) visual.show_in_captures = parse_bool(val, visual.show_in_captures);
                break;
            case Filter:
                if (!std::strcmp(key, "alpha")) filter.alpha = parse_float(val, filter.alpha);
                else if (!std::strcmp(key, "deadzone_deg")) filter.deadzone_deg = parse_float(val, filter.deadzone_deg);
                break;
            case Calibration:
                if (!std::strcmp(key, "pitch_offset")) calibration.pitch_offset = parse_float(val, calibration.pitch_offset);
                else if (!std::strcmp(key, "roll_offset")) calibration.roll_offset = parse_float(val, calibration.roll_offset);
                else if (!std::strcmp(key, "invert_pitch")) calibration.invert_pitch = parse_bool(val, calibration.invert_pitch);
                else if (!std::strcmp(key, "invert_roll")) calibration.invert_roll = parse_bool(val, calibration.invert_roll);
                else if (!std::strcmp(key, "auto_calibrate")) calibration.auto_calibrate = parse_bool(val, calibration.auto_calibrate);
                else if (!std::strcmp(key, "sensitivity_pitch")) calibration.sensitivity_pitch = parse_float(val, calibration.sensitivity_pitch);
                else if (!std::strcmp(key, "sensitivity_roll")) calibration.sensitivity_roll = parse_float(val, calibration.sensitivity_roll);
                else if (!std::strcmp(key, "console_sensitivity_offset")) calibration.console_sensitivity_offset = parse_int(val, calibration.console_sensitivity_offset);
                break;
            case None:
                break;
        }
    }

    std::fclose(file);
    sanitize();
    return true;
}

bool Config::save(const char* filepath) const {
    ensure_parent_dirs(filepath);

    FILE* file = std::fopen(filepath, "w");
    if (!file) {
        const char* alt = strip_device(filepath);
        if (alt) {
            ensure_parent_dirs(alt);
            file = std::fopen(alt, "w");
        }
    }
    if (!file) return false;

    // %g matches the default ostream float formatting the original used.
    std::fprintf(file,
        "; ==========================================\n"
        "; Kinestop Configuration (by SertAy)\n"
        "; ==========================================\n\n");

    std::fprintf(file, "[general]\n");
    std::fprintf(file, "poll_rate_hz = %d\n", general.poll_rate_hz);
    std::fprintf(file, "language = %d\n", general.language);
    std::fprintf(file, "console_sensor = %s\n\n", bool_str(general.console_sensor));

    std::fprintf(file, "[visual]\n");
    std::fprintf(file, "style = %d\n", visual.style);
    std::fprintf(file, "color = %d\n", visual.color);
    std::fprintf(file, "density = %d\n", visual.density);
    std::fprintf(file, "opacity = %g\n", (double)visual.opacity);
    std::fprintf(file, "line_thickness = %d\n", visual.line_thickness);
    std::fprintf(file, "free_floating = %s\n", bool_str(visual.free_floating));
    std::fprintf(file, "show_in_captures = %s\n\n", bool_str(visual.show_in_captures));

    std::fprintf(file, "[filter]\n");
    std::fprintf(file, "alpha = %g\n", (double)filter.alpha);
    std::fprintf(file, "deadzone_deg = %g\n\n", (double)filter.deadzone_deg);

    std::fprintf(file, "[calibration]\n");
    std::fprintf(file, "pitch_offset = %g\n", (double)calibration.pitch_offset);
    std::fprintf(file, "roll_offset = %g\n", (double)calibration.roll_offset);
    std::fprintf(file, "invert_pitch = %s\n", bool_str(calibration.invert_pitch));
    std::fprintf(file, "invert_roll = %s\n", bool_str(calibration.invert_roll));
    std::fprintf(file, "auto_calibrate = %s\n", bool_str(calibration.auto_calibrate));
    std::fprintf(file, "sensitivity_pitch = %g\n", (double)calibration.sensitivity_pitch);
    std::fprintf(file, "sensitivity_roll = %g\n", (double)calibration.sensitivity_roll);
    std::fprintf(file, "console_sensitivity_offset = %d\n", calibration.console_sensitivity_offset);

    bool ok = !std::ferror(file);
    if (std::fclose(file) != 0) ok = false;
    return ok;
}

} // namespace kinestop
