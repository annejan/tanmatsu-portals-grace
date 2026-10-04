#include "player.h"
#include <math.h>

#define GRAVITY      15.0f
#define JUMP_SPEED   5.0f
#define WALK_SPEED   4.5f
#define GROUND_ACCEL 40.0f
#define AIR_ACCEL    8.0f
#define MAX_FALL     30.0f
#define SUBSTEP      0.2f   // longest move between collision checks, metres
#define FIT_TOL      0.02f  // slack when asking whether the box fits a hole
#define EPS          0.001f

// Leaving a portal in a floor, slower than this would drop you straight
// back in; leaving any other, you at least clear its plane.
#define EXIT_MIN_UP    6.5f
#define EXIT_MIN_OTHER 1.0f

void player_spawn(player_t* p, level_t const* lv) {
    *p     = (player_t){0};
    p->pos = lv->spawn;
    p->yaw = lv->spawn_yaw;
}

vec3_t player_eye(player_t const* p) {
    return v3(p->pos.x, p->pos.y + PL_EYE, p->pos.z);
}

basis_t player_view(player_t const* p) {
    return basis_from_angles(p->yaw, p->pitch, 0.0f);
}

static vec3_t box_center(player_t const* p) {
    return v3(p->pos.x, p->pos.y + PL_HEIGHT * 0.5f, p->pos.z);
}

// The box's half extent along a unit axis-aligned direction.
static float half_along(vec3_t a) {
    return fabsf(a.x) * PL_HALF_W + fabsf(a.y) * (PL_HEIGHT * 0.5f) + fabsf(a.z) * PL_HALF_W;
}

static bool linked(portal_t const portals[2]) {
    return portals[0].open && portals[1].open;
}

// Behind an open portal is a tunnel two cells deep the player may stand
// in, as long as the box fits through the hole.
static bool in_tunnel(player_t const* p, portal_t const* pt, int x, int y, int z) {
    vec3_t const c = portal_local(pt, v3((float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f));
    if (fabsf(c.x) > PORTAL_HALF_W || fabsf(c.y) > PORTAL_HALF_H || c.z > 0.0f || c.z < -2.5f) return false;
    vec3_t const b = portal_local(pt, box_center(p));
    return fabsf(b.x) + half_along(pt->right) <= PORTAL_HALF_W + FIT_TOL &&
           fabsf(b.y) + half_along(pt->up) <= PORTAL_HALF_H + FIT_TOL && b.z < 2.0f;
}

static bool blocks(player_t const* p, level_t const* lv, portal_t const portals[2], int x, int y, int z) {
    if (!level_solid(lv, x, y, z)) return false;
    if (linked(portals) && (in_tunnel(p, &portals[0], x, y, z) || in_tunnel(p, &portals[1], x, y, z))) return false;
    return true;
}

static float* axis_of(vec3_t* v, int a) {
    return a == 0 ? &v->x : a == 1 ? &v->y : &v->z;
}

// Move along one axis and push back out of whatever it ran into.
// True if it hit something.
static bool move_axis(player_t* p, level_t const* lv, portal_t const portals[2], int a, float delta) {
    if (delta == 0.0f) return false;
    *axis_of(&p->pos, a) += delta;

    float const lo[3] = {p->pos.x - PL_HALF_W, p->pos.y, p->pos.z - PL_HALF_W};
    float const hi[3] = {p->pos.x + PL_HALF_W, p->pos.y + PL_HEIGHT, p->pos.z + PL_HALF_W};
    int         c0[3], c1[3];
    for (int i = 0; i < 3; i++) {
        c0[i] = (int)floorf(lo[i] + EPS);
        c1[i] = (int)floorf(hi[i] - EPS);
    }
    float const below = (a == 1) ? 0.0f : PL_HALF_W;           // pos to the box's low side
    float const above = (a == 1) ? PL_HEIGHT : PL_HALF_W;      // pos to the box's high side
    bool        hit   = false;
    float       best  = *axis_of(&p->pos, a);
    for (int y = c0[1]; y <= c1[1]; y++)
        for (int z = c0[2]; z <= c1[2]; z++)
            for (int x = c0[0]; x <= c1[0]; x++) {
                if (!blocks(p, lv, portals, x, y, z)) continue;
                int const   cell = a == 0 ? x : a == 1 ? y : z;
                float const fix  = delta > 0 ? (float)cell - above - EPS : (float)(cell + 1) + below + EPS;
                if (!hit || (delta > 0 ? fix < best : fix > best)) best = fix;
                hit = true;
            }
    if (hit) *axis_of(&p->pos, a) = best;
    return hit;
}

// Ease the box sideways into a hole it is heading for, so a player who
// is nearly lined up goes through rather than catching on the rim.
static void funnel(player_t* p, portal_t const* pt, float dt) {
    vec3_t const l = portal_local(pt, box_center(p));
    if (v3_dot(p->vel, pt->n) > -0.5f || l.z < 0.0f || l.z > 2.5f) return;
    float const k = fminf(1.0f, 10.0f * dt);
    for (int i = 0; i < 2; i++) {
        vec3_t const ax   = i == 0 ? pt->right : pt->up;
        if (ax.y != 0.0f) continue;  // gravity does the vertical
        float const  off  = i == 0 ? l.x : l.y;
        float const  half = i == 0 ? PORTAL_HALF_W : PORTAL_HALF_H;
        float const  room = half - half_along(ax) - 0.01f;
        if (fabsf(off) <= room || fabsf(off) > half + half_along(ax)) continue;
        float const want = off > 0 ? room : -room;
        p->pos           = v3_mad(p->pos, ax, (want - off) * k);
    }
}

static void teleport(player_t* p, portal_t const* a, portal_t const* b) {
    vec3_t const  c    = portal_map_point(a, b, box_center(p));
    basis_t const view = player_view(p);
    basis_t const m    = portal_map_basis(a, b, &view);
    float         yaw, pitch, roll;
    basis_to_angles(&m, &yaw, &pitch, &roll);
    // Upright again at once; the roll is dropped.
    p->yaw   = yaw;
    p->pitch = fmaxf(-PL_PITCH_MAX, fminf(PL_PITCH_MAX, pitch));
    p->vel   = portal_map_dir(a, b, p->vel);
    p->pos   = v3(c.x, c.y - PL_HEIGHT * 0.5f, c.z);

    float const vn   = v3_dot(p->vel, b->n);
    float const vmin = b->n.y > 0.5f ? EXIT_MIN_UP : EXIT_MIN_OTHER;
    if (vn < vmin) p->vel = v3_mad(p->vel, b->n, vmin - vn);

    // Out of a ceiling the eye would start above the hole; drop it in.
    float const e = portal_local(b, player_eye(p)).z;
    if (e < 0.05f) p->pos = v3_mad(p->pos, b->n, 0.05f - e);
    p->on_ground = false;
}

int player_update(player_t* p, level_t const* lv, portal_t const portals[2], player_input_t const* in, float dt) {
    int ev = 0;
    if (dt <= 0.0f) return 0;
    if (dt > 0.1f) dt = 0.1f;

    // Wanting to move.
    float const sy = sinf(p->yaw), cy = cosf(p->yaw);
    vec3_t      wish = v3(in->fwd * sy + in->strafe * cy, 0.0f, in->fwd * cy - in->strafe * sy);
    if (v3_len(wish) > 1.0f) wish = v3_norm(wish);

    if (p->on_ground) {
        vec3_t const target = v3_scale(wish, WALK_SPEED);
        vec3_t       dv     = v3(target.x - p->vel.x, 0.0f, target.z - p->vel.z);
        float const  l      = v3_len(dv);
        float const  step   = GROUND_ACCEL * dt;
        if (l > step) dv = v3_scale(dv, step / l);
        p->vel.x += dv.x;
        p->vel.z += dv.z;
        if (in->jump) {
            p->vel.y     = JUMP_SPEED;
            p->on_ground = false;
        }
    } else {
        // A little steering in the air, but never enough to add speed
        // past walking pace -- a fling keeps all it had.
        float const before = sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z);
        p->vel.x += wish.x * AIR_ACCEL * dt;
        p->vel.z += wish.z * AIR_ACCEL * dt;
        float const after = sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z);
        float const cap   = fmaxf(before, WALK_SPEED);
        if (after > cap) {
            p->vel.x *= cap / after;
            p->vel.z *= cap / after;
        }
    }
    p->vel.y = fmaxf(p->vel.y - GRAVITY * dt, -MAX_FALL);

    if (linked(portals)) {
        funnel(p, &portals[0], dt);
        funnel(p, &portals[1], dt);
    }

    bool const was_ground = p->on_ground;
    float const fall_speed = -p->vel.y;
    p->on_ground          = false;
    int n                 = (int)ceilf(v3_len(p->vel) * dt / SUBSTEP);
    if (n < 1) n = 1;
    if (n > 32) n = 32;
    float const sdt = dt / (float)n;
    for (int s = 0; s < n; s++) {
        vec3_t const eye0 = player_eye(p);
        if (move_axis(p, lv, portals, 1, p->vel.y * sdt)) {
            if (p->vel.y < 0.0f) p->on_ground = true;
            p->vel.y = 0.0f;
        }
        if (move_axis(p, lv, portals, 0, p->vel.x * sdt)) p->vel.x = 0.0f;
        if (move_axis(p, lv, portals, 2, p->vel.z * sdt)) p->vel.z = 0.0f;

        if (linked(portals)) {
            vec3_t const eye1 = player_eye(p);
            for (int i = 0; i < 2; i++) {
                vec3_t const a = portal_local(&portals[i], eye0);
                vec3_t const b = portal_local(&portals[i], eye1);
                if (a.z >= 0.0f && b.z < 0.0f && fabsf(b.x) < PORTAL_HALF_W && fabsf(b.y) < PORTAL_HALF_H) {
                    teleport(p, &portals[i], &portals[i ^ 1]);
                    ev |= PL_EV_TELEPORT;
                    break;
                }
            }
        }
    }

    if (p->on_ground && !was_ground && fall_speed > 3.0f) ev |= PL_EV_LANDED;
    if (p->on_ground) {
        uint8_t const under = level_get(lv, (int)floorf(p->pos.x), (int)floorf(p->pos.y - 0.05f), (int)floorf(p->pos.z));
        if (under == MAT_GOO) ev |= PL_EV_DIED;
        if (under == MAT_EXIT) ev |= PL_EV_EXIT;
    }
    if (p->pos.y < -4.0f) ev |= PL_EV_DIED;
    return ev;
}
