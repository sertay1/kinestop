// Kinestop host tests - IPC contract (common/kinestop_ipc.h) and the build
// metadata that has to agree with it (npdm, toolbox.json).
//
// Part of the sysmodule/overlay split of the original Kinestop by SertAy
// (https://github.com/sertay1/Kinestop).

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "check.hpp"
#include "kinestop_ipc.h"

// Compiled as C by the Makefile (exercises the _Static_assert branch).
extern "C" int kinestop_ipc_c_sizes(size_t* wire, size_t* status, size_t* calib);

namespace {

std::string read_file(const char* path) {
    std::string out;
    FILE* f = std::fopen(path, "rb");
    if (!f) return out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

// JSON with all whitespace removed, so `"key":value` can be searched for.
std::string squeeze(const std::string& s) {
    std::string out;
    for (char c : s)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') out += c;
    return out;
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// Value of `APP_VERSION := x` in a devkitPro Makefile ("" if absent).
std::string makefile_version(const std::string& path) {
    const std::string text = read_file(path.c_str());
    const std::string key = "APP_VERSION";
    for (size_t pos = text.find(key); pos != std::string::npos; pos = text.find(key, pos + 1)) {
        if (pos != 0 && text[pos - 1] != '\n') continue;  // must start a line
        size_t i = text.find(":=", pos);
        const size_t eol = text.find('\n', pos);
        if (i == std::string::npos || (eol != std::string::npos && i > eol)) continue;
        i += 2;
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
        size_t end = i;
        while (end < text.size() && text[end] != '\n' && text[end] != '\r' && text[end] != ' ' && text[end] != '\t')
            ++end;
        return text.substr(i, end - i);
    }
    return "";
}

void test_struct_sizes() {
    CHECK_EQ(sizeof(KinestopWireConfig), 40u);
    CHECK_EQ(sizeof(KinestopStatus), 64u);
    CHECK_EQ(sizeof(KinestopCalibration), 8u);
    CHECK_EQ(alignof(KinestopWireConfig), 4u);
    CHECK_EQ(alignof(KinestopStatus), 4u);

    size_t w = 0, s = 0, c = 0;
    CHECK_EQ(kinestop_ipc_c_sizes(&w, &s, &c), 1);
    CHECK_EQ(w, 40u);
    CHECK_EQ(s, 64u);
    CHECK_EQ(c, 8u);
}

void test_layout() {
    // KinestopWireConfig
    CHECK_EQ(offsetof(KinestopWireConfig, style), 0u);
    CHECK_EQ(offsetof(KinestopWireConfig, color), 1u);
    CHECK_EQ(offsetof(KinestopWireConfig, density), 2u);
    CHECK_EQ(offsetof(KinestopWireConfig, dot_size), 3u);
    CHECK_EQ(offsetof(KinestopWireConfig, free_floating), 4u);
    CHECK_EQ(offsetof(KinestopWireConfig, invert_pitch), 5u);
    CHECK_EQ(offsetof(KinestopWireConfig, invert_roll), 6u);
    CHECK_EQ(offsetof(KinestopWireConfig, show_in_captures), 7u);
    CHECK_EQ(offsetof(KinestopWireConfig, poll_rate_hz), 8u);
    CHECK_EQ(offsetof(KinestopWireConfig, debug_flags), 10u);  // was reserved[0] in API v1
    CHECK_EQ(offsetof(KinestopWireConfig, reserved), 11u);
    CHECK_EQ(sizeof(((KinestopWireConfig*)nullptr)->debug_flags), 1u);
    CHECK_EQ(offsetof(KinestopWireConfig, opacity), 12u);
    CHECK_EQ(offsetof(KinestopWireConfig, filter_alpha), 16u);
    CHECK_EQ(offsetof(KinestopWireConfig, deadzone_deg), 20u);
    CHECK_EQ(offsetof(KinestopWireConfig, pitch_offset), 24u);
    CHECK_EQ(offsetof(KinestopWireConfig, roll_offset), 28u);
    CHECK_EQ(offsetof(KinestopWireConfig, sensitivity_pitch), 32u);
    CHECK_EQ(offsetof(KinestopWireConfig, sensitivity_roll), 36u);

    // KinestopStatus
    CHECK_EQ(offsetof(KinestopStatus, api_version), 0u);
    CHECK_EQ(offsetof(KinestopStatus, flags), 4u);
    CHECK_EQ(offsetof(KinestopStatus, pitch_deg), 8u);
    CHECK_EQ(offsetof(KinestopStatus, roll_deg), 12u);
    CHECK_EQ(offsetof(KinestopStatus, last_error_rc), 16u);
    CHECK_EQ(offsetof(KinestopStatus, last_error_stage), 20u);
    CHECK_EQ(offsetof(KinestopStatus, frames_presented), 24u);
    // API v2 additions; the v1 prefix above is unchanged.
    CHECK_EQ(offsetof(KinestopStatus, loop_iterations), 28u);
    CHECK_EQ(offsetof(KinestopStatus, sensor_source), 32u);
    CHECK_EQ(offsetof(KinestopStatus, sensor_advance_1s), 36u);
    CHECK_EQ(offsetof(KinestopStatus, dequeue_timeouts), 40u);
    CHECK_EQ(offsetof(KinestopStatus, max_present_us), 44u);
    CHECK_EQ(offsetof(KinestopStatus, layer_inits), 48u);
    CHECK_EQ(offsetof(KinestopStatus, seven_init_rc), 52u);
    CHECK_EQ(offsetof(KinestopStatus, reserved), 56u);
    CHECK_EQ(sizeof(((KinestopStatus*)nullptr)->reserved), 8u);

    // KinestopCalibration
    CHECK_EQ(offsetof(KinestopCalibration, pitch_offset), 0u);
    CHECK_EQ(offsetof(KinestopCalibration, roll_offset), 4u);

    // Payloads must fit in a single CMIF raw-data area (sys-clk ipc_server:
    // IPC_SERVER_MAX_DATA_SIZE = 0x100 - 0x10 - 16 = 224 bytes).
    CHECK(sizeof(KinestopWireConfig) <= 224u);
    CHECK(sizeof(KinestopStatus) <= 224u);
    CHECK(sizeof(KinestopCalibration) <= 224u);
}

void test_constants() {
    CHECK_EQ(KinestopCmd_GetApiVersion, 0);
    CHECK_EQ(KinestopCmd_GetStatus, 1);
    CHECK_EQ(KinestopCmd_SetConfig, 2);
    CHECK_EQ(KinestopCmd_Exit, 3);  // ovl-sysmodules toolbox.json "shutdown_cmd"
    CHECK_EQ(KinestopCmd_Calibrate, 4);
    CHECK_EQ(KinestopCmd_SetSuspended, 5);  // payload: one u8 (bool)

    CHECK_EQ(KinestopStage_None, 0);
    CHECK_EQ(KinestopStage_ViInit, 1);
    CHECK_EQ(KinestopStage_Display, 2);
    CHECK_EQ(KinestopStage_Layer, 3);
    CHECK_EQ(KinestopStage_LayerStack, 4);
    CHECK_EQ(KinestopStage_NWindow, 5);
    CHECK_EQ(KinestopStage_Framebuffer, 6);
    CHECK_EQ(KinestopStage_Sensor, 7);

    CHECK_EQ(KINESTOP_STATUS_LAYER_OK, 1u);
    CHECK_EQ(KINESTOP_STATUS_SENSOR_OK, 2u);
    CHECK_EQ(KINESTOP_STATUS_SUSPENDED, 4u);
    CHECK_EQ(KINESTOP_API_VERSION, 2);

    CHECK_EQ(KinestopSensor_None, 0);
    CHECK_EQ(KinestopSensor_SevenSixAxis, 1);
    CHECK_EQ(KinestopSensor_NpadSixAxis, 2);

    // debug_flags: five distinct bits, KINESTOP_DEBUG_ALL is exactly their
    // union, all fitting the u8 wire field (no API bump: same struct).
    CHECK_EQ(KINESTOP_DEBUG_BLINK_TEST, 1u);
    CHECK_EQ(KINESTOP_DEBUG_UNSCALED_LAYER, 2u);
    CHECK_EQ(KINESTOP_DEBUG_VERBOSE_LOG, 4u);
    CHECK_EQ(KINESTOP_DEBUG_NO_SUSPEND, 8u);
    CHECK_EQ(KINESTOP_DEBUG_SCALED_SMALL, 16u);
    CHECK_EQ(KINESTOP_DEBUG_ALL, (KINESTOP_DEBUG_BLINK_TEST | KINESTOP_DEBUG_UNSCALED_LAYER | KINESTOP_DEBUG_VERBOSE_LOG |
                                  KINESTOP_DEBUG_NO_SUSPEND | KINESTOP_DEBUG_SCALED_SMALL));
    CHECK(KINESTOP_DEBUG_ALL <= 0xFFu);

    // SM names are at most 8 bytes.
    const size_t name_len = std::strlen(KINESTOP_SERVICE_NAME);
    CHECK(name_len >= 1 && name_len <= 8);

    // The string form matches the numeric program id.
    CHECK_EQ(std::strtoull(KINESTOP_PROGRAM_ID_STR, nullptr, 16), KINESTOP_PROGRAM_ID);
    CHECK_EQ(std::strlen(KINESTOP_PROGRAM_ID_STR), 16u);
    // Homebrew sysmodule range (0x0100000000000000 .. 0x01FFFFFFFFFFFFFF is
    // Nintendo's system range; 0x42.. is the de-facto homebrew prefix).
    CHECK_EQ(KINESTOP_PROGRAM_ID >> 56, 0x42ull);
}

// sysmodule/kinestop.json (npdm) and sysmodule/toolbox.json (ovl-sysmodules)
// must name the same program id / service as the header.
void test_metadata(const char* root) {
    const std::string base = root;
    const std::string npdm = squeeze(read_file((base + "/sysmodule/kinestop.json").c_str()));
    const std::string toolbox = squeeze(read_file((base + "/sysmodule/toolbox.json").c_str()));
    CHECK(!npdm.empty());
    CHECK(!toolbox.empty());

    const std::string pid_hex = std::string("0x") + KINESTOP_PROGRAM_ID_STR;
    CHECK(contains(npdm, "\"title_id\":\"" + pid_hex + "\""));
    CHECK(contains(npdm, "\"title_id_range_min\":\"" + pid_hex + "\""));
    CHECK(contains(npdm, "\"title_id_range_max\":\"" + pid_hex + "\""));
    CHECK(contains(npdm, std::string("\"service_host\":[\"") + KINESTOP_SERVICE_NAME + "\"]"));
    // Services the sysmodule opens (see sysmodule/source/main.cpp, renderer.cpp).
    for (const char* srv : {"fsp-srv", "set:sys", "hid", "apm", "vi:m", "nvdrv:s"})
        CHECK_ALL(contains(npdm, std::string("\"") + srv + "\""), "service_access lacks %s", srv);

    CHECK(contains(toolbox, std::string("\"tid\":\"") + KINESTOP_PROGRAM_ID_STR + "\""));
    CHECK(contains(toolbox, std::string("\"shutdown_service\":\"") + KINESTOP_SERVICE_NAME + "\""));
    CHECK(contains(toolbox, "\"shutdown_cmd\":" + std::to_string((int)KinestopCmd_Exit)));
    CHECK(contains(toolbox, "\"requires_reboot\":false"));

    // One version string for the whole release: both Makefiles, toolbox.json
    // and the sysmodule's fallback define.
    const std::string sys_ver = makefile_version(base + "/sysmodule/Makefile");
    const std::string ovl_ver = makefile_version(base + "/overlay/Makefile");
    CHECK(!sys_ver.empty());
    CHECK_EQ(sys_ver, ovl_ver);
    CHECK(contains(toolbox, "\"version\":\"" + sys_ver + "\""));
    CHECK(contains(read_file((base + "/sysmodule/source/main.cpp").c_str()),
                   "#define APP_VERSION \"" + sys_ver + "\""));
}

// Both ends name every command: the sysmodule handles it (a `case`), the
// overlay's client sends it. Catches a command added on one side only.
void test_both_sides(const char* root) {
    const std::string base = root;
    const std::string server = read_file((base + "/sysmodule/source/main.cpp").c_str());
    const std::string client = read_file((base + "/overlay/source/ipc_client.cpp").c_str());
    CHECK(!server.empty());
    CHECK(!client.empty());
    for (const char* cmd : {"KinestopCmd_GetApiVersion", "KinestopCmd_GetStatus", "KinestopCmd_SetConfig",
                            "KinestopCmd_Exit", "KinestopCmd_Calibrate", "KinestopCmd_SetSuspended"}) {
        CHECK_ALL(contains(server, std::string("case ") + cmd + ":"), "sysmodule does not handle %s", cmd);
        CHECK_ALL(contains(client, cmd), "overlay never sends %s", cmd);
    }
    // SetSuspended carries exactly one byte; the server reads it as u8.
    CHECK(contains(client, "const u8 in = suspended ? 1 : 0;"));
    CHECK(contains(server, "*static_cast<const u8*>(r->data.ptr) != 0"));
}

} // namespace

int main(int argc, char** argv) {
    test_struct_sizes();
    test_layout();
    test_constants();
    test_metadata(argc > 1 ? argv[1] : "..");
    test_both_sides(argc > 1 ? argv[1] : "..");
    return check::summary("ipc");
}
