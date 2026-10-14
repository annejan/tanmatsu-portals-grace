// Host debugger: a broken round's debugger as the badge draws it, for the
// story films (tools/story_film.py): main/editor.c itself -- its debugger
// state, debug_paint and editor_draw -- into an RGB565 framebuffer laid
// out as the badge's (as tests/movie_hud.c draws the HUD), the engine's
// rendertext.c drawing its letters.
//
//   host_debugger CHAMBER_FILE OUT_DIR < SCRIPT
//
// opens the chamber as main.c does a broken round (review_parse, then
// editor_open_debug) and plays SCRIPT, a line each:
//   layer Y        the layer shown (Q / E)
//   at X Z         the cursor (the arrows)
//   brush C        the change picked (its key)
//   paint          Space: the change made, if the debugger allows it
//   tick SECONDS   time going by (the message fades as editor_frame's)
//   shot NAME      editor_draw, to OUT_DIR/NAME.ppm (800 x 480)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "se_config.h"
#include "se_text.h"  // the real PAX headers come in with it: struct pax_buf
#include "se_ui.h"    // editor.c's menus: there, not opened here

#include "../main/editor.c"
#include "../synthengine3D/src/rendertext.c"

// --- The badge's framebuffer, as tests/movie_hud.c -----------------------

#define FB_PIXELS (DISPLAY_RAW_STRIDE * DISPLAY_RAW_H)

static uint16_t  s_fb565[FB_PIXELS];
static pax_buf_t s_fb;

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

void pax_outline_rect(pax_buf_t* buf, pax_col_t color, float x, float y, float width, float height) {
    (void)buf;
    uint16_t const p  = pack565(color);
    int const      x0 = (int)x, x1 = (int)(x + width), y0 = (int)y, y1 = (int)(y + height);
    for (int xx = x0; xx <= x1; xx++) plot(xx, y0, p), plot(xx, y1, p);
    for (int yy = y0; yy <= y1; yy++) plot(x0, yy, p), plot(x1, yy, p);
}

void pax_background(pax_buf_t* buf, pax_col_t color) {
    (void)buf;
    uint16_t const p = pack565(color);
    for (int i = 0; i < FB_PIXELS; i++) s_fb565[i] = p;
}

void pax_simple_circle(pax_buf_t* buf, pax_col_t color, float x, float y, float r) {
    (void)buf;
    uint16_t const p = pack565(color);
    for (int yy = (int)(y - r); yy <= (int)(y + r); yy++)
        for (int xx = (int)(x - r); xx <= (int)(x + r); xx++) {
            float const dx = (float)xx + 0.5f - x, dy = (float)yy + 0.5f - y;
            if (dx * dx + dy * dy <= r * r) plot(xx, yy, p);
        }
}

// A line, a pixel wide (the lift stations' outlines: not in the debugger).
void pax_draw_line(pax_buf_t* buf, pax_col_t color, float x0, float y0, float x1, float y1) {
    (void)buf;
    uint16_t const p = pack565(color);
    int const      n = (int)fmaxf(fabsf(x1 - x0), fabsf(y1 - y0)) + 1;
    for (int i = 0; i <= n; i++) plot((int)(x0 + (x1 - x0) * i / n), (int)(y0 + (y1 - y0) * i / n), p);
}

// The start's arrow: filled by its pixel centres, as PAX fills.
void pax_draw_tri(pax_buf_t* buf, pax_col_t color, float x0, float y0, float x1, float y1, float x2, float y2) {
    (void)buf;
    uint16_t const p    = pack565(color);
    float const    minx = fminf(x0, fminf(x1, x2)), maxx = fmaxf(x0, fmaxf(x1, x2));
    float const    miny = fminf(y0, fminf(y1, y2)), maxy = fmaxf(y0, fmaxf(y1, y2));
    for (int yy = (int)miny; yy <= (int)maxy; yy++)
        for (int xx = (int)minx; xx <= (int)maxx; xx++) {
            float const px = (float)xx + 0.5f, py = (float)yy + 0.5f;
            float const d0 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
            float const d1 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
            float const d2 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2);
            if ((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0)) plot(xx, yy, p);
        }
}

pax_font_t const pax_font_sky_mono_raw;

// What editor.c reaches for and the debugger's pictures do not need: the
// keyboard (driven by the script instead), its menu (not opened), the
// clock, the lifts setting (not shown in the debugger), its sounds.
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
se_menu_result_t se_menu_input(se_menu_t* menu, se_menu_action_t action) {
    (void)menu, (void)action;
    return SE_MENU_RESULT_NONE;
}
void se_menu_draw(se_menu_t const* menu, pax_buf_t* fb) {
    (void)menu, (void)fb;
}
int64_t esp_timer_get_time(void) {
    return 0;
}
bool settings_lifts(void) {
    return false;
}
void sound_play(sound_t s) {
    (void)s;
}

static void shot(char const* dir, char const* name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", DISPLAY_LOG_W, DISPLAY_LOG_H);
    for (int y = 0; y < DISPLAY_LOG_H; y++)
        for (int x = 0; x < DISPLAY_LOG_W; x++) {
            uint16_t const v = s_fb565[fb_index(x, y)];
            unsigned char  rgb[3] = {(unsigned char)((v >> 11) << 3 | (v >> 13)),
                                     (unsigned char)(((v >> 5) & 63) << 2 | ((v >> 9) & 3)),
                                     (unsigned char)((v & 31) << 3 | ((v >> 2) & 7))};
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

static char* slurp(char const* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long const n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = malloc((size_t)n + 1);
    if (text == NULL || fread(text, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(text);
        return NULL;
    }
    text[n] = '\0';
    fclose(f);
    return text;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: host_debugger CHAMBER_FILE OUT_DIR < SCRIPT\n");
        return 2;
    }
    char* const text = slurp(argv[1]);
    if (text == NULL) {
        perror(argv[1]);
        return 1;
    }
    static review_t r;
    char            err[160] = "";
    char const*     slash    = strrchr(argv[1], '/');
    char            id[CHAMBER_ID_N];
    snprintf(id, sizeof(id), "%s", slash ? slash + 1 : argv[1]);
    if (strlen(id) > 4 && strcmp(id + strlen(id) - 4, ".txt") == 0) id[strlen(id) - 4] = '\0';
    if (!review_parse(text, &r, err, sizeof(err)) || r.kind != REVIEW_BROKEN ||
        !editor_open_debug(text, id, &r, err, sizeof(err))) {
        fprintf(stderr, "%s: not a broken round the debugger opens: %s\n", argv[1], err);
        return 1;
    }
    char line[256];
    while (fgets(line, sizeof(line), stdin)) {
        int   a = 0, b = 0;
        char  c = 0;
        float t = 0;
        char  name[200];
        if (sscanf(line, "layer %d", &a) == 1)
            s_y = a;
        else if (sscanf(line, "at %d %d", &a, &b) == 2)
            s_x = a, s_z = b;
        else if (sscanf(line, "brush %c", &c) == 1)
            s_brush = c;
        else if (strncmp(line, "paint", 5) == 0)
            paint_here();
        else if (sscanf(line, "tick %f", &t) == 1)
            s_msg_t -= t;
        else if (sscanf(line, "shot %199s", name) == 1) {
            editor_draw(&s_fb);
            shot(argv[2], name);
        }
    }
    free(text);
    return 0;
}
