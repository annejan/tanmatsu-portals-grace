#include "attract.h"
#include <math.h>
#include <string.h>
#include "chamber.h"
#include "demo.h"

#define ATTRACT_PACE   0.35f  // s: the player looks this long before each shot and Use
#define ATTRACT_MAX_S  40.0f  // a run this long has gone on long enough
#define ATTRACT_DONE_S 2.5f   // the script has run out short of the exit: this long more
#define ATTRACT_END_S  1.2f   // through the exit: this long, fading, before the next
#define ATTRACT_FADE_S 0.6f

static demo_player_t s_play;
static int           s_order[CHAMBER_MAX];  // the chambers' solution demos, shuffled
static int           s_n = -1;              // how many; -1 until counted
static int           s_k;                   // the one playing, in s_order
static float         s_t;                   // seconds into it
static float         s_end;                 // > 0: it is over, and the next comes in this long
static uint32_t      s_seed = 1u;
static int           s_runs, s_exits;

void attract_seed(uint32_t seed) {
    s_seed = seed | 1u;
}

void attract_recount(void) {
    s_n = -1;
}

int attract_runs(void) {
    return s_runs;
}

int attract_exits(void) {
    return s_exits;
}

static void shuffle(void) {
    for (int i = s_n - 1; i > 0; i--) {
        s_seed      = s_seed * 1103515245u + 12345u;
        int const j = (int)((s_seed >> 8) % (uint32_t)(i + 1));
        int const t = s_order[i];
        s_order[i]  = s_order[j];
        s_order[j]  = t;
    }
}

static void count(void) {
    // The chambers with a solution: their demos are named after them.
    s_n = 0;
    for (int i = 0; i < demo_count() && s_n < CHAMBER_MAX; i++)
        if (strcmp(demo_name(i), chamber_id(demo_chamber(i))) == 0 && demo_has_solution(i)) s_order[s_n++] = i;
    shuffle();
    s_k = -1;
}

void attract_begin(game_t* g) {
    if (s_n < 0) count();
    s_t   = 0.0f;
    s_end = 0.0f;
    s_runs++;
    if (s_n == 0) {  // none: the first chamber, standing still
        static step_t const none[] = {{OP_END, 0, 0, 0, 0}};
        game_load(g, 0);
        demo_player_start(&s_play, none);
        return;
    }
    if (++s_k >= s_n) {
        int const last = s_order[s_n - 1];
        shuffle();
        if (s_n > 1 && s_order[0] == last) {  // not the same one twice running
            s_order[0]       = s_order[s_n - 1];
            s_order[s_n - 1] = last;
        }
        s_k = 0;
    }
    int const           i     = s_order[s_k];
    step_t const* const steps = i < demo_count() ? demo_steps(i) : NULL;
    if (steps == NULL) {  // the list changed under it: counted again
        s_runs--;
        count();
        attract_begin(g);
        return;
    }
    game_load(g, demo_chamber(i));
    demo_player_start(&s_play, steps);
}

bool attract_update(game_t* g, float dt) {
    if (s_end > 0.0f && (s_end -= dt) <= 0.0f) {
        attract_begin(g);
        return true;
    }
    int const ev  = demo_player_step(&s_play, g, dt, ATTRACT_PACE);
    s_t          += dt;
    if (s_end <= 0.0f) {
        if (ev & PL_EV_EXIT) s_exits++;
        if (ev & (PL_EV_EXIT | PL_EV_DIED))
            s_end = ATTRACT_END_S;
        else if (s_t > ATTRACT_MAX_S)
            s_end = ATTRACT_FADE_S;
        else if (demo_player_done(&s_play))
            s_end = ATTRACT_DONE_S;
    }
    return (ev & (GAME_EV_PORTAL | GAME_EV_PAINT)) != 0;
}

float attract_lit(void) {
    float const in  = s_t / ATTRACT_FADE_S;
    float const out = s_end > 0.0f ? s_end / ATTRACT_FADE_S : 1.0f;
    return fmaxf(0.0f, fminf(1.0f, fminf(in, out)));
}
