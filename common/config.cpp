#include "config.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define MKDIR(dir) _mkdir(dir)
#else
#include <unistd.h>
#define MKDIR(dir) mkdir(dir, 0777)
#endif

namespace kinestop {

const char* Config::DEFAULT_PATH = "sdmc:/config/kinestop/config.ini";

static inline std::string trim(const std::string& str) {
    auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    auto last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

static inline std::string to_lower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

static inline bool parse_bool(const std::string& val, bool default_val = false) {
    std::string s = to_lower(trim(val));
    if (s == "true" || s == "1" || s == "yes" || s == "on") return true;
    if (s == "false" || s == "0" || s == "no" || s == "off") return false;
    return default_val;
}

static inline int parse_int(const std::string& val, int default_val = 0) {
    std::string s = trim(val);
    if (s.empty()) return default_val;
    char* endptr = nullptr;
    long result = std::strtol(s.c_str(), &endptr, 10);
    if (endptr == s.c_str()) return default_val;
    return static_cast<int>(result);
}

static inline float parse_float(const std::string& val, float default_val = 0.0f) {
    std::string s = trim(val);
    if (s.empty()) return default_val;
    char* endptr = nullptr;
    float result = std::strtof(s.c_str(), &endptr);
    if (endptr == s.c_str()) return default_val;
    return result;
}

static void ensure_dir_exists(const std::string& path) {
    std::string current;
    for (size_t i = 0; i < path.length(); ++i) {
        char c = path[i];
        if (c == '/' || c == '\\') {
            if (!current.empty() && current != "sdmc:" && current != "sdmc:/") {
                MKDIR(current.c_str());
            }
        }
        current += c;
    }
}

void Config::set_defaults() {
    general.enabled = true;
    general.poll_rate_hz = 60;
    general.language = 0; // 0: English, 1: Türkçe

    visual.style = 0;
    visual.color = 0;
    visual.density = 1;
    visual.opacity = 0.80f;
    visual.line_thickness = 3;
    visual.free_floating = true;
    visual.draw_center_reticle = false;
    visual.draw_roll_indicators = false;

    filter.alpha = 0.85f;
    filter.deadzone_deg = 0.5f;

    calibration.pitch_offset = 0.0f;
    calibration.roll_offset = 0.0f;
    calibration.invert_pitch = false;
    calibration.invert_roll = false;
    calibration.sensitivity_pitch = 1.0f;
    calibration.sensitivity_roll = 1.0f;
}

bool Config::load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        if (filepath.rfind("sdmc:", 0) == 0) {
            std::string alt = filepath.substr(5);
            file.open(alt);
        }
    }
    if (!file.is_open()) {
        set_defaults();
        return false;
    }

    std::string line;
    std::string current_section;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line.front() == '[' && line.back() == ']') {
            current_section = to_lower(line.substr(1, line.length() - 2));
            continue;
        }

        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = to_lower(trim(line.substr(0, eq_pos)));
        std::string val = trim(line.substr(eq_pos + 1));

        if (current_section == "general") {
            if (key == "enabled") general.enabled = parse_bool(val, general.enabled);
            else if (key == "poll_rate_hz") general.poll_rate_hz = parse_int(val, general.poll_rate_hz);
            else if (key == "language") general.language = parse_int(val, general.language);
        } else if (current_section == "visual") {
            if (key == "style") visual.style = parse_int(val, visual.style);
            else if (key == "color") visual.color = parse_int(val, visual.color);
            else if (key == "density") visual.density = parse_int(val, visual.density);
            else if (key == "opacity") visual.opacity = parse_float(val, visual.opacity);
            else if (key == "line_thickness") visual.line_thickness = parse_int(val, visual.line_thickness);
            else if (key == "free_floating") visual.free_floating = parse_bool(val, visual.free_floating);
        } else if (current_section == "filter") {
            if (key == "alpha") filter.alpha = parse_float(val, filter.alpha);
            else if (key == "deadzone_deg") filter.deadzone_deg = parse_float(val, filter.deadzone_deg);
        } else if (current_section == "calibration") {
            if (key == "pitch_offset") calibration.pitch_offset = parse_float(val, calibration.pitch_offset);
            else if (key == "roll_offset") calibration.roll_offset = parse_float(val, calibration.roll_offset);
            else if (key == "invert_pitch") calibration.invert_pitch = parse_bool(val, calibration.invert_pitch);
            else if (key == "invert_roll") calibration.invert_roll = parse_bool(val, calibration.invert_roll);
            else if (key == "sensitivity_pitch") calibration.sensitivity_pitch = parse_float(val, calibration.sensitivity_pitch);
            else if (key == "sensitivity_roll") calibration.sensitivity_roll = parse_float(val, calibration.sensitivity_roll);
        }
    }

    return true;
}

bool Config::save(const std::string& filepath) const {
    ensure_dir_exists(filepath);

    std::ofstream file(filepath);
    if (!file.is_open()) {
        if (filepath.rfind("sdmc:", 0) == 0) {
            std::string alt = filepath.substr(5);
            ensure_dir_exists(alt);
            file.open(alt);
        }
    }
    if (!file.is_open()) return false;

    file << "; ==========================================\n";
    file << "; Kinestop Configuration (by SertAy)\n";
    file << "; ==========================================\n\n";

    file << "[general]\n";
    file << "enabled = " << (general.enabled ? "true" : "false") << "\n";
    file << "poll_rate_hz = " << general.poll_rate_hz << "\n";
    file << "language = " << general.language << "\n\n";

    file << "[visual]\n";
    file << "style = " << visual.style << "\n";
    file << "color = " << visual.color << "\n";
    file << "density = " << visual.density << "\n";
    file << "opacity = " << visual.opacity << "\n";
    file << "line_thickness = " << visual.line_thickness << "\n";
    file << "free_floating = " << (visual.free_floating ? "true" : "false") << "\n\n";

    file << "[filter]\n";
    file << "alpha = " << filter.alpha << "\n";
    file << "deadzone_deg = " << filter.deadzone_deg << "\n\n";

    file << "[calibration]\n";
    file << "pitch_offset = " << calibration.pitch_offset << "\n";
    file << "roll_offset = " << calibration.roll_offset << "\n";
    file << "invert_pitch = " << (calibration.invert_pitch ? "true" : "false") << "\n";
    file << "invert_roll = " << (calibration.invert_roll ? "true" : "false") << "\n";
    file << "sensitivity_pitch = " << calibration.sensitivity_pitch << "\n";
    file << "sensitivity_roll = " << calibration.sensitivity_roll << "\n";

    return true;
}

} // namespace kinestop
