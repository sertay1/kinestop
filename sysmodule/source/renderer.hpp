#pragma once

#include <switch.h>
#include <cstdint>
#include "../../common/math_types.hpp"
#include "../../common/config.hpp"

namespace kinestop {

class OverlayRenderer {
public:
    OverlayRenderer();
    ~OverlayRenderer();

    bool init();
    void exit();

    /**
     * @brief Render the artificial horizon HUD on top of the screen.
     * @param ori Filtered orientation (pitch and roll in degrees)
     * @param cfg Current visual configuration
     */
    void render_frame(const Orientation& ori, const Config& cfg);

    /**
     * @brief Clear screen to full transparency (when disabled).
     */
    void clear_transparent();

    bool is_initialized() const { return m_initialized; }

private:
    bool m_initialized;
    ViDisplay m_display;
    ViLayer m_layer;
    NWindow m_nwindow;
    Framebuffer m_framebuffer;
    Event m_vsync_event;

    static constexpr u32 SCREEN_WIDTH = 1280;
    static constexpr u32 SCREEN_HEIGHT = 720;
    static constexpr float CENTER_X = 640.0f;
    static constexpr float CENTER_Y = 360.0f;

    // Helper to add layer to compositor stack
    Result add_to_layer_stack(ViLayerStack stack);

    // Drawing primitives for RGBA4444
    void draw_pixel(u16* buffer, s32 x, s32 y, u16 color);
    void draw_line_thick(u16* buffer, float x0, float y0, float x1, float y1, int thickness, u16 color);
    void draw_circle(u16* buffer, float cx, float cy, float radius, int thickness, u16 color);
    void draw_filled_circle(u16* buffer, float cx, float cy, float radius, u16 color);

    // Style renderers
    void render_horizon_line(u16* buffer, float cx, float cy, float roll_deg, const VisualConfig& visual, u16 color);
    void render_pitch_ladder(u16* buffer, float cx, float cy, float pitch_deg, float roll_deg, const VisualConfig& visual, u16 color);
    void render_minimal_dots(u16* buffer, float cx, float cy, float roll_deg, const VisualConfig& visual, u16 color);
    void render_center_reticle(u16* buffer, float cx, float cy, const VisualConfig& visual, u16 color);
    void render_roll_indicators(u16* buffer, float roll_deg, const VisualConfig& visual, u16 color);
};

} // namespace kinestop
