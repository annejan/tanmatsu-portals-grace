// Host splash: the two splash screens the badge shows at boot, as frames
// for a film (tools/make_movie.py --interludes).
//
//   host_splash
//
// writes $BUILD/splash/engine_NNN.ppm -- SynthEngine3D's own splash --
// then $BUILD/splash/portals_NNN.ppm -- the game's -- at 10 frames a
// second, and $BUILD/splash/splash.json: [[path, seconds], ...], every
// frame in order, a list to put under an interlude's key.
//
// Both are the badge's own code. The engine's is synthengine3D's real
// se_splash.c, its clock (esp_timer_get_time) a tenth of a second on at
// each frame it presents, its scene drawn by host_shot's rasterizer
// (included below). The game's is main/splash.c, a frame as main.c's
// MODE_SPLASH makes one: splash_update, render_frame, hud_fade by
// splash_lit, then splash_draw -- whose title the engine's own text
// (rendertext.c, Hershey strokes straight into RGB565) draws. Every frame
// goes through an RGB565 framebuffer laid out as the badge's (rotated,
// DISPLAY_RAW_STRIDE), so the text, the fade and the colours are the
// badge's to the bit.
//
// The corridor is drawn at quarter resolution and doubled, as the
// badge's default setting draws it (settings.c: half on);
// HOST_SPLASH_FULL=1 draws it at full resolution. HOST_SHOT_TEXTURES
// (host_shot.c) gives it its textures; `make splash` sets that up.

#define main host_shot_main
#include "host_shot.c"
#undef main

#include <limits.h>
#include "pax_fonts.h"
#include "se_splash.h"
#include "se_text.h"  // the real PAX headers come in with it: struct pax_buf
#include "sound.h"
#include "splash.h"

#define FPS 10

// --- The badge's framebuffer ---------------------------------------------

#define FB_PIXELS (DISPLAY_RAW_STRIDE * DISPLAY_RAW_H)

static uint16_t  s_fb565[FB_PIXELS];
static pax_buf_t s_fb;  // reverse_endianness false: 565 as it is

static int fb_index(int x, int y) {
    return x * DISPLAY_RAW_STRIDE + (DISPLAY_RAW_W - 1 - y);  // PAX_O_ROT_CW, as se_direct565.h
}

static uint16_t pack565(uint32_t argb) {
    return (uint16_t)(((argb >> 16) & 0xF8u) << 8 | ((argb >> 8) & 0xFCu) << 3 | (argb & 0xFFu) >> 3);
}

// The rasterizer's picture into the framebuffer, as the engine writes it:
// packed 565. A quarter-resolution picture doubled, as the PPA does.
static void picture_to_fb(void) {
    int const q = s_quarter > 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) s_fb565[fb_index(x, y)] = pack565(s_px[q ? (y / 2) * W + x / 2 : y * W + x]);
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

// The whole frame one colour: the picture the engine's scene draws over,
// and the framebuffer.
void pax_background(pax_buf_t* buf, pax_col_t color) {
    (void)buf;
    for (int i = 0; i < W * H; i++) s_px[i] = color;
    uint16_t const p = pack565(color);
    for (int i = 0; i < FB_PIXELS; i++) s_fb565[i] = p;
}

// Opaque: every colour splash.c draws a rectangle in is. The pixels whose
// centres are inside, as PAX fills.
void pax_simple_rect(pax_buf_t* buf, pax_col_t color, float x, float y, float width, float height) {
    (void)buf;
    if (width < 0) x += width, width = -width;
    if (height < 0) y += height, height = -height;
    int x0 = (int)ceilf(x - 0.5f), x1 = (int)ceilf(x + width - 0.5f);
    int y0 = (int)ceilf(y - 0.5f), y1 = (int)ceilf(y + height - 0.5f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    uint16_t const p = pack565(color);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) s_fb565[fb_index(xx, yy)] = p;
}

// hud.c's hud_fade, the same arithmetic: each 565 channel times lit, in
// 32nds.
static void fade(float lit) {
    uint32_t const k = (uint32_t)(32.0f * lit);
    for (int i = 0; i < FB_PIXELS; i++) {
        uint32_t const p = s_fb565[i];
        s_fb565[i]       = (uint16_t)(((p & 0xF81Fu) * k >> 5 & 0xF81Fu) | ((p & 0x07E0u) * k >> 5 & 0x07E0u));
    }
}

// rendertext.c asks for a font, and draws Hershey strokes whatever it is.
pax_font_t const pax_font_sky_mono_raw;

// The splash's sounds: the film has none in its interludes.
void sound_play(sound_t s) {
    (void)s;
}

// --- The frames ---------------------------------------------------------

static char  s_dir[PATH_MAX];
static FILE* s_json;
static int   s_frames;

static void frame_out(char const* kind, int n) {
    char path[PATH_MAX + 32];
    snprintf(path, sizeof(path), "%s/%s_%03d.ppm", s_dir, kind, n);
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    static uint8_t rgb[W * H * 3];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint16_t const p = s_fb565[fb_index(x, y)];
            uint8_t* const o = &rgb[(y * W + x) * 3];
            uint32_t const r = p >> 11, g = (p >> 5) & 63, b = p & 31;
            o[0]             = (uint8_t)(r << 3 | r >> 2);
            o[1]             = (uint8_t)(g << 2 | g >> 4);
            o[2]             = (uint8_t)(b << 3 | b >> 2);
        }
    fwrite(rgb, 1, sizeof(rgb), f);
    fclose(f);
    fprintf(s_json, "%s\n  [\"%s\", %.1f]", s_frames ? "," : "", path, 1.0 / FPS);
    s_frames++;
}

// --- The engine's splash: se_splash.c's own loop -------------------------

static int64_t s_clock_us;  // the engine's clock: a frame on at each present
static int     s_engine_n;

int64_t esp_timer_get_time(void) {
    return s_clock_us;
}

pax_buf_t* se_frame_back(void) {
    return &s_fb;
}

void se_frame_present(void) {
    picture_to_fb();
    frame_out("engine", s_engine_n++);
    s_clock_us += 1000000 / FPS;
}

// --- The game's splash: main.c's MODE_SPLASH, a frame at a time --------

static int portals(void) {
    static game_t g;
    s_quarter = getenv("HOST_SPLASH_FULL") != NULL && getenv("HOST_SPLASH_FULL")[0] == '1' ? 0 : 1;
    if (!splash_start(&g)) {
        fprintf(stderr, "splash_start: the corridor would not build\n");
        return -1;
    }
    render_set_level(&g.lv, g.portals);  // as main.c, after splash_start
    float const dt    = 1.0f / FPS;
    float       clock = 0.0f;
    int         n     = 0;
    for (;;) {
        // on_update: the clock, then the splash -- over, on to the title.
        clock += dt;
        render_set_time(clock);
        if (!splash_update(&g, dt)) break;
        // on_render: the corridor, faded, then the title over it.
        for (int p = 0; p < W * H; p++) s_px[p] = 0xFF000000u;
        render_frame(NULL, &g);
        picture_to_fb();
        if (splash_lit() < 1.0f) fade(splash_lit());
        splash_draw(&s_fb);
        frame_out("portals", n++);
    }
    s_quarter = 0;
    return n;
}

int main(void) {
    render_init("textures");
    char const* build = getenv("BUILD");
    char        rel[PATH_MAX];
    snprintf(rel, sizeof(rel), "%s/splash", build != NULL && build[0] ? build : "build");
    if (realpath(rel, s_dir) == NULL) {
        perror(rel);
        return 1;
    }
    char json[PATH_MAX + 16];
    snprintf(json, sizeof(json), "%s/splash.json", s_dir);
    s_json = fopen(json, "w");
    if (s_json == NULL) {
        perror(json);
        return 1;
    }
    fprintf(s_json, "[");

    // main.c: se_splash(), then splash_start() and MODE_SPLASH.
    s_quarter = 0;  // the engine draws its splash at full resolution
    se_splash();
    int const engine = s_engine_n;
    int const game   = portals();
    fprintf(s_json, "\n]\n");
    fclose(s_json);
    if (game < 0) return 1;
    printf("engine splash %d frames (%.1f s), Portals splash %d frames (%.1f s): %s\n", engine,
           (double)engine / FPS, game, (double)game / FPS, json);
    return 0;
}
