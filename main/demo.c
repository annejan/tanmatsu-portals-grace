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
    static step_t  steps[SCRIPT_MAX_STEPS];
    static level_t lv;
    int            n = 0;
    return chamber_build(i - N_FIXED, &lv, steps, &n) && n > 0;
}

static void face_point(player_t* p, vec3_t target) {
    vec3_t const d = v3_sub(target, player_eye(p));
    p->yaw         = atan2f(d.x, d.z);
    p->pitch       = -atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z));
}

void demo_eval(int i, float t, demo_state_t* s) {
    memset(s, 0, sizeof(*s));
    if (i < 0 || i >= demo_count()) return;
    game_t* g = &s->g;
    game_load(g, demo_chamber(i));
    static step_t parsed[SCRIPT_MAX_STEPS];
    step_t const* steps = parsed;
    if (i < N_FIXED) {
        steps = s_demos[i].steps;
    } else {
        static level_t lv;
        int            n = 0;
        if (!chamber_build(i - N_FIXED, &lv, parsed, &n)) parsed[0] = (step_t){0};
    }

    int   k       = 0;      // the step running
    float in_step = 0.0f;   // seconds into it
    bool  left    = false;  // OP_STEP_OFF: off the edge
    for (float now = 0.0f; now + STEP_DT * 0.5f < t; now += STEP_DT) {
        game_input_t in = {0};
        // Instant steps take no time: run them all before this tick.
        for (;;) {
            step_t const* st = &steps[k];
            if (st->op == OP_FACE) {
                g->pl.yaw   = st->a;
                g->pl.pitch = st->b;
            } else if (st->op == OP_FACE_POINT) {
                face_point(&g->pl, v3(st->a, st->b, st->c));
            } else if (st->op == OP_SHOOT) {
                face_point(&g->pl, v3(st->a, st->b, st->c));
                if (game_fire(g, st->which)) s->events |= GAME_EV_PORTAL;
            } else if (st->op == OP_SHOOT_VIEW) {
                if (game_fire(g, st->which)) s->events |= GAME_EV_PORTAL;
            } else if (st->op == OP_USE) {
                s->events |= game_use(g);
            } else {
                break;
            }
            k++;
        }
        step_t const* st   = &steps[k];
        bool          done = false;
        switch (st->op) {
            case OP_WALK:
                in.fwd = 1.0f;
                done   = in_step >= st->a;
                break;
            case OP_WALK_TO: {
                float const dx = st->a - g->pl.pos.x, dz = st->c - g->pl.pos.z;
                float const dist = sqrtf(dx * dx + dz * dz);
                g->pl.yaw        = atan2f(dx, dz);
                in.fwd           = fminf(st->b, dist * 2.0f);
                done             = (dist < 0.2f && g->pl.on_ground) || in_step > 6.0f;
                break;
            }
            case OP_STEP_OFF:
                if (g->pl.on_ground && !left) {
                    in.fwd = st->a;
                } else {
                    left = true;
                    done = true;
                }
                break;
            case OP_WAIT:
                done = in_step >= st->a;
                break;
            default:
                break;  // OP_END: stand still
        }
        int const ev  = game_step(g, &in, STEP_DT);
        s->events    |= ev;
        if (st->op == OP_WALK_TO && (ev & PL_EV_TELEPORT)) done = true;
        in_step += STEP_DT;
        if (done) {
            k++;
            in_step = 0.0f;
        }
    }
}
