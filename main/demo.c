#include "demo.h"
#include <math.h>
#include <string.h>
#include "chamber.h"
#include "script.h"

#define STEP_DT (1.0f / 50.0f)
#define HALF_PI 1.5707963f

typedef struct {
    char const*   name;
    int           chamber;
    float         duration;
    step_t const* steps;
} demo_t;

// Shot at eye height, as a player does: two portals side by side on the
// left wall, then straight into the first.
static step_t const s_c1walk[] = {
    {OP_FACE, 0, -HALF_PI, 0.0f, 0}, {OP_SHOOT_VIEW, PORTAL_BLUE, 0, 0, 0},
    {OP_FACE, 0, -1.2f, 0.0f, 0},    {OP_SHOOT_VIEW, PORTAL_ORANGE, 0, 0, 0},
    {OP_FACE, 0, -HALF_PI, 0.0f, 0}, {OP_WAIT, 0, 0.5f, 0, 0},
    {OP_WALK, 0, 4.0f, 0, 0},        {OP_END, 0, 0, 0, 0},
};

// Facing each other across the room: walking on goes round and round.
static step_t const s_c1loop[] = {
    {OP_SHOOT, PORTAL_BLUE, 1.0f, 1.9f, 3.5f},
    {OP_SHOOT, PORTAL_ORANGE, 9.0f, 1.9f, 3.5f},
    {OP_WALK_TO, 0, 5.0f, 1.0f, 3.5f},
    {OP_FACE, 0, -HALF_PI, 0.0f, 0},
    {OP_WALK, 0, 6.0f, 0, 0},
    {OP_END, 0, 0, 0, 0},
};

// The hand-written demos; after them, one per chamber with a solution
// in its file, named after the file (chamber.h).
static demo_t const s_demos[] = {
    {"c1walk", 0, 6.0f, s_c1walk},
    {"c1loop", 0, 9.0f, s_c1loop},
};

#define N_FIXED    ((int)(sizeof(s_demos) / sizeof(s_demos[0])))
#define SOLUTION_S 30.0f  // a solution demo runs this long; it stands still once done

int demo_count(void) {
    return N_FIXED + chamber_count();
}

int demo_find(char const* name) {
    for (int i = 0; i < demo_count(); i++)
        if (strcmp(demo_name(i), name) == 0) return i;
    return -1;
}

char const* demo_name(int i) {
    if (i >= 0 && i < N_FIXED) return s_demos[i].name;
    return chamber_id(i - N_FIXED);
}

float demo_duration(int i) {
    if (i >= 0 && i < N_FIXED) return s_demos[i].duration;
    return i < demo_count() ? SOLUTION_S : 0.0f;
}

int demo_chamber(int i) {
    return i >= 0 && i < N_FIXED ? s_demos[i].chamber : i - N_FIXED;
}

bool demo_has_solution(int i) {
    if (i < N_FIXED) return true;
    static step_t steps[SCRIPT_MAX_STEPS];
    int           n = 0;
    return chamber_build(i - N_FIXED, level_scratch(), steps, &n) && n > 0;
}

static void face_point(player_t* p, vec3_t target) {
    vec3_t const d = v3_sub(target, player_eye(p));
    p->yaw         = atan2f(d.x, d.z);
    p->pitch       = -atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z));
}

void demo_eval(int i, float t, demo_state_t* s) {
    demo_eval_dt(i, t, STEP_DT, s);
}

void demo_eval_dt(int i, float t, float dt, demo_state_t* s) {
    demo_run(i, t, dt, s, NULL, NULL, 0.0f);
}

static bool is_act(int op) {
    return op == OP_SHOOT || op == OP_SHOOT_VIEW || op == OP_USE || op == OP_GRAB;
}

// A shot's events, as game_step() reports one fired by a key.
static int shot_events(bool ok, int which) {
    return ok ? GAME_EV_PORTAL | (which == 0 ? GAME_EV_SHOT_BLUE : GAME_EV_SHOT_ORANGE) : GAME_EV_SHOT_FAIL;
}

void demo_player_start(demo_player_t* p, step_t const* steps) {
    *p       = (demo_player_t){0};
    p->steps = steps;
    p->paced = -1;
}

bool demo_player_done(demo_player_t const* p) {
    return p->steps[p->k].op == OP_END;
}

int demo_player_step(demo_player_t* p, game_t* g, float dt, float pace) {
    game_input_t in      = {0};
    int          instant = 0;      // events of this step's instant steps
    bool         still   = false;  // paced: this step acted, and does nothing more
    // Instant steps take no time: run them all before this step -- but
    // with `pace`, standing still a while before each act first.
    for (; p->hold <= 0.0f;) {
        step_t const* st = &p->steps[p->k];
        if (pace > 0.0f && is_act(st->op) && p->paced != p->k) {
            p->paced = p->k;
            p->hold  = pace;
            break;
        }
        if (st->op == OP_FACE) {
            g->pl.yaw   = st->a;
            g->pl.pitch = st->b;
        } else if (st->op == OP_FACE_POINT) {
            face_point(&g->pl, v3(st->a, st->b, st->c));
        } else if (st->op == OP_SHOOT) {
            face_point(&g->pl, v3(st->a, st->b, st->c));
            instant |= shot_events(game_fire(g, st->which), st->which);
        } else if (st->op == OP_SHOOT_VIEW) {
            instant |= shot_events(game_fire(g, st->which), st->which);
        } else if (st->op == OP_USE) {
            instant |= game_use(g);
        } else if (st->op == OP_JUMP) {
            p->jump = true;
        } else if (st->op == OP_GRAB) {
            // The nearest cube not already carried: look at its middle
            // and pick it up. Where a cube lands can shift by a few
            // centimetres with the frame rate; a script that names
            // the spot would miss it.
            int   near = -1;
            float best = 1e9f;
            for (int c = 0; c < g->n_cubes; c++) {
                if (c == g->held) continue;
                float const d = v3_len(v3_sub(body_center(&g->cubes[c].body), player_eye(&g->pl)));
                if (d < best) {
                    best = d;
                    near = c;
                }
            }
            if (near >= 0) {
                face_point(&g->pl, body_center(&g->cubes[near].body));
                instant |= game_use(g);
            }
        } else {
            break;
        }
        p->k++;
        p->in_step = 0.0f;  // the next step's time starts now, not with a pause before this one
        // Paced, the act's step is all it does: the next step would
        // turn the player away from what it just shot at, unseen.
        if (pace > 0.0f && is_act(p->steps[p->k - 1].op)) {
            still = true;
            break;
        }
    }
    if (p->hold > 0.0f) p->hold -= dt;
    static step_t const idle = {OP_END, 0, 0, 0, 0};
    step_t const*       st   = still ? &idle : &p->steps[p->k];
    bool                done = false;
    switch (st->op) {
        case OP_WALK:
            in.fwd = 1.0f;
            done   = p->in_step >= st->a;
            break;
        case OP_WALK_TO: {
            float const dx = st->a - g->pl.pos.x, dz = st->c - g->pl.pos.z;
            float const dist = sqrtf(dx * dx + dz * dz);
            g->pl.yaw        = atan2f(dx, dz);
            in.fwd           = fminf(st->b, dist * 2.0f);
            done             = (dist < 0.2f && g->pl.on_ground) || p->in_step > 6.0f;
            break;
        }
        case OP_STEP_OFF:
            // Walk until the ground is gone. Begun in the air, it waits
            // to land first; each step_off starts afresh.
            if (p->in_step == 0.0f) p->walked = false;
            if (g->pl.on_ground) {
                in.fwd    = st->a;
                p->walked = true;
            } else if (p->walked) {
                done = true;
            }
            break;
        case OP_WAIT:
            done = p->in_step >= st->a;
            break;
        default:
            break;  // OP_END: stand still
    }
    in.jump      = p->jump;
    p->jump      = false;
    int const ev = game_step(g, &in, dt) | instant;
    if (st->op == OP_WALK_TO && (ev & PL_EV_TELEPORT)) done = true;
    if (!still) p->in_step += dt;
    if (done) {
        p->k++;
        p->in_step = 0.0f;
    }
    return ev;
}

step_t const* demo_steps(int i) {
    static step_t parsed[SCRIPT_MAX_STEPS];
    if (i < 0 || i >= demo_count()) return NULL;
    if (i < N_FIXED) return s_demos[i].steps;
    int n = 0;
    if (!chamber_build(i - N_FIXED, level_scratch(), parsed, &n)) parsed[0] = (step_t){0};
    return parsed;
}

void demo_run(int i, float t, float dt, demo_state_t* s, demo_tick_fn tick, void* ctx, float pace) {
    memset(s, 0, sizeof(*s));
    if (i < 0 || i >= demo_count()) return;
    game_t* g = &s->g;
    game_load(g, demo_chamber(i));
    demo_player_t p;
    demo_player_start(&p, demo_steps(i));
    for (float now = 0.0f; now + dt * 0.5f < t; now += dt) {
        int const ev  = demo_player_step(&p, g, dt, pace);
        s->events    |= ev;
        if (tick != NULL) tick(g, ev, now + dt, ctx);
    }
}
