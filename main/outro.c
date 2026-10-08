#include "outro.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "chamber.h"
#include "demo.h"
#include "level.h"
#include "pax_fonts.h"
#include "se_text.h"
#include "synthengine3d.h"

#define WALK_LIT   0.55f  // the lights are low
#define FADE_S     2.5f
#define TYPE_RATE  24.0f  // letters a second
#define CARD_HOLD  2.8f   // seconds a card stays once typed
#define WALK_MAX_S 90.0f

typedef enum {
    O_WALK,
    O_FADE,
    O_CARDS,
    O_DONE
} phase_t;

static phase_t            s_phase = O_DONE;
static float              s_t;  // into the phase
static int                s_card;
static step_t             s_steps[SCRIPT_MAX_STEPS];
static demo_player_t      s_player;
static desk_data_t const* s_d;
static float              s_done_t;  // when the script ran out, or -1

bool outro_start(game_t* g, int chamber, desk_data_t const* d) {
    level_t* const lv = level_scratch();
    int            n  = 0;
    if (!chamber_build(chamber, lv, s_steps, &n) || n == 0) return false;
    game_load_level(g, lv);
    g->chamber = chamber;
    demo_player_start(&s_player, s_steps);
    s_d      = d;
    s_phase  = O_WALK;
    s_t      = 0.0f;
    s_card   = 0;
    s_done_t = -1.0f;
    return true;
}

static int n_cards(void) {
    return s_d->n_cards + (s_d->reveal != NULL ? 1 : 0);
}

static char const* card_text(int i) {
    return i < s_d->n_cards ? s_d->cards[i] : s_d->reveal;
}

static void next_phase(phase_t p) {
    s_phase = p;
    s_t     = 0.0f;
}

bool outro_update(game_t* g, float dt, int* events) {
    *events  = 0;
    s_t     += dt;
    switch (s_phase) {
        case O_WALK: {
            *events = demo_player_step(&s_player, g, dt, 0.0f);
            if (demo_player_done(&s_player) && s_done_t < 0.0f) s_done_t = s_t;
            // Through the door, or the walk over and a moment more.
            if ((*events & PL_EV_EXIT) || (s_done_t >= 0.0f && s_t - s_done_t > 2.0f) || s_t > WALK_MAX_S)
                next_phase(O_FADE);
            return true;
        }
        case O_FADE:
            // Still walking, into the dark.
            *events = demo_player_step(&s_player, g, dt, 0.0f) & ~PL_EV_EXIT;
            if (s_t > FADE_S) next_phase(n_cards() > 0 ? O_CARDS : O_DONE);
            return true;
        case O_CARDS: {
            bool const  reveal = s_card == n_cards() - 1 && s_d->reveal != NULL;
            float const typed  = (float)strlen(card_text(s_card)) / TYPE_RATE;
            if (s_t > typed + (reveal ? CARD_HOLD * 3.0f : CARD_HOLD)) {
                if (++s_card >= n_cards()) next_phase(O_DONE);
                s_t = 0.0f;
            }
            return s_phase != O_DONE;
        }
        default:
            return false;
    }
}

void outro_skip(void) {
    switch (s_phase) {
        case O_WALK:
            next_phase(O_FADE);
            break;
        case O_FADE:
            s_t = FADE_S;
            break;
        case O_CARDS: {
            float const typed = (float)strlen(card_text(s_card)) / TYPE_RATE;
            if (s_t < typed) {
                s_t = typed;  // all of it at once
            } else if (++s_card >= n_cards()) {
                next_phase(O_DONE);
            } else {
                s_t = 0.0f;
            }
            break;
        }
        default:
            break;
    }
}

bool outro_world(void) {
    return s_phase == O_WALK || s_phase == O_FADE;
}

float outro_lit(void) {
    if (s_phase == O_WALK) return WALK_LIT;
    if (s_phase == O_FADE) return WALK_LIT * fmaxf(0.0f, 1.0f - s_t / FADE_S);
    return 0.0f;
}

// `text`, centred on `y`, in lines of whole words at most `chars` long.
static void centred(pax_buf_t* fb, pax_col_t col, float size, float y, char const* text, size_t shown, size_t chars) {
    char   line[128];
    size_t at = 0, len = strlen(text);
    if (shown > len) shown = len;
    while (at < shown) {
        size_t end = at + chars < len ? at + chars : len;
        if (end < len)
            for (size_t k = end; k > at + chars / 2; k--)
                if (text[k] == ' ') {
                    end = k;
                    break;
                }
        size_t const n = (end < shown ? end : shown) - at;
        snprintf(line, sizeof(line), "%.*s", (int)(n < sizeof(line) ? n : sizeof(line) - 1), text + at);
        // Centred as if whole, so the line does not move as it is typed.
        char whole[128];
        snprintf(whole, sizeof(whole), "%.*s", (int)(end - at < sizeof(whole) ? end - at : sizeof(whole) - 1),
                 text + at);
        float const w = rendertext_size(pax_font_sky_mono, size, whole).x;
        rendertext_draw(fb, col, pax_font_sky_mono, size, ((float)DISPLAY_LOG_W - w) * 0.5f, y, line);
        y  += size * 1.5f;
        at  = end;
        while (at < len && text[at] == ' ') at++;
    }
}

void outro_draw(pax_buf_t* fb) {
    pax_background(fb, 0xFF000000u);
    if (s_phase != O_CARDS) return;
    char const* const text   = card_text(s_card);
    size_t const      shown  = (size_t)(s_t * TYPE_RATE);
    bool const        reveal = s_card == n_cards() - 1 && s_d->reveal != NULL;
    if (!reveal) {
        centred(fb, 0xFFE8E8E8u, 20.0f, (float)DISPLAY_LOG_H * 0.42f, text, shown, 56);
        return;
    }
    // The reveal: big, and what is under it once it is typed.
    centred(fb, 0xFFFFFFFFu, 44.0f, (float)DISPLAY_LOG_H * 0.36f, text, shown, 24);
    if (shown < strlen(text)) return;
    for (int i = 0; i < s_d->n_coda; i++)
        centred(fb, 0xFFB0B0B0u, 16.0f, (float)DISPLAY_LOG_H * 0.58f + (float)i * 26.0f, s_d->coda[i],
                (size_t)((s_t - (float)strlen(text) / TYPE_RATE) * TYPE_RATE), 70);
}
