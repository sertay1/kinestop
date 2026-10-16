/*
 * Kinestop - IPC contract between the dot-rendering sysmodule and the
 * configuration overlay.
 *
 * Original Kinestop by SertAy (https://github.com/sertay1/Kinestop).
 * The sysmodule/overlay split is a modification of the original project.
 *
 * Every struct here crosses the IPC boundary by value, so they are plain C,
 * fixed-size, and checked with static asserts below. Bump
 * KINESTOP_API_VERSION whenever a command or a struct layout changes; the
 * overlay refuses to talk to a sysmodule reporting a different version.
 */

#pragma once

#include <stdint.h>  /* no switch.h: keeps config/dots code host-testable */

#ifdef __cplusplus
extern "C" {
#endif

#define KINESTOP_PROGRAM_ID      0x4200000000001042ULL
#define KINESTOP_PROGRAM_ID_STR  "4200000000001042"

/* SM service name. Must be <= 8 bytes (smEncodeName + ovl-sysmodules limit). */
#define KINESTOP_SERVICE_NAME    "kinestop"

#define KINESTOP_API_VERSION     2

typedef enum {
    /* void -> uint32_t (KINESTOP_API_VERSION) */
    KinestopCmd_GetApiVersion = 0,
    /* void -> KinestopStatus */
    KinestopCmd_GetStatus     = 1,
    /* KinestopWireConfig -> void. Applied immediately; NOT persisted.
     * The overlay is the only writer of config.ini. */
    KinestopCmd_SetConfig     = 2,
    /* void -> void. Clears and destroys the layer, then the process exits.
     * Numbered 3 to match the sys-clk/hoc-clk convention that
     * ovl-sysmodules' toolbox.json "shutdown_cmd" expects. */
    KinestopCmd_Exit          = 3,
    /* void -> KinestopCalibration. Uses the current raw (un-offset)
     * orientation as the new zero, applies it, and returns the offsets so the
     * overlay can persist them. */
    KinestopCmd_Calibrate     = 4,
    /* uint8_t (bool) -> void. true: tear the VI layer down completely (frees
     * the framebuffers, keeps sampling the IMU) so another overlay's layer can
     * be shown; false: bring it back. The Kinestop overlay suspends while its
     * panel is visible. Not persisted; a fresh process starts unsuspended. */
    KinestopCmd_SetSuspended  = 5,
} KinestopCmd;

/* KinestopWireConfig.debug_flags */
#define KINESTOP_DEBUG_BLINK_TEST     (1u << 0)  /* layer down 3 s out of every 10 s */
#define KINESTOP_DEBUG_UNSCALED_LAYER (1u << 1)  /* 1:1 test layer: 640x360 fb in a 960x540 (VI units) layer, top-left quarter */
#define KINESTOP_DEBUG_VERBOSE_LOG    (1u << 2)  /* 1 Hz diagnostics line in sysmodule.log */

/* Mirror of kinestop::Config that the renderer needs. Index fields use the
 * same meaning as config.ini (see common/config.hpp). */
typedef struct {
    uint8_t    style;              /* placement pattern 0..5 */
    uint8_t    color;              /* theme index 0..11 */
    uint8_t    density;            /* 0..3 */
    uint8_t    dot_size;           /* index into kinestop::dots::SIZES, 0..9 */
    uint8_t    free_floating;      /* bool */
    uint8_t    invert_pitch;       /* bool */
    uint8_t    invert_roll;        /* bool */
    uint8_t    show_in_captures;   /* bool: also add the layer to screenshot/recording stacks */
    uint16_t   poll_rate_hz;       /* render/sample rate, clamped 10..120 by the sysmodule */
    uint8_t    debug_flags;        /* KINESTOP_DEBUG_* (test builds) */
    uint8_t    reserved;
    float opacity;            /* 0.10 .. 1.0 */
    float filter_alpha;       /* 0.50 .. 0.97 */
    float deadzone_deg;       /* 0.0 .. 5.0 */
    float pitch_offset;       /* calibration zero, degrees */
    float roll_offset;        /* calibration zero, degrees */
    float sensitivity_pitch;  /* 0.2 .. 6.0 */
    float sensitivity_roll;   /* 0.2 .. 6.0 */
} KinestopWireConfig;

/* Bits of KinestopStatus.flags */
#define KINESTOP_STATUS_LAYER_OK   (1u << 0)  /* VI layer + framebuffer are up */
#define KINESTOP_STATUS_SENSOR_OK  (1u << 1)  /* a sensor sample with an ADVANCING sampling number within the last 0.5 s */
#define KINESTOP_STATUS_SUSPENDED  (1u << 2)  /* layer deliberately down (SetSuspended or blink test) */

/* KinestopStatus.sensor_source */
typedef enum {
    KinestopSensor_None        = 0,
    KinestopSensor_SevenSixAxis = 1,  /* console IMU via hidInitializeSevenSixAxisSensor */
    KinestopSensor_NpadSixAxis = 2,   /* npad six-axis (handheld / controller) */
} KinestopSensorSource;

typedef struct {
    uint32_t   api_version;
    uint32_t   flags;              /* KINESTOP_STATUS_* */
    float pitch_deg;          /* raw filtered orientation (before calibration) */
    float roll_deg;
    uint32_t   last_error_rc;      /* most recent failing libnx/HOS Result; reset to 0 once that stage recovers */
    uint32_t   last_error_stage;   /* KinestopStage of last_error_rc */
    uint32_t   frames_presented;   /* wraps */
    uint32_t   loop_iterations;    /* main-loop heartbeat, published at the TOP of each iteration (wraps) */
    uint32_t   sensor_source;      /* KinestopSensorSource currently used */
    uint32_t   sensor_advance_1s;  /* sampling-number advance of the chosen source over the last second */
    uint32_t   dequeue_timeouts;   /* presents abandoned because no buffer was free in time (wraps) */
    uint32_t   max_present_us;     /* longest present() over the last second, microseconds */
    uint32_t   layer_inits;        /* successful layer creations since start (wraps) */
    uint32_t   seven_init_rc;      /* SevenSixAxis: failing initialize Result, else the latest start Result (kept current); 0 = started */
    uint32_t   reserved[2];
} KinestopStatus;

typedef enum {
    KinestopStage_None        = 0,
    KinestopStage_ViInit      = 1,
    KinestopStage_Display     = 2,
    KinestopStage_Layer       = 3,
    KinestopStage_LayerStack  = 4,
    KinestopStage_NWindow     = 5,
    KinestopStage_Framebuffer = 6,
    KinestopStage_Sensor      = 7,
} KinestopStage;

typedef struct {
    float pitch_offset;
    float roll_offset;
} KinestopCalibration;

#ifdef __cplusplus
static_assert(sizeof(KinestopWireConfig) == 40, "KinestopWireConfig layout changed: bump KINESTOP_API_VERSION");
static_assert(sizeof(KinestopStatus) == 64, "KinestopStatus layout changed: bump KINESTOP_API_VERSION");
static_assert(sizeof(KinestopCalibration) == 8, "KinestopCalibration layout changed: bump KINESTOP_API_VERSION");
#else
_Static_assert(sizeof(KinestopWireConfig) == 40, "KinestopWireConfig layout changed: bump KINESTOP_API_VERSION");
_Static_assert(sizeof(KinestopStatus) == 64, "KinestopStatus layout changed: bump KINESTOP_API_VERSION");
_Static_assert(sizeof(KinestopCalibration) == 8, "KinestopCalibration layout changed: bump KINESTOP_API_VERSION");
#endif

#ifdef __cplusplus
}
#endif
