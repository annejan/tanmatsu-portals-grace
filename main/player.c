#include "player.h"
#include <math.h>
#include <stddef.h>

#define JUMP_SPEED   5.0f
#define WALK_SPEED   4.5f
#define GROUND_ACCEL 40.0f
#define AIR_ACCEL    8.0f

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

// The player as a physics body, and back.
static body_t as_body(player_t const* p) {
    return (body_t){p->pos, p->vel, PL_HALF_W, PL_HEIGHT, PL_EYE, p->on_ground};
}

int player_update(player_t* p, level_t const* lv, portal_t const portals[2], player_input_t const* in, float dt) {
    phys_world_t const w = {lv, portals, NULL, 0};
    return player_update_in(p, &w, in, dt, NULL);
}

int player_update_in(player_t* p, phys_world_t const* w, player_input_t const* in, float dt, int* through) {
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
    p->vel.y = fmaxf(p->vel.y - PHYS_GRAVITY * dt, -PHYS_MAX_FALL);

    body_t b   = as_body(p);
    int    via = -1;
    float  impact;
    int const pev = body_move(&b, w, dt, &via, &impact);
    p->pos       = b.pos;
    p->vel       = b.vel;
    p->on_ground = b.on_ground;
    if (pev & PHYS_TELEPORT) {
        // The view goes through too, upright again at once: the roll is dropped.
        basis_t const view = player_view(p);
        basis_t const m    = portal_map_basis(&w->portals[via], &w->portals[via ^ 1], &view);
        float         yaw, pitch, roll;
        basis_to_angles(&m, &yaw, &pitch, &roll);
        p->yaw   = yaw;
        p->pitch = fmaxf(-PL_PITCH_MAX, fminf(PL_PITCH_MAX, pitch));
        ev |= PL_EV_TELEPORT;
        if (through) *through = via;
    }
    if ((pev & PHYS_LANDED) && impact > 3.0f) ev |= PL_EV_LANDED;
    if (p->on_ground) {
        uint8_t const under = level_get(w->lv, (int)floorf(p->pos.x), (int)floorf(p->pos.y - 0.05f), (int)floorf(p->pos.z));
        if (under == MAT_GOO) ev |= PL_EV_DIED;
        if (under == MAT_EXIT) ev |= PL_EV_EXIT;
    }
    if (p->pos.y < -4.0f) ev |= PL_EV_DIED;
    return ev;
}
