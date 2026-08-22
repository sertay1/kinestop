#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

#include "../../common/config.hpp"
#include "../../common/math_types.hpp"
#include "../../sysmodule/source/filter.hpp"
#include "../../sysmodule/source/sensor.hpp"

namespace {
    constexpr float KINESTOP_PI = 3.14159265358979323846f;

    kinestop::Config g_config;
    kinestop::SensorManager g_sensor;
    kinestop::ComplementaryFilter g_filter;
    kinestop::Orientation g_orientation;
    bool g_sensor_ready = false;
    bool g_needGuiReload = false;

    // Proper 4-bit RGBA4444 color constructor (0..15 per channel)
    inline constexpr tsl::Color make_color(u8 r, u8 g, u8 b, u8 a = 255) {
        return tsl::Color(
            static_cast<u8>(r >> 4),
            static_cast<u8>(g >> 4),
            static_cast<u8>(b >> 4),
            static_cast<u8>(a >> 4)
        );
    }

    struct ColorTheme {
        const char* name_en;
        const char* name_tr;
        u8 cr, cg, cb; // Core color
        u8 br, bg, bb; // Border contrast color
    };

    static const ColorTheme THEMES[] = {
        {"Ice Blue",      "Buz Mavisi",    175, 235, 255,   10,  25,  50},  // 0: Classic Ice Cyan
        {"Pure White",    "Beyaz",         255, 255, 255,   20,  20,  20},  // 1: Clean Monochrome
        {"Neon Green",    "Fıstık Yeşili",  80, 250, 140,    5,  40,  15},  // 2: Zelda Lime Green
        {"Dark Green",    "Koyu Yeşil",     40, 180,  80,    5,  30,  10},  // 3: Deep Forest Green
        {"Bright Yellow", "Sarı",          255, 235,  60,   45,  35,   5},  // 4: Vivid Lemon Yellow
        {"Amber Orange",  "Kehribar",      255, 140,  40,   45,  20,   5},  // 5: Warm Sunset Amber
        {"Crimson Red",   "Kırmızı",       255,  65,  75,   45,  10,  15},  // 6: Night Vision Red
        {"Hot Pink",      "Neon Pembe",    255, 105, 180,   45,  10,  30},  // 7: Vibrant Neon Pink
        {"Deep Purple",   "Mor",           185,  90, 255,   30,  10,  45},  // 8: Synthwave Purple
        {"Sky Blue",      "Gök Mavisi",     90, 190, 255,   10,  25,  45},  // 9: Azure Sky Blue
        {"Navy Blue",     "Koyu Mavi",      40, 110, 240,    5,  15,  40},  // 10: Deep Royal Blue
        {"Warm Gray",     "Mat Gri",       180, 185, 195,   30,  30,  35},  // 11: Subtle Minimalist Gray
    };
    constexpr size_t NUM_THEMES = sizeof(THEMES) / sizeof(THEMES[0]);

    static const int SIZES[] = {1, 2, 3, 4, 5, 6, 7, 8, 10, 12};
    constexpr size_t NUM_SIZES = sizeof(SIZES) / sizeof(SIZES[0]);

    static const float SENSITIVITIES[] = {0.2f, 0.5f, 0.8f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    constexpr size_t NUM_SENSITIVITIES = sizeof(SENSITIVITIES) / sizeof(SENSITIVITIES[0]);

    static const float SMOOTHINGS[] = {0.50f, 0.75f, 0.85f, 0.92f, 0.97f};
    constexpr size_t NUM_SMOOTHINGS = sizeof(SMOOTHINGS) / sizeof(SMOOTHINGS[0]);

    static const float DEADZONES[] = {0.0f, 0.2f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 5.0f};
    constexpr size_t NUM_DEADZONES = sizeof(DEADZONES) / sizeof(DEADZONES[0]);

    void load_config() {
        g_config.load();
    }

    void save_config() {
        g_config.save();
    }

    void init_sensor() {
        if (!g_sensor_ready) {
            if (g_sensor.init()) {
                g_sensor_ready = true;
            }
        }
    }

    inline void draw_pixel(tsl::gfx::Renderer *renderer, s32 x, s32 y, const tsl::Color& color) {
        if (x >= 0 && x < (s32)tsl::cfg::FramebufferWidth && y >= 0 && y < (s32)tsl::cfg::FramebufferHeight) {
            renderer->setPixelBlendDst((u32)x, (u32)y, color);
        }
    }

    void draw_circle(tsl::gfx::Renderer *renderer, float cx, float cy, float radius, int thickness, const tsl::Color& color) {
        int steps = std::max(16, (int)(2.0f * KINESTOP_PI * radius));
        float prev_x = cx + radius;
        float prev_y = cy;

        for (int i = 1; i <= steps; ++i) {
            float angle = (float)i * (2.0f * KINESTOP_PI / (float)steps);
            float curr_x = cx + radius * std::cos(angle);
            float curr_y = cy + radius * std::sin(angle);
            
            float dx = curr_x - prev_x;
            float dy = curr_y - prev_y;
            float s = std::max(std::abs(dx), std::abs(dy));
            if (s > 0.0f) {
                float xi = dx / s;
                float yi = dy / s;
                float x = prev_x;
                float y = prev_y;
                for (int j = 0; j <= (int)s; ++j) {
                    draw_pixel(renderer, (s32)std::round(x), (s32)std::round(y), color);
                    x += xi;
                    y += yi;
                }
            }
            prev_x = curr_x;
            prev_y = curr_y;
        }
    }

    void draw_filled_circle(tsl::gfx::Renderer *renderer, float cx, float cy, float radius, const tsl::Color& color) {
        int r = (int)std::ceil(radius);
        int r_sq = r * r;
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dy * dy <= r_sq) {
                    draw_pixel(renderer, (s32)std::round(cx + dx), (s32)std::round(cy + dy), color);
                }
            }
        }
    }

    void draw_motion_cue_dot(tsl::gfx::Renderer *renderer, float cx, float cy, float radius, const tsl::Color& core_color, const tsl::Color& border_color) {
        draw_circle(renderer, cx, cy, radius + 2.0f, 2, border_color);
        draw_filled_circle(renderer, cx, cy, radius, core_color);
    }

    // Generates dot anchors based on Placement Style and Density Level with 0 overlapping
    std::vector<std::pair<float, float>> get_anchors(int style, int density) {
        std::vector<std::pair<float, float>> anchors;
        int d = std::clamp(density, 0, 3); // 0: Low, 1: Medium, 2: High, 3: Ultra

        switch (style) {
            default:
            case 0: { // 4-Border Perimeter
                int counts[] = {3, 5, 7, 9};
                int n_col = counts[d];
                int n_row = counts[d];

                for (int i = 0; i < n_col; ++i) {
                    float y = 90.0f + (float)i * (540.0f / (float)(n_col - 1));
                    anchors.push_back({32.0f, y});
                    anchors.push_back({1248.0f, y});
                }
                for (int i = 1; i < n_row - 1; ++i) {
                    float x = 32.0f + (float)i * (1216.0f / (float)(n_row - 1));
                    anchors.push_back({x, 32.0f});
                    anchors.push_back({x, 688.0f});
                }
                break;
            }

            case 1: { // Full Screen Grid (Covering middle & borders)
                struct GridDim { int cols; int rows; };
                GridDim dims[] = {{4, 3}, {6, 4}, {8, 5}, {10, 6}};
                GridDim g = dims[d];

                float start_x = 60.0f, end_x = 1220.0f;
                float start_y = 50.0f, end_y = 670.0f;

                for (int r = 0; r < g.rows; ++r) {
                    float y = start_y + (float)r * ((end_y - start_y) / (float)(g.rows - 1));
                    for (int c = 0; c < g.cols; ++c) {
                        float x = start_x + (float)c * ((end_x - start_x) / (float)(g.cols - 1));
                        anchors.push_back({x, y});
                    }
                }
                break;
            }

            case 2: { // Side Columns
                int counts[] = {4, 7, 10, 13};
                int n = counts[d];
                for (int i = 0; i < n; ++i) {
                    float y = 70.0f + (float)i * (580.0f / (float)(n - 1));
                    anchors.push_back({32.0f, y});
                    anchors.push_back({1248.0f, y});
                }
                break;
            }

            case 3: { // Clean 4 Corners (Zero Overlap Guaranteed)
                if (d == 0) { // Low: 2 dots per corner
                    anchors.push_back({35.0f, 35.0f});   anchors.push_back({115.0f, 35.0f});
                    anchors.push_back({1245.0f, 35.0f}); anchors.push_back({1165.0f, 35.0f});
                    anchors.push_back({35.0f, 685.0f});  anchors.push_back({115.0f, 685.0f});
                    anchors.push_back({1245.0f, 685.0f});anchors.push_back({1165.0f, 685.0f});
                } else if (d == 1) { // Medium: 4 dots per corner (2x2)
                    anchors.push_back({35.0f, 35.0f});   anchors.push_back({115.0f, 35.0f});
                    anchors.push_back({35.0f, 115.0f});  anchors.push_back({115.0f, 115.0f});

                    anchors.push_back({1245.0f, 35.0f}); anchors.push_back({1165.0f, 35.0f});
                    anchors.push_back({1245.0f, 115.0f});anchors.push_back({1165.0f, 115.0f});

                    anchors.push_back({35.0f, 685.0f});  anchors.push_back({115.0f, 685.0f});
                    anchors.push_back({35.0f, 605.0f});  anchors.push_back({115.0f, 605.0f});

                    anchors.push_back({1245.0f, 685.0f});anchors.push_back({1165.0f, 685.0f});
                    anchors.push_back({1245.0f, 605.0f});anchors.push_back({1165.0f, 605.0f});
                } else if (d == 2) { // High: 6 dots per corner (L-bracket)
                    anchors.push_back({35.0f, 35.0f});   anchors.push_back({115.0f, 35.0f});  anchors.push_back({195.0f, 35.0f});
                    anchors.push_back({35.0f, 115.0f});  anchors.push_back({35.0f, 195.0f});  anchors.push_back({115.0f, 115.0f});

                    anchors.push_back({1245.0f, 35.0f}); anchors.push_back({1165.0f, 35.0f}); anchors.push_back({1085.0f, 35.0f});
                    anchors.push_back({1245.0f, 115.0f});anchors.push_back({1245.0f, 195.0f});anchors.push_back({1165.0f, 115.0f});

                    anchors.push_back({35.0f, 685.0f});  anchors.push_back({115.0f, 685.0f});  anchors.push_back({195.0f, 685.0f});
                    anchors.push_back({35.0f, 605.0f});  anchors.push_back({35.0f, 525.0f});  anchors.push_back({115.0f, 605.0f});

                    anchors.push_back({1245.0f, 685.0f});anchors.push_back({1165.0f, 685.0f}); anchors.push_back({1085.0f, 685.0f});
                    anchors.push_back({1245.0f, 605.0f});anchors.push_back({1245.0f, 525.0f});anchors.push_back({1165.0f, 605.0f});
                } else { // Ultra: 8 dots per corner (3x3 diagonal fan)
                    anchors.push_back({35.0f, 35.0f});   anchors.push_back({115.0f, 35.0f});  anchors.push_back({195.0f, 35.0f});  anchors.push_back({275.0f, 35.0f});
                    anchors.push_back({35.0f, 115.0f});  anchors.push_back({35.0f, 195.0f});  anchors.push_back({35.0f, 275.0f});  anchors.push_back({115.0f, 115.0f});

                    anchors.push_back({1245.0f, 35.0f}); anchors.push_back({1165.0f, 35.0f}); anchors.push_back({1085.0f, 35.0f}); anchors.push_back({1005.0f, 35.0f});
                    anchors.push_back({1245.0f, 115.0f});anchors.push_back({1245.0f, 195.0f});anchors.push_back({1245.0f, 275.0f});anchors.push_back({1165.0f, 115.0f});

                    anchors.push_back({35.0f, 685.0f});  anchors.push_back({115.0f, 685.0f});  anchors.push_back({195.0f, 685.0f});  anchors.push_back({275.0f, 685.0f});
                    anchors.push_back({35.0f, 605.0f});  anchors.push_back({35.0f, 525.0f});  anchors.push_back({35.0f, 445.0f});  anchors.push_back({115.0f, 605.0f});

                    anchors.push_back({1245.0f, 685.0f});anchors.push_back({1165.0f, 685.0f}); anchors.push_back({1085.0f, 685.0f}); anchors.push_back({1005.0f, 685.0f});
                    anchors.push_back({1245.0f, 605.0f});anchors.push_back({1245.0f, 525.0f});anchors.push_back({1245.0f, 445.0f});anchors.push_back({1165.0f, 605.0f});
                }
                break;
            }

            case 4: { // Top & Bottom Rows
                int counts[] = {4, 7, 10, 13};
                int n = counts[d];
                for (int i = 0; i < n; ++i) {
                    float x = 120.0f + (float)i * (1040.0f / (float)(n - 1));
                    anchors.push_back({x, 32.0f});
                    anchors.push_back({x, 688.0f});
                }
                break;
            }

            case 5: { // Surround Field
                for (int i = 0; i < 5; ++i) {
                    float y = 100.0f + (float)i * 130.0f;
                    anchors.push_back({32.0f, y});
                    anchors.push_back({1248.0f, y});
                    float x = 240.0f + (float)i * 200.0f;
                    anchors.push_back({x, 32.0f});
                    anchors.push_back({x, 688.0f});
                }
                int inner_counts[] = {4, 8, 12, 16};
                int in_n = inner_counts[d];
                for (int i = 0; i < in_n; ++i) {
                    float angle = (float)i * (2.0f * KINESTOP_PI / (float)in_n);
                    anchors.push_back({640.0f + 480.0f * std::cos(angle), 360.0f + 240.0f * std::sin(angle)});
                }
                break;
            }
        }
        return anchors;
    }

    void draw_motion_dots(tsl::gfx::Renderer *renderer) {
        if (!g_config.general.enabled) return;

        int color_idx = std::clamp(g_config.visual.color, 0, (int)NUM_THEMES - 1);
        const auto& theme = THEMES[color_idx];

        u8 alpha_255 = (u8)std::clamp((int)(g_config.visual.opacity * 255.0f), 25, 255);
        tsl::Color dot_core   = make_color(theme.cr, theme.cg, theme.cb, alpha_255);
        tsl::Color dot_border = make_color(theme.br, theme.bg, theme.bb, (u8)std::min(255, alpha_255 + 50));

        // Handheld calibration delta calculation
        float pitch = g_orientation.pitch_deg - g_config.calibration.pitch_offset;
        float roll  = g_orientation.roll_deg  - g_config.calibration.roll_offset;

        // Apply Deadzone
        if (std::abs(pitch) < g_config.filter.deadzone_deg) pitch = 0.0f;
        if (std::abs(roll)  < g_config.filter.deadzone_deg) roll  = 0.0f;

        if (g_config.calibration.invert_pitch) pitch = -pitch;
        if (g_config.calibration.invert_roll)  roll  = -roll;

        // Dynamic floating displacement range
        float sens_multiplier = g_config.calibration.sensitivity_roll;
        float max_offset = g_config.visual.free_floating ? (36.0f * std::max(1.0f, sens_multiplier * 0.7f)) : 36.0f;
        float sens_x = 1.3f * g_config.calibration.sensitivity_roll;
        float sens_y = 1.1f * g_config.calibration.sensitivity_pitch;

        float target_dx = -std::clamp(roll * sens_x, -max_offset, max_offset);
        float target_dy =  std::clamp(pitch * sens_y, -max_offset, max_offset);

        int size_idx = std::clamp(g_config.visual.line_thickness, 0, (int)NUM_SIZES - 1);
        float dot_radius = (float)SIZES[size_idx] * 0.9f;

        auto anchors = get_anchors(g_config.visual.style, g_config.visual.density);

        for (const auto& anchor : anchors) {
            float px = anchor.first + target_dx;
            float py = anchor.second + target_dy;

            if (!g_config.visual.free_floating) {
                px = std::clamp(px, 10.0f, 1270.0f);
                py = std::clamp(py, 10.0f, 710.0f);
            }

            draw_motion_cue_dot(renderer, px, py, dot_radius, dot_core, dot_border);
        }
    }
}

// Forward declarations
class GuiKinestopSettings;
class GuiKinestopGuide;

// -------------------------------------------------------------
// IN-GAME FULLSCREEN HUD ELEMENT & GUI
// -------------------------------------------------------------
class KinestopHudElement : public tsl::elm::Element {
public:
    KinestopHudElement() {
        this->m_isItem = false;
    }

    virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        this->setBoundaries(0, 0, tsl::cfg::FramebufferWidth, tsl::cfg::FramebufferHeight);
    }

    virtual void draw(tsl::gfx::Renderer *renderer) override {
        renderer->clearScreen();
        draw_motion_dots(renderer);
    }
};

class GuiKinestopInGameHUD : public tsl::Gui {
private:
    bool m_touchStartedLeft = false;

public:
    GuiKinestopInGameHUD() {
        load_config();
        init_sensor();
        // Give 100% controls to running game
        tsl::hlp::requestForeground(false);
    }

    virtual tsl::elm::Element* createUI() override {
        return new KinestopHudElement();
    }

    virtual void update() override {
        // Focus Sentinel: ensure Home Menu (qlaunch) / system applet controls are never starved of input!
        u64 appPid = 0;
        pmdmntGetApplicationProcessId(&appPid);
        if (appPid == 0) {
            hidsysEnableAppletToGetInput(true);
        }

        if (g_sensor_ready) {
            kinestop::Vec3f accel{}, gyro{};
            if (g_sensor.sample(accel, gyro)) {
                g_orientation = g_filter.update(accel, gyro, 1.0f / 60.0f, g_config);
            }
        }
    }

    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override {
        // 1. Immediate Controller Combo Summon Check
        bool comboTriggered = 
            ((keysHeld & HidNpadButton_L) && (keysHeld & HidNpadButton_Down) && ((keysHeld & HidNpadButton_StickR) || (keysDown & HidNpadButton_StickR))) ||
            ((keysHeld & HidNpadButton_L) && (keysHeld & HidNpadButton_R) && ((keysHeld & HidNpadButton_Down) || (keysDown & HidNpadButton_Down))) ||
            ((keysHeld & HidNpadButton_ZL) && (keysHeld & HidNpadButton_ZR) && ((keysHeld & HidNpadButton_Down) || (keysDown & HidNpadButton_Down))) ||
            ((keysDown & HidNpadButton_Minus) && (keysDown & HidNpadButton_Plus));

        if (comboTriggered) {
            tsl::hlp::requestForeground(true);
            tsl::swapTo<GuiKinestopSettings>();
            return true;
        }

        // 2. High-Responsiveness Touchscreen Left-to-Right Swipe Check
        if (touchPos.x > 0 || touchPos.y > 0) {
            if (touchPos.x < 150) {
                m_touchStartedLeft = true;
            }
            if (m_touchStartedLeft && touchPos.x > 160) {
                m_touchStartedLeft = false;
                tsl::hlp::requestForeground(true);
                tsl::swapTo<GuiKinestopSettings>();
                return true;
            }
        } else {
            m_touchStartedLeft = false;
        }

        // Return true to keep background HUD transparent
        return true;
    }
};

// -------------------------------------------------------------
// STANDARD 448px SIDEBAR FRAME (by SertAy)
// -------------------------------------------------------------
class KinestopSidebarMenu : public tsl::elm::OverlayFrame {
public:
    KinestopSidebarMenu(const std::string& title, const std::string& subtitle)
        : tsl::elm::OverlayFrame(title, subtitle) {}

    virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        this->setBoundaries(0, 0, 448, 720);
        if (m_contentElement != nullptr) {
            m_contentElement->setBoundaries(35, 97, 448 - 85, 720 - 73 - 105);
            m_contentElement->invalidate();
        }
    }

    virtual void draw(tsl::gfx::Renderer *renderer) override {
        renderer->clearScreen();

        // Live Real-Time Dots Preview behind menu
        draw_motion_dots(renderer);

        renderer->drawRect(0, 0, 448, 720, a(tsl::defaultBackgroundColor));
        renderer->drawString(m_title, false, 20, 50, 32, tsl::defaultOverlayColor);
        renderer->drawString(m_subtitle, false, 20, 75, 15, tsl::bannerVersionTextColor);

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
            add_header("ÖNEMLİ: ULTRAHAND RAM AYARI", make_color(255, 90, 100));
            add_body("Kinestop'un akıcı 60 FPS çalışması için");
            add_body("Ultrahand Overlay ayarlarından bellek");
            add_body("limitini en az 8 MB yapmanız önerilir.");

            add_header("GELİŞTİRİCİ VE BAĞIŞ LİNKLERİ", make_color(80, 200, 255));
            add_body("Geliştirici: SertAy");
            add_body("Destek olmak veya ulaşmak isterseniz:");
            add_sub("Bağış: buymeacoffee.com/shiftbase", make_color(80, 240, 120));
            add_sub("Tüm Linkler: linktr.ee/sertay", make_color(80, 200, 255));
            add_sub("Instagram: @cilsertay", make_color(225, 120, 255));
            add_sub("YouTube: @SertAyyy", make_color(255, 90, 100));
            add_sub("TikTok: @sertayintardis", make_color(80, 240, 220));

            add_header("KİNESTOP NEDİR?", make_color(255, 170, 50));
            add_body("Kinestop, araçta (araba, otobüs, tren vb.)");
            add_body("oyun oynarken oluşan taşıt tutmasını (kinetoz)");
            add_body("önlemek için geliştirilmiştir. Fiziksel hareketle");
            add_body("senkronize çalışan noktalar çizerek gözünüzü");
            add_body("iç kulak denge sistemiyle senkronize eder.");

            add_header("AYARLAR VE ÖZELLİKLER", make_color(80, 200, 255));
            
            add_sub("Hareket Noktalarını Aç:", make_color(255, 210, 60));
            add_body("Noktaları açıp kapatan ana şalter.");

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

            add_sub("Yumuşatma (Alpha):", make_color(80, 200, 255));
            add_body("El titremelerini filtreler, akıcılık sağlar.");

            add_sub("Ölü Bölge (0.0 - 5.0 deg):", make_color(80, 200, 255));
            add_body("Küçük mikro titreşimleri yok sayar.");

            add_sub("Dikey / Yatay Yönü Ters Çevir:", make_color(255, 90, 100));
            add_body("Noktaların kayma yönünü tersine çevirir.");

            add_sub("Sıfır Noktasını Kalibre Et:", make_color(80, 240, 120));
            add_body("Konsolu o an tuttuğunuz açıyı (yatakta veya");
            add_body("araçta) yeni merkez sıfır açısı yapar.");

            add_sub("Sıfır Noktasını Sıfırla:", make_color(80, 240, 120));
            add_body("Düz ufuk seviyesine geri döndürür.");

            add_sub("Ayarları Sıfırla:", make_color(255, 90, 100));
            add_body("Tüm ayarları fabrika varsayılanlarına alır.");
        } else {
            // ==================== ENGLISH GUIDE ====================
            add_header("IMPORTANT: ULTRAHAND MEMORY", make_color(255, 90, 100));
            add_body("To ensure smooth 60 FPS operation, set");
            add_body("the Ultrahand Overlay memory limit to");
            add_body("at least 8 MB in Ultrahand settings.");

            add_header("CREATOR & DONATION LINKS", make_color(80, 200, 255));
            add_body("Developer: SertAy");
            add_body("If you want to support or reach out:");
            add_sub("Donation: buymeacoffee.com/shiftbase", make_color(80, 240, 120));
            add_sub("All Links: linktr.ee/sertay", make_color(80, 200, 255));
            add_sub("Instagram: @cilsertay", make_color(225, 120, 255));
            add_sub("YouTube: @SertAyyy", make_color(255, 90, 100));
            add_sub("TikTok: @sertayintardis", make_color(80, 240, 220));

            add_header("WHAT IS KINESTOP?", make_color(255, 170, 50));
            add_body("Kinestop helps eliminate vehicle motion");
            add_body("sickness (kinetosis) while playing in cars,");
            add_body("buses or trains by rendering floating motion");
            add_body("cue dots that sync your vision with the inner");
            add_body("ear vestibular balance system.");

            add_header("SETTINGS EXPLAINED", make_color(80, 200, 255));
            
            add_sub("Enable Motion Dots:", make_color(255, 210, 60));
            add_body("Master switch to turn dots on or off.");

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

            add_sub("Smoothing (Alpha):", make_color(80, 200, 255));
            add_body("Filters hand tremors for silky movement.");

            add_sub("Deadzone (0.0 - 5.0 deg):", make_color(80, 200, 255));
            add_body("Ignores small micro-vibrations.");

            add_sub("Invert Pitch / Roll:", make_color(255, 90, 100));
            add_body("Inverts vertical or horizontal dot motion.");

            add_sub("Calibrate Zero Position:", make_color(80, 240, 120));
            add_body("Locks your current handheld holding angle");
            add_body("(e.g. reclining in bed/car) as center zero.");

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
    GuiKinestopGuide() {
        tsl::hlp::requestForeground(true);
    }

    virtual tsl::elm::Element* createUI() override {
        std::string title = (g_config.general.language == 1) ? "Yardım ve Rehber" : "Guide & Info";
        auto *frame = new KinestopSidebarMenu(title, "by SertAy");
        m_textElem = new ScrollableGuideTextElement();
        frame->setContent(m_textElem);
        return frame;
    }

    virtual void update() override {
        if (g_sensor_ready) {
            kinestop::Vec3f accel{}, gyro{};
            if (g_sensor.sample(accel, gyro)) {
                g_orientation = g_filter.update(accel, gyro, 1.0f / 60.0f, g_config);
            }
        }
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
    GuiKinestopSettings() {
        load_config();
        init_sensor();
        tsl::hlp::requestForeground(true);
    }

    virtual tsl::elm::Element* createUI() override {
        bool isTr = (g_config.general.language == 1);

        auto *frame = new KinestopSidebarMenu("Kinestop", "by SertAy");
        auto *list  = new tsl::elm::List();

        // ------------------ MASTER CONTROL ------------------
        list->addItem(new tsl::elm::CategoryHeader(isTr ? "Ana Kontrol" : "Master Control"));

        auto *toggleEnabled = new tsl::elm::ToggleListItem(isTr ? "Hareket Noktalarını Aç" : "Enable Motion Dots", g_config.general.enabled);
        toggleEnabled->setStateChangedListener([](bool newState) {
            g_config.general.enabled = newState;
            save_config();
        });
        list->addItem(toggleEnabled);

        // ------------------ PATTERN & DENSITY ------------------
        list->addItem(new tsl::elm::CategoryHeader(isTr ? "Yerleşim ve Yoğunluk" : "Placement & Density"));

        auto *patternTrackBar = isTr ?
            new tsl::elm::NamedStepTrackBar("", {"Perimeter", "Tüm Ekran", "Yanlar", "Köşeler", "Üst ve Alt", "Çevre"}, true, "Desen") :
            new tsl::elm::NamedStepTrackBar("", {"Perimeter", "Full Grid", "Sides", "Corners", "Top/Bottom", "Surround"}, true, "Pattern");
        patternTrackBar->setProgress(g_config.visual.style);
        patternTrackBar->setValueChangedListener([](u8 newIndex) {
            g_config.visual.style = newIndex;
            save_config();
        });
        list->addItem(patternTrackBar);

        auto *densityTrackBar = isTr ?
            new tsl::elm::NamedStepTrackBar("", {"Düşük", "Orta", "Yüksek", "Ultra"}, true, "Yoğunluk") :
            new tsl::elm::NamedStepTrackBar("", {"Low", "Medium", "High", "Ultra"}, true, "Density");
        densityTrackBar->setProgress(g_config.visual.density);
        densityTrackBar->setValueChangedListener([](u8 newIndex) {
            g_config.visual.density = newIndex;
            save_config();
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
        colorTrackBar->setProgress(g_config.visual.color);
        colorTrackBar->setValueChangedListener([](u8 newIndex) {
            g_config.visual.color = newIndex;
            save_config();
        });
        list->addItem(colorTrackBar);

        // ------------------ VISUAL CUSTOMIZATION ------------------
        list->addItem(new tsl::elm::CategoryHeader(isTr ? "Görsel Özelleştirme" : "Visual Customization"));

        auto *sizeTrackBar = new tsl::elm::NamedStepTrackBar("", {
            "1 px", "2 px", "3 px", "4 px", "5 px", "6 px", "7 px", "8 px", "10 px", "12 px"
        }, true, isTr ? "Nokta Boyutu" : "Dot Size");
        int current_size_idx = std::clamp(g_config.visual.line_thickness, 0, (int)NUM_SIZES - 1);
        sizeTrackBar->setProgress(current_size_idx);
        sizeTrackBar->setValueChangedListener([](u8 index) {
            g_config.visual.line_thickness = (int)index;
            save_config();
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
            save_config();
        });
        list->addItem(opacityTrackBar);

        auto *sensTrackBar = new tsl::elm::NamedStepTrackBar("", {
            "0.2x", "0.5x", "0.8x", "1.0x", "1.5x", "2.0x", "3.0x", "4.0x", "5.0x", "6.0x"
        }, true, isTr ? "Hassasiyet" : "Sensitivity");
        int sens_idx = 3; // default 1.0x
        for (int i = 0; i < (int)NUM_SENSITIVITIES; ++i) {
            if (std::abs(g_config.calibration.sensitivity_roll - SENSITIVITIES[i]) < 0.1f) {
                sens_idx = i; break;
            }
        }
        sensTrackBar->setProgress(sens_idx);
        sensTrackBar->setValueChangedListener([](u8 index) {
            float val = SENSITIVITIES[std::clamp((int)index, 0, (int)NUM_SENSITIVITIES - 1)];
            g_config.calibration.sensitivity_roll = val;
            g_config.calibration.sensitivity_pitch = val;
            save_config();
        });
        list->addItem(sensTrackBar);

        auto *toggleFreeFloat = new tsl::elm::ToggleListItem(
            isTr ? "Serbest Hareket Modu" : "Free Floating Dots",
            g_config.visual.free_floating
        );
        toggleFreeFloat->setStateChangedListener([](bool newState) {
            g_config.visual.free_floating = newState;
            save_config();
        });
        list->addItem(toggleFreeFloat);

        // ------------------ FILTER & SMOOTHING ------------------
        list->addItem(new tsl::elm::CategoryHeader(isTr ? "Filtre ve Yumuşatma" : "Filter & Smoothing"));

        auto *alphaTrackBar = isTr ?
            new tsl::elm::NamedStepTrackBar("", {"Düşük (%50)", "Orta (%75)", "Akıcı (%85)", "Yüksek (%92)", "Ultra (%97)"}, true, "Yumuşatma") :
            new tsl::elm::NamedStepTrackBar("", {"Low (50%)", "Medium (75%)", "Smooth (85%)", "High (92%)", "Ultra (97%)"}, true, "Smoothing");
        int alpha_idx = 2;
        for (int i = 0; i < (int)NUM_SMOOTHINGS; ++i) {
            if (std::abs(g_config.filter.alpha - SMOOTHINGS[i]) < 0.05f) {
                alpha_idx = i; break;
            }
        }
        alphaTrackBar->setProgress(alpha_idx);
        alphaTrackBar->setValueChangedListener([](u8 index) {
            g_config.filter.alpha = SMOOTHINGS[std::clamp((int)index, 0, (int)NUM_SMOOTHINGS - 1)];
            save_config();
        });
        list->addItem(alphaTrackBar);

        auto *deadzoneTrackBar = new tsl::elm::NamedStepTrackBar("", {
            "0.0 deg", "0.2 deg", "0.5 deg", "1.0 deg", "1.5 deg", "2.0 deg", "3.0 deg", "5.0 deg"
        }, true, isTr ? "Ölü Bölge" : "Deadzone");
        int deadzone_idx = 2;
        for (int i = 0; i < (int)NUM_DEADZONES; ++i) {
            if (std::abs(g_config.filter.deadzone_deg - DEADZONES[i]) < 0.15f) {
                deadzone_idx = i; break;
            }
        }
        deadzoneTrackBar->setProgress(deadzone_idx);
        deadzoneTrackBar->setValueChangedListener([](u8 index) {
            g_config.filter.deadzone_deg = DEADZONES[std::clamp((int)index, 0, (int)NUM_DEADZONES - 1)];
            save_config();
        });
        list->addItem(deadzoneTrackBar);

        // ------------------ CALIBRATION & CONTROLS ------------------
        list->addItem(new tsl::elm::CategoryHeader(isTr ? "Kalibrasyon ve Kontroller" : "Calibration & Controls"));

        auto *toggleInvertPitch = new tsl::elm::ToggleListItem(isTr ? "Dikey Yönü Ters Çevir" : "Invert Pitch (Up/Down)", g_config.calibration.invert_pitch);
        toggleInvertPitch->setStateChangedListener([](bool newState) {
            g_config.calibration.invert_pitch = newState;
            save_config();
        });
        list->addItem(toggleInvertPitch);

        auto *toggleInvertRoll = new tsl::elm::ToggleListItem(isTr ? "Yatay Yönü Ters Çevir" : "Invert Roll (Left/Right)", g_config.calibration.invert_roll);
        toggleInvertRoll->setStateChangedListener([](bool newState) {
            g_config.calibration.invert_roll = newState;
            save_config();
        });
        list->addItem(toggleInvertRoll);

        // Calibration Handheld Zero Button
        auto *calibCurrentBtn = new tsl::elm::ListItem(
            isTr ? "Sıfır Noktasını Kalibre Et" : "Calibrate Zero Position",
            isTr ? "Mevcut tutuş açısını merkez sıfır yapar" : "Lock current holding angle as center zero"
        );
        calibCurrentBtn->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                g_config.calibration.pitch_offset = g_orientation.pitch_deg;
                g_config.calibration.roll_offset  = g_orientation.roll_deg;
                save_config();
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
                save_config();
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

        // 2. Language Selector (Between Guide and Reset Settings)
        auto *langTrackBar = new tsl::elm::NamedStepTrackBar("", {"English", "Türkçe"}, true, isTr ? "Dil / Language" : "Language");
        langTrackBar->setProgress(g_config.general.language);
        langTrackBar->setValueChangedListener([](u8 newIndex) {
            if (g_config.general.language != newIndex) {
                g_config.general.language = newIndex;
                save_config();
                g_needGuiReload = true;
            }
        });
        list->addItem(langTrackBar);

        // 3. Reset Settings
        auto *resetDefaultsBtn = new tsl::elm::ListItem(isTr ? "Ayarları Sıfırla" : "Reset Settings");
        resetDefaultsBtn->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                g_config.set_defaults();
                save_config();
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

        if (g_sensor_ready) {
            kinestop::Vec3f accel{}, gyro{};
            if (g_sensor.sample(accel, gyro)) {
                g_orientation = g_filter.update(accel, gyro, 1.0f / 60.0f, g_config);
            }
        }
    }

    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override {
        // SMART (B) BUTTON:
        // - If Enabled: swap to InGameHUD (closes sidebar, continues floating dots in game!)
        // - If Disabled: exit directly to Ultrahand main menu!
        if (keysDown & HidNpadButton_B) {
            if (g_config.general.enabled) {
                tsl::swapTo<GuiKinestopInGameHUD>();
            } else {
                tsl::goBack();
            }
            return true;
        }
        return false;
    }
};

class KinestopOverlay : public tsl::Overlay {
public:
    virtual void initServices() override {
        load_config();
        init_sensor();
    }

    virtual void exitServices() override {
        save_config();
        if (g_sensor_ready) {
            g_sensor.exit();
            g_sensor_ready = false;
        }
    }

    virtual void onShow() override {
        load_config();
        init_sensor();
    }

    virtual void onHide() override {
        save_config();
    }

    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<GuiKinestopSettings>();
    }
};

int main(int argc, char **argv) {
    ult::DefaultFramebufferWidth  = 1280;
    ult::DefaultFramebufferHeight = 720;
    return tsl::loop<KinestopOverlay>(argc, argv);
}
