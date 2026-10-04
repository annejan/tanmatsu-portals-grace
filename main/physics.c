#include "physics.h"
#include <math.h>

#define SUBSTEP 0.2f   // longest move between collision checks, metres
#define FIT_TOL 0.02f  // slack when asking whether the box fits a hole
#define EPS     0.001f

// Leaving a portal in a floor, slower than this would drop you straight
// back in; leaving any other, you at least clear its plane.
#define EXIT_MIN_UP    6.5f
#define EXIT_MIN_OTHER 1.0f

// The box's half extent along a unit axis-aligned direction.
static float half_along(body_t const* b, vec3_t a) {
    return fabsf(a.x) * b->hw + fabsf(a.y) * (b->h * 0.5f) + fabsf(a.z) * b->hw;
}

static bool linked(portal_t const portals[2]) {
    return portals[0].open && portals[1].open;
}

// Whether the box fits through portal `pt`'s hole. Asked ONCE per
// substep, before it moves (see body_move): asked again mid-move it
// flickered -- gravity's first push into the floor made a box standing
// in the hole stop fitting, the wall behind the portal turned solid
// round a box already inside it, and the box was thrown up the wall.
static bool fits(body_t const* b, portal_t const* pt) {
    vec3_t const l = portal_local(pt, body_center(b));
    return fabsf(l.x) + half_along(b, pt->right) <= PORTAL_HALF_W + FIT_TOL &&
           fabsf(l.y) + half_along(b, pt->up) <= PORTAL_HALF_H + FIT_TOL && l.z < 2.0f;
}

// Behind an open portal is a tunnel two cells deep a box may stand in,
// while it fits through the hole.
static bool in_tunnel(portal_t const* pt, int x, int y, int z) {
    vec3_t const c = portal_local(pt, v3((float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f));
    return fabsf(c.x) < PORTAL_HALF_W && fabsf(c.y) < PORTAL_HALF_H && c.z < 0.0f && c.z > -2.5f;
}

typedef struct {
    phys_world_t const* w;
    bool                open[2];  // this substep: the box fits portal i, and the pair is linked
} step_t;

static bool blocks(step_t const* s, int x, int y, int z) {
    if (!level_solid(s->w->lv, x, y, z)) return false;
    for (int i = 0; i < 2; i++)
        if (s->open[i] && in_tunnel(&s->w->portals[i], x, y, z)) return false;
    return true;
}

static float* axis_of(vec3_t* v, int a) {
    return a == 0 ? &v->x : a == 1 ? &v->y : &v->z;
}

static float axis_get(vec3_t v, int a) {
    return a == 0 ? v.x : a == 1 ? v.y : v.z;
}

static void box_cells(body_t const* b, int c0[3], int c1[3]) {
    float const lo[3] = {b->pos.x - b->hw, b->pos.y, b->pos.z - b->hw};
    float const hi[3] = {b->pos.x + b->hw, b->pos.y + b->h, b->pos.z + b->hw};
    for (int i = 0; i < 3; i++) {
        c0[i] = (int)floorf(lo[i] + EPS);
        c1[i] = (int)floorf(hi[i] - EPS);
    }
}

// Move along one axis and push back out of whatever it ran into.
// True if it hit something. A cell or box it was already in before the
// move does not push it: being shoved along this axis out of something
// it got into some other way is how a box ends up on top of a wall.
static bool move_axis(body_t* b, step_t const* s, int a, float delta) {
    if (delta == 0.0f) return false;
    int b0[3], b1[3];
    box_cells(b, b0, b1);
    aabb_t const before = body_aabb(b);
    *axis_of(&b->pos, a) += delta;
    int c0[3], c1[3];
    box_cells(b, c0, c1);
    float const below = (a == 1) ? 0.0f : b->hw;  // pos to the box's low side
    float const above = (a == 1) ? b->h : b->hw;  // pos to the box's high side
    bool        hit   = false;
    float       best  = *axis_of(&b->pos, a);
    for (int y = c0[1]; y <= c1[1]; y++)
        for (int z = c0[2]; z <= c1[2]; z++)
            for (int x = c0[0]; x <= c1[0]; x++) {
                if (!blocks(s, x, y, z)) continue;
                bool const was_in = x >= b0[0] && x <= b1[0] && y >= b0[1] && y <= b1[1] && z >= b0[2] && z <= b1[2];
                if (was_in) continue;
                int const   cell = a == 0 ? x : a == 1 ? y : z;
                float const fix  = delta > 0 ? (float)cell - above - EPS : (float)(cell + 1) + below + EPS;
                if (!hit || (delta > 0 ? fix < best : fix > best)) best = fix;
                hit = true;
            }
    aabb_t const after = body_aabb(b);
    for (int i = 0; i < s->w->n_boxes; i++) {
        aabb_t const* o = &s->w->boxes[i];
        if (!aabb_overlap(&after, o) || aabb_overlap(&before, o)) continue;
        float const fix = delta > 0 ? axis_get(o->lo, a) - above - EPS : axis_get(o->hi, a) + below + EPS;
        if (!hit || (delta > 0 ? fix < best : fix > best)) best = fix;
        hit = true;
    }
    if (hit) *axis_of(&b->pos, a) = best;
    return hit;
}

// Ease the box sideways into a hole it is heading for, so a box that is
// nearly lined up goes through rather than catching on the rim.
static void funnel(body_t* b, portal_t const* pt, float dt) {
    vec3_t const l = portal_local(pt, body_center(b));
    if (v3_dot(b->vel, pt->n) > -0.5f || l.z < 0.0f || l.z > 2.5f) return;
    float const k = fminf(1.0f, 10.0f * dt);
    for (int i = 0; i < 2; i++) {
        vec3_t const ax = i == 0 ? pt->right : pt->up;
        if (ax.y != 0.0f) continue;  // gravity does the vertical
        float const off  = i == 0 ? l.x : l.y;
        float const half = i == 0 ? PORTAL_HALF_W : PORTAL_HALF_H;
        float const room = half - half_along(b, ax) - 0.01f;
        if (fabsf(off) <= room || fabsf(off) > half + half_along(b, ax)) continue;
        float const want = off > 0 ? room : -room;
        b->pos           = v3_mad(b->pos, ax, (want - off) * k);
    }
}

static vec3_t probe_point(body_t const* b) {
    return v3(b->pos.x, b->pos.y + b->probe, b->pos.z);
}

static void teleport(body_t* b, portal_t const* a, portal_t const* o) {
    vec3_t const c = portal_map_point(a, o, body_center(b));
    b->vel         = portal_map_dir(a, o, b->vel);
    b->pos         = v3(c.x, c.y - b->h * 0.5f, c.z);

    float const vn   = v3_dot(b->vel, o->n);
    float const vmin = o->n.y > 0.5f ? EXIT_MIN_UP : EXIT_MIN_OTHER;
    if (vn < vmin) b->vel = v3_mad(b->vel, o->n, vmin - vn);

    // Out of a ceiling the probe would start above the hole; drop it in.
    float const e = portal_local(o, probe_point(b)).z;
    if (e < 0.05f) b->pos = v3_mad(b->pos, o->n, 0.05f - e);
    b->on_ground = false;
}

int body_move(body_t* b, phys_world_t const* w, float dt, int* through, float* impact) {
    int ev = 0;
    if (impact) *impact = 0.0f;
    if (linked(w->portals)) {
        funnel(b, &w->portals[0], dt);
        funnel(b, &w->portals[1], dt);
    }

    bool const  was_ground = b->on_ground;
    float const fall_speed = -b->vel.y;
    b->on_ground           = false;
    int n                  = (int)ceilf(v3_len(b->vel) * dt / SUBSTEP);
    if (n < 1) n = 1;
    if (n > 32) n = 32;
    float const sdt = dt / (float)n;
    for (int s = 0; s < n; s++) {
        vec3_t const p0 = probe_point(b);
        step_t       st = {w, {false, false}};
        if (linked(w->portals))
            for (int i = 0; i < 2; i++) st.open[i] = fits(b, &w->portals[i]);
        if (move_axis(b, &st, 1, b->vel.y * sdt)) {
            if (b->vel.y < 0.0f) b->on_ground = true;
            b->vel.y = 0.0f;
        }
        if (move_axis(b, &st, 0, b->vel.x * sdt)) b->vel.x = 0.0f;
        if (move_axis(b, &st, 2, b->vel.z * sdt)) b->vel.z = 0.0f;

        if (linked(w->portals)) {
            vec3_t const p1 = probe_point(b);
            for (int i = 0; i < 2; i++) {
                vec3_t const a = portal_local(&w->portals[i], p0);
                vec3_t const c = portal_local(&w->portals[i], p1);
                if (a.z >= 0.0f && c.z < 0.0f && fabsf(c.x) < PORTAL_HALF_W && fabsf(c.y) < PORTAL_HALF_H) {
                    teleport(b, &w->portals[i], &w->portals[i ^ 1]);
                    if (through) *through = i;
                    ev |= PHYS_TELEPORT;
                    break;
                }
            }
        }
    }
    if (b->on_ground && !was_ground) {
        ev |= PHYS_LANDED;
        if (impact) *impact = fall_speed;
    }
    return ev;
}
