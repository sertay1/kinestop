/*
 * Kinestop overlay (by SertAy).
 *
 * This is a modification of the original Kinestop by SertAy
 * (https://github.com/sertay1/Kinestop), optimized to run in Ultrahand's
 * default 4 MB overlay memory. kinestop.ovl runs in one of two modes, picked
 * by its launch arguments in main() (the same relaunch-per-mode pattern
 * Status Monitor uses):
 *
 *   SETTINGS (default): the normal 448x720 settings panel. Leaving it (B, or
 *     a hide: menu combo, HOME, power) with the dots enabled relaunches
 *     kinestop.ovl in HUD mode; B with the dots disabled goes back to the
 *     Ultrahand menu.
 *   HUD ("-hud"): a 640x360 framebuffer that VI scales up to a full-screen
 *     layer (Kinestop patch in libtesla). It draws only the dots, gives every
 *     button to the game and is never hidden; the menu combo relaunches
 *     kinestop.ovl in SETTINGS mode.
 *
 * The original kept a 1280x720 framebuffer and needed 8 MB of overlay memory.
 */

#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "config.hpp"
#include "dots.hpp"
#include "filter.hpp"
#include "hud_frame.hpp"
#include "sensor.hpp"

namespace {
    namespace dots = kinestop::dots;

    constexpr const char* HudModeLabel = "Motion Dots";  // its name in Ultrahand's "Launch modes"
    constexpr u16 HudFramebufferWidth  = 640;           // scaled 2x (handheld) / 3x (docked) to full screen
    constexpr u16 HudFramebufferHeight = 360;

    kinestop::Config g_config;
    bool g_hudMode = false;
    std::string g_selfName = "kinestop.ovl";  // our file name in /switch/.overlays (argv[0])
    bool g_needGuiReload = false;

    constexpr u64 SaveDebounceNs   =  500000000ULL;  // coalesce trackbar drags into one ini write
    constexpr u64 NoticeNs         = 3000000000ULL;  // transient notice duration
    constexpr u64 SensorRetryNs    = 1000000000ULL;  // retry starting sensors that failed to start
    constexpr u64 SensorFreshNs    =  500000000ULL;  // orientation counts as current this long after a sample

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

    inline bool is_tr() {
        return g_config.general.language == 1;
    }

    inline u64 now_ns() {
        return armTicksToNs(armGetSystemTick());
    }

    // ---------------------------------------------------------------------
    // MODE SWITCHING
    // Each mode is its own launch of kinestop.ovl: the next launch is queued
    // with setNextOverlay() and this process exits.
    // ---------------------------------------------------------------------
    void queue_relaunch(bool hud) {
        // A mode switch is not an exit: no exit rumble/sound on the way out,
        // and the HUD comes up silently.
        skipClosingExitFeedback = true;
        tsl::setNextOverlay(ult::OVERLAY_PATH + g_selfName, hud ? std::string(kinestop::hud::HUD_ARG) + " --silentLaunch" : "");
    }

    // From the main (UI) thread.
    void relaunch(bool hud) {
        queue_relaunch(hud);
        tsl::Overlay::get()->close(true);
    }

    // Closes Kinestop into the Ultrahand menu, also when this session was
    // started by a key combo (which clears Ultrahand's in_overlay flag): the
    // same in_overlay + --skipCombo return libtesla does for combo returns.
    void open_ultrahand_menu() {
        ult::setIniFileValue(ult::ULTRAHAND_CONFIG_INI_PATH, ult::ULTRAHAND_PROJECT_NAME, ult::IN_OVERLAY_STR, ult::TRUE_STR);
        tsl::setNextOverlay(returnOverlayPath, "--skipCombo");
        tsl::Overlay::get()->close(true);
    }

    // ---------------------------------------------------------------------
    // SENSOR
    // Sampled on the main thread once per frame. libtesla's background poller
    // restarts HID on the power button (hidExit + hidInitialize, Kinestop
    // patch hook): the sensor is shut down before that (a registered
    // SevenSixAxis buffer makes libnx abort in hidExit) and started again
    // after, under g_sensorMutex. In between (g_hidDown) a sensor_start()
    // only records the request; the mutex is not held across the restart
    // because the poller takes libtesla's input mutex inside it, while the
    // main thread calls sensor_start() from handleInput() under that mutex.
    // ---------------------------------------------------------------------
    std::mutex                  g_sensorMutex;
    kinestop::SensorManager     g_sensor;
    bool                        g_sensorWanted = false;  // between sensor_start() and sensor_stop()
    bool                        g_sensorSeven  = false;  // console sensor (SevenSixAxis) requested
    bool                        g_hidDown      = false;  // libtesla is restarting HID
    kinestop::ComplementaryFilter g_filter;
    kinestop::Orientation       g_orientation;           // raw filtered orientation (calibration not applied)
    u64                         g_lastSampleTick = 0;    // 0 = no sample yet
    u64                         g_nextRetryTick  = 0;

    // Returns the SevenSixAxis Result (0 when it runs or was not requested).
    Result sensor_start(bool seven) {
        std::lock_guard lock(g_sensorMutex);
        g_sensor.exit();
        g_sensorWanted = true;
        g_sensorSeven  = seven;
        if (!g_hidDown)
            g_sensor.init(seven);
        return g_sensor.seven_rc();
    }

    void sensor_stop() {
        std::lock_guard lock(g_sensorMutex);
        g_sensorWanted = false;
        g_sensor.exit();
    }

    // tsl::hidReinitHook, on the background poller thread.
    void on_hid_reinit(bool reinitialized) {
        std::lock_guard lock(g_sensorMutex);
        g_hidDown = !reinitialized;
        if (!reinitialized)
            g_sensor.exit();
        else if (g_sensorWanted)
            g_sensor.init(g_sensorSeven);  // checks that hid came back
    }

    void sensor_tick() {
        std::lock_guard lock(g_sensorMutex);
        const u64 now = armGetSystemTick();
        if (now >= g_nextRetryTick) {
            g_nextRetryTick = now + armNsToTicks(SensorRetryNs);
            g_sensor.retry_start();
        }

        kinestop::SensorSample smp;
        if (!g_sensor.sample(smp))
            return;
        // dt since the previous fresh sample, so gyro integration covers
        // frames without one (clamped inside update()).
        const float dt = g_lastSampleTick != 0 ? (float)armTicksToNs(now - g_lastSampleTick) / 1e9f : 0.0f;
        g_orientation = g_filter.update(smp.accel, smp.gyro, dt, g_config);
        g_lastSampleTick = now;
    }

    bool sensor_fresh() {
        return g_lastSampleTick != 0 && armTicksToNs(armGetSystemTick() - g_lastSampleTick) < SensorFreshNs;
    }

    // ---------------------------------------------------------------------
    // CONFIG PERSISTENCE (settings mode)
    // Trackbar drags fire one change per step, so writes are debounced;
    // toggles, language, resets and calibration are written at once.
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

    void apply_setting() {
        g_config.sanitize();
        g_saveDirty   = true;
        g_saveDirtyNs = now_ns();
    }

    // Transient notice next to "by SertAy".
    std::string g_noticeStr;
    tsl::Color  g_noticeColor = ColorOk;
    u64         g_noticeUntilNs = 0;

    __attribute__((format(printf, 2, 3)))
    void show_notice(const tsl::Color& color, const char* fmt, ...) {
        char buf[96];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        g_noticeStr     = buf;
        g_noticeColor   = color;
        g_noticeUntilNs = now_ns() + NoticeNs;
    }

    // Per-frame housekeeping of the settings Guis.
    void tick_settings() {
        sensor_tick();
        const u64 now = now_ns();
        if (g_saveDirty && now - g_saveDirtyNs >= SaveDebounceNs)
            save_config_now();
        if (g_noticeUntilNs != 0 && now >= g_noticeUntilNs) {
            g_noticeUntilNs = 0;
            g_noticeStr.clear();
        }
    }

    // (Re)starts the sensor for the "Console Motion Sensor" setting.
    void apply_console_sensor() {
        const Result rc = sensor_start(g_config.general.console_sensor);
        if (R_FAILED(rc))
            show_notice(ColorError, is_tr() ? "Konsol sensörü başlatılamadı: %04u-%04u" : "Console sensor failed: %04u-%04u",
                        (unsigned)(2000 + R_MODULE(rc)), (unsigned)R_DESCRIPTION(rc));
    }

    // Lets Ultrahand assign a key combo to the HUD mode ("Launch modes" in
    // the overlay's settings), as Status Monitor does. Written only when
    // missing or different, so the SD card is not touched on every launch.
    // overlays.ini itself is Ultrahand's: left alone when it does not exist.
    void register_launch_modes() {
        if (!ult::isFile(ult::OVERLAYS_INI_FILEPATH))
            return;
        const std::string args   = std::string("(") + kinestop::hud::HUD_ARG + ")";
        const std::string labels = std::string("(") + HudModeLabel + ")";
        if (ult::parseValueFromIniSection(ult::OVERLAYS_INI_FILEPATH, g_selfName, "mode_args") != args)
            ult::setIniFileValue(ult::OVERLAYS_INI_FILEPATH, g_selfName, "mode_args", args);
        if (ult::parseValueFromIniSection(ult::OVERLAYS_INI_FILEPATH, g_selfName, "mode_labels") != labels)
            ult::setIniFileValue(ult::OVERLAYS_INI_FILEPATH, g_selfName, "mode_labels", labels);
    }

    // Static orange note lines under a list item.
    constexpr s32 NoteLineStep = 20;

    void add_note(tsl::elm::List* list, const char* const* lines, int count) {
        list->addItem(new tsl::elm::CustomDrawer([lines, count](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            const tsl::Color color = make_color(255, 170, 50);
            for (int i = 0; i < count; ++i)
                renderer->drawString(lines[i], false, x + 19, y + 20 + i * NoteLineStep, 15, color);
        }), (u16)(count * NoteLineStep + 12));
    }

    // Draws the dots for the current orientation into libtesla's current
    // framebuffer (cleared by the caller): same premultiplied RGBA4444,
    // block-linear layout as libtesla's own drawing.
    void draw_hud_dots(tsl::gfx::Renderer* renderer, const kinestop::hud::DotFrame& frame) {
        const kinestop::raster::Surface surface{
            static_cast<uint16_t*>(renderer->getCurrentFramebuffer()),
            tsl::cfg::FramebufferWidth, tsl::cfg::FramebufferHeight};
        if (surface.pixels == nullptr)
            return;
        kinestop::hud::draw_dots(surface, frame, dots::compute_offset(g_orientation, g_config),
                                 g_config.visual.free_floating);
    }
}

// Forward declarations
class GuiKinestopSettings;
class GuiKinestopGuide;

// -------------------------------------------------------------
// IN-GAME FULL-SCREEN HUD (HUD mode)
// -------------------------------------------------------------
class KinestopHudElement : public tsl::elm::Element {
private:
    kinestop::hud::DotFrame m_frame;

public:
    KinestopHudElement() {
        this->m_isItem = false;
        kinestop::hud::build_dot_frame(g_config, tsl::cfg::FramebufferWidth, m_frame);
    }

    virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        this->setBoundaries(0, 0, tsl::cfg::FramebufferWidth, tsl::cfg::FramebufferHeight);
    }

    virtual void draw(tsl::gfx::Renderer *renderer) override {
        renderer->clearScreen();
        if (g_config.general.enabled)
            draw_hud_dots(renderer, m_frame);
    }
};

class GuiKinestopHud : public tsl::Gui {
public:
    virtual tsl::elm::Element* createUI() override {
        return new KinestopHudElement();
    }

    virtual void update() override {
        // Launched with the dots turned off (Ultrahand's "Motion Dots"
        // launch mode or its combo): open the settings instead.
        if (!g_config.general.enabled) {
            relaunch(false);
            return;
        }
        sensor_tick();
    }

    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override {
        // With tsl::disableHiding libtesla ignores the menu combo while we
        // are shown: it opens the settings here instead. (Another overlay's
        // combo still works: libtesla's poller launches it directly.)
        const u64 combo = tsl::cfg::launchCombo;
        if ((keysHeld & combo) == combo && (keysDown & combo)) {
            relaunch(false);
            return true;
        }
        // Everything else belongs to the game (also keeps B from closing us).
        return true;
    }
};

// -------------------------------------------------------------
// STANDARD 448px SIDEBAR FRAME (by SertAy)
// -------------------------------------------------------------
class KinestopSidebarMenu : public tsl::elm::OverlayFrame {
private:
    bool m_showNotice;
    s32  m_subtitleWidth = -1;

public:
    KinestopSidebarMenu(const std::string& title, const std::string& subtitle, bool showNotice = false)
        : tsl::elm::OverlayFrame(title, subtitle), m_showNotice(showNotice) {}

    virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        this->setBoundaries(0, 0, 448, 720);
        if (m_contentElement != nullptr) {
            m_contentElement->setBoundaries(35, 97, 448 - 85, 720 - 73 - 105);
            m_contentElement->invalidate();
        }
    }

    virtual void draw(tsl::gfx::Renderer *renderer) override {
        renderer->clearScreen();

        renderer->drawRect(0, 0, 448, 720, a(tsl::defaultBackgroundColor));
        renderer->drawString(m_title, false, 20, 50, 32, tsl::defaultOverlayColor);
        renderer->drawString(m_subtitle, false, 20, 75, 15, tsl::bannerVersionTextColor);

        // Transient notice (calibration, sensor errors) next to "by SertAy".
        if (m_showNotice && !g_noticeStr.empty()) {
            if (m_subtitleWidth < 0)
                m_subtitleWidth = renderer->getTextDimensions(m_subtitle, false, 15).first;
            const s32 x = 20 + m_subtitleWidth + 18;
            renderer->drawString(g_noticeStr, false, x, 75, 15, g_noticeColor, 448 - 15 - x);
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
            add_body("Bu panelde ayarlar bulunur. B tuşu veya");
            add_body("menü kombinasyonuyla çıktığınızda");
            add_body("noktalar başlar: oyun oynarken ekranda");
            add_body("görünürler ve tüm tuşlar oyunda kalır.");
            add_body("Buraya dönmek için menü kombinasyonuna");
            add_body("yeniden basın. Noktalar kapalıyken B,");
            add_body("Ultrahand menüsüne döner.");
            add_body("Aynı anda yalnızca bir Tesla overlay'i");
            add_body("çalışabilir: başka bir overlay'i (veya");
            add_body("Ultrahand menüsünü) açmak noktaları");
            add_body("durdurur. Geri getirmek için Kinestop'u");
            add_body("yeniden açıp B'ye basın. Noktalar");
            add_body("açıkken Ultrahand menüsüne gitmek için");
            add_body("Rehber ve Bakım altındaki Ultrahand");
            add_body("Menüsü seçeneğini kullanın.");
            add_body("Bu panel açıkken noktalar görünmez:");
            add_body("değişiklikleri görmek için B'ye basın.");
            add_body("Ultrahand'in varsayılan 4 MB overlay");
            add_body("belleği yeterlidir.");

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
            add_body("projesinin değiştirilmiş (4 MB overlay");
            add_body("belleği için optimize edilmiş) bir");
            add_body("sürümüdür. Orijinal proje:");
            add_sub("github.com/sertay1/Kinestop", make_color(80, 200, 255));

            add_header("KİNESTOP NEDİR?", make_color(255, 170, 50));
            add_body("Kinestop, araçta (araba, otobüs, tren vb.)");
            add_body("oyun oynarken oluşan taşıt tutmasını (kinetoz)");
            add_body("önlemek için geliştirilmiştir. Fiziksel hareketle");
            add_body("senkronize çalışan noktalar çizerek gözünüzü");
            add_body("iç kulak denge sistemiyle senkronize eder.");

            add_header("AYARLAR VE ÖZELLİKLER", make_color(80, 200, 255));

            add_sub("Hareket Noktalarını Aç:", make_color(255, 210, 60));
            add_body("Açıkken bu paneli kapatınca noktalar");
            add_body("başlar. Kapalıyken B, Ultrahand");
            add_body("menüsüne döner.");

            add_sub("Konsol Hareket Sensörü:", make_color(255, 210, 60));
            add_body("Konsolun kendi hareket sensörünü de");
            add_body("okur; böylece noktalar HOME menüsünde");
            add_body("de hareket eder (orada kumanda hareket");
            add_body("verisi durur). 512 KB overlay belleği");
            add_body("kullanır; varsayılan olarak kapalıdır.");

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
            add_body("HOME menüsünde Konsol Hareket Sensörü");
            add_body("açık olmalıdır.");

            add_sub("Sıfır Noktasını Sıfırla:", make_color(80, 240, 120));
            add_body("Düz ufuk seviyesine geri döndürür.");

            add_sub("Ayarları Sıfırla:", make_color(255, 90, 100));
            add_body("Tüm ayarları fabrika varsayılanlarına alır.");
        } else {
            // ==================== ENGLISH GUIDE ====================
            add_header("HOW IT WORKS", make_color(80, 240, 120));
            add_body("This panel holds the settings. Leave it");
            add_body("with B or the menu combo and the dots");
            add_body("start: they stay on screen while you play,");
            add_body("and the game keeps every button. Press");
            add_body("the menu combo again to come back here.");
            add_body("With the dots off, B returns to the");
            add_body("Ultrahand menu.");
            add_body("Only one Tesla overlay runs at a time:");
            add_body("opening another overlay (or the Ultrahand");
            add_body("menu) stops the dots. Open Kinestop again");
            add_body("and press B to bring them back. With");
            add_body("the dots on, use Ultrahand Menu (under");
            add_body("Guide & Maintenance) to get there.");
            add_body("The dots are not shown while this panel");
            add_body("is open: press B to see your changes.");
            add_body("Ultrahand's default 4 MB overlay memory");
            add_body("is enough.");

            add_header("CREATOR & DONATION LINKS", make_color(80, 200, 255));
            add_body("Developer: SertAy");
            add_body("If you want to support or reach out:");
            add_sub("Donation: buymeacoffee.com/shiftbase", make_color(80, 240, 120));
            add_sub("All Links: linktr.ee/sertay", make_color(80, 200, 255));
            add_sub("Instagram: @cilsertay", make_color(225, 120, 255));
            add_sub("YouTube: @SertAyyy", make_color(255, 90, 100));
            add_sub("TikTok: @sertayintardis", make_color(80, 240, 220));

            add_header("ABOUT THIS BUILD", make_color(255, 210, 60));
            add_body("This is a modified version (optimized");
            add_body("for 4 MB of overlay memory) of the");
            add_body("original Kinestop by SertAy:");
            add_sub("github.com/sertay1/Kinestop", make_color(80, 200, 255));

            add_header("WHAT IS KINESTOP?", make_color(255, 170, 50));
            add_body("Kinestop helps eliminate vehicle motion");
            add_body("sickness (kinetosis) while playing in cars,");
            add_body("buses or trains by rendering floating motion");
            add_body("cue dots that sync your vision with the inner");
            add_body("ear vestibular balance system.");

            add_header("SETTINGS EXPLAINED", make_color(80, 200, 255));

            add_sub("Enable Motion Dots:", make_color(255, 210, 60));
            add_body("When on, leaving this panel starts the");
            add_body("dots. When off, B returns to the");
            add_body("Ultrahand menu.");

            add_sub("Console Motion Sensor:", make_color(255, 210, 60));
            add_body("Also reads the console's own motion");
            add_body("sensor, so the dots move at the HOME");
            add_body("menu too (controller motion data stops");
            add_body("there). Uses 512 KB of overlay memory;");
            add_body("off by default.");

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
            add_body("At the HOME menu this needs the Console");
            add_body("Motion Sensor.");

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
        tick_settings();
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
public:
    GuiKinestopSettings() {}

    virtual tsl::elm::Element* createUI() override {
        bool isTr = (g_config.general.language == 1);

        auto *frame = new KinestopSidebarMenu("Kinestop", "by SertAy", true);
        auto *list  = new tsl::elm::List();

        // ------------------ MASTER CONTROL ------------------
        list->addItem(new tsl::elm::CategoryHeader(isTr ? "Ana Kontrol" : "Master Control"));

        auto *toggleEnabled = new tsl::elm::ToggleListItem(isTr ? "Hareket Noktalarını Aç" : "Enable Motion Dots",
                                                           g_config.general.enabled);
        toggleEnabled->setStateChangedListener([](bool newState) {
            g_config.general.enabled = newState;
            save_config_now();
        });
        list->addItem(toggleEnabled);

        static constexpr const char* enabledEn[] = {
            "On: B or the menu combo shows the dots.",
            "Off: B returns to the Ultrahand menu.",
        };
        static constexpr const char* enabledTr[] = {
            "Açık: B veya menü kombinasyonu",
            "noktaları gösterir. Kapalı: B,",
            "Ultrahand menüsüne döner.",
        };
        if (isTr) add_note(list, enabledTr, 3);
        else      add_note(list, enabledEn, 2);

        auto *toggleConsoleSensor = new tsl::elm::ToggleListItem(isTr ? "Konsol Hareket Sensörü" : "Console Motion Sensor",
                                                                 g_config.general.console_sensor);
        toggleConsoleSensor->setStateChangedListener([](bool newState) {
            g_config.general.console_sensor = newState;
            save_config_now();
            apply_console_sensor();
        });
        list->addItem(toggleConsoleSensor);

        static constexpr const char* sensorEn[] = {
            "Lets dots move at the HOME menu",
            "(uses 512 KB).",
        };
        static constexpr const char* sensorTr[] = {
            "Noktalar HOME menüsünde de hareket",
            "eder (512 KB kullanır).",
        };
        add_note(list, isTr ? sensorTr : sensorEn, 2);

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

        // Calibration Handheld Zero Button: the current filtered orientation,
        // sampled by this panel, becomes the zero.
        auto *calibCurrentBtn = new tsl::elm::ListItem(
            isTr ? "Sıfır Noktasını Kalibre Et" : "Calibrate Zero Position",
            isTr ? "Mevcut tutuş açısını merkez sıfır yapar" : "Lock current holding angle as center zero"
        );
        calibCurrentBtn->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                const bool tr = is_tr();
                if (!sensor_fresh()) {
                    // No npad data at the HOME menu, and no console sensor.
                    show_notice(ColorWarn, tr ? "Hareket verisi yok" : "No motion data");
                    return true;
                }
                g_config.calibration.pitch_offset = g_orientation.pitch_deg;
                g_config.calibration.roll_offset  = g_orientation.roll_deg;
                save_config_now();
                show_notice(ColorOk, tr ? "Sıfır noktası kalibre edildi" : "Zero position calibrated");
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
                return true;
            }
            return false;
        });
        list->addItem(resetCalibItem);

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

        // Ultrahand menu without turning the dots off (they stop until
        // Kinestop is opened again).
        auto *menuBtn = new tsl::elm::ListItem(isTr ? "Ultrahand Menüsü" : "Ultrahand Menu");
        menuBtn->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                flush_config();
                open_ultrahand_menu();
                return true;
            }
            return false;
        });
        list->addItem(menuBtn);

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
                const bool consoleSensor = g_config.general.console_sensor;
                g_config.set_defaults();
                save_config_now();
                if (consoleSensor != g_config.general.console_sensor)
                    apply_console_sensor();
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
            tsl::swapTo<GuiKinestopSettings>();
            return;
        }
        tick_settings();
    }

    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override {
        // SMART (B) BUTTON:
        // - If Enabled: relaunch as the in-game HUD (floating dots in game!)
        // - If Disabled: exit directly to Ultrahand main menu!
        if (keysDown & HidNpadButton_B) {
            flush_config();
            if (g_config.general.enabled)
                relaunch(true);
            else
                open_ultrahand_menu();
            return true;
        }
        return false;
    }
};

class KinestopOverlay : public tsl::Overlay {
public:
    // initServices() runs before libtesla creates its layer and framebuffer
    // (initScreen), so the optional 512 KiB SevenSixAxis buffer is carved out
    // of an unfragmented heap. exitServices() runs after the background
    // poller has stopped and before libtesla's final hidExit().
    virtual void initServices() override {
        // libtesla derives these from heap_size.bin or, without it, from a
        // per-HOS default (8 MB before HOS 20) while nx-ovlloader then gives
        // 4 MB: always take the 4 MB paths (no wallpaper, no 4 x 128 KiB
        // render threads per filled rect). Set after tsl::loop() assigns them
        // and before initScreen()/createUI().
        ult::limitedMemory         = true;
        ult::expandedMemory        = false;
        ult::furtherExpandedMemory = false;

        g_config.load();
        if (g_hudMode) {
            // Keep the dots out of screenshots/videos unless asked for: the
            // layer joins only the screen stacks (Kinestop patch), and
            // libtesla must not add the capture stacks back.
            tsl::minimalLayerStacksInCaptures = g_config.visual.show_in_captures;
            if (!g_config.visual.show_in_captures) {
                screenshotsAreDisabled.store(true, std::memory_order_release);
                screenshotsAreForceDisabled.store(true, std::memory_order_release);
            }
        } else {
            register_launch_modes();
        }
        sensor_start(g_config.general.console_sensor);
    }

    virtual void exitServices() override {
        sensor_stop();
        if (!g_hudMode)
            flush_config();
    }

    virtual void onShow() override {
        // libtesla grabs the controls right before show(); the HUD hands
        // them straight back to the game (and the HOME menu).
        if (g_hudMode)
            tsl::hlp::requestForeground(false);
    }

    // Settings mode only (the HUD is never hidden): a hide (menu combo,
    // HOME, power, touch outside) leaves the panel, so the dots come back
    // as with B. May run on libtesla's background poller thread: only
    // queues the relaunch; libtesla's main loop sees launchingOverlay and
    // exits, and exitServices() saves the config.
    virtual void onHide() override {
        if (g_hudMode || !g_config.general.enabled)
            return;
        queue_relaunch(true);
        ult::launchingOverlay.store(true, std::memory_order_release);
    }

    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
        if (g_hudMode)
            return initially<GuiKinestopHud>();
        return initially<GuiKinestopSettings>();
    }
};

int main(int argc, char **argv) {
    if (argc > 0 && argv[0][0] != '\0')
        g_selfName = ult::getNameFromPath(argv[0]);
    g_hudMode = kinestop::hud::is_hud_launch(argc, argv);

    tsl::hidReinitHook = on_hid_reinit;  // Kinestop patch: SevenSixAxis-safe power button

    if (g_hudMode) {
        // 640x360 RGBA4444 (983,040 B for both buffers) stretched over a
        // full-screen layer (Kinestop patch), instead of the original's
        // 1280x720 (3.9 MB) that needed 8 MB of overlay memory.
        ult::DefaultFramebufferWidth  = HudFramebufferWidth;
        ult::DefaultFramebufferHeight = HudFramebufferHeight;
        tsl::fullScreenScaledLayer = true;
        tsl::minimalLayerStacks    = true;
        // Never hidden by the menu combo, HOME, power or touches: the dots
        // stay up until another overlay is launched.
        tsl::disableHiding = true;
        // Pressing the HUD mode's own Ultrahand combo again opens our
        // settings instead of the Ultrahand menu.
        comboReturnToSelfMenu.store(true, std::memory_order_release);
    }

    return tsl::loop<KinestopOverlay>(argc, argv);
}
