#include "renderer.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <cstdarg>

extern "C" u64 __nx_vi_layer_id;

namespace kinestop {

static void log_renderer(const char* fmt, ...) {
    FILE* f = fopen("sdmc:/config/kinestop/sysmodule.log", "a");
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fprintf(f, "\n");
        fclose(f);
    }
}

// Blocklinear swizzle offset calculation for 16-bit RGBA4444 (width = 1280)
static inline u32 get_pixel_offset_4444(u32 x, u32 y) {
    constexpr u32 offset_w = 320; // (((1280 / 2) >> 4) << 3)
    return ((((y & 127) >> 4) + ((x >> 5) << 3) + ((y >> 7) * offset_w)) << 9) +
           ((y & 8) << 5) + ((x & 16) << 3) + ((y & 6) << 4) + 
           ((x & 8) << 1) + ((y & 1) << 3) + (x & 7);
}

OverlayRenderer::OverlayRenderer()
    : m_initialized(false)
{
    std::memset(&m_display, 0, sizeof(m_display));
    std::memset(&m_layer, 0, sizeof(m_layer));
    std::memset(&m_nwindow, 0, sizeof(m_nwindow));
    std::memset(&m_framebuffer, 0, sizeof(m_framebuffer));
    std::memset(&m_vsync_event, 0, sizeof(m_vsync_event));
}

OverlayRenderer::~OverlayRenderer() {
    exit();
}

Result OverlayRenderer::add_to_layer_stack(ViLayerStack stack) {
    Service* srv = viGetSession_IManagerDisplayService();
    if (!serviceIsActive(srv)) {
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    }

    const struct {
        u32 stack;
        u64 layerId;
    } in = { (u32)stack, m_layer.layer_id };

    return serviceDispatchIn(srv, 6000, in);
}

bool OverlayRenderer::init() {
    if (m_initialized) return true;

    log_renderer("Renderer: Starting init...");

    // 1. Initialize VI service
    Result rc = viInitialize(ViServiceType_Manager);
    log_renderer("Renderer: viInitialize Manager rc=0x%X", rc);
    if (R_FAILED(rc)) {
        rc = viInitialize(ViServiceType_System);
        log_renderer("Renderer: fallback viInitialize System rc=0x%X", rc);
        if (R_FAILED(rc)) {
            rc = viInitialize(ViServiceType_Application);
            log_renderer("Renderer: fallback viInitialize Application rc=0x%X", rc);
            if (R_FAILED(rc)) {
                log_renderer("Renderer: FATAL - Could not initialize VI service");
                return false;
            }
        }
    }

    // 2. Open default system display
    rc = viOpenDefaultDisplay(&m_display);
    log_renderer("Renderer: viOpenDefaultDisplay rc=0x%X", rc);
    if (R_FAILED(rc)) {
        viExit();
        return false;
    }

    // 3. Create managed layer
    rc = viCreateManagedLayer(&m_display, static_cast<ViLayerFlags>(0), 0, &__nx_vi_layer_id);
    log_renderer("Renderer: viCreateManagedLayer rc=0x%X, layer_id=0x%llX", rc, (unsigned long long)__nx_vi_layer_id);
    if (R_FAILED(rc)) {
        viCloseDisplay(&m_display);
        viExit();
        return false;
    }

    // 4. Create the layer instance
    rc = viCreateLayer(&m_display, &m_layer);
    log_renderer("Renderer: viCreateLayer rc=0x%X, layer_id=0x%llX", rc, (unsigned long long)m_layer.layer_id);
    if (R_FAILED(rc)) {
        viDestroyManagedLayer(&m_layer);
        viCloseDisplay(&m_display);
        viExit();
        return false;
    }

    // 5. Set scaling and maximum Z-order
    viSetLayerScalingMode(&m_layer, ViScalingMode_FitToLayer);

    s32 layerZ = 0;
    if (R_SUCCEEDED(viGetZOrderCountMax(&m_display, &layerZ)) && layerZ > 0) {
        viSetLayerZ(&m_layer, layerZ);
    } else {
        viSetLayerZ(&m_layer, 255);
    }
    log_renderer("Renderer: SetLayerZ (z=%d)", (layerZ > 0) ? layerZ : 255);

    // 6. Add layer to compositor stacks
    Result rc_stack = add_to_layer_stack(ViLayerStack_Default);
    log_renderer("Renderer: add_to_layer_stack Default rc=0x%X", rc_stack);
    add_to_layer_stack(ViLayerStack_Lcd);
    add_to_layer_stack(ViLayerStack_Screenshot);
    add_to_layer_stack(ViLayerStack_Recording);

    viSetLayerSize(&m_layer, SCREEN_WIDTH, SCREEN_HEIGHT);
    viSetLayerPosition(&m_layer, 0.0f, 0.0f);

    // 7. Create Native Window from the VI layer
    rc = nwindowCreateFromLayer(&m_nwindow, &m_layer);
    log_renderer("Renderer: nwindowCreateFromLayer rc=0x%X", rc);
    if (R_FAILED(rc)) {
        viDestroyManagedLayer(&m_layer);
        viCloseDisplay(&m_display);
        viExit();
        return false;
    }

    nwindowSetDimensions(&m_nwindow, SCREEN_WIDTH, SCREEN_HEIGHT);

    // 8. Create double-buffered RGBA4444 framebuffer
    rc = framebufferCreate(&m_framebuffer, &m_nwindow, SCREEN_WIDTH, SCREEN_HEIGHT, PIXEL_FORMAT_RGBA_4444, 2);
    log_renderer("Renderer: framebufferCreate RGBA4444 (2 fbs) rc=0x%X", rc);
    if (R_FAILED(rc)) {
        rc = framebufferCreate(&m_framebuffer, &m_nwindow, SCREEN_WIDTH, SCREEN_HEIGHT, PIXEL_FORMAT_RGBA_4444, 1);
        log_renderer("Renderer: framebufferCreate RGBA4444 (1 fb) rc=0x%X", rc);
    }
    if (R_FAILED(rc)) {
        nwindowClose(&m_nwindow);
        viDestroyManagedLayer(&m_layer);
        viCloseDisplay(&m_display);
        viExit();
        return false;
    }

    m_initialized = true;
    clear_transparent();
    log_renderer("Renderer: Init COMPLETED SUCCESSFULLY!");
    return true;
}

void OverlayRenderer::exit() {
    if (!m_initialized) return;

    clear_transparent();

    framebufferClose(&m_framebuffer);
    nwindowClose(&m_nwindow);
    viDestroyManagedLayer(&m_layer);
    viCloseDisplay(&m_display);
    eventClose(&m_vsync_event);
    viExit();

    m_initialized = false;
}

void OverlayRenderer::clear_transparent() {
    if (!m_initialized) return;

    u32 stride = 0;
    u16* buf = (u16*)framebufferBegin(&m_framebuffer, &stride);
    if (buf) {
        std::memset(buf, 0, (stride > 0 ? stride : (SCREEN_WIDTH * sizeof(u16))) * SCREEN_HEIGHT);
        framebufferEnd(&m_framebuffer);
    }
}

void OverlayRenderer::draw_pixel(u16* buffer, s32 x, s32 y, u16 color) {
    if (x >= 0 && x < (s32)SCREEN_WIDTH && y >= 0 && y < (s32)SCREEN_HEIGHT) {
        u32 offset = get_pixel_offset_4444((u32)x, (u32)y);
        buffer[offset] = color;
    }
}

void OverlayRenderer::draw_line_thick(u16* buffer, float x0, float y0, float x1, float y1, int thickness, u16 color) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) return;

    float nx = -dy / len;
    float ny = dx / len;

    int half_t = thickness / 2;

    for (int t = -half_t; t <= half_t; ++t) {
        float ox = nx * (float)t;
        float oy = ny * (float)t;

        s32 sx0 = (s32)std::round(x0 + ox);
        s32 sy0 = (s32)std::round(y0 + oy);
        s32 sx1 = (s32)std::round(x1 + ox);
        s32 sy1 = (s32)std::round(y1 + oy);

        s32 adx = std::abs(sx1 - sx0);
        s32 ady = std::abs(sy1 - sy0);
        s32 step_x = (sx0 < sx1) ? 1 : -1;
        s32 step_y = (sy0 < sy1) ? 1 : -1;
        s32 err = adx - ady;

        s32 cx = sx0;
        s32 cy = sy0;

        while (true) {
            draw_pixel(buffer, cx, cy, color);
            if (cx == sx1 && cy == sy1) break;
            s32 e2 = 2 * err;
            if (e2 > -ady) {
                err -= ady;
                cx += step_x;
            }
            if (e2 < adx) {
                err += adx;
                cy += step_y;
            }
        }
    }
}

void OverlayRenderer::draw_circle(u16* buffer, float cx, float cy, float radius, int thickness, u16 color) {
    int r_outer = (int)std::round(radius + thickness / 2.0f);
    int r_inner = (int)std::round(radius - thickness / 2.0f);
    if (r_inner < 0) r_inner = 0;

    int min_x = std::max(0, (int)std::floor(cx - r_outer));
    int max_x = std::min((int)SCREEN_WIDTH - 1, (int)std::ceil(cx + r_outer));
    int min_y = std::max(0, (int)std::floor(cy - r_outer));
    int max_y = std::min((int)SCREEN_HEIGHT - 1, (int)std::ceil(cy + r_outer));

    for (int y = min_y; y <= max_y; ++y) {
        float dy = (float)y - cy;
        for (int x = min_x; x <= max_x; ++x) {
            float dx = (float)x - cx;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist >= r_inner && dist <= r_outer) {
                draw_pixel(buffer, x, y, color);
            }
        }
    }
}

void OverlayRenderer::draw_filled_circle(u16* buffer, float cx, float cy, float radius, u16 color) {
    int r = (int)std::ceil(radius);
    int min_x = std::max(0, (int)std::floor(cx - r));
    int max_x = std::min((int)SCREEN_WIDTH - 1, (int)std::ceil(cx + r));
    int min_y = std::max(0, (int)std::floor(cy - r));
    int max_y = std::min((int)SCREEN_HEIGHT - 1, (int)std::ceil(cy + r));

    float r_sq = radius * radius;
    for (int y = min_y; y <= max_y; ++y) {
        float dy = (float)y - cy;
        for (int x = min_x; x <= max_x; ++x) {
            float dx = (float)x - cx;
            if (dx * dx + dy * dy <= r_sq) {
                draw_pixel(buffer, x, y, color);
            }
        }
    }
}

void OverlayRenderer::render_horizon_line(u16* buffer, float cx, float cy, float roll_deg, const VisualConfig& visual, u16 color) {
    float rad = -roll_deg * DEG_TO_RAD;
    float cos_r = std::cos(rad);
    float sin_r = std::sin(rad);

    float half_len = 450.0f;
    float gap = 45.0f;

    // Left segment
    float lx0 = cx - cos_r * half_len;
    float ly0 = cy - sin_r * half_len;
    float lx1 = cx - cos_r * gap;
    float ly1 = cy - sin_r * gap;
    draw_line_thick(buffer, lx0, ly0, lx1, ly1, visual.line_thickness, color);

    // Right segment
    float rx0 = cx + cos_r * gap;
    float ry0 = cy + sin_r * gap;
    float rx1 = cx + cos_r * half_len;
    float ry1 = cy + sin_r * half_len;
    draw_line_thick(buffer, rx0, ry0, rx1, ry1, visual.line_thickness, color);

    // End ticks (bank bars)
    float tick_len = 20.0f;
    float nx = -sin_r;
    float ny = cos_r;
    draw_line_thick(buffer, lx0, ly0, lx0 - nx * tick_len, ly0 - ny * tick_len, visual.line_thickness, color);
    draw_line_thick(buffer, rx1, ry1, rx1 - nx * tick_len, ry1 - ny * tick_len, visual.line_thickness, color);
}

void OverlayRenderer::render_pitch_ladder(u16* buffer, float cx, float cy, float pitch_deg, float roll_deg, const VisualConfig& visual, u16 color) {
    render_horizon_line(buffer, cx, cy, roll_deg, visual, color);

    float rad = -roll_deg * DEG_TO_RAD;
    float cos_r = std::cos(rad);
    float sin_r = std::sin(rad);
    float nx = -sin_r;
    float ny = cos_r;

    float px_per_deg = 5.0f;
    float rungs[] = {-20.0f, -10.0f, 10.0f, 20.0f};

    for (float rung_deg : rungs) {
        float rung_offset_px = (rung_deg - pitch_deg) * px_per_deg;
        float rcx = cx + nx * rung_offset_px;
        float rcy = cy + ny * rung_offset_px;

        float half_w = (std::abs(rung_deg) == 10.0f) ? 90.0f : 60.0f;
        float gap = 20.0f;

        float lx0 = rcx - cos_r * half_w;
        float ly0 = rcy - sin_r * half_w;
        float lx1 = rcx - cos_r * gap;
        float ly1 = rcy - sin_r * gap;
        draw_line_thick(buffer, lx0, ly0, lx1, ly1, std::max(1, visual.line_thickness - 1), color);

        float rx0 = rcx + cos_r * gap;
        float ry0 = rcy + sin_r * gap;
        float rx1 = rcx + cos_r * half_w;
        float ry1 = rcy + sin_r * half_w;
        draw_line_thick(buffer, rx0, ry0, rx1, ry1, std::max(1, visual.line_thickness - 1), color);
    }
}

void OverlayRenderer::render_minimal_dots(u16* buffer, float cx, float cy, float roll_deg, const VisualConfig& visual, u16 color) {
    float rad = -roll_deg * DEG_TO_RAD;
    float cos_r = std::cos(rad);
    float sin_r = std::sin(rad);

    float dot_distances[] = {-350.0f, -220.0f, -120.0f, -40.0f, 40.0f, 120.0f, 220.0f, 350.0f};
    float radius = (float)visual.line_thickness * 1.5f + 1.5f;

    for (float d : dot_distances) {
        float x = cx + cos_r * d;
        float y = cy + sin_r * d;
        draw_filled_circle(buffer, x, y, radius, color);
    }
}

void OverlayRenderer::render_center_reticle(u16* buffer, float cx, float cy, const VisualConfig& visual, u16 color) {
    float r = 10.0f;
    draw_circle(buffer, cx, cy, r, visual.line_thickness, color);
    draw_filled_circle(buffer, cx, cy, 3.0f, color);

    draw_line_thick(buffer, cx - 22.0f, cy, cx - 12.0f, cy, visual.line_thickness, color);
    draw_line_thick(buffer, cx + 12.0f, cy, cx + 22.0f, cy, visual.line_thickness, color);
    draw_line_thick(buffer, cx, cy - 22.0f, cx, cy - 12.0f, visual.line_thickness, color);
    draw_line_thick(buffer, cx, cy + 12.0f, cx, cy + 22.0f, visual.line_thickness, color);
}

void OverlayRenderer::render_roll_indicators(u16* buffer, float roll_deg, const VisualConfig& visual, u16 color) {
    float top_cx = CENTER_X;
    float top_cy = 70.0f;
    float arc_radius = 50.0f;

    draw_line_thick(buffer, top_cx - 10.0f, top_cy - 14.0f, top_cx + 10.0f, top_cy - 14.0f, visual.line_thickness, color);
    draw_line_thick(buffer, top_cx - 10.0f, top_cy - 14.0f, top_cx, top_cy - 2.0f, visual.line_thickness, color);
    draw_line_thick(buffer, top_cx + 10.0f, top_cy - 14.0f, top_cx, top_cy - 2.0f, visual.line_thickness, color);

    float rad = (-roll_deg - 90.0f) * DEG_TO_RAD;
    float px = top_cx + std::cos(rad) * arc_radius;
    float py = top_cy + 40.0f + std::sin(rad) * arc_radius;
    draw_filled_circle(buffer, px, py, (float)visual.line_thickness * 1.5f + 1.0f, color);
}

void OverlayRenderer::render_frame(const Orientation& ori, const Config& cfg) {
    if (!m_initialized) {
        if (!init()) return;
    }

    if (!cfg.general.enabled) {
        clear_transparent();
        return;
    }

    u32 stride = 0;
    u16* buf = (u16*)framebufferBegin(&m_framebuffer, &stride);
    if (!buf) return;

    // Clear entire frame to full transparency
    std::memset(buf, 0, (stride > 0 ? stride : (SCREEN_WIDTH * sizeof(u16))) * SCREEN_HEIGHT);

    // Calculate color in RGBA4444 (4-bit per channel: 0x0 to 0xF)
    u8 alpha4 = (u8)std::round(clamp(cfg.visual.opacity, 0.05f, 1.0f) * 15.0f);
    if (alpha4 < 1) alpha4 = 1;
    // Cyan-white HUD color (R: 0xA, G: 0xE, B: 0xF, A: alpha4)
    u16 hud_color = RGBA4(0xA, 0xE, 0xF, alpha4);

    float px_per_deg = 5.0f;
    float horizon_cy = CENTER_Y + (ori.pitch_deg * px_per_deg);
    float horizon_cx = CENTER_X;

    horizon_cy = clamp(horizon_cy, 60.0f, (float)SCREEN_HEIGHT - 60.0f);

    switch (static_cast<OverlayStyle>(cfg.visual.style)) {
        case OverlayStyle::HorizonLine:
            render_horizon_line(buf, horizon_cx, horizon_cy, ori.roll_deg, cfg.visual, hud_color);
            break;
        case OverlayStyle::PitchLadder:
            render_pitch_ladder(buf, horizon_cx, horizon_cy, ori.pitch_deg, ori.roll_deg, cfg.visual, hud_color);
            break;
        case OverlayStyle::MinimalDots:
            render_minimal_dots(buf, horizon_cx, horizon_cy, ori.roll_deg, cfg.visual, hud_color);
            break;
        case OverlayStyle::FullHUD:
            render_pitch_ladder(buf, horizon_cx, horizon_cy, ori.pitch_deg, ori.roll_deg, cfg.visual, hud_color);
            if (cfg.visual.draw_roll_indicators) {
                render_roll_indicators(buf, ori.roll_deg, cfg.visual, hud_color);
            }
            break;
    }

    if (cfg.visual.draw_center_reticle) {
        render_center_reticle(buf, CENTER_X, CENTER_Y, cfg.visual, hud_color);
    }

    if (cfg.visual.draw_roll_indicators && cfg.visual.style != static_cast<int>(OverlayStyle::FullHUD)) {
        render_roll_indicators(buf, ori.roll_deg, cfg.visual, hud_color);
    }

    framebufferEnd(&m_framebuffer);
}

} // namespace kinestop
