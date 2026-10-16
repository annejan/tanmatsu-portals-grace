// Host desk: a desk story's screens as the badge draws them, for a
// badge-faithful film (tools/story_film.py --badge).
//
//   host_desk badge|dos SCREENS_DIR OUT_DIR
//
// reads each screen host_review wrote (`host_review story PACK OUTDIR`:
// OUTDIR/screens/SEG-NNN.txt -- seconds, cursor row and column, the
// highlighted row, then the 25 rows) and draws it with main/deskview.c,
// the badge's own desk view, into an RGB565 framebuffer laid out as the
// badge's (as tests/movie_hud.c draws the HUD): OUT_DIR/SEG-NNN.ppm with
// the cursor up, and, where there is a cursor, OUT_DIR/SEG-NNN-off.ppm
// in the other half of its blink. `badge` is day.txt's `style: badge`
// (the launcher's purple), `dos` the green terminal.
//
// A screen file is desk_screen()'s output, as is: here desk_screen hands
// it back to deskview_draw, so what is drawn is what the badge would draw
// for the desk in that state. The engine's text (rendertext.c, Hershey
// strokes straight into RGB565) draws its letters.

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "se_config.h"
#include "se_text.h"  // the real PAX headers come in with it: struct pax_buf

#include "../main/deskview.c"
#include "../main/text.c"
#include "../synthengine3D/src/rendertext.c"

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

void pax_background(pax_buf_t* buf, pax_col_t color) {
    (void)buf;
    uint16_t const p = pack565(color);
    for (int i = 0; i < FB_PIXELS; i++) s_fb565[i] = p;
}

// Opaque: every colour deskview.c fills with is. The pixels whose centres
// are inside, as PAX fills.
void pax_simple_rect(pax_buf_t* buf, pax_col_t color, float x, float y, float width, float height) {
    (void)buf;
    if (width < 0) x += width, width = -width;
    if (height < 0) y += height, height = -height;
    int x0 = (int)ceilf(x - 0.5f), x1 = (int)ceilf(x + width - 0.5f);
    int y0 = (int)ceilf(y - 0.5f), y1 = (int)ceilf(y + height - 0.5f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > DISPLAY_LOG_W) x1 = DISPLAY_LOG_W;
    if (y1 > DISPLAY_LOG_H) y1 = DISPLAY_LOG_H;
    uint16_t const p = pack565(color);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) s_fb565[fb_index(xx, yy)] = p;
}

// rendertext.c asks for a font, and draws Hershey strokes whatever it is.
pax_font_t const pax_font_sky_mono_raw;

// --- The desk: the screen read, handed to deskview_draw -----------------

static char s_rows[DESK_ROWS][DESK_COLS + 1];
static int  s_cur_row, s_cur_col, s_hl;

void desk_screen(desk_t const* k, char rows[DESK_ROWS][DESK_COLS + 1], int* cur_row, int* cur_col, int* hl) {
    (void)k;
    memcpy(rows, s_rows, sizeof(s_rows));
    *cur_row = s_cur_row;
    *cur_col = s_cur_col;
    *hl      = s_hl;
}

void desk_set_size(desk_t* k, int cols, int rows) {
    k->cols = cols;
    k->rows = rows;
}

// deskview_event's: no keys here.
desk_action_t desk_key(desk_t* k, int key, char ch) {
    (void)k, (void)key, (void)ch;
    return DESK_NONE;
}

// The cursor's blink goes by the clock: 0 up, half a second down.
static int64_t s_now_us;

int64_t esp_timer_get_time(void) {
    return s_now_us;
}

static bool read_screen(char const* path) {
    FILE* f = fopen(path, "r");
    if (f == NULL) return false;
    double secs = 0.0;
    bool   ok   = fscanf(f, "%lf %d %d %d", &secs, &s_cur_row, &s_cur_col, &s_hl) == 4;
    int    c;
    while (ok && (c = fgetc(f)) != '\n' && c != EOF) {
    }
    for (int r = 0; r < DESK_ROWS; r++) {
        char line[256];
        s_rows[r][0] = '\0';
        if (!ok || fgets(line, sizeof(line), f) == NULL) continue;
        line[strcspn(line, "\n")] = '\0';
        snprintf(s_rows[r], sizeof(s_rows[r]), "%.*s", DESK_COLS, line);
    }
    fclose(f);
    return ok;
}

static bool write_ppm(char const* path) {
    FILE* f = fopen(path, "wb");
    if (f == NULL) return false;
    fprintf(f, "P6\n%d %d\n255\n", DISPLAY_LOG_W, DISPLAY_LOG_H);
    static uint8_t rgb[DISPLAY_LOG_W * DISPLAY_LOG_H * 3];
    for (int y = 0; y < DISPLAY_LOG_H; y++)
        for (int x = 0; x < DISPLAY_LOG_W; x++) {
            uint32_t const p = s_fb565[fb_index(x, y)];
            uint8_t* const o = &rgb[(y * DISPLAY_LOG_W + x) * 3];
            uint32_t const r = p >> 11, g = (p >> 5) & 63, b = p & 31;
            o[0]             = (uint8_t)(r << 3 | r >> 2);
            o[1]             = (uint8_t)(g << 2 | g >> 4);
            o[2]             = (uint8_t)(b << 3 | b >> 2);
        }
    bool const ok = fwrite(rgb, 1, sizeof(rgb), f) == sizeof(rgb);
    return fclose(f) == 0 && ok;
}

int main(int argc, char** argv) {
    if (argc != 4 || (strcmp(argv[1], "badge") != 0 && strcmp(argv[1], "dos") != 0)) {
        fprintf(stderr, "usage: host_desk badge|dos SCREENS_DIR OUT_DIR\n");
        return 2;
    }
    static desk_data_t d;
    static desk_t      k;
    d.badge = strcmp(argv[1], "badge") == 0;
    k.d     = &d;
    // DESK_LARGE=1: screens host_review wrote with it, drawn as Large text draws them.
    if (getenv("DESK_LARGE") != NULL && getenv("DESK_LARGE")[0] == '1') desk_set_size(&k, 56, 18);
    DIR* dir = opendir(argv[2]);
    if (dir == NULL) {
        perror(argv[2]);
        return 1;
    }
    int            n = 0;
    struct dirent* e;
    while ((e = readdir(dir)) != NULL) {
        size_t const len = strlen(e->d_name);
        if (len < 5 || strcmp(e->d_name + len - 4, ".txt") != 0) continue;
        char in[PATH_MAX], out[PATH_MAX];
        snprintf(in, sizeof(in), "%s/%s", argv[2], e->d_name);
        if (!read_screen(in)) {
            fprintf(stderr, "%s: does not read\n", in);
            return 1;
        }
        // Up, then -- if there is a cursor to blink -- down.
        for (int half = 0; half < (s_cur_row >= 0 ? 2 : 1); half++) {
            s_now_us = half * 500000;
            deskview_draw(&s_fb, &k);
            snprintf(out, sizeof(out), "%s/%.*s%s.ppm", argv[3], (int)(len - 4), e->d_name, half ? "-off" : "");
            if (!write_ppm(out)) {
                perror(out);
                return 1;
            }
        }
        n++;
    }
    closedir(dir);
    printf("%d desk screens drawn (%s)\n", n, argv[1]);
    return 0;
}
