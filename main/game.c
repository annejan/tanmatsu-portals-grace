#include "game.h"
#include <math.h>
#include <string.h>

#define CUBE_FRICTION 8.0f   // per second, on the ground
#define CUBE_PULL     12.0f  // how hard a carried cube is pulled to the hold point
#define CUBE_MAX_PULL 12.0f  // m/s
#define DOOR_SPEED    2.5f   // of the way open, a second

static void cube_spawn(game_t* g, int i) {
    vec3_t const s     = g->lv.cubes[i];
    g->cubes[i].body = (body_t){s, v3(0, 0, 0), CUBE_HALF, 2.0f * CUBE_HALF, CUBE_HALF, false};
}

void game_load(game_t* g, int chamber) {
    memset(g, 0, sizeof(*g));
    level_load(&g->lv, chamber);
    player_spawn(&g->pl, &g->lv);
    g->chamber  = chamber;
    g->n_cubes  = g->lv.n_cubes;
    g->held     = -1;
    g->held_via = -1;
    for (int i = 0; i < g->n_cubes; i++) cube_spawn(g, i);
}

aabb_t cube_aabb(cube_t const* c) {
    return body_aabb(&c->body);
}

static aabb_t player_aabb(player_t const* p) {
    return (aabb_t){v3(p->pos.x - PL_HALF_W, p->pos.y, p->pos.z - PL_HALF_W),
                    v3(p->pos.x + PL_HALF_W, p->pos.y + PL_HEIGHT, p->pos.z + PL_HALF_W)};
}

static void drop(game_t* g) {
    if (g->held < 0) return;
    // It keeps the player's motion, carried through whatever portal lies
    // between them.
    vec3_t v = g->pl.vel;
    if (g->held_via >= 0) v = portal_map_dir(&g->portals[g->held_via], &g->portals[g->held_via ^ 1], v);
    g->cubes[g->held].body.vel = v;
    g->held                    = -1;
    g->held_via                = -1;
}

bool game_fire(game_t* g, int which) {
    portal_t p;
    if (!portal_place(&g->lv, player_eye(&g->pl), player_view(&g->pl).fwd, &g->portals[which ^ 1], &p)) return false;
    g->portals[which] = p;
    // A cube carried through the old portal has lost its way back.
    if (g->held_via >= 0) drop(g);
    return true;
}

// Where along the ray `o + t d` it enters `b`, or a negative number.
static float ray_aabb(vec3_t o, vec3_t d, aabb_t const* b) {
    float const ol[3] = {o.x, o.y, o.z}, dl[3] = {d.x, d.y, d.z};
    float const lo[3] = {b->lo.x, b->lo.y, b->lo.z}, hi[3] = {b->hi.x, b->hi.y, b->hi.z};
    float t0 = 0.0f, t1 = 1e30f;
    for (int a = 0; a < 3; a++) {
        if (fabsf(dl[a]) < 1e-9f) {
            if (ol[a] < lo[a] || ol[a] > hi[a]) return -1.0f;
            continue;
        }
        float ta = (lo[a] - ol[a]) / dl[a], tb = (hi[a] - ol[a]) / dl[a];
        if (ta > tb) {
            float const t = ta;
            ta            = tb;
            tb            = t;
        }
        t0 = fmaxf(t0, ta);
        t1 = fminf(t1, tb);
        if (t0 > t1) return -1.0f;
    }
    return t0;
}

int game_use(game_t* g) {
    if (g->held >= 0) {
        drop(g);
        return GAME_EV_DROP;
    }
    vec3_t const    eye  = player_eye(&g->pl);
    vec3_t const    fwd  = player_view(&g->pl).fwd;
    ray_hit_t const wall = level_raycast(&g->lv, eye, fwd, CUBE_REACH);
    float           best = wall.hit ? wall.dist : CUBE_REACH;
    int             pick = -1;
    for (int i = 0; i < g->n_cubes; i++) {
        aabb_t const b = cube_aabb(&g->cubes[i]);
        float const  t = ray_aabb(eye, fwd, &b);
        if (t >= 0.0f && t < best) {
            best = t;
            pick = i;
        }
    }
    if (pick < 0) return 0;
    g->held     = pick;
    g->held_via = -1;
    g->held_far = 0.0f;
    return GAME_EV_PICKUP;
}

// The solid boxes body `skip` (a cube index, or -1 for the player) meets:
// every free cube but itself, and the player, for a cube.
static int gather_boxes(game_t const* g, int skip, aabb_t* out) {
    int n = 0;
    for (int i = 0; i < g->n_cubes; i++)
        if (i != skip && i != g->held) out[n++] = cube_aabb(&g->cubes[i]);
    if (skip >= 0 && skip != g->held) out[n++] = player_aabb(&g->pl);
    return n;
}

static void step_cube(game_t* g, int i, float dt) {
    body_t* b = &g->cubes[i].body;
    aabb_t  boxes[LV_MAX_CUBES + 1];
    int     n = gather_boxes(g, i, boxes);
    phys_world_t const w = {&g->lv, g->portals, boxes, n};

    if (i == g->held) {
        // Pulled to a point in front of the eye -- on the far side of a
        // portal when it went through one ahead of the player.
        vec3_t target = v3_mad(player_eye(&g->pl), player_view(&g->pl).fwd, CUBE_HOLD);
        if (g->held_via >= 0) target = portal_map_point(&g->portals[g->held_via], &g->portals[g->held_via ^ 1], target);
        vec3_t pull = v3_scale(v3_sub(target, body_center(b)), CUBE_PULL);
        if (v3_len(pull) > CUBE_MAX_PULL) pull = v3_scale(v3_norm(pull), CUBE_MAX_PULL);
        b->vel = pull;
    } else {
        b->vel.y = fmaxf(b->vel.y - PHYS_GRAVITY * dt, -PHYS_MAX_FALL);
        if (b->on_ground) {
            float const k = expf(-CUBE_FRICTION * dt);
            b->vel.x *= k;
            b->vel.z *= k;
        }
    }

    int via = -1;
    if (body_move(b, &w, dt, &via, NULL) & PHYS_TELEPORT) {
        if (i == g->held) g->held_via = g->held_via < 0 ? via : -1;
    }

    if (i == g->held) {
        vec3_t target = v3_mad(player_eye(&g->pl), player_view(&g->pl).fwd, CUBE_HOLD);
        if (g->held_via >= 0) target = portal_map_point(&g->portals[g->held_via], &g->portals[g->held_via ^ 1], target);
        g->held_far = v3_len(v3_sub(target, body_center(b))) > CUBE_LETGO ? g->held_far + dt : 0.0f;
        if (g->held_far > CUBE_STUCK) drop(g);
    }

    // Lost in the goo or out of the world: a new one where it started.
    uint8_t const under = level_get(&g->lv, (int)floorf(b->pos.x), (int)floorf(b->pos.y - 0.05f), (int)floorf(b->pos.z));
    if (b->pos.y < -4.0f || (b->on_ground && under == MAT_GOO)) {
        if (i == g->held) drop(g);
        cube_spawn(g, i);
    }
}

// On top of a button: its base within the pad, resting on it.
static bool on_button(button_t const* bt, aabb_t const* a) {
    float const top = (float)bt->y + 1.0f;
    return a->lo.x < (float)bt->x + 0.9f && a->hi.x > (float)bt->x + 0.1f && a->lo.z < (float)bt->z + 0.9f &&
           a->hi.z > (float)bt->z + 0.1f && a->lo.y > top - 0.05f && a->lo.y < top + 0.3f;
}

static bool door_blocked(game_t const* g, door_t const* d) {
    aabb_t const cells = {v3((float)d->x0, (float)d->y0, (float)d->z0), v3((float)d->x1, (float)d->y1, (float)d->z1)};
    aabb_t const p     = player_aabb(&g->pl);
    if (aabb_overlap(&cells, &p)) return true;
    for (int i = 0; i < g->n_cubes; i++) {
        aabb_t const c = cube_aabb(&g->cubes[i]);
        if (aabb_overlap(&cells, &c)) return true;
    }
    return false;
}

int game_step(game_t* g, game_input_t const* in, float dt) {
    int ev = 0;
    if (dt <= 0.0f) return 0;
    if (dt > 0.1f) dt = 0.1f;

    g->pl.yaw += in->dyaw;
    g->pl.pitch = fmaxf(-PL_PITCH_MAX, fminf(PL_PITCH_MAX, g->pl.pitch + in->dpitch));
    for (int i = 0; i < 2; i++) {
        if (!in->fire[i]) continue;
        if (game_fire(g, i)) {
            ev |= GAME_EV_PORTAL | (i == 0 ? GAME_EV_SHOT_BLUE : GAME_EV_SHOT_ORANGE);
        } else {
            ev |= GAME_EV_SHOT_FAIL;
        }
    }
    if (in->use) ev |= game_use(g);

    // The player, among the cubes.
    aabb_t boxes[LV_MAX_CUBES + 1];
    int const          n   = gather_boxes(g, -1, boxes);
    phys_world_t const w   = {&g->lv, g->portals, boxes, n};
    player_input_t const pin = {.fwd = in->fwd, .strafe = in->strafe, .jump = in->jump};
    int                via = -1;
    ev |= player_update_in(&g->pl, &w, &pin, dt, &via);
    if ((ev & PL_EV_TELEPORT) && g->held >= 0) g->held_via = g->held_via < 0 ? (via ^ 1) : -1;

    for (int i = 0; i < g->n_cubes; i++) step_cube(g, i, dt);

    // Buttons: down under the player or a cube that is not being carried.
    aabb_t const pa = player_aabb(&g->pl);
    for (int b = 0; b < g->lv.n_buttons; b++) {
        button_t* bt   = &g->lv.buttons[b];
        bool      down = on_button(bt, &pa);
        for (int i = 0; i < g->n_cubes && !down; i++) {
            aabb_t const c = cube_aabb(&g->cubes[i]);
            down           = i != g->held && on_button(bt, &c);
        }
        if (down != bt->pressed) ev |= GAME_EV_BUTTON | (down ? GAME_EV_BUTTON_DOWN : GAME_EV_BUTTON_UP);
        bt->pressed = down;
    }

    // Doors: open while any of their buttons is down; they do not shut on
    // anything standing in them.
    for (int d = 0; d < g->lv.n_doors; d++) {
        door_t* dr   = &g->lv.doors[d];
        bool    want = false;
        for (int b = 0; b < g->lv.n_buttons; b++)
            if (g->lv.buttons[b].link == dr->link && g->lv.buttons[b].pressed) want = true;
        float const was = dr->open;
        if (want) {
            dr->open = fminf(1.0f, dr->open + DOOR_SPEED * dt);
        } else if (!door_blocked(g, dr)) {
            dr->open = fmaxf(0.0f, dr->open - DOOR_SPEED * dt);
        }
        // Started moving, from either end.
        if ((was == 0.0f || was == 1.0f) && dr->open != was) ev |= GAME_EV_DOOR;
    }
    return ev;
}
