// The badge's own HUD for host_movie's badge-faithful films
// (HOST_MOVIE_BADGE=1, tools/make_movie.py --badge): main/hud.c drawn
// into an RGB565 framebuffer laid out as the badge's, as
// tests/host_splash.c draws splash.c's title. Its own translation unit:
// hud.c and the engine's rendertext.c want the real PAX headers, the
// rasterizer in host_shot.c the host's stand-ins.
//
// A frame: the rasterizer's picture packed to 565 as the engine writes
// it, hud_fade'd by `lit` (main.c's on_render: the 3D image faded before
// the HUD goes on), hud_draw over it, and back out as 8-bit channels.
// The keys' help is main/input.c's own names for its default bindings.

#include "se_bindings.h"
#include "se_config.h"
#include "se_text.h"  // the real PAX headers come in with it: struct pax_buf

#include "../main/hud.c"
#include "../main/input.c"
#include "../synthengine3D/src/rendertext.c"

#include "movie_hud.h"

// --- The badge's framebuffer -------------------------------------------

#define FB_PIXELS (DISPLAY_RAW_STRIDE * DISPLAY_RAW_H)

static uint16_t  s_fb565[FB_PIXELS];
static pax_buf_t s_fb;  // reverse_endianness false: 565 as it is

static int fb_index(int x, int y) {
    return x * DISPLAY_RAW_STRIDE + (DISPLAY_RAW_W - 1 - y);  // PAX_O_ROT_CW, as se_direct565.h
}

static uint16_t pack565(uint32_t argb) {
    return (uint16_t)(((argb >> 16) & 0xF8u) << 8 | ((argb >> 8) & 0xFCu) << 3 | (argb & 0xFFu) >> 3);
}

void const* pax_buf_get_pixels(pax_buf_t const* buf) {
    (void)buf;
    return s_fb565;
}
void* pax_buf_get_pixels_rw(pax_buf_t* buf) {
    (void)buf;
    return s_fb565;
}
size_t pax_buf_get_size(pax_buf_t const* buf) {
    (void)buf;
    return sizeof(s_fb565);
}

static void plot(int x, int y, uint16_t p) {
    if (x >= 0 && x < DISPLAY_LOG_W && y >= 0 && y < DISPLAY_LOG_H) s_fb565[fb_index(x, y)] = p;
}

// Opaque: every colour hud.c fills with is. The pixels whose centres are
// inside, as PAX fills.
void pax_simple_rect(pax_buf_t* buf, pax_col_t color, float x, float y, float width, float height) {
    (void)buf;
    if (width < 0) x += width, width = -width;
    if (height < 0) y += height, height = -height;
    int const      x0 = (int)ceilf(x - 0.5f), x1 = (int)ceilf(x + width - 0.5f);
    int const      y0 = (int)ceilf(y - 0.5f), y1 = (int)ceilf(y + height - 0.5f);
    uint16_t const p  = pack565(color);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) plot(xx, yy, p);
}

// PAX's outline: four lines along the edges, each with both ends in.
void pax_outline_rect(pax_buf_t* buf, pax_col_t color, float x, float y, float width, float height) {
    (void)buf;
    uint16_t const p  = pack565(color);
    int const      x0 = (int)x, x1 = (int)(x + width), y0 = (int)y, y1 = (int)(y + height);
    for (int xx = x0; xx <= x1; xx++) plot(xx, y0, p), plot(xx, y1, p);
    for (int yy = y0; yy <= y1; yy++) plot(x0, yy, p), plot(x1, yy, p);
}

// REC's dot: a film is not a recording; drawn in case.
void pax_simple_circle(pax_buf_t* buf, pax_col_t color, float x, float y, float r) {
    (void)buf;
    uint16_t const p = pack565(color);
    for (int yy = (int)(y - r); yy <= (int)(y + r); yy++)
        for (int xx = (int)(x - r); xx <= (int)(x + r); xx++) {
            float const dx = (float)xx + 0.5f - x, dy = (float)yy + 0.5f - y;
            if (dx * dx + dy * dy <= r * r) plot(xx, yy, p);
        }
}

// rendertext.c asks for a font, and draws Hershey strokes whatever it is.
pax_font_t const pax_font_sky_mono_raw;

// --- The keyboard: input.c's defaults, nothing pressed ------------------

static uint16_t s_bind[ACT_COUNT];

void se_bindings_init(se_bindings_config_t const* cfg) {
    for (int i = 0; i < cfg->count; i++) s_bind[cfg->defs[i].id] = cfg->defs[i].default_sc;
}
uint16_t se_bindings_get(int id) {
    return id >= 0 && id < ACT_COUNT ? s_bind[id] : 0;
}
void se_bindings_set(int id, uint16_t sc) {
    if (id >= 0 && id < ACT_COUNT && sc != 0) s_bind[id] = sc;
}
esp_err_t gl_input_read_scancode(bsp_input_scancode_t key, bool* out_state) {
    (void)key;
    *out_state = false;
    return ESP_OK;
}
esp_err_t gl_input_read_navigation_key(bsp_input_navigation_key_t key, bool* out_state) {
    (void)key;
    *out_state = false;
    return ESP_OK;
}
esp_err_t bsp_orientation_enable_gyroscope(void) {
    return ESP_FAIL;
}
esp_err_t bsp_orientation_get(bool* gr, bool* ar, float* gx, float* gy, float* gz, float* ax, float* ay, float* az) {
    (void)gr, (void)ar, (void)gx, (void)gy, (void)gz, (void)ax, (void)ay, (void)az;
    return ESP_FAIL;
}

// --- A frame --------------------------------------------------------------

void movie_hud_init(void) {
    input_init();  // the bindings as they ship
}

void movie_hud_frame(uint32_t* px, float lit, game_t const* g) {
    for (int y = 0; y < DISPLAY_LOG_H; y++)
        for (int x = 0; x < DISPLAY_LOG_W; x++) s_fb565[fb_index(x, y)] = pack565(px[y * DISPLAY_LOG_W + x]);
    if (lit < 1.0f) hud_fade(&s_fb, lit);
    // main.c's play HUD, less what a PC would make up: no frame rate.
    hud_info_t const info = {.no_stats = true};
    hud_draw(&s_fb, g, &info);
    for (int y = 0; y < DISPLAY_LOG_H; y++)
        for (int x = 0; x < DISPLAY_LOG_W; x++) {
            uint32_t const p = s_fb565[fb_index(x, y)];
            uint32_t const r = p >> 11, gg = (p >> 5) & 63, b = p & 31;
            px[y * DISPLAY_LOG_W + x] = 0xFF000000u | (r << 3 | r >> 2) << 16 | (gg << 2 | gg >> 4) << 8 | (b << 3 | b >> 2);
        }
}
