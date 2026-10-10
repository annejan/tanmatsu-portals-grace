#include "hud.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "input.h"
#include "pax_fonts.h"
#include "pax_text.h"
#include "render.h"
#include "sound.h"
#include "synthengine3d.h"

#define STORY_CPS    30.0f  // the story line types out this fast ...
#define STORY_HOLD   5.0f   // ... and stays this long once it is all there
#define TURRET_SUB_S 2.5f   // what a turret said, as a subtitle, for this long

static char        s_msg[HUD_MESSAGE_N];
static float       s_msg_t;
static float       s_story_t;  // seconds since the story line started typing
static float       s_sub_t;
static int         s_sub_seen;
static char const* s_sub;

void hud_message(char const* text) {
    snprintf(s_msg, sizeof(s_msg), "%s", text);
    s_msg_t = HUD_MESSAGE_S;
}

bool hud_message_up(void) {
    return s_msg_t > 0.0f;
}

char const* hud_message_text(void) {
    return s_msg;
}

void hud_story_start(void) {
    s_story_t = 0.0f;
}

void hud_quiet(void) {
    s_msg_t    = 0.0f;
    s_sub_t    = 0.0f;
    s_sub_seen = sound_turret_said(&s_sub);
}

void hud_tick_message(float dt) {
    if (s_msg_t > 0.0f) s_msg_t -= dt;
}

void hud_tick(float dt) {
    hud_tick_message(dt);
    s_story_t += dt;
    if (s_sub_t > 0.0f) s_sub_t -= dt;
    char const* said = NULL;
    int const   n    = sound_turret_said(&said);
    if (n != s_sub_seen) {
        s_sub_seen = n;
        s_sub      = said;
        s_sub_t    = TURRET_SUB_S;
    }
}

// The story line, typed out a letter at a time, in lines of whole words
// along the bottom of the screen. Returns how many lines it drew.
static int draw_story(pax_buf_t* fb, char const* story) {
    int const len = (int)strlen(story);
    // Up while it types, a while after, and as long as GLaDOS is still saying it.
    if (len == 0 || (s_story_t > (float)len / STORY_CPS + STORY_HOLD && !sound_saying())) return 0;
    int shown = (int)(s_story_t * STORY_CPS);
    if (shown > len) shown = len;
    enum {
        WIDTH = 70,
        LINES = 3
    };
    char line[LINES][WIDTH + 1];
    int  n = 0, at = 0;
    while (at < shown && n < LINES) {
        int end = at + WIDTH < len ? at + WIDTH : len;
        if (end < len)  // break at the last space that fits
            for (int k = end; k > at; k--)
                if (story[k] == ' ') {
                    end = k;
                    break;
                }
        int const upto = end < shown ? end : shown;
        snprintf(line[n++], sizeof(line[0]), "%.*s", upto - at, story + at);
        at = end;
        while (at < len && story[at] == ' ') at++;
    }
    for (int i = 0; i < n; i++) {
        float const y = (float)(DISPLAY_LOG_H - 44 - (n - 1 - i) * 18);
        rendertext_draw(fb, 0xFFFFE08Au, pax_font_sky_mono, 14, 16, y, line[i]);
    }
    return n;
}

// A turret's last words, in its own colour, where the story line goes --
// above it, if `above` lines of it are up.
static void draw_subtitle(pax_buf_t* fb, int above) {
    if (s_sub_t <= 0.0f || s_sub == NULL) return;
    char text[48];
    snprintf(text, sizeof(text), "Turret: %s", s_sub);
    rendertext_draw(fb, 0xFFFF7A6Au, pax_font_sky_mono, 14, 16, (float)(DISPLAY_LOG_H - 44 - above * 18), text);
}

static void draw_message(pax_buf_t* fb) {
    if (s_msg_t <= 0.0f) return;
    pax_vec2f const sz = rendertext_size(pax_font_sky_mono, 24, s_msg);
    rendertext_draw(fb, 0xFFFFFFFFu, pax_font_sky_mono, 24, RENDER_HALF_W - sz.x * 0.5f, RENDER_HORIZON_Y - 70, s_msg);
}

// A recording's timer, where the keys' help goes: the run so far, and
// this chamber's.
static void draw_timer(pax_buf_t* fb, hud_timer_t const* t) {
    char big[24], small[80];
    snprintf(big, sizeof(big), "%d:%05.2f", (int)(t->total / 60.0f), (double)fmodf(t->total, 60.0f));
    if (t->run >= 0.0f)
        snprintf(small, sizeof(small), "%s  %.2f", t->name, (double)t->run);
    else
        snprintf(small, sizeof(small), "%s", t->name);
    pax_vec2f const bs = rendertext_size(pax_font_sky_mono, 24, big);
    pax_simple_rect(fb, 0xC0000000u, DISPLAY_LOG_W - bs.x - 20, 4, bs.x + 14, 46);
    rendertext_draw(fb, 0xFF78FF8Cu, pax_font_sky_mono, 24, DISPLAY_LOG_W - bs.x - 13, 6, big);
    pax_vec2f const ss = rendertext_size(pax_font_sky_mono, 12, small);
    rendertext_draw(fb, 0xFFC8C8C8u, pax_font_sky_mono, 12, DISPLAY_LOG_W - ss.x - 13, 34, small);
}

void hud_draw(pax_buf_t* fb, game_t const* g, hud_info_t const* info) {
    float const cx = RENDER_HALF_W, cy = RENDER_HORIZON_Y;
    // The crosshair: blue half left, orange half right, filled when placed.
    for (int i = 0; i < 2; i++) {
        uint32_t const col = i == 0 ? 0xFF2C8CFFu : 0xFFFF8A1Cu;
        float const    x   = i == 0 ? cx - 12 : cx + 6;
        if (g->portals[i].open) {
            pax_simple_rect(fb, col, x, cy - 8, 6, 16);
        } else {
            pax_outline_rect(fb, col, x, cy - 8, 6, 16);
        }
    }
    pax_simple_rect(fb, 0xFFFFFFFFu, cx - 1, cy - 1, 2, 2);

    rendertext_draw(fb, 0xFFFFFFFFu, pax_font_sky_mono, 16, 8, 6, g->lv.name);
    rendertext_draw(fb, 0xFFA0A0A0u, pax_font_sky_mono, 12, 8, 26, g->lv.hint);

    if (info->timer != NULL) {
        draw_timer(fb, info->timer);
        draw_subtitle(fb, draw_story(fb, g->lv.story));
        draw_message(fb);
        return;
    }
    // The keys as they are bound now, not as they shipped.
    char blue[16], orange[16], use[16], help[112];
    snprintf(help, sizeof(help), "%s blue   %s orange   %s use   Esc menu",
             input_key_name(input_key(ACT_BLUE), blue, sizeof(blue)),
             input_key_name(input_key(ACT_ORANGE), orange, sizeof(orange)),
             input_key_name(input_key(ACT_USE), use, sizeof(use)));
    pax_vec2f const hs = rendertext_size(pax_font_sky_mono, 12, help);
    rendertext_draw(fb, 0xFFA0A0A0u, pax_font_sky_mono, 12, DISPLAY_LOG_W - 8 - hs.x, 6, help);
    if (info->recording) {
        pax_vec2f const rs = rendertext_size(pax_font_sky_mono, 14, "REC");
        pax_simple_circle(fb, 0xFFFF3030u, DISPLAY_LOG_W - 14, 30, 5);
        rendertext_draw(fb, 0xFFFF6060u, pax_font_sky_mono, 14, DISPLAY_LOG_W - 24 - rs.x, 23, "REC");
    }
    if (info->race != NULL) {
        // The race: this attempt's time, green while it beats them all,
        // and each racer's time in its colour, under the keys.
        hud_race_t const* r     = info->race;
        float             y     = info->recording ? 42.0f : 24.0f;
        bool              ahead = true;
        for (int i = 0; i < r->n; i++) ahead &= r->now <= r->line[i].time;
        char now[24];
        snprintf(now, sizeof(now), "%.2f", (double)r->now);
        pax_vec2f const ns = rendertext_size(pax_font_sky_mono, 20, now);
        rendertext_draw(fb, ahead ? 0xFF78FF8Cu : 0xFFFF8A6Au, pax_font_sky_mono, 20, DISPLAY_LOG_W - 8 - ns.x, y, now);
        y += 22.0f;
        if (r->n == 0) {
            pax_vec2f const fs = rendertext_size(pax_font_sky_mono, 12, "first run: no ghost yet");
            rendertext_draw(fb, 0xFFC8C8C8u, pax_font_sky_mono, 12, DISPLAY_LOG_W - 8 - fs.x, y,
                            "first run: no ghost yet");
        }
        for (int i = 0; i < r->n; i++, y += 14.0f) {
            char line[48];
            snprintf(line, sizeof(line), "%.20s %.2f", r->line[i].name, (double)r->line[i].time);
            pax_vec2f const ls = rendertext_size(pax_font_sky_mono, 12, line);
            rendertext_draw(fb, r->line[i].col, pax_font_sky_mono, 12, DISPLAY_LOG_W - 8 - ls.x, y, line);
        }
        // Their names over their heads.
        for (int i = 0; i < r->n_tags; i++) {
            pax_vec2f const ts = rendertext_size(pax_font_sky_mono, 12, r->tag[i].name);
            float const     x  = r->tag[i].x - ts.x * 0.5f, ty = r->tag[i].y - 14.0f;
            if (x < 0 || x + ts.x > DISPLAY_LOG_W || ty < 0 || ty > DISPLAY_LOG_H - 14) continue;
            rendertext_draw(fb, r->tag[i].col, pax_font_sky_mono, 12, x, ty, r->tag[i].name);
        }
    }

    if (info->test) return;
    if (!info->no_stats) {
        int  passes, tris;
        char stat[64];
        render_stats(&passes, &tris);
        snprintf(stat, sizeof(stat), "%2.0f fps %3d ms  %d pass %d tri%s%s", (double)info->fps, info->render_ms,
                 passes, tris, info->half ? "  half" : "", info->gyro ? "  gyro" : "");
        rendertext_draw(fb, 0xFFA0A0A0u, pax_font_sky_mono, 12, 8, DISPLAY_LOG_H - 18, stat);
    }
    draw_subtitle(fb, draw_story(fb, g->lv.story));
    draw_message(fb);
}

void hud_draw_title(pax_buf_t* fb, game_t const* g, bool message) {
    rendertext_draw(fb, 0xFFA0A0A0u, pax_font_sky_mono, 12, 8, DISPLAY_LOG_H - 18, g->lv.name);
    if (s_msg_t > 0.0f && message) {
        pax_vec2f const sz = rendertext_size(pax_font_sky_mono, 20, s_msg);
        rendertext_draw(fb, 0xFFFFFFFFu, pax_font_sky_mono, 20, RENDER_HALF_W - sz.x * 0.5f, DISPLAY_LOG_H - 44, s_msg);
    }
}

// PAX's translucent rectangle took some 70 ms a frame over the whole screen.
void hud_fade(pax_buf_t* buf, float lit) {
    uint16_t* const px   = pax_buf_get_pixels_rw(buf);
    size_t const    n    = pax_buf_get_size(buf) / 2;
    uint32_t const  k    = (uint32_t)(32.0f * lit);
    bool const      swap = buf->reverse_endianness;
    for (size_t i = 0; i < n; i++) {
        uint32_t p = px[i];
        if (swap) p = (p >> 8 | p << 8) & 0xFFFFu;
        p = ((p & 0xF81Fu) * k >> 5 & 0xF81Fu) | ((p & 0x07E0u) * k >> 5 & 0x07E0u);
        if (swap) p = (p >> 8 | p << 8) & 0xFFFFu;
        px[i] = (uint16_t)p;
    }
}
