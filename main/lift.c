#include "lift.h"
#include <math.h>
#include "player.h"

float lift_room(level_t const* lv, vec3_t p, float most) {
    int const x = (int)floorf(p.x), z = (int)floorf(p.z);
    int       c = (int)floorf(p.y + 0.01f) + 1;  // the cells above the one the feet are in
    while ((float)c - p.y < most + PL_EYE + 1.0f && !level_solid(lv, x, c, z)) c++;
    float const room = (float)c - p.y - PL_EYE - 0.25f;  // the eye a hand under the ceiling
    return room < 0.0f ? 0.0f : room > most ? most : room;
}

void lift_up(lift_t* l, game_t const* g) {
    l->phase = LIFT_UP;
    l->t     = 0.0f;
    l->x     = g->pl.pos.x;
    l->z     = g->pl.pos.z;
    l->from  = g->pl.pos.y;
    l->to    = l->from + lift_room(&g->lv, g->pl.pos, LIFT_RISE);
}

void lift_down(lift_t* l, game_t* g) {
    l->phase     = LIFT_DOWN;
    l->t         = 0.0f;
    l->x         = g->pl.pos.x;
    l->z         = g->pl.pos.z;
    l->to        = g->pl.pos.y;
    l->from      = l->to + lift_room(&g->lv, g->pl.pos, LIFT_RISE);
    g->pl.pos.y  = l->from;
    g->pl.vel    = v3(0, 0, 0);
}

int lift_step(lift_t* l, game_t* g, float dt, bool wait) {
    if (l->phase == LIFT_NONE) return 0;
    l->t           += dt;
    float const k   = fminf(1.0f, l->t / LIFT_S);
    float const e   = k * k * (3.0f - 2.0f * k);
    float const y0  = g->pl.pos.y;
    g->pl.vel       = v3(0, 0, 0);
    g->pl.pos.y     = l->from + (l->to - l->from) * e;
    if (l->phase == LIFT_UP && g->held >= 0) {
        // The cube carried comes up too.
        body_t* b = &g->cubes[g->held].body;
        b->pos.y += g->pl.pos.y - y0;
        b->vel    = v3(0, 0, 0);
    }
    if (k < 1.0f) return 0;
    if (l->phase == LIFT_DOWN) {
        l->phase = LIFT_NONE;  // there: on with it
        return LIFT_EV_LANDED;
    }
    return wait ? 0 : LIFT_EV_TOP;
}

bool lift_on(lift_t const* l) {
    return l->phase != LIFT_NONE;
}

float lift_lit(lift_t const* l) {
    if (l->phase == LIFT_NONE) return 1.0f;
    float const k = fminf(1.0f, l->t / LIFT_S);
    return l->phase == LIFT_UP ? 1.0f - k : k;
}

void lift_tube(lift_t const* l, float* x, float* y0, float* y1, float* z) {
    float const lo = fminf(l->from, l->to);
    *x             = l->x;
    *z             = l->z;
    *y0            = lo - 0.02f;
    *y1            = lo + fabsf(l->to - l->from) + PL_EYE + 0.25f;  // a hand over the eye at the top
}
