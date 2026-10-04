#include "demo.h"
#include <math.h>
#include <string.h>

#define STEP_DT (1.0f / 50.0f)
#define HALF_PI 1.5707963f

typedef enum {
    OP_END = 0,
    OP_FACE,        // yaw a, pitch b, at once
    OP_SHOOT,       // portal `which` at the point (a, b, c)
    OP_SHOOT_VIEW,  // portal `which` straight along the view
    OP_WALK,        // forward for a seconds
    OP_WALK_TO,     // to (a, c) at pace b, until there, through a portal, or 6 s
    OP_STEP_OFF,    // forward at pace a until off the ground, then let go
    OP_WAIT,        // a seconds
} op_t;

typedef struct {
    op_t  op;
    int   which;
    float a, b, c;
} step_t;

typedef struct {
    char const*   name;
    int           chamber;
    float         duration;
    step_t const* steps;
} demo_t;

// Shot at eye height, as a player does: two portals side by side on the
// left wall, then straight into the first.
static step_t const s_c1walk[] = {
    {OP_FACE, 0, -HALF_PI, 0.0f, 0},  {OP_SHOOT_VIEW, PORTAL_BLUE, 0, 0, 0}, {OP_FACE, 0, -1.2f, 0.0f, 0},
    {OP_SHOOT_VIEW, PORTAL_ORANGE, 0, 0, 0}, {OP_FACE, 0, -HALF_PI, 0.0f, 0}, {OP_WAIT, 0, 0.5f, 0, 0},
    {OP_WALK, 0, 4.0f, 0, 0},         {OP_END, 0, 0, 0, 0},
};

// Facing each other across the room: walking on goes round and round.
static step_t const s_c1loop[] = {
    {OP_SHOOT, PORTAL_BLUE, 1.0f, 1.9f, 3.5f}, {OP_SHOOT, PORTAL_ORANGE, 9.0f, 1.9f, 3.5f},
    {OP_WALK_TO, 0, 5.0f, 1.0f, 3.5f},         {OP_FACE, 0, -HALF_PI, 0.0f, 0},
    {OP_WALK, 0, 6.0f, 0, 0},                  {OP_END, 0, 0, 0, 0},
};

static step_t const s_c2ledge[] = {
    {OP_SHOOT, PORTAL_BLUE, 1.0f, 1.9f, 4.5f}, {OP_SHOOT, PORTAL_ORANGE, 5.5f, 6.9f, 13.0f},
    {OP_WALK_TO, 0, -1.0f, 1.0f, 4.5f},        {OP_WAIT, 0, 0.8f, 0, 0},
    {OP_WALK_TO, 0, 5.0f, 1.0f, 11.5f},        {OP_END, 0, 0, 0, 0},
};

static step_t const s_c3fling[] = {
    {OP_WALK_TO, 0, 6.5f, 1.0f, 2.85f},        {OP_SHOOT, PORTAL_BLUE, 6.5f, 1.0f, 6.2f},
    {OP_SHOOT, PORTAL_ORANGE, 1.0f, 8.9f, 5.5f}, {OP_FACE, 0, 0.0f, 0.9f, 0},
    {OP_STEP_OFF, 0, 0.5f, 0, 0},              {OP_WAIT, 0, 3.0f, 0, 0},
    {OP_WALK_TO, 0, 20.0f, 1.0f, 4.5f},        {OP_END, 0, 0, 0, 0},
};

static demo_t const s_demos[] = {
    {"c1walk", 0, 6.0f, s_c1walk},
    {"c1loop", 0, 9.0f, s_c1loop},
    {"c2ledge", 1, 7.0f, s_c2ledge},
    {"c3fling", 2, 9.0f, s_c3fling},
};

#define N_DEMOS ((int)(sizeof(s_demos) / sizeof(s_demos[0])))

int demo_count(void) {
    return N_DEMOS;
}

int demo_find(char const* name) {
    for (int i = 0; i < N_DEMOS; i++)
        if (strcmp(s_demos[i].name, name) == 0) return i;
    return -1;
}

char const* demo_name(int i) {
    return i >= 0 && i < N_DEMOS ? s_demos[i].name : "?";
}

float demo_duration(int i) {
    return i >= 0 && i < N_DEMOS ? s_demos[i].duration : 0.0f;
}

static void face_point(player_t* p, vec3_t target) {
    vec3_t const d = v3_sub(target, player_eye(p));
    p->yaw         = atan2f(d.x, d.z);
    p->pitch       = -atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z));
}

static void shoot(demo_state_t* s, int which) {
    portal_t p;
    if (portal_place(&s->lv, player_eye(&s->pl), player_view(&s->pl).fwd, &s->portals[which ^ 1], &p))
        s->portals[which] = p;
}

void demo_eval(int i, float t, demo_state_t* s) {
    memset(s, 0, sizeof(*s));
    if (i < 0 || i >= N_DEMOS) return;
    demo_t const* d = &s_demos[i];
    level_load(&s->lv, d->chamber);
    player_spawn(&s->pl, &s->lv);

    int   k       = 0;     // the step running
    float in_step = 0.0f;  // seconds into it
    bool  left    = false; // OP_STEP_OFF: off the edge
    for (float now = 0.0f; now + STEP_DT * 0.5f < t; now += STEP_DT) {
        player_input_t in = {0};
        // Instant steps take no time: run them all before this tick.
        for (;;) {
            step_t const* st = &d->steps[k];
            if (st->op == OP_FACE) {
                s->pl.yaw   = st->a;
                s->pl.pitch = st->b;
            } else if (st->op == OP_SHOOT) {
                face_point(&s->pl, v3(st->a, st->b, st->c));
                shoot(s, st->which);
            } else if (st->op == OP_SHOOT_VIEW) {
                shoot(s, st->which);
            } else {
                break;
            }
            k++;
        }
        step_t const* st   = &d->steps[k];
        bool          done = false;
        switch (st->op) {
            case OP_WALK:
                in.fwd = 1.0f;
                done   = in_step >= st->a;
                break;
            case OP_WALK_TO: {
                float const dx = st->a - s->pl.pos.x, dz = st->c - s->pl.pos.z;
                float const dist = sqrtf(dx * dx + dz * dz);
                s->pl.yaw        = atan2f(dx, dz);
                in.fwd           = fminf(st->b, dist * 2.0f);
                done             = (dist < 0.2f && s->pl.on_ground) || in_step > 6.0f;
                break;
            }
            case OP_STEP_OFF:
                if (s->pl.on_ground && !left) {
                    in.fwd = st->a;
                } else {
                    left = true;
                    done = true;
                }
                break;
            case OP_WAIT: done = in_step >= st->a; break;
            default: break;  // OP_END: stand still
        }
        int const ev = player_update(&s->pl, &s->lv, s->portals, &in, STEP_DT);
        s->events |= ev;
        if (st->op == OP_WALK_TO && (ev & PL_EV_TELEPORT)) done = true;
        in_step += STEP_DT;
        if (done) {
            k++;
            in_step = 0.0f;
        }
    }
}
