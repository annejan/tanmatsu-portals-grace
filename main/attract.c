#include "attract.h"
#include <math.h>
#include <string.h>
#include "chamber.h"
#include "cine.h"
#include "demo.h"

// Each run is played twice. The first time, at once and unseen while the
// screen is black between two runs, logs the view at every step, and the
// camera is worked out from that as the films' is (cine.h): on the aim
// before every shot, looking where it walks, easing into every turn. The
// second time is the one on screen, stepped at the same fixed step as the
// first so it goes exactly the same way, whatever the frames.

#define ATTRACT_TICK   (1.0f / 30.0f)  // s: a step, both times
#define ATTRACT_PACE   0.8f            // s: the player stands this long before each act, looking
#define ATTRACT_MAX_S  40.0f           // a run this long has gone on long enough
#define ATTRACT_DONE_S 2.5f            // the script has run out short of the exit: this long more
#define ATTRACT_END_S  1.2f            // through the exit: this long, fading, before the next
#define ATTRACT_FADE_S 0.6f
#define ATTRACT_STEPS  4     // steps a frame at most: a slow frame slows the run, not jumps it
#define ATTRACT_TICKS  1300  // steps logged at most: (ATTRACT_MAX_S + ATTRACT_DONE_S) * 30, and some

static demo_player_t s_play;
static int           s_order[CHAMBER_MAX];  // the chambers' solution demos, shuffled
static int           s_n = -1;              // how many; -1 until counted
static int           s_k;                   // the one playing, in s_order
static float         s_t;                   // seconds into it, on screen
static float         s_end;                 // > 0: it is over, and the next comes in this long
static float         s_acc;                 // seconds not yet stepped
static float         s_pace;                // the run's pace: ATTRACT_PACE, or 0 if paced it misses the exit
static int           s_tick;                // steps into the run
static uint32_t      s_seed = 1u;
static int           s_runs, s_exits;

// The first run's views, and the camera worked out from them.
static cine_view_t s_log[ATTRACT_TICKS];
static float       s_cam[ATTRACT_TICKS][2], s_want[ATTRACT_TICKS][2];
static bool        s_aimed[ATTRACT_TICKS];
static int         s_logged;

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
    // The built-in chambers with a solution (their demos are named after
    // them): not the card's, which would be read whole to find out.
    s_n = 0;
    for (int i = 0; i < demo_count() && s_n < CHAMBER_MAX; i++)
        if (demo_chamber(i) < chamber_builtin_n() && strcmp(demo_name(i), chamber_id(demo_chamber(i))) == 0 &&
            demo_has_solution(i))
            s_order[s_n++] = i;
    shuffle();
    s_k = -1;
}

// When a run on screen is over, from what its step brought: how long
// until the next, or 0 if it goes on. The first run ends by the same rule.
static float over(int ev, float t) {
    if (ev & (PL_EV_EXIT | PL_EV_DIED)) return ATTRACT_END_S;
    if (t > ATTRACT_MAX_S) return ATTRACT_FADE_S;
    if (demo_player_done(&s_play)) return ATTRACT_DONE_S;
    return 0.0f;
}

// The first run, logged, to its end and the wait after it. True if it
// reached the exit.
static bool rehearse(game_t* g, int chamber, step_t const* steps, float pace) {
    game_load(g, chamber);
    demo_player_start(&s_play, steps);
    s_logged     = 0;
    bool  exited = false;
    float end    = 0.0f;
    while (s_logged < ATTRACT_TICKS) {
        int const ev       = demo_player_step(&s_play, g, ATTRACT_TICK, pace);
        s_log[s_logged++]  = (cine_view_t){g->pl.yaw,   g->pl.pitch,     g->pl.vel.x,  g->pl.vel.y,
                                           g->pl.vel.z, g->pl.on_ground, g->held >= 0, ev};
        exited            |= (ev & PL_EV_EXIT) != 0;
        if (end > 0.0f) {
            if ((end -= ATTRACT_TICK) <= 0.0f) break;
            continue;
        }
        end = over(ev, (float)s_logged * ATTRACT_TICK);
    }
    return exited;
}

void attract_begin(game_t* g) {
    if (s_n < 0) count();
    s_t    = 0.0f;
    s_end  = 0.0f;
    s_acc  = 0.0f;
    s_tick = 0;
    s_runs++;
    if (s_n == 0) {  // none: the first chamber, standing still
        static step_t const none[] = {{OP_END, 0, 0, 0, 0}};
        s_logged                   = 0;
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
    // Paced, unless that misses the exit: a solution timed to a moving
    // platform or a crusher keeps the script's own timing.
    s_pace = ATTRACT_PACE;
    if (!rehearse(g, demo_chamber(i), steps, s_pace)) {
        s_pace = 0.0f;
        rehearse(g, demo_chamber(i), steps, s_pace);
    }
    cine_plan(s_log, s_logged, ATTRACT_TICK, s_cam, s_want, s_aimed);
    game_load(g, demo_chamber(i));
    demo_player_start(&s_play, steps);
}

bool attract_update(game_t* g, float dt) {
    s_t += dt;
    if (s_end > 0.0f && (s_end -= dt) <= 0.0f) {
        attract_begin(g);
        return true;
    }
    int ev  = 0;
    s_acc  += dt;
    for (int n = 0; s_acc >= ATTRACT_TICK && n < ATTRACT_STEPS; n++) {
        int const e  = demo_player_step(&s_play, g, ATTRACT_TICK, s_pace);
        s_acc       -= ATTRACT_TICK;
        ev          |= e;
        s_tick++;
        if (s_end <= 0.0f) {
            if (e & PL_EV_EXIT) s_exits++;
            s_end = over(e, (float)s_tick * ATTRACT_TICK);
        }
    }
    if (s_acc > ATTRACT_TICK) s_acc = 0.0f;  // behind: slowed, not caught up
    return (ev & (GAME_EV_PORTAL | GAME_EV_PAINT)) != 0;
}

void attract_camera(game_t const* g, float* yaw, float* pitch) {
    if (s_logged == 0) {
        *yaw   = g->pl.yaw;
        *pitch = g->pl.pitch;
        return;
    }
    int k = s_tick - 1;  // the log's step k is the state after k + 1 steps
    if (k < 0) k = 0;
    if (k >= s_logged) k = s_logged - 1;
    *yaw   = s_cam[k][0];
    *pitch = s_cam[k][1];
}

float attract_lit(void) {
    float const in  = s_t / ATTRACT_FADE_S;
    float const out = s_end > 0.0f ? s_end / ATTRACT_FADE_S : 1.0f;
    return fmaxf(0.0f, fminf(1.0f, fminf(in, out)));
}
