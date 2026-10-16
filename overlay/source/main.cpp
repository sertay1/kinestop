/*
 * Kinestop overlay - settings / control panel (by SertAy).
 *
 * This is a modification of the original Kinestop by SertAy
 * (https://github.com/sertay1/Kinestop): the motion-cue dots are now drawn by
 * a background sysmodule (title 4200000000001042, service "kinestop"), so
 * this overlay is a normal 448px Tesla/Ultrahand panel that starts/stops the
 * sysmodule, edits config.ini and pushes live settings to it over IPC. It no
 * longer draws dots or touches the IMU, fits in the default 4MB overlay
 * memory, and other overlays keep working while the dots are shown.
 */

#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <sys/stat.h>

#include "config.hpp"
#include "dots.hpp"
#include "kinestop_ipc.h"
#include "ipc_client.hpp"

namespace {
    namespace dots = kinestop::dots;
    namespace ipc  = kinestop::ipc;

    kinestop::Config g_config;
    bool g_needGuiReload = false;

    constexpr const char* BootFlagDir  = "sdmc:/atmosphere/contents/" KINESTOP_PROGRAM_ID_STR "/flags";
    constexpr const char* BootFlagPath = "sdmc:/atmosphere/contents/" KINESTOP_PROGRAM_ID_STR "/flags/boot2.flag";

    constexpr u64 StatusRefreshNs = 1000000000ULL;  // status line refresh period
    constexpr u64 SaveDebounceNs  =  500000000ULL;  // coalesce trackbar drags into one ini write
    constexpr u64 NoticeNs        = 3000000000ULL;  // transient notice duration

    // Proper 4-bit RGBA4444 color constructor (0..15 per channel)
    inline constexpr tsl::Color make_color(u8 r, u8 g, u8 b, u8 a = 255) {
        return tsl::Color(
            static_cast<u8>(r >> 4),
            static_cast<u8>(g >> 4),
            static_cast<u8>(b >> 4),
            static_cast<u8>(a >> 4)
        );
    }

    constexpr tsl::Color ColorOk    = make_color(80, 240, 120);
    constexpr tsl::Color ColorWarn  = make_color(255, 210, 60);
    constexpr tsl::Color ColorError = make_color(255, 90, 100);
    constexpr tsl::Color ColorIdle  = make_color(170, 180, 195);

    inline bool is_tr() {
        return g_config.general.language == 1;
    }

    inline u64 now_ns() {
        return armTicksToNs(armGetSystemTick());
    }

    bool file_exists(const char* path) {
        struct stat st;
        return stat(path, &st) == 0;
    }

    // KINESTOP_DEBUG_NO_SUSPEND (coexistence test): the overlay never takes
    // the dots down for its panel or for the Ultrahand menu.
    inline bool no_suspend_test() {
        return (g_config.general.debug_flags & (int)KINESTOP_DEBUG_NO_SUSPEND) != 0;
    }

    // Name of the layer the sysmodule builds for the current debug_flags
    // (UNSCALED_LAYER wins over SCALED_SMALL, see kinestop_ipc.h).
    const char* layer_mode_name() {
        const bool tr = g_config.general.language == 1;
        const int dbg = g_config.general.debug_flags;
        if (dbg & (int)KINESTOP_DEBUG_UNSCALED_LAYER)
            return tr ? "Çeyrek 1x (640x360)" : "Quarter 1x (640x360)";
        if (dbg & (int)KINESTOP_DEBUG_SCALED_SMALL)
            return tr ? "Çeyrek 2x (320x180)" : "Quarter 2x (320x180)";
        return tr ? "Tam ekran 2x (640x360)" : "Full screen 2x (640x360)";
    }

    // ---------------------------------------------------------------------
    // CONFIG PERSISTENCE
    // The overlay is the only writer of config.ini. Trackbar drags fire one
    // change per step, so writes are debounced; lifecycle-relevant changes
    // (master toggle, language, resets, calibration) are written at once.
    // ---------------------------------------------------------------------
    bool g_saveDirty   = false;
    u64  g_saveDirtyNs = 0;

    void save_config_now() {
        g_config.save();
        g_saveDirty = false;
    }

    void flush_config() {
        if (g_saveDirty) save_config_now();
    }

    void mark_config_dirty() {
        g_saveDirty   = true;
        g_saveDirtyNs = now_ns();
    }

    // ---------------------------------------------------------------------
    // SYSMODULE LIFECYCLE WORKER
    // pmshellLaunchProgram + waiting for the service (up to ~1.5s) and the
    // graceful-exit wait would freeze the panel, so start/stop run on a
    // short-lived thread. While it runs, the UI thread makes no IPC calls
    // (ipc_client is not thread-safe); pushes are redone when it finishes.
    //
    // g_ctlMutex serialises everything that touches ipc_client or the
    // worker handle between the UI thread and Ultrahand's background event
    // thread, which calls Overlay::hide() -> onHide() itself (launch combo
    // while open, HOME, power). Recursive: the helpers below nest. The
    // worker thread never takes it, so joining under it cannot deadlock.
    // ---------------------------------------------------------------------
    std::recursive_mutex g_ctlMutex;
    using CtlLock = std::lock_guard<std::recursive_mutex>;

    enum class Job : u8 { None, Start, Stop };

    Thread            g_worker{};
    std::atomic<bool> g_workerActive{false};
    Job               g_job          = Job::None;
    Result            g_jobRc        = 0;
    bool              g_jobNoSuspend = false;  // no_suspend_test() when the job was queued
    std::atomic<bool> g_jobDone{false};

    Job    g_lastJob   = Job::None;  // outcome shown in the status line
    Result g_lastJobRc = 0;

    // True between onShow and onHide. Read by the worker, which suspends a
    // freshly started sysmodule while the panel is still on screen.
    std::atomic<bool> g_panelVisible{false};

    // initServices() already suspended the dots (and timed it) right before
    // the panel's layer was created: the first onShow() need not repeat it.
    bool g_suspendedAtInit = false;

    void worker_main(void*) {
        if (g_job == Job::Start) {
            g_jobRc = ipc::start();
            // The new process starts unsuspended and puts its layer up next
            // to this panel's: take it down again at once. onHide joins the
            // worker before resuming, so this cannot outlive the panel.
            if (R_SUCCEEDED(g_jobRc) && !g_jobNoSuspend && g_panelVisible.load(std::memory_order_acquire))
                ipc::setSuspended(true);
        } else {
            g_jobRc = ipc::stop();
        }
        g_jobDone.store(true, std::memory_order_release);
    }

    inline bool worker_busy() {
        return g_workerActive;
    }

    void join_worker() {
        if (!g_workerActive) return;
        threadWaitForExit(&g_worker);
        threadClose(&g_worker);
        g_workerActive = false;
    }

    // ---------------------------------------------------------------------
    // STATUS LINE
    // ---------------------------------------------------------------------
    std::string g_statusStr;
    tsl::Color  g_statusColor = ColorIdle;
    bool        g_statusRunning = false;   // last known process state
    bool        g_installed     = false;
    bool        g_installedKnown = false;

    std::string g_noticeStr;
    tsl::Color  g_noticeColor = ColorIdle;
    u64         g_noticeUntilNs = 0;

    u64  g_lastRefreshNs = 0;
    bool g_refreshNow    = true;

    // Diagnostics block (test builds): re-formatted on each status refresh
    // (~1 Hz), drawn as-is every frame.
    constexpr int DiagMaxLines   = 7;
    constexpr u16 DiagLineStep   = 17;
    constexpr u32 DiagFontSize   = 14;
    constexpr u16 DiagBlockHeight = DiagMaxLines * DiagLineStep + 12;

    std::string g_diagLines[DiagMaxLines];
    tsl::Color  g_diagColors[DiagMaxLines] = {};
    int         g_diagCount = 0;

    // Previous snapshot, to show whether the counters move between refreshes.
    bool g_diagPrevValid    = false;
    u32  g_diagPrevLoop     = 0;
    u32  g_diagPrevFrames   = 0;
    u32  g_diagPrevTimeouts = 0;

    // How long the sysmodule took to report its layer down after the last
    // SetSuspended(true) sent on show: >= 0 ms, SuspendAckTimeout, or
    // SuspendAckNone when no suspend was waited for.
    constexpr s32 SuspendAckNone    = -2;
    constexpr s32 SuspendAckTimeout = -1;
    s32 g_suspendAckMs = SuspendAckNone;

    constexpr u64 SuspendWaitNs = 300000000ULL;  // max wait for the layer to go down
    constexpr u64 SuspendPollNs =  10000000ULL;

    // Results are shown the way HOS error screens and the sysmodule log
    // (sdmc:/config/kinestop/sysmodule.log) print them: 2xxx-yyyy.
    #define KS_RC_FMT "%04u-%04u"
    #define KS_RC_ARGS(rc) (unsigned)(2000 + R_MODULE(rc)), (unsigned)R_DESCRIPTION(rc)

    void format_into(std::string& out, const char* fmt, va_list args) {
        char buf[96];
        std::vsnprintf(buf, sizeof(buf), fmt, args);
        if (out != buf) out = buf;
    }

    __attribute__((format(printf, 2, 3)))
    void set_status(const tsl::Color& color, const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        format_into(g_statusStr, fmt, args);
        va_end(args);
        g_statusColor = color;
    }

    __attribute__((format(printf, 2, 3)))
    void show_notice(const tsl::Color& color, const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        format_into(g_noticeStr, fmt, args);
        va_end(args);
        g_noticeColor   = color;
        g_noticeUntilNs = now_ns() + NoticeNs;
    }

    __attribute__((format(printf, 2, 3)))
    void add_diag_line(const tsl::Color& color, const char* fmt, ...) {
        if (g_diagCount >= DiagMaxLines) return;
        va_list args;
        va_start(args, fmt);
        format_into(g_diagLines[g_diagCount], fmt, args);
        va_end(args);
        g_diagColors[g_diagCount] = color;
        ++g_diagCount;
    }

    const char* sensor_name(u32 source) {
        switch (source) {
            case KinestopSensor_SevenSixAxis: return "SevenSixAxis";
            case KinestopSensor_NpadSixAxis:  return "Npad";
            default:                          return is_tr() ? "yok" : "none";
        }
    }

    // Rebuilds the diagnostics block from a fresh snapshot (nullptr: none).
    void update_diagnostics(const KinestopStatus* st) {
        const bool tr = is_tr();
        g_diagCount = 0;

        // From the settings pushed to the sysmodule, so shown even without data.
        add_diag_line(ColorIdle, tr ? "Katman: %s%s" : "Layer: %s%s", layer_mode_name(),
                      no_suspend_test() ? (tr ? ", duraklatma yok" : ", no suspend") : "");

        if (st == nullptr) {
            g_diagPrevValid = false;
            add_diag_line(ColorIdle, tr ? "Tanılama: veri yok" : "Diagnostics: no data");
            return;
        }

        const bool prev = g_diagPrevValid;

        // Sensor: which source and how far its sampling number moved in 1 s.
        const bool sensorLive = st->sensor_source != KinestopSensor_None && st->sensor_advance_1s > 0;
        add_diag_line(sensorLive ? ColorOk : ColorWarn,
                      tr ? "Sensör: %s, +%u/sn" : "Sensor: %s, +%u/s",
                      sensor_name(st->sensor_source), (unsigned)st->sensor_advance_1s);
        if (st->seven_init_rc != 0)
            add_diag_line(ColorWarn, tr ? "SevenSixAxis başlatılamadı: " KS_RC_FMT : "SevenSixAxis not started: " KS_RC_FMT,
                          KS_RC_ARGS(st->seven_init_rc));

        add_diag_line(ColorIdle, tr ? "Dikey %.1f  Yatay %.1f derece" : "Pitch %.1f  Roll %.1f deg",
                      (double)st->pitch_deg, (double)st->roll_deg);

        // Heartbeat: published at the top of every main-loop iteration, so a
        // loop wedged in present() shows up as a counter that stops moving.
        const u32 loopDelta   = st->loop_iterations - g_diagPrevLoop;
        const u32 framesDelta = st->frames_presented - g_diagPrevFrames;
        if (!prev) {
            add_diag_line(ColorIdle, tr ? "Döngü %u | Kare %u" : "Loop %u | Frames %u",
                          (unsigned)st->loop_iterations, (unsigned)st->frames_presented);
        } else if (loopDelta == 0) {
            add_diag_line(ColorError, tr ? "Döngü %u DURDU | Kare %u" : "Loop %u STALLED | Frames %u",
                          (unsigned)st->loop_iterations, (unsigned)st->frames_presented);
        } else {
            add_diag_line(ColorOk, tr ? "Döngü %u (+%u) | Kare %u (+%u)" : "Loop %u (+%u) | Frames %u (+%u)",
                          (unsigned)st->loop_iterations, (unsigned)loopDelta,
                          (unsigned)st->frames_presented, (unsigned)framesDelta);
        }

        const bool newTimeouts = prev && st->dequeue_timeouts != g_diagPrevTimeouts;
        add_diag_line(newTimeouts ? ColorWarn : ColorIdle,
                      tr ? "Tampon zaman aşımı %u | Maks. sunum %u.%u ms" : "Dequeue timeouts %u | Max present %u.%u ms",
                      (unsigned)st->dequeue_timeouts,
                      (unsigned)(st->max_present_us / 1000), (unsigned)(st->max_present_us % 1000 / 100));

        // Suspended state, plus how long the last suspend-on-show took.
        const bool suspended = (st->flags & KINESTOP_STATUS_SUSPENDED) != 0;
        char ack[32] = "";
        if (g_suspendAckMs >= 0)
            std::snprintf(ack, sizeof(ack), " (%d ms)", (int)g_suspendAckMs);
        else if (g_suspendAckMs == SuspendAckTimeout)
            std::snprintf(ack, sizeof(ack), "%s", tr ? " (onay yok)" : " (no ack)");
        const bool expectSuspended = g_panelVisible.load(std::memory_order_relaxed) && !no_suspend_test();
        add_diag_line((suspended != expectSuspended || g_suspendAckMs == SuspendAckTimeout) ? ColorWarn : ColorIdle,
                      tr ? "Katman kurulumu %u | Askıda: %s%s" : "Layer inits %u | Suspended: %s%s",
                      (unsigned)st->layer_inits,
                      suspended ? (tr ? "evet" : "yes") : (tr ? "hayır" : "no"), ack);

        g_diagPrevValid    = true;
        g_diagPrevLoop     = st->loop_iterations;
        g_diagPrevFrames   = st->frames_presented;
        g_diagPrevTimeouts = st->dequeue_timeouts;
    }

    const char* stage_name(u32 stage) {
        switch (stage) {
            case KinestopStage_ViInit:      return "VI init";
            case KinestopStage_Display:     return "Display";
            case KinestopStage_Layer:       return "Layer";
            case KinestopStage_LayerStack:  return "Layer stack";
            case KinestopStage_NWindow:     return "NWindow";
            case KinestopStage_Framebuffer: return "Framebuffer";
            case KinestopStage_Sensor:      return is_tr() ? "Sensör" : "Sensor";
            default:                        return is_tr() ? "Bilinmeyen" : "Unknown";
        }
    }

    void set_start_failed_status(Result rc) {
        const bool tr = is_tr();
        if (rc == ipc::ResultNotInstalled)
            set_status(ColorError, tr ? "Yüklü değil" : "Not installed");
        else if (rc == ipc::ResultTimeout)
            set_status(ColorError, tr ? "Başlatılamadı: yanıt yok" : "Start failed: no response");
        else if (rc == ipc::ResultExitedEarly)
            set_status(ColorError, tr ? "Başlatılamadı: modül kapandı" : "Start failed: sysmodule exited");
        else
            set_status(ColorError, tr ? "Başlatılamadı: " KS_RC_FMT : "Start failed: " KS_RC_FMT, KS_RC_ARGS(rc));
    }

    // Rebuilds the status line; returns true when *st_out holds a fresh
    // GetStatus snapshot.
    bool refresh_status_line(KinestopStatus* st_out) {
        const bool tr = is_tr();

        if (worker_busy()) {
            if (g_job == Job::Start)
                set_status(ColorWarn, tr ? "Başlatılıyor..." : "Starting...");
            else
                set_status(ColorWarn, tr ? "Durduruluyor..." : "Stopping...");
            return false;
        }

        const bool running = ipc::isRunning();
        g_statusRunning = running;

        if (!running) {
            ipc::disconnect();
            if (!g_installedKnown) {
                g_installed      = ipc::isInstalled();
                g_installedKnown = true;
            }
            if (!g_installed)
                set_status(ColorError, tr ? "Yüklü değil" : "Not installed");
            else if (g_lastJob == Job::Start && R_FAILED(g_lastJobRc))
                set_start_failed_status(g_lastJobRc);
            else
                set_status(ColorIdle, tr ? "Durduruldu" : "Stopped");
            return false;
        }

        if (g_lastJob == Job::Stop && R_FAILED(g_lastJobRc)) {
            set_status(ColorError, tr ? "Durdurulamadı: " KS_RC_FMT : "Stop failed: " KS_RC_FMT, KS_RC_ARGS(g_lastJobRc));
            return false;
        }

        if (!ipc::connect()) {
            set_status(ColorWarn, tr ? "Çalışıyor - IPC yok" : "Running - no IPC");
            return false;
        }

        if (!ipc::apiCompatible()) {
            set_status(ColorError, tr ? "API uyumsuz (v%u, beklenen v%u)" : "API mismatch (v%u, need v%u)",
                       ipc::remoteApiVersion(), (u32)KINESTOP_API_VERSION);
            return false;
        }

        KinestopStatus& st = *st_out;
        Result rc = ipc::getStatus(&st);
        if (R_FAILED(rc) && ipc::connect())  // stale session (module restarted): retry once
            rc = ipc::getStatus(&st);
        if (R_FAILED(rc)) {
            set_status(ColorError, tr ? "Hata: IPC " KS_RC_FMT : "Error: IPC " KS_RC_FMT, KS_RC_ARGS(rc));
            return false;
        }

        if (st.last_error_rc != 0) {
            set_status(ColorError, tr ? "Hata: %s " KS_RC_FMT : "Error: %s " KS_RC_FMT,
                       stage_name(st.last_error_stage), KS_RC_ARGS(st.last_error_rc));
        } else if (!(st.flags & KINESTOP_STATUS_SENSOR_OK)) {
            set_status(ColorWarn, tr ? "Çalışıyor - sensör verisi yok" : "Running - no sensor data");
        } else {
            set_status(ColorOk, tr ? "Çalışıyor" : "Running");
        }
        return true;
    }

    // Queries the sysmodule and rebuilds the status line and the diagnostics
    // block. A handful of cheap, fail-fast IPCs (pm:dmnt, sm HasService,
    // GetStatus): called about once per second from update(), never per
    // frame.
    void refresh_status() {
        CtlLock lock(g_ctlMutex);
        g_lastRefreshNs = now_ns();
        g_refreshNow    = false;

        KinestopStatus st{};
        const bool have = refresh_status_line(&st);
        update_diagnostics(have ? &st : nullptr);

        // Panel on screen but the layer is up (sysmodule started from
        // elsewhere, or a lost SetSuspended): ask again. Not in the
        // coexistence test, where the layer is meant to stay up.
        if (have && g_panelVisible.load(std::memory_order_relaxed) && !no_suspend_test() &&
            !(st.flags & KINESTOP_STATUS_SUSPENDED))
            ipc::setSuspended(true);
    }

    // Pushes the current settings to a running sysmodule. Not persisted by
    // the sysmodule; config.ini is written by the overlay.
    void push_config() {
        CtlLock lock(g_ctlMutex);
        if (worker_busy()) return;  // re-pushed when the job finishes
        KinestopWireConfig wire{};
        g_config.to_wire(wire);     // carries debug_flags too
        ipc::setConfig(wire);       // no-op (fail-fast) when not running
    }

    // Sends SetSuspended to a running sysmodule (fail-fast otherwise). With
    // waitForLayerDown, polls its status for up to SuspendWaitNs until it
    // reports the layer gone, so this panel's layer does not have to share
    // the display with ours; the time it took is shown in the diagnostics.
    // In the coexistence test (KINESTOP_DEBUG_NO_SUSPEND) every suspend
    // request becomes a resume, so dots left suspended earlier come back.
    void suspend_dots(bool suspend, bool waitForLayerDown) {
        CtlLock lock(g_ctlMutex);
        if (suspend && no_suspend_test()) {
            suspend = false;
            g_suspendAckMs = SuspendAckNone;
        }
        if (worker_busy() || !ipc::isRunning())
            return;
        if (R_FAILED(ipc::setSuspended(suspend)) || !suspend || !waitForLayerDown)
            return;

        const u64 start = now_ns();
        for (;;) {
            KinestopStatus st{};
            if (R_FAILED(ipc::getStatus(&st)))
                break;
            const u64 elapsed = now_ns() - start;
            if ((st.flags & KINESTOP_STATUS_SUSPENDED) && !(st.flags & KINESTOP_STATUS_LAYER_OK)) {
                g_suspendAckMs = (s32)(elapsed / 1000000ULL);
                return;
            }
            if (elapsed >= SuspendWaitNs)
                break;
            svcSleepThread(SuspendPollNs);
        }
        g_suspendAckMs = SuspendAckTimeout;
    }

    // Every setting except the master toggle goes through here.
    void apply_setting() {
        g_config.sanitize();
        mark_config_dirty();
        push_config();
    }

    void on_job_finished() {
        CtlLock lock(g_ctlMutex);
        join_worker();

        g_lastJob   = g_job;
        g_lastJobRc = g_jobRc;
        g_installedKnown = false;

        if (g_job == Job::Start) {
            if (ipc::isRunning()) {
                // Push in case settings changed while it was starting.
                push_config();
                // The worker suspended (or not) per the coexistence-test
                // flag as it was when the job was queued; match the flag as
                // it is now (suspend_dots turns this into a resume when the
                // test is on).
                if (g_panelVisible.load(std::memory_order_acquire))
                    suspend_dots(true, false);
            } else if (g_config.general.enabled) {
                // Did not come up: don't leave "enabled" claiming otherwise.
                g_config.general.enabled = false;
                save_config_now();
            }
        }
        g_refreshNow = true;
    }

    void start_job(Job job) {
        CtlLock lock(g_ctlMutex);
        if (g_workerActive) return;

        g_job = job;
        g_jobNoSuspend = no_suspend_test();
        g_lastJob = Job::None;
        g_lastJobRc = 0;
        g_jobDone.store(false, std::memory_order_relaxed);

        if (R_SUCCEEDED(threadCreate(&g_worker, worker_main, nullptr, nullptr, 0x8000, 0x2C, -2))) {
            if (R_SUCCEEDED(threadStart(&g_worker))) {
                g_workerActive = true;
                refresh_status();  // shows "Starting..." / "Stopping..."
                return;
            }
            threadClose(&g_worker);
        }

        // Could not spawn a thread: do it inline (the panel stalls briefly).
        worker_main(nullptr);
        on_job_finished();  // join_worker() is a no-op: no thread is active
    }

    // Static orange note lines under a list item. `en`/`tr` hold up to
    // NoteMaxLines lines each (nullptr-terminated when shorter); the NoteText
    // must outlive the list (static storage).
    constexpr int NoteMaxLines = 6;
    constexpr s32 NoteLineStep = 20;

    struct NoteText { const char* en[NoteMaxLines]; const char* tr[NoteMaxLines]; };

    int note_line_count(const char* const* lines) {
        int n = 0;
        while (n < NoteMaxLines && lines[n] != nullptr) ++n;
        return n;
    }

    void add_note(tsl::elm::List* list, const NoteText& text, bool tr) {
        const char* const* lines = tr ? text.tr : text.en;
        const int count = note_line_count(lines);
        list->addItem(new tsl::elm::CustomDrawer([lines, count](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            const tsl::Color color = make_color(255, 170, 50);
            for (int i = 0; i < count; ++i)
                renderer->drawString(lines[i], false, x + 19, y + 20 + i * NoteLineStep, 15, color);
        }), (u16)(count * NoteLineStep + 12));
    }

    // Per-frame housekeeping shared by every Gui. Cheap unless a refresh is
    // due: an atomic load and a few tick comparisons.
    void tick_background() {
        CtlLock lock(g_ctlMutex);
        if (g_workerActive && g_jobDone.load(std::memory_order_acquire))
            on_job_finished();

        const u64 now = now_ns();

        if (g_saveDirty && now - g_saveDirtyNs >= SaveDebounceNs)
            save_config_now();

        if (g_noticeUntilNs != 0 && now >= g_noticeUntilNs) {
            g_noticeUntilNs = 0;
            g_noticeStr.clear();
        }

        if (g_refreshNow || now - g_lastRefreshNs >= StatusRefreshNs)
            refresh_status();
    }
}

// Forward declarations
class GuiKinestopSettings;
class GuiKinestopGuide;

// -------------------------------------------------------------
// STANDARD 448px SIDEBAR FRAME (by SertAy)
// -------------------------------------------------------------
class KinestopSidebarMenu : public tsl::elm::OverlayFrame {
private:
    bool m_showStatus;
    s32  m_subtitleWidth = -1;

public:
    KinestopSidebarMenu(const std::string& title, const std::string& subtitle, bool showStatus = false)
        : tsl::elm::OverlayFrame(title, subtitle), m_showStatus(showStatus) {}

    virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        this->setBoundaries(0, 0, 448, 720);
        if (m_contentElement != nullptr) {
            m_contentElement->setBoundaries(35, 97, 448 - 85, 720 - 73 - 105);
            m_contentElement->invalidate();
        }
    }

    virtual void draw(tsl::gfx::Renderer *renderer) override {
        renderer->clearScreen();

        // No dots here any more: the live preview is the sysmodule's real
        // dots, visible next to this 448px panel.
        renderer->drawRect(0, 0, 448, 720, a(tsl::defaultBackgroundColor));
        renderer->drawString(m_title, false, 20, 50, 32, tsl::defaultOverlayColor);
        renderer->drawString(m_subtitle, false, 20, 75, 15, tsl::bannerVersionTextColor);

        // Sysmodule status line, next to "by SertAy". Strings only change on
        // refresh (~1 Hz), so nothing is formatted here.
        if (m_showStatus) {
            if (m_subtitleWidth < 0)
                m_subtitleWidth = renderer->getTextDimensions(m_subtitle, false, 15).first;
            const s32 x = 20 + m_subtitleWidth + 18;
            const bool notice = !g_noticeStr.empty();
            renderer->drawString(notice ? g_noticeStr : g_statusStr, false, x, 75, 15,
                                 notice ? g_noticeColor : g_statusColor, 448 - 15 - x);
        }

        if (m_contentElement != nullptr) {
            m_contentElement->frame(renderer);
        }

        // Clean localized ASCII footer
        renderer->drawRect(15, 720 - 73, 448 - 30, 1, a(tsl::bottomSeparatorColor));
        if (g_config.general.language == 1) {
            renderer->drawString("B  Geri    A  Seç", false, 25, 720 - 73 + 36, 16, tsl::bannerVersionTextColor);
        } else {
            renderer->drawString("B  Back    A  Select", false, 25, 720 - 73 + 36, 16, tsl::bannerVersionTextColor);
        }
    }
};

// -------------------------------------------------------------
// PURE TEXT SCROLLABLE GUIDE (LOCALIZED WITH SertAy AS CREATOR)
// -------------------------------------------------------------
class ScrollableGuideTextElement : public tsl::elm::Element {
private:
    struct FormattedLine {
        std::string text;
        tsl::Color color;
        u32 fontSize;
        s32 topGap;
    };

    std::vector<FormattedLine> m_lines;
    float m_scrollY = 0.0f;
    float m_maxScroll = 0.0f;

public:
    ScrollableGuideTextElement() {
        this->m_isItem = false;
        initContent();
    }

    void initContent() {
        m_lines.clear();

        auto add_header = [this](const std::string& txt, tsl::Color col) {
            m_lines.push_back({txt, col, 20, 14});
        };
        auto add_sub = [this](const std::string& txt, tsl::Color col) {
            m_lines.push_back({txt, col, 17, 7});
        };
        auto add_body = [this](const std::string& txt) {
            m_lines.push_back({txt, make_color(225, 235, 245), 15, 3});
        };

        if (g_config.general.language == 1) {
            // ==================== TÜRKÇE REHBER ====================
            add_header("NASIL ÇALIŞIR?", make_color(80, 240, 120));
            add_body("Noktaları bu menü değil, arka planda çalışan");
            add_body("küçük bir sistem modülü (sysmodule) çizer.");
            add_body("Menüyü gizleseniz de noktalar ekranda kalır;");
            add_body("böylece Status Monitor gibi diğer overlay'leri");
            add_body("aynı anda kullanabilirsiniz. Ultrahand'in");
            add_body("varsayılan 4 MB overlay belleği yeterlidir.");
            add_body("Bu panel açıkken noktalar duraklatılır;");
            add_body("paneli açma kombinasyonuyla gizleyince");
            add_body("yeniden görünürler. B ile çıkarsanız");
            add_body("(Ultrahand menüsüne dönüş) duraklatılmış");
            add_body("kalırlar, böylece menü görünür kalır");
            add_body("(deneme sürümü). Geri getirmek için");
            add_body("Kinestop'u tekrar açıp kombinasyonla");
            add_body("gizleyin.");

            add_header("GELİŞTİRİCİ VE BAĞIŞ LİNKLERİ", make_color(80, 200, 255));
            add_body("Geliştirici: SertAy");
            add_body("Destek olmak veya ulaşmak isterseniz:");
            add_sub("Bağış: buymeacoffee.com/shiftbase", make_color(80, 240, 120));
            add_sub("Tüm Linkler: linktr.ee/sertay", make_color(80, 200, 255));
            add_sub("Instagram: @cilsertay", make_color(225, 120, 255));
            add_sub("YouTube: @SertAyyy", make_color(255, 90, 100));
            add_sub("TikTok: @sertayintardis", make_color(80, 240, 220));

            add_header("BU SÜRÜM HAKKINDA", make_color(255, 210, 60));
            add_body("Bu sürüm, SertAy'ın orijinal Kinestop");
            add_body("projesinin değiştirilmiş (sysmodule ayrımlı)");
            add_body("bir sürümüdür. Orijinal proje:");
            add_sub("github.com/sertay1/Kinestop", make_color(80, 200, 255));

            add_header("KİNESTOP NEDİR?", make_color(255, 170, 50));
            add_body("Kinestop, araçta (araba, otobüs, tren vb.)");
            add_body("oyun oynarken oluşan taşıt tutmasını (kinetoz)");
            add_body("önlemek için geliştirilmiştir. Fiziksel hareketle");
            add_body("senkronize çalışan noktalar çizerek gözünüzü");
            add_body("iç kulak denge sistemiyle senkronize eder.");

            add_header("AYARLAR VE ÖZELLİKLER", make_color(80, 200, 255));

            add_sub("Hareket Noktalarını Aç:", make_color(255, 210, 60));
            add_body("Noktaları çizen arka plan modülünü başlatır");
            add_body("veya durdurur. Menüyü kombinasyonla");
            add_body("gizleyince noktalar çalışmaya devam eder.");

            add_sub("Açılışta Noktaları Başlat:", make_color(255, 210, 60));
            add_body("Noktaları konsol açılırken otomatik başlatır.");
            add_body("Sistem belleği kullandığı için varsayılan");
            add_body("olarak kapalıdır. Açılışta sorun olursa SD");
            add_body("karttan şu klasörü silin:");
            add_body("atmosphere/contents/" KINESTOP_PROGRAM_ID_STR "/flags");

            add_sub("Desen (Yerleşim):", make_color(255, 210, 60));
            add_body("Noktaların ekrandaki yerleşim şablonu:");
            add_body("- Perimeter: 4 dış kenar çerçevesi");
            add_body("- Tüm Ekran: Ekranın ortası dahil her yer");
            add_body("- Yanlar: Yalnızca sağ ve sol kenarlar");
            add_body("- Köşeler: 4 köşeye odaklı gruplar");
            add_body("- Üst ve Alt: Yalnızca üst ve alt şeritler");
            add_body("- Çevre: Çift halkalı dairesel matris");

            add_sub("Yoğunluk (Nokta Sayısı):", make_color(255, 210, 60));
            add_body("Düşük, Orta, Yüksek ve Ultra seviyeleri.");

            add_sub("Renk Teması (12 Renk):", make_color(225, 120, 255));
            add_body("Her oyuna uygun 12 yüksek kontrastlı renk.");

            add_sub("Nokta Boyutu (1 px - 12 px):", make_color(80, 240, 120));
            add_body("Noktaların ekrandaki büyüklüğü.");

            add_sub("Nokta Saydamlığı (%10 - %100):", make_color(80, 240, 120));
            add_body("Noktaların şeffaflık seviyesi.");

            add_sub("Hassasiyet (0.2x - 6.0x):", make_color(255, 170, 50));
            add_body("Eğime ve ivmeye verilen hareket tepki gücü.");

            add_sub("Serbest Hareket Modu:", make_color(80, 240, 120));
            add_body("Açıkken noktalar ekran sınırlarına takılmaz;");
            add_body("gerçek bir araba penceresi gibi ekranın");
            add_body("dışına doğru doğal olarak süzülüp kaybolur.");

            add_sub("Görüntü ve Kayıtlarda Göster:", make_color(80, 240, 120));
            add_body("Açıkken noktalar ekran görüntülerine ve video");
            add_body("kayıtlarına da eklenir. Kapalıyken yalnızca");
            add_body("ekranda görünür.");

            add_sub("Yumuşatma (Alpha):", make_color(80, 200, 255));
            add_body("El titremelerini filtreler, akıcılık sağlar.");

            add_sub("Ölü Bölge (0.0 - 5.0 deg):", make_color(80, 200, 255));
            add_body("Küçük mikro titreşimleri yok sayar.");

            add_sub("Dikey / Yatay Yönü Ters Çevir:", make_color(255, 90, 100));
            add_body("Noktaların kayma yönünü tersine çevirir.");

            add_sub("Sıfır Noktasını Kalibre Et:", make_color(80, 240, 120));
            add_body("Konsolu o an tuttuğunuz açıyı (yatakta veya");
            add_body("araçta) yeni merkez sıfır açısı yapar.");
            add_body("Noktaların açık olması gerekir.");

            add_sub("Sıfır Noktasını Sıfırla:", make_color(80, 240, 120));
            add_body("Düz ufuk seviyesine geri döndürür.");

            add_sub("Ayarları Sıfırla:", make_color(255, 90, 100));
            add_body("Tüm ayarları fabrika varsayılanlarına alır.");
        } else {
            // ==================== ENGLISH GUIDE ====================
            add_header("HOW IT WORKS", make_color(80, 240, 120));
            add_body("The dots are drawn by a small background");
            add_body("sysmodule, not by this menu. Hide the menu");
            add_body("and the dots keep running, so you can use");
            add_body("other overlays (e.g. Status Monitor) at the");
            add_body("same time. Ultrahand's default 4 MB overlay");
            add_body("memory is enough.");
            add_body("The dots pause while this panel is open");
            add_body("and come back when you hide it with the");
            add_body("launch combo. Leaving with B (back to the");
            add_body("Ultrahand menu) keeps them paused so that");
            add_body("menu stays visible (test build). To bring");
            add_body("them back, open Kinestop again and hide it");
            add_body("with the combo.");

            add_header("CREATOR & DONATION LINKS", make_color(80, 200, 255));
            add_body("Developer: SertAy");
            add_body("If you want to support or reach out:");
            add_sub("Donation: buymeacoffee.com/shiftbase", make_color(80, 240, 120));
            add_sub("All Links: linktr.ee/sertay", make_color(80, 200, 255));
            add_sub("Instagram: @cilsertay", make_color(225, 120, 255));
            add_sub("YouTube: @SertAyyy", make_color(255, 90, 100));
            add_sub("TikTok: @sertayintardis", make_color(80, 240, 220));

            add_header("ABOUT THIS BUILD", make_color(255, 210, 60));
            add_body("This is a modified version (sysmodule split)");
            add_body("of the original Kinestop by SertAy:");
            add_sub("github.com/sertay1/Kinestop", make_color(80, 200, 255));

            add_header("WHAT IS KINESTOP?", make_color(255, 170, 50));
            add_body("Kinestop helps eliminate vehicle motion");
            add_body("sickness (kinetosis) while playing in cars,");
            add_body("buses or trains by rendering floating motion");
            add_body("cue dots that sync your vision with the inner");
            add_body("ear vestibular balance system.");

            add_header("SETTINGS EXPLAINED", make_color(80, 200, 255));

            add_sub("Enable Motion Dots:", make_color(255, 210, 60));
            add_body("Starts or stops the background sysmodule");
            add_body("that draws the dots. Hiding this menu");
            add_body("with the combo keeps the dots running.");

            add_sub("Start Dots at Boot:", make_color(255, 210, 60));
            add_body("Starts the dots automatically at boot. Off");
            add_body("by default because it uses System memory.");
            add_body("If boot ever fails, delete this SD folder:");
            add_body("atmosphere/contents/" KINESTOP_PROGRAM_ID_STR "/flags");

            add_sub("Pattern (Placement):", make_color(255, 210, 60));
            add_body("Choose where dots appear on screen:");
            add_body("- Perimeter: 4 screen borders");
            add_body("- Full Grid: Entire screen (including center)");
            add_body("- Sides: Left and right columns only");
            add_body("- Corners: 4 screen corners focused");
            add_body("- Top/Bottom: Top and bottom rows only");
            add_body("- Surround: Dual-ring circular field");

            add_sub("Density (Dot Count):", make_color(255, 210, 60));
            add_body("Controls dot quantity: Low, Med, High, Ultra.");

            add_sub("Color Theme:", make_color(225, 120, 255));
            add_body("12 high-contrast palettes for any game.");

            add_sub("Dot Size (1 px - 12 px):", make_color(80, 240, 120));
            add_body("Adjusts dot diameter on screen.");

            add_sub("Dot Opacity (10% - 100%):", make_color(80, 240, 120));
            add_body("Controls dot transparency level.");

            add_sub("Motion Sensitivity (0.2x - 6.0x):", make_color(255, 170, 50));
            add_body("Controls displacement strength on tilt.");

            add_sub("Free Floating Dots:", make_color(80, 240, 120));
            add_body("When enabled, dots float naturally beyond");
            add_body("screen borders like looking out a car window.");

            add_sub("Show in Screenshots/Videos:", make_color(80, 240, 120));
            add_body("When enabled, the dots also appear in");
            add_body("screenshots and video recordings. When off,");
            add_body("they are only shown on screen.");

            add_sub("Smoothing (Alpha):", make_color(80, 200, 255));
            add_body("Filters hand tremors for silky movement.");

            add_sub("Deadzone (0.0 - 5.0 deg):", make_color(80, 200, 255));
            add_body("Ignores small micro-vibrations.");

            add_sub("Invert Pitch / Roll:", make_color(255, 90, 100));
            add_body("Inverts vertical or horizontal dot motion.");

            add_sub("Calibrate Zero Position:", make_color(80, 240, 120));
            add_body("Locks your current handheld holding angle");
            add_body("(e.g. reclining in bed/car) as center zero.");
            add_body("The dots must be running.");

            add_sub("Reset Zero to Flat Horizon:", make_color(80, 240, 120));
            add_body("Restores flat level horizon zero reference.");

            add_sub("Reset Settings:", make_color(255, 90, 100));
            add_body("Restores all settings to factory defaults.");
        }
    }

    virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        this->setBoundaries(25, 95, 398, 545);

        float totalH = 10.0f;
        for (const auto& l : m_lines) {
            totalH += (float)l.topGap + (float)l.fontSize + 3.0f;
        }
        m_maxScroll = std::max(0.0f, totalH - 530.0f);
    }

    virtual void draw(tsl::gfx::Renderer *renderer) override {
        s32 bx = 30;
        s32 by = 97;
        s32 bh = 540;

        float curY = (float)by + 5.0f - m_scrollY;

        for (const auto& line : m_lines) {
            curY += (float)line.topGap;
            // Strict viewport clipping: Never draw above header or below footer!
            if (curY >= (float)by && (curY + (float)line.fontSize) <= (float)(by + bh)) {
                renderer->drawString(line.text, false, bx, (s32)std::round(curY), line.fontSize, line.color);
            }
            curY += (float)line.fontSize + 3.0f;
        }

        // Draw sleek vertical scrollbar
        if (m_maxScroll > 0.0f) {
            float barH = std::max(25.0f, (float)bh * ((float)bh / (m_maxScroll + (float)bh)));
            float barY = (float)by + (m_scrollY / m_maxScroll) * ((float)bh - barH);
            renderer->drawRect(415, (s32)std::round(barY), 3, (s32)std::round(barH), make_color(80, 140, 200, 180));
        }
    }

    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override {
        float scrollStep = 28.0f;

        if ((keysHeld & HidNpadButton_Up) || (keysDown & HidNpadButton_Up) || joyStickPosLeft.y > 12000) {
            m_scrollY = std::max(0.0f, m_scrollY - scrollStep);
            return true;
        }
        if ((keysHeld & HidNpadButton_Down) || (keysDown & HidNpadButton_Down) || joyStickPosLeft.y < -12000) {
            m_scrollY = std::min(m_maxScroll, m_scrollY + scrollStep);
            return true;
        }

        return false;
    }
};

// -------------------------------------------------------------
// USER GUIDE GUI
// -------------------------------------------------------------
class GuiKinestopGuide : public tsl::Gui {
private:
    ScrollableGuideTextElement *m_textElem = nullptr;

public:
    GuiKinestopGuide() {}

    virtual tsl::elm::Element* createUI() override {
        std::string title = (g_config.general.language == 1) ? "Yardım ve Rehber" : "Guide & Info";
        auto *frame = new KinestopSidebarMenu(title, "by SertAy");
        m_textElem = new ScrollableGuideTextElement();
        frame->setContent(m_textElem);
        return frame;
    }

    virtual void update() override {
        tick_background();
    }

    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override {
        // Pressing B pops Guide from stack, returning directly to Settings!
        if (keysDown & HidNpadButton_B) {
            tsl::goBack();
            return true;
        }

        if (m_textElem) {
            if (m_textElem->handleInput(keysDown, keysHeld, touchPos, joyStickPosLeft, joyStickPosRight)) {
                return true;
            }
        }

        return false;
    }
};

// -------------------------------------------------------------
// SETTINGS MENU GUI (FULL DYNAMIC LOCALIZATION & SEAMLESS FLOW)
// -------------------------------------------------------------
class GuiKinestopSettings : public tsl::Gui {
private:
    tsl::elm::ToggleListItem *m_toggleEnabled = nullptr;
    tsl::elm::ToggleListItem *m_toggleBoot    = nullptr;

public:
    GuiKinestopSettings() {
        // Fail-fast probes only (pm:dmnt, sm HasService): never blocks when
        // the sysmodule is stopped. Note: no auto-start here even if the
        // config says enabled; the status line just shows "Stopped".
        refresh_status();
    }

    virtual tsl::elm::Element* createUI() override {
        bool isTr = (g_config.general.language == 1);

        auto *frame = new KinestopSidebarMenu("Kinestop", "by SertAy", true);
        auto *list  = new tsl::elm::List();

        // ------------------ MASTER CONTROL ------------------
        list->addItem(new tsl::elm::CategoryHeader(isTr ? "Ana Kontrol" : "Master Control"));

        // Reflects whether the sysmodule is actually running.
        m_toggleEnabled = new tsl::elm::ToggleListItem(isTr ? "Hareket Noktalarını Aç" : "Enable Motion Dots",
                                                       g_statusRunning || (worker_busy() && g_job == Job::Start));
        m_toggleEnabled->setStateChangedListener([this](bool newState) {
            if (worker_busy()) {
                m_toggleEnabled->setState(!newState);  // a start/stop is already in flight
                return;
            }
            g_config.general.enabled = newState;
            save_config_now();  // the sysmodule reads config.ini when it starts
            start_job(newState ? Job::Start : Job::Stop);
        });
        list->addItem(m_toggleEnabled);

        // How the dots pause around this panel. Read live: the coexistence
        // test toggle further down changes it.
        list->addItem(new tsl::elm::CustomDrawer([isTr](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            static constexpr const char* pausedEn[] = {
                "Dots pause while this panel is open;",
                "hide it with the combo to resume them.",
                "After B they stay paused until you open",
                "Kinestop again and hide it with the combo.",
            };
            static constexpr const char* pausedTr[] = {
                "Panel açıkken noktalar duraklatılır;",
                "devam için paneli kombinasyonla gizleyin.",
                "B ile çıkarsanız, Kinestop'u tekrar açıp",
                "kombinasyonla gizleyene dek duraklar.",
            };
            static constexpr const char* testEn[] = {
                "Coexistence test on: the dots are not",
                "paused for this panel or other menus.",
            };
            static constexpr const char* testTr[] = {
                "Uyumluluk testi açık: noktalar bu panel",
                "ve diğer menüler için duraklatılmaz.",
            };
            const bool test = no_suspend_test();
            const char* const* lines = test ? (isTr ? testTr : testEn) : (isTr ? pausedTr : pausedEn);
            const int count = test ? 2 : 4;
            const tsl::Color note = make_color(255, 170, 50);
            for (int i = 0; i < count; ++i)
                renderer->drawString(lines[i], false, x + 19, y + 20 + i * NoteLineStep, 15, note);
        }), 4 * NoteLineStep + 12);

        // Diagnostics block (test build): lines are re-formatted by
        // refresh_status() about once per second.
        list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            for (int i = 0; i < g_diagCount; ++i)
                renderer->drawString(g_diagLines[i], false, x + 19, y + 16 + i * DiagLineStep, DiagFontSize,
                                     g_diagColors[i], w - 30);
        }), DiagBlockHeight);

        m_toggleBoot = new tsl::elm::ToggleListItem(isTr ? "Açılışta Noktaları Başlat" : "Start Dots at Boot",
                                                    file_exists(BootFlagPath));
        m_toggleBoot->setStateChangedListener([this](bool newState) {
            const bool tr = is_tr();
            if (newState) {
                if (!ipc::isInstalled()) {
                    show_notice(ColorError, tr ? "Yüklü değil" : "Not installed");
                    m_toggleBoot->setState(false);
                    return;
                }
                mkdir(BootFlagDir, 0777);
                FILE* f = std::fopen(BootFlagPath, "wb");
                if (f == nullptr) {
                    show_notice(ColorError, tr ? "Açılış bayrağı yazılamadı" : "Could not write boot flag");
                    m_toggleBoot->setState(false);
                    return;
                }
                std::fclose(f);
            } else {
                std::remove(BootFlagPath);
                if (file_exists(BootFlagPath)) {
                    show_notice(ColorError, tr ? "Açılış bayrağı silinemedi" : "Could not remove boot flag");
                    m_toggleBoot->setState(true);
                }
            }
        });
        list->addItem(m_toggleBoot);

        list->addItem(new tsl::elm::CustomDrawer([isTr](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            const tsl::Color note = make_color(255, 170, 50);
            renderer->drawString(isTr ? "Açılışta sistem belleği kullanır." : "Boot start uses System memory.",
                                 false, x + 19, y + 20, 15, note);
            renderer->drawString(isTr ? "Emin değilseniz kapalı bırakın." : "Leave it off if you are unsure.",
                                 false, x + 19, y + 40, 15, note);
        }), 52);

        // ------------------ PATTERN & DENSITY ------------------
        list->addItem(new tsl::elm::CategoryHeader(isTr ? "Yerleşim ve Yoğunluk" : "Placement & Density"));

        auto *patternTrackBar = isTr ?
            new tsl::elm::NamedStepTrackBar("", {"Perimeter", "Tüm Ekran", "Yanlar", "Köşeler", "Üst ve Alt", "Çevre"}, true, "Desen") :
            new tsl::elm::NamedStepTrackBar("", {"Perimeter", "Full Grid", "Sides", "Corners", "Top/Bottom", "Surround"}, true, "Pattern");
        patternTrackBar->setProgress(g_config.visual.style);
        patternTrackBar->setValueChangedListener([](u8 newIndex) {
            g_config.visual.style = newIndex;
            apply_setting();
        });
        list->addItem(patternTrackBar);

        auto *densityTrackBar = isTr ?
            new tsl::elm::NamedStepTrackBar("", {"Düşük", "Orta", "Yüksek", "Ultra"}, true, "Yoğunluk") :
            new tsl::elm::NamedStepTrackBar("", {"Low", "Medium", "High", "Ultra"}, true, "Density");
        densityTrackBar->setProgress(g_config.visual.density);
        densityTrackBar->setValueChangedListener([](u8 newIndex) {
            g_config.visual.density = newIndex;
            apply_setting();
        });
        list->addItem(densityTrackBar);

        // ------------------ COLOR THEME ------------------
        list->addItem(new tsl::elm::CategoryHeader(isTr ? "Renk Teması" : "Color Theme"));

        auto *colorTrackBar = isTr ?
            new tsl::elm::NamedStepTrackBar("", {
                "Buz Mavisi", "Beyaz", "Fıstık Yeşili", "Koyu Yeşil", "Sarı", "Kehribar",
                "Kırmızı", "Neon Pembe", "Mor", "Gök Mavisi", "Koyu Mavi", "Mat Gri"
            }, true, "Nokta Rengi") :
            new tsl::elm::NamedStepTrackBar("", {
                "Ice Blue", "White", "Neon Green", "Dark Green", "Yellow", "Amber",
                "Crimson", "Hot Pink", "Purple", "Sky Blue", "Navy Blue", "Warm Gray"
            }, true, "Dot Color");
        colorTrackBar->setProgress(std::clamp(g_config.visual.color, 0, (int)dots::NUM_THEMES - 1));
        colorTrackBar->setValueChangedListener([](u8 newIndex) {
            g_config.visual.color = newIndex;
            apply_setting();
        });
        list->addItem(colorTrackBar);

        // ------------------ VISUAL CUSTOMIZATION ------------------
        list->addItem(new tsl::elm::CategoryHeader(isTr ? "Görsel Özelleştirme" : "Visual Customization"));

        auto *sizeTrackBar = new tsl::elm::NamedStepTrackBar("", {
            "1 px", "2 px", "3 px", "4 px", "5 px", "6 px", "7 px", "8 px", "10 px", "12 px"
        }, true, isTr ? "Nokta Boyutu" : "Dot Size");
        int current_size_idx = std::clamp(g_config.visual.line_thickness, 0, (int)dots::NUM_SIZES - 1);
        sizeTrackBar->setProgress(current_size_idx);
        sizeTrackBar->setValueChangedListener([](u8 index) {
            g_config.visual.line_thickness = (int)index;
            apply_setting();
        });
        list->addItem(sizeTrackBar);

        auto *opacityTrackBar = new tsl::elm::StepTrackBar("", 21, false, true, isTr ? "Saydamlık" : "Dot Opacity", "%");
        int op_step = (int)std::round(g_config.visual.opacity * 20.0f);
        op_step = std::clamp(op_step, 2, 20); // 10% to 100%
        opacityTrackBar->setProgress(op_step);
        opacityTrackBar->setValueChangedListener([](u8 step) {
            if (step < 2) step = 2; // Min 10%
            if (step > 20) step = 20; // Max 100%
            g_config.visual.opacity = (float)step * 0.05f;
            apply_setting();
        });
        list->addItem(opacityTrackBar);

        auto *sensTrackBar = new tsl::elm::NamedStepTrackBar("", {
            "0.2x", "0.5x", "0.8x", "1.0x", "1.5x", "2.0x", "3.0x", "4.0x", "5.0x", "6.0x"
        }, true, isTr ? "Hassasiyet" : "Sensitivity");
        int sens_idx = 3; // default 1.0x
        for (int i = 0; i < (int)dots::NUM_SENSITIVITIES; ++i) {
            if (std::abs(g_config.calibration.sensitivity_roll - dots::SENSITIVITIES[i]) < 0.1f) {
                sens_idx = i; break;
            }
        }
        sensTrackBar->setProgress(sens_idx);
        sensTrackBar->setValueChangedListener([](u8 index) {
            float val = dots::SENSITIVITIES[std::clamp((int)index, 0, (int)dots::NUM_SENSITIVITIES - 1)];
            g_config.calibration.sensitivity_roll = val;
            g_config.calibration.sensitivity_pitch = val;
            apply_setting();
        });
        list->addItem(sensTrackBar);

        auto *toggleFreeFloat = new tsl::elm::ToggleListItem(
            isTr ? "Serbest Hareket Modu" : "Free Floating Dots",
            g_config.visual.free_floating
        );
        toggleFreeFloat->setStateChangedListener([](bool newState) {
            g_config.visual.free_floating = newState;
            apply_setting();
        });
        list->addItem(toggleFreeFloat);

        auto *toggleCaptures = new tsl::elm::ToggleListItem(
            isTr ? "Görüntü ve Kayıtlarda Göster" : "Show in Screenshots/Videos",
            g_config.visual.show_in_captures
        );
        toggleCaptures->setStateChangedListener([](bool newState) {
            g_config.visual.show_in_captures = newState;
            apply_setting();
        });
        list->addItem(toggleCaptures);

        // ------------------ FILTER & SMOOTHING ------------------
        list->addItem(new tsl::elm::CategoryHeader(isTr ? "Filtre ve Yumuşatma" : "Filter & Smoothing"));

        auto *alphaTrackBar = isTr ?
            new tsl::elm::NamedStepTrackBar("", {"Düşük (%50)", "Orta (%75)", "Akıcı (%85)", "Yüksek (%92)", "Ultra (%97)"}, true, "Yumuşatma") :
            new tsl::elm::NamedStepTrackBar("", {"Low (50%)", "Medium (75%)", "Smooth (85%)", "High (92%)", "Ultra (97%)"}, true, "Smoothing");
        int alpha_idx = 2;
        for (int i = 0; i < (int)dots::NUM_SMOOTHINGS; ++i) {
            if (std::abs(g_config.filter.alpha - dots::SMOOTHINGS[i]) < 0.05f) {
                alpha_idx = i; break;
            }
        }
        alphaTrackBar->setProgress(alpha_idx);
        alphaTrackBar->setValueChangedListener([](u8 index) {
            g_config.filter.alpha = dots::SMOOTHINGS[std::clamp((int)index, 0, (int)dots::NUM_SMOOTHINGS - 1)];
            apply_setting();
        });
        list->addItem(alphaTrackBar);

        auto *deadzoneTrackBar = new tsl::elm::NamedStepTrackBar("", {
            "0.0 deg", "0.2 deg", "0.5 deg", "1.0 deg", "1.5 deg", "2.0 deg", "3.0 deg", "5.0 deg"
        }, true, isTr ? "Ölü Bölge" : "Deadzone");
        int deadzone_idx = 2;
        for (int i = 0; i < (int)dots::NUM_DEADZONES; ++i) {
            if (std::abs(g_config.filter.deadzone_deg - dots::DEADZONES[i]) < 0.15f) {
                deadzone_idx = i; break;
            }
        }
        deadzoneTrackBar->setProgress(deadzone_idx);
        deadzoneTrackBar->setValueChangedListener([](u8 index) {
            g_config.filter.deadzone_deg = dots::DEADZONES[std::clamp((int)index, 0, (int)dots::NUM_DEADZONES - 1)];
            apply_setting();
        });
        list->addItem(deadzoneTrackBar);

        // ------------------ CALIBRATION & CONTROLS ------------------
        list->addItem(new tsl::elm::CategoryHeader(isTr ? "Kalibrasyon ve Kontroller" : "Calibration & Controls"));

        auto *toggleInvertPitch = new tsl::elm::ToggleListItem(isTr ? "Dikey Yönü Ters Çevir" : "Invert Pitch (Up/Down)", g_config.calibration.invert_pitch);
        toggleInvertPitch->setStateChangedListener([](bool newState) {
            g_config.calibration.invert_pitch = newState;
            apply_setting();
        });
        list->addItem(toggleInvertPitch);

        auto *toggleInvertRoll = new tsl::elm::ToggleListItem(isTr ? "Yatay Yönü Ters Çevir" : "Invert Roll (Left/Right)", g_config.calibration.invert_roll);
        toggleInvertRoll->setStateChangedListener([](bool newState) {
            g_config.calibration.invert_roll = newState;
            apply_setting();
        });
        list->addItem(toggleInvertRoll);

        // Calibration Handheld Zero Button: the sysmodule owns the IMU, so it
        // takes the zero and hands the offsets back for config.ini.
        auto *calibCurrentBtn = new tsl::elm::ListItem(
            isTr ? "Sıfır Noktasını Kalibre Et" : "Calibrate Zero Position",
            isTr ? "Mevcut tutuş açısını merkez sıfır yapar" : "Lock current holding angle as center zero"
        );
        calibCurrentBtn->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                const bool tr = is_tr();
                CtlLock lock(g_ctlMutex);
                if (worker_busy() || !ipc::isRunning()) {
                    show_notice(ColorWarn, tr ? "Önce noktaları açın" : "Enable the dots first");
                    return true;
                }
                KinestopCalibration cal{};
                const Result rc = ipc::calibrate(&cal);
                if (R_SUCCEEDED(rc)) {
                    g_config.calibration.pitch_offset = cal.pitch_offset;
                    g_config.calibration.roll_offset  = cal.roll_offset;
                    save_config_now();
                    show_notice(ColorOk, tr ? "Sıfır noktası kalibre edildi" : "Zero position calibrated");
                } else if (rc == ipc::ResultNotRunning) {
                    show_notice(ColorWarn, tr ? "Önce noktaları açın" : "Enable the dots first");
                } else {
                    show_notice(ColorError, tr ? "Kalibrasyon başarısız: " KS_RC_FMT : "Calibration failed: " KS_RC_FMT, KS_RC_ARGS(rc));
                }
                return true;
            }
            return false;
        });
        list->addItem(calibCurrentBtn);

        // Reset Zero to Horizon
        auto *resetCalibItem = new tsl::elm::ListItem(isTr ? "Sıfır Noktasını Sıfırla" : "Reset Zero to Flat Horizon");
        resetCalibItem->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                g_config.calibration.pitch_offset = 0.0f;
                g_config.calibration.roll_offset = 0.0f;
                save_config_now();
                push_config();
                return true;
            }
            return false;
        });
        list->addItem(resetCalibItem);

        // ------------------ DIAGNOSTICS (TEST BUILD) ------------------
        list->addItem(new tsl::elm::CategoryHeader(isTr ? "Tanılama (test sürümü)" : "Diagnostics (test build)"));

        // Each toggle is followed by a note saying what it does. The two
        // layer toggles pick one of three layer modes; both off is the
        // normal full-screen layer.
        struct DebugToggle { const char* en; const char* tr; u32 bit; NoteText intro; NoteText note; };
        static constexpr DebugToggle debugToggles[] = {
            { "Blink test", "Yanıp sönme testi", KINESTOP_DEBUG_BLINK_TEST, {}, {
                { "Dots off 3 s out of every 10 s.", nullptr },
                { "Noktalar her 10 sn'de 3 sn kapanır.", nullptr },
            } },
            { "Unscaled test layer", "Ölçeklenmemiş test katmanı", KINESTOP_DEBUG_UNSCALED_LAYER, {
                { "Layer mode. With both toggles below off:",
                  "full screen 2x (default), the 640x360",
                  "image scaled 2x to fill the screen.", nullptr },
                { "Katman modu. Alttaki iki ayar kapalıysa:",
                  "tam ekran 2x (varsayılan), 640x360",
                  "görüntü 2 kat büyütülüp ekranı doldurur.", nullptr },
            }, {
                { "Quarter 1x: 640x360 image at 1:1 in the",
                  "top-left quarter (pattern at half size).", nullptr },
                { "Çeyrek 1x: 640x360 görüntü 1:1 boyutta",
                  "sol üst çeyrekte (tüm desen, yarı boyut).", nullptr },
            } },
            { "Small 2x-scaled test layer", "Küçük 2x ölçekli test katmanı", KINESTOP_DEBUG_SCALED_SMALL, {}, {
                { "Quarter 2x: a 320x180 image scaled 2x",
                  "into the top-left quarter. If both layer",
                  "toggles are on, the unscaled one wins.", nullptr },
                { "Çeyrek 2x: 320x180 görüntü 2 kat",
                  "büyütülüp sol üst çeyreğe çizilir. İki",
                  "katman ayarı da açıksa ölçeklenmemiş",
                  "katman geçerli olur.", nullptr },
            } },
            { "Verbose log", "Ayrıntılı günlük", KINESTOP_DEBUG_VERBOSE_LOG, {}, {
                { "1 line per second in sysmodule.log;",
                  "each SD write may cause a small hitch.", nullptr },
                { "sysmodule.log'a saniyede 1 satır yazar;",
                  "her SD yazımı küçük takılma yapabilir.", nullptr },
            } },
            { "Low Z test layer (use with the test below)", "Düşük Z test katmanı (alttaki testle kullanın)",
              KINESTOP_DEBUG_LOW_Z, {}, {
                { "Puts the dots at Z 33, far below Tesla",
                  "menus. Turn on, then turn on 'Keep dots",
                  "while menus open' and check whether this",
                  "panel and the dots both stay visible.", nullptr },
                { "Noktaları Z 33'e, Tesla menülerinin çok",
                  "altına koyar. Açın, sonra 'Menüler",
                  "açıkken noktaları göster'i açıp bu panel",
                  "ve noktalar birlikte görünüyor mu, bakın.", nullptr },
            } },
            { "Keep dots while menus open (coexistence test)",
              "Menüler açıkken noktaları göster (uyumluluk testi)", KINESTOP_DEBUG_NO_SUSPEND, {}, {
                { "Dots stay on while this panel and the",
                  "Ultrahand menu are open. If a menu is",
                  "gone for more than a few seconds, reboot.",
                  "This stays on after a reboot: turn it",
                  "off before starting the dots, or set",
                  "debug_flags=4 in config.ini from a PC." },
                { "Bu panel ve Ultrahand menüsü açıkken",
                  "noktalar ekranda kalır. Menü birkaç",
                  "saniyede geri gelmezse yeniden başlatın.",
                  "Bu ayar yeniden başlatınca da açık kalır:",
                  "noktaları başlatmadan önce kapatın ya da",
                  "PC'den config.ini'de debug_flags=4 yapın." },
            } },
        };
        for (const auto& dt : debugToggles) {
            if (dt.intro.en[0] != nullptr)
                add_note(list, dt.intro, isTr);
            auto *toggle = new tsl::elm::ToggleListItem(isTr ? dt.tr : dt.en,
                                                        (g_config.general.debug_flags & dt.bit) != 0);
            const u32 bit = dt.bit;
            toggle->setStateChangedListener([bit](bool newState) {
                if (newState)
                    g_config.general.debug_flags |= (int)bit;
                else
                    g_config.general.debug_flags &= ~(int)bit;
                g_config.sanitize();
                save_config_now();
                push_config();
                // Coexistence test: takes effect at once, with this panel
                // still on screen. On: resume the dots (the experiment).
                // Off: suspend them again like a normal open panel.
                if (bit == KINESTOP_DEBUG_NO_SUSPEND)
                    suspend_dots(!newState, !newState);
                g_refreshNow = true;
            });
            list->addItem(toggle);
            add_note(list, dt.note, isTr);
        }

        // ------------------ GUIDE & MAINTENANCE ------------------
        list->addItem(new tsl::elm::CategoryHeader(isTr ? "Rehber ve Bakım" : "Guide & Maintenance"));

        // 1. Guide & Info
        auto *guideBtn = new tsl::elm::ListItem(isTr ? "Yardım ve Rehber" : "Guide & Info");
        guideBtn->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<GuiKinestopGuide>();
                return true;
            }
            return false;
        });
        list->addItem(guideBtn);

        // 2. Language Selector (Between Guide and Reset Settings)
        auto *langTrackBar = new tsl::elm::NamedStepTrackBar("", {"English", "Türkçe"}, true, isTr ? "Dil / Language" : "Language");
        langTrackBar->setProgress(g_config.general.language);
        langTrackBar->setValueChangedListener([](u8 newIndex) {
            if (g_config.general.language != newIndex) {
                g_config.general.language = newIndex;
                save_config_now();
                g_needGuiReload = true;
            }
        });
        list->addItem(langTrackBar);

        // 3. Reset Settings
        auto *resetDefaultsBtn = new tsl::elm::ListItem(isTr ? "Ayarları Sıfırla" : "Reset Settings");
        resetDefaultsBtn->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                // Everything goes back to defaults (as in the original),
                // except "enabled": it mirrors the master toggle, i.e.
                // whether the sysmodule is running, which a reset does not
                // start or stop.
                const bool enabled = g_config.general.enabled;
                g_config.set_defaults();
                g_config.general.enabled = enabled;
                save_config_now();
                push_config();
                g_needGuiReload = true;
                return true;
            }
            return false;
        });
        list->addItem(resetDefaultsBtn);

        frame->setContent(list);
        return frame;
    }

    virtual void update() override {
        if (g_needGuiReload) {
            g_needGuiReload = false;
            g_refreshNow = true;  // re-format the status line in the new language
            tsl::swapTo<GuiKinestopSettings>();
            return;
        }

        tick_background();

        // Keep the master toggle in sync with reality (failed start, the
        // sysmodule exiting, ...). Not while a job is in flight: the toggle
        // already shows where it is heading.
        if (!worker_busy() && m_toggleEnabled != nullptr && m_toggleEnabled->getState() != g_statusRunning)
            m_toggleEnabled->setState(g_statusRunning);
    }

    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override {
        // B closes the panel (back to the Ultrahand menu). The dots stay
        // suspended (see exitServices) until Kinestop is opened again and
        // hidden with the combo; the note under the master toggle says so.
        if (keysDown & HidNpadButton_B) {
            flush_config();
            tsl::goBack();
            return true;
        }
        return false;
    }
};

class KinestopOverlay : public tsl::Overlay {
public:
    // While this panel is on screen the sysmodule takes its own VI layer
    // down (test.2 showed the panel and the Ultrahand menu stay visible that
    // way). The KINESTOP_DEBUG_NO_SUSPEND coexistence test turns every
    // suspend into a resume (suspend_dots). initServices() runs
    // before libtesla creates its layer (initScreen), exitServices() after
    // it is destroyed (exitScreen). onShow() runs on the main loop thread;
    // onHide() also from Ultrahand's background event thread, hence the lock.
    virtual void initServices() override {
        g_config.load();
        ipc::initialize();  // pm:shell (launch/terminate) + pm:dmnt (liveness)
        suspend_dots(true, true);
        g_suspendedAtInit = true;
    }

    // The process only exits to load another Tesla overlay: the Ultrahand
    // menu (B at the root, combo return - often shown at once) or another
    // overlay's launch combo. Resuming here would put our layer back up
    // just as that overlay's layer appears, i.e. symptom (A) for the menu
    // itself. So the dots stay suspended (and are suspended again if the
    // panel was hidden); they resume when this panel is hidden with the
    // combo (onHide), or when the sysmodule is restarted. Closing the
    // Ultrahand menu afterwards cannot be detected from here, so the panel
    // and the guide tell the user to reopen Kinestop and hide it instead.
    virtual void exitServices() override {
        CtlLock lock(g_ctlMutex);
        g_panelVisible.store(false, std::memory_order_release);
        if (worker_busy())
            on_job_finished();  // joins: bounded, start() <= ~1.5s, stop() <= ~1s
        flush_config();
        suspend_dots(true, false);
        ipc::exit();
    }

    virtual void onShow() override {
        CtlLock lock(g_ctlMutex);
        g_panelVisible.store(true, std::memory_order_release);
        if (g_suspendedAtInit)
            g_suspendedAtInit = false;  // keep the ack time measured there
        else
            suspend_dots(true, true);   // waits <= SuspendWaitNs for the layer to go
        g_refreshNow = true;
    }

    virtual void onHide() override {
        CtlLock lock(g_ctlMutex);
        g_panelVisible.store(false, std::memory_order_release);
        g_suspendedAtInit = false;
        // Rare: hidden while a start/stop is in flight. Finish it first (the
        // worker may be about to suspend, and ipc_client is single-threaded).
        if (worker_busy())
            on_job_finished();
        flush_config();
        suspend_dots(false, false);
    }

    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<GuiKinestopSettings>();
    }
};

int main(int argc, char **argv) {
    return tsl::loop<KinestopOverlay>(argc, argv);
}
