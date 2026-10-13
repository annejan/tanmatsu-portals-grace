#include "loadsplash.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "card.h"
#include "pax_fonts.h"
#include "se_text.h"
#include "sound.h"
#include "synthengine3d.h"

#define SPLASH_S  3.8f   // all of it
#define LINE_S    0.16f  // the log, a line every so often ...
#define TYPE_CPS  90.0f  // ... typed this fast
#define MAP_AT_S  0.30f  // the memory map fills in from here ...
#define MAP_S     1.10f  // ... over this long
#define NAME_AT_S 1.35f  // the name comes in
#define FADE_S    0.45f  // and it all goes, at the end

// Amber on black: a loader's terminal.
#define AMBER  0xFFFFB13Cu
#define GOLD   0xFFFFD27Au
#define EMBER  0xFF7A4A12u
#define FAINT  0xFF5A4630u

static loadsplash_info_t s_info;
static float             s_t = -1.0f;
static int               s_lines_said;  // log lines that have ticked

// --- What it shows: read from the files ------------------------------------------

static uint32_t le32(uint8_t const* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t le16(uint8_t const* p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

// An ELF32 app.so's allocated sections, by kind, and its dynamic symbols
// that are not defined in it: what the loader resolves.
static bool read_elf(FILE* f, loadsplash_info_t* out) {
    uint8_t eh[52];
    if (fread(eh, 1, sizeof(eh), f) != sizeof(eh) || memcmp(eh, "\x7f" "ELF", 4) != 0 || eh[4] != 1) return false;
    uint32_t const shoff = le32(eh + 32);
    uint16_t const shentsize = le16(eh + 46), shnum = le16(eh + 48);
    if (shentsize != 40 || shnum == 0 || shnum > 256) return false;
    for (int i = 0; i < shnum; i++) {
        uint8_t sh[40];
        if (fseek(f, (long)(shoff + (uint32_t)i * 40u), SEEK_SET) != 0 || fread(sh, 1, sizeof(sh), f) != sizeof(sh))
            return false;
        uint32_t const type = le32(sh + 4), flags = le32(sh + 8), off = le32(sh + 16), size = le32(sh + 20);
        uint32_t const entsize = le32(sh + 36);
        if (flags & 2u) {  // SHF_ALLOC: in memory once loaded
            if (type == 8u)
                out->bss += size;  // SHT_NOBITS: zeroed
            else if (flags & 4u)
                out->text += size;  // SHF_EXECINSTR: code
            else
                out->data += size;
        }
        if (type == 11u && entsize == 16u) {  // SHT_DYNSYM: count the undefined ones
            long const back = ftell(f);
            for (uint32_t k = 1; k < size / 16u; k++) {
                uint8_t sym[16];
                if (fseek(f, (long)(off + k * 16u), SEEK_SET) != 0 || fread(sym, 1, sizeof(sym), f) != sizeof(sym))
                    break;
                if (le16(sym + 14) == 0) out->symbols++;  // SHN_UNDEF
            }
            fseek(f, back, SEEK_SET);
        }
    }
    return true;
}

void loadsplash_read(char const* app_so, char const* meta_json, char const* app, loadsplash_info_t* out) {
    memset(out, 0, sizeof(*out));
    snprintf(out->app, sizeof(out->app), "%s", app);
    FILE* f = card_fopen(app_so, "rb");
    if (f != NULL) {
        out->ok = read_elf(f, out);
        fclose(f);
    }
    // "version": "2.8.0" -- found by hand: a whole JSON reader for one string.
    FILE* m = meta_json != NULL ? card_fopen(meta_json, "rb") : NULL;
    if (m != NULL) {
        char       buf[512];
        size_t const n = fread(buf, 1, sizeof(buf) - 1, m);
        fclose(m);
        buf[n] = '\0';
        char const* v = strstr(buf, "\"version\"");
        if (v != NULL && (v = strchr(v + 9, '"')) != NULL) {
            int k = 0;
            for (v++; *v && *v != '"' && k < (int)sizeof(out->version) - 1; v++) out->version[k++] = *v;
            out->version[k] = '\0';
        }
    }
}

// --- The splash ---------------------------------------------------------------------

void loadsplash_start(loadsplash_info_t const* info) {
    s_info       = *info;
    s_t          = 0.0f;
    s_lines_said = 0;
}

bool loadsplash_update(float dt) {
    if (s_t < 0.0f) return false;
    s_t += dt;
    // A tick as each line of the log begins.
    int const lines = s_info.ok ? 6 : 3;
    while (s_lines_said < lines && s_t > (float)s_lines_said * LINE_S) {
        sound_play(SND_TICK);
        s_lines_said++;
    }
    if (s_t < SPLASH_S) return true;
    s_t = -1.0f;
    return false;
}

void loadsplash_skip(void) {
    if (s_t >= 0.0f && s_t < SPLASH_S - FADE_S) s_t = SPLASH_S - FADE_S;
}

static float clamp01(float x) {
    return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

static pax_col_t dim(uint32_t argb, float k) {
    k                = clamp01(k);
    uint32_t const r = (uint32_t)((float)((argb >> 16) & 255) * k);
    uint32_t const g = (uint32_t)((float)((argb >> 8) & 255) * k);
    uint32_t const b = (uint32_t)((float)(argb & 255) * k);
    return 0xFF000000u | r << 16 | g << 8 | b;
}

// Text as a terminal shows it: a character to a cell, each centred in its
// own (the font is proportional; deskview.c draws the desks the same way).
static void mono(pax_buf_t* fb, pax_col_t col, float size, float x, float y, char const* text) {
    float const cell = size * 0.62f;
    for (int i = 0; text[i]; i++) {
        if (text[i] == ' ') continue;
        char const  s[2] = {text[i], 0};
        float const adv  = rendertext_size(pax_font_sky_mono, size, s).x;
        rendertext_draw(fb, col, pax_font_sky_mono, size, x + cell * (float)i + (cell - adv) * 0.5f, y, s);
    }
}

static float mono_width(float size, char const* text) {
    return size * 0.62f * (float)strlen(text);
}

// "6.4 MB", "220 KB"
static void size_text(char* out, size_t n, uint32_t bytes) {
    if (bytes >= 1024u * 1024u)
        snprintf(out, n, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    else
        snprintf(out, n, "%u KB", (unsigned)((bytes + 1023u) / 1024u));
}

void loadsplash_draw(pax_buf_t* fb) {
    pax_background(fb, 0xFF000000u);
    if (s_t < 0.0f) return;
    float const lit = 1.0f - clamp01((s_t - (SPLASH_S - FADE_S)) / FADE_S);
    // One block, in the middle: the log, the map under it, the name below.
    float const x0 = 170.0f, step = 26.0f, y0 = 58.0f;

    // The log, a line at a time, typed.
    char lines[6][64];
    int  n = 0;
    char t[16], d[16], b[16];
    size_text(t, sizeof(t), s_info.text);
    size_text(d, sizeof(d), s_info.data);
    size_text(b, sizeof(b), s_info.bss);
    snprintf(lines[n++], sizeof(lines[0]), "graceloader%s%s", s_info.version[0] ? " " : "", s_info.version);
    snprintf(lines[n++], sizeof(lines[0]), "load    %.40s", s_info.app);
    if (s_info.ok) {
        snprintf(lines[n++], sizeof(lines[0]), ".text   %s", t);
        snprintf(lines[n++], sizeof(lines[0]), ".data   %s", d);
        snprintf(lines[n++], sizeof(lines[0]), ".bss    %s", b);
        snprintf(lines[n++], sizeof(lines[0]), "resolve %d symbols ... ok", s_info.symbols);
    } else {
        snprintf(lines[n++], sizeof(lines[0]), "run");
    }
    for (int i = 0; i < n; i++) {
        float const since = s_t - (float)i * LINE_S;
        if (since <= 0.0f) break;
        int const len   = (int)strlen(lines[i]);
        int       shown = (int)(since * TYPE_CPS);
        if (shown > len) shown = len;
        char part[64];
        snprintf(part, sizeof(part), "%.*s", shown, lines[i]);
        mono(fb, dim(i == 0 ? GOLD : AMBER, lit), 18, x0, y0 + (float)i * step, part);
    }

    // The memory map: the app as it lies in memory, code, data and the
    // zeroed rest, filling in from the left.
    uint32_t const total = s_info.text + s_info.data + s_info.bss;
    float const    mw = (float)DISPLAY_LOG_W - 2.0f * x0, my = y0 + 6.0f * step + 16.0f, mh = 16.0f;
    float const    fill = clamp01((s_t - MAP_AT_S) / MAP_S);
    pax_outline_rect(fb, dim(FAINT, lit), x0 - 2, my - 2, mw + 3, mh + 3);
    if (s_info.ok && total > 0u) {
        uint32_t const part[3] = {s_info.text, s_info.data, s_info.bss};
        uint32_t const col[3]  = {GOLD, AMBER, EMBER};
        float          at      = x0;
        for (int i = 0; i < 3; i++) {
            float const w     = mw * (float)part[i] / (float)total;
            float const shown = fminf(w, fmaxf(0.0f, x0 + mw * fill - at));
            // At least a sliver, so code and data show beside 7 MB of zeroes.
            float const sliver = part[i] > 0u && shown > 0.0f ? fmaxf(shown, 3.0f) : shown;
            if (sliver > 0.0f) pax_simple_rect(fb, dim(col[i], lit), at, my, sliver, mh);
            at += fmaxf(w, part[i] > 0u ? 3.0f : 0.0f);
        }
    }

    // Its name, brightening in, a little letter-spaced.
    float const k = clamp01((s_t - NAME_AT_S) / 0.5f);
    if (k > 0.0f) {
        char const* const name = "Graceloader";
        float const       size = 64.0f;
        float const       ny   = my + 62.0f;
        float const       nx   = ((float)DISPLAY_LOG_W - mono_width(size, name)) * 0.5f;
        for (int dy = 0; dy < 3; dy++)
            for (int dx = 0; dx < 3; dx++) mono(fb, dim(GOLD, k * lit), size, nx + (float)dx, ny + (float)dy, name);
        float const     sub = clamp01((s_t - NAME_AT_S - 0.35f) / 0.4f) * lit;
        char const*     tag = "app loader for Tanmatsu";
        if (sub > 0.0f)
            mono(fb, dim(AMBER, sub), 18, ((float)DISPLAY_LOG_W - mono_width(18, tag)) * 0.5f, ny + size + 16.0f, tag);
    }
}
