#include "game.h"
#include <math.h>
#include <string.h>

#define CUBE_FRICTION 8.0f   // per second, on the ground
#define CUBE_PULL     12.0f  // how hard a carried cube is pulled to the hold point
#define CUBE_MAX_PULL 12.0f  // m/s
#define DOOR_SPEED    2.5f   // of the way open, a second
#define PLAT_SPEED    1.5f   // m/s
#define PLAT_PAUSE    1.0f   // s at each end

static void trace_bridges(game_t* g);

static void cube_spawn(game_t* g, int i) {
    vec3_t const s   = g->lv.cubes[i];
    g->cubes[i].body = (body_t){s, v3(0, 0, 0), CUBE_HALF, 2.0f * CUBE_HALF, CUBE_HALF, false};
}

// A cube lost: a new one where it started -- out of its dropper, if it
// came from one.
static int cube_respawn(game_t* g, int i) {
    cube_spawn(g, i);
    return g->lv.cube_drop[i] ? GAME_EV_DROPPER : 0;
}

// Where a pedestal button can be pressed: its pad, and a little above.
static aabb_t pedestal_pad(button_t const* bt) {
    float const x = (float)bt->x, z = (float)bt->z, top = (float)bt->y + 1.0f;
    return (aabb_t){v3(x + 0.05f, top, z + 0.05f), v3(x + 0.95f, top + 0.25f, z + 0.95f)};
}

void game_load(game_t* g, int chamber) {
    static level_t lv;  // static: a level is too big for the stack
    level_load(&lv, chamber);
    game_load_level(g, &lv);
    g->chamber = chamber;
}

void game_load_level(game_t* g, level_t const* lv) {
    memset(g, 0, sizeof(*g));
    g->lv = *lv;
    player_spawn(&g->pl, &g->lv);
    g->chamber  = -1;
    g->n_cubes  = g->lv.n_cubes;
    g->held     = -1;
    g->held_via = -1;
    for (int i = 0; i < g->n_cubes; i++) cube_spawn(g, i);
    trace_bridges(g);
}

aabb_t cube_aabb(cube_t const* c) {
    return body_aabb(&c->body);
}

// How far down crusher k is at time t: up, a fast slam, a pause, a slower rise.
static float crush_depth(crusher_t const* c, float t, int k) {
    // Each lags the one before it (in the order the file lists them) by a
    // quarter cycle: a green wave for whoever times it right.
    float u = fmodf(t - (float)k * CRUSH_CYCLE * 0.25f + CRUSH_CYCLE * 4.0f, CRUSH_CYCLE);
    if (u < CRUSH_UP) return 0.0f;
    u -= CRUSH_UP;
    if (u < CRUSH_SLAM) return c->drop * u / CRUSH_SLAM;
    u -= CRUSH_SLAM;
    if (u < CRUSH_DOWN) return c->drop;
    u -= CRUSH_DOWN;
    return c->drop * (1.0f - u / CRUSH_RISE);
}

aabb_t crusher_aabb(game_t const* g, int k) {
    crusher_t const* c = &g->lv.crushers[k];
    vec3_t const     d = v3(0, crush_depth(c, g->crush_t, k), 0);
    return (aabb_t){v3_sub(c->lo, d), v3_sub(c->hi, d)};
}

aabb_t platform_aabb(game_t const* g) {
    platform_t const* p = &g->lv.platform;
    return (aabb_t){v3_add(p->lo, g->plat_at), v3_add(p->hi, g->plat_at)};
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

static float ray_aabb(vec3_t o, vec3_t d, aabb_t const* b);

bool game_fire(game_t* g, int which) {
    vec3_t const eye = player_eye(&g->pl), fwd = player_view(&g->pl).fwd;
    // The moving platform stops a shot, like a wall that takes no portal.
    if (g->lv.n_platforms) {
        aabb_t const    plat = platform_aabb(g);
        float const     tp   = ray_aabb(eye, fwd, &plat);
        ray_hit_t const wall = level_raycast(&g->lv, eye, fwd, 64.0f);
        if (tp >= 0.0f && (!wall.hit || tp < wall.dist)) return false;
    }
    portal_t p;
    if (!portal_place(&g->lv, eye, fwd, &g->portals[which ^ 1], &p)) return false;
    // A cube carried through the old portal has lost its way back: let go
    // of it while that portal still stands, so its motion goes through
    // the portal it really went through.
    if (g->held_via >= 0) drop(g);
    g->portals[which] = p;
    return true;
}

// Where along the ray `o + t d` it enters `b`, or a negative number.
static float ray_aabb(vec3_t o, vec3_t d, aabb_t const* b) {
    float const ol[3] = {o.x, o.y, o.z}, dl[3] = {d.x, d.y, d.z};
    float const lo[3] = {b->lo.x, b->lo.y, b->lo.z}, hi[3] = {b->hi.x, b->hi.y, b->hi.z};
    float       t0 = 0.0f, t1 = 1e30f;
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
    // Nor through the moving platform.
    if (g->lv.n_platforms) {
        aabb_t const plat = platform_aabb(g);
        float const  tp   = ray_aabb(eye, fwd, &plat);
        if (tp >= 0.0f && tp < best) best = tp;
    }
    for (int i = 0; i < g->n_cubes; i++) {
        aabb_t const b = cube_aabb(&g->cubes[i]);
        float const  t = ray_aabb(eye, fwd, &b);
        if (t >= 0.0f && t < best) {
            best = t;
            pick = i;
        }
    }
    // A pedestal button, if that is nearer: down for the chamber's time,
    // from the start again if it was down already.
    int press = -1;
    for (int b = 0; b < g->lv.n_buttons; b++) {
        if (!g->lv.buttons[b].pedestal) continue;
        aabb_t const pad = pedestal_pad(&g->lv.buttons[b]);
        float const  t   = ray_aabb(eye, fwd, &pad);
        if (t >= 0.0f && t < best) {
            best  = t;
            press = b;
        }
    }
    if (press >= 0) {
        g->lv.buttons[press].timer_left = g->lv.timer;
        return GAME_EV_PRESS;
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
    if (g->lv.n_platforms) out[n++] = platform_aabb(g);
    // A pedestal: its post and head, half a cell across. A laser relay: its
    // post, slimmer.
    for (int b = 0; b < g->lv.n_buttons; b++) {
        button_t const* bt = &g->lv.buttons[b];
        if (!bt->pedestal && !bt->relay) continue;
        float const x = (float)bt->x, y = (float)bt->y, z = (float)bt->z, r = bt->pedestal ? 0.25f : 0.2f;
        out[n++] = (aabb_t){v3(x + 0.5f - r, y, z + 0.5f - r), v3(x + 0.5f + r, y + 1.0f, z + 0.5f + r)};
    }
    for (int k = 0; k < g->lv.n_crushers; k++) out[n++] = crusher_aabb(g, k);
    // A light bridge: a slab 1 m wide and a few cm thick under its line.
    for (int k = 0; k < g->lv.n_bridges; k++)
        for (int i = 0; i < g->bridge_n[k]; i++) {
            beam_seg_t const* s    = &g->bridge[k][i];
            bool const        on_x = fabsf(s->b.x - s->a.x) > fabsf(s->b.z - s->a.z);
            float const       wx = on_x ? 0.0f : 0.5f, wz = on_x ? 0.5f : 0.0f;
            out[n++] = (aabb_t){v3(fminf(s->a.x, s->b.x) - wx, s->a.y - 0.06f, fminf(s->a.z, s->b.z) - wz),
                                v3(fmaxf(s->a.x, s->b.x) + wx, s->a.y, fmaxf(s->a.z, s->b.z) + wz)};
        }
    return n;
}

// The velocity that carries a body from `from` (its base) to land on
// `to`: an arc peaking JUMP_APEX above the higher of the two.
#define JUMP_APEX 2.5f

static vec3_t jump_velocity(vec3_t from, vec3_t to) {
    float const apex = fmaxf(from.y, to.y) + JUMP_APEX;
    float const vy   = sqrtf(2.0f * PHYS_GRAVITY * (apex - from.y));
    float const t    = vy / PHYS_GRAVITY + sqrtf(2.0f * (apex - to.y) / PHYS_GRAVITY);
    return v3((to.x - from.x) / t, vy, (to.z - from.z) / t);
}

// The faith plate a body standing at `pos` is on, or NULL.
static jump_t const* plate_under(level_t const* lv, vec3_t pos) {
    int const x = (int)floorf(pos.x), y = (int)floorf(pos.y - 0.05f), z = (int)floorf(pos.z);
    for (int i = 0; i < lv->n_jumps; i++)
        if (lv->jumps[i].x == x && lv->jumps[i].y == y && lv->jumps[i].z == z) return &lv->jumps[i];
    return NULL;
}

// Whether a box reaches into any fizzler cell.
// Whether a box reaches into any cell of material `m`.
static bool box_touches(level_t const* lv, aabb_t const* a, uint8_t m) {
    for (int y = (int)floorf(a->lo.y); y <= (int)floorf(a->hi.y - 0.001f); y++)
        for (int z = (int)floorf(a->lo.z); z <= (int)floorf(a->hi.z - 0.001f); z++)
            for (int x = (int)floorf(a->lo.x); x <= (int)floorf(a->hi.x - 0.001f); x++)
                if (level_get(lv, x, y, z) == m) return true;
    return false;
}

// Whether a box touched a cell of material `m` (a fizzler, a laser field)
// anywhere on its way from `from` to `to` this step. Testing only where it
// ended let anything faster than about 18 m/s at 10 fps jump clean over a
// sheet a cell thick. Not across a teleport: there the path is not a line.
static bool swept_touch(level_t const* lv, uint8_t m, aabb_t const* from, aabb_t const* to, bool teleported) {
    if (box_touches(lv, to, m)) return true;
    if (teleported) return false;
    vec3_t const d = v3_sub(to->lo, from->lo);
    int          n = (int)ceilf(v3_len(d) / 0.25f);
    if (n > 64) n = 64;
    for (int k = 1; k < n; k++) {
        vec3_t const off = v3_scale(d, (float)k / (float)n);
        aabb_t const a   = {v3_add(from->lo, off), v3_add(from->hi, off)};
        if (box_touches(lv, &a, m)) return true;
    }
    return false;
}

static bool swept_fizzler(level_t const* lv, aabb_t const* from, aabb_t const* to, bool teleported) {
    return swept_touch(lv, MAT_FIZZ, from, to, teleported);
}

// Whether a box overlaps anything solid in the grid.
static bool box_in_solid(level_t const* lv, aabb_t const* a) {
    for (int y = (int)floorf(a->lo.y + 0.001f); y <= (int)floorf(a->hi.y - 0.001f); y++)
        for (int z = (int)floorf(a->lo.z + 0.001f); z <= (int)floorf(a->hi.z - 0.001f); z++)
            for (int x = (int)floorf(a->lo.x + 0.001f); x <= (int)floorf(a->hi.x - 0.001f); x++)
                if (level_solid(lv, x, y, z)) return true;
    return false;
}

static int step_cube(game_t* g, int i, float dt) {
    int                ev = 0;
    body_t*            b  = &g->cubes[i].body;
    aabb_t             boxes[GAME_MAX_BOXES];
    int                n = gather_boxes(g, i, boxes);
    phys_world_t const w = {&g->lv, g->portals, boxes, n};

    if (i == g->held) {
        // A reflection cube turns with you, in eighths of a turn: a beam
        // can be aimed with the keys of a badge.
        g->cubes[i].yaw = roundf(g->pl.yaw / 0.78539816f) * 0.78539816f;
        // Pulled to a point in front of the eye -- on the far side of a
        // portal when it went through one ahead of the player.
        vec3_t target   = v3_mad(player_eye(&g->pl), player_view(&g->pl).fwd, CUBE_HOLD);
        if (g->held_via >= 0) target = portal_map_point(&g->portals[g->held_via], &g->portals[g->held_via ^ 1], target);
        vec3_t pull = v3_scale(v3_sub(target, body_center(b)), CUBE_PULL);
        if (v3_len(pull) > CUBE_MAX_PULL) pull = v3_scale(v3_norm(pull), CUBE_MAX_PULL);
        b->vel = pull;
    } else {
        b->vel.y = fall_half(b->vel.y, dt);
        if (b->on_ground) {
            float const k  = expf(-CUBE_FRICTION * dt);
            b->vel.x      *= k;
            b->vel.z      *= k;
        }
    }

    int          via    = -1;
    aabb_t const start  = body_aabb(b);
    float        impact = 0.0f;
    int const    pev    = body_move(b, &w, dt, &via, &impact);
    // A cube down on blue gel, fast enough, bounces too.
    if ((pev & PHYS_LANDED) && i != g->held && impact > 3.0f &&
        level_paint(&g->lv, (int)floorf(b->pos.x), (int)floorf(b->pos.y - 0.05f), (int)floorf(b->pos.z)) == GEL_BLUE) {
        b->vel.y     = impact * 0.9f;
        b->on_ground = false;
    }
    if (pev & PHYS_TELEPORT) {
        if (i == g->held) g->held_via = g->held_via < 0 ? via : -1;
    }
    if (i != g->held && !b->on_ground) b->vel.y = fall_half(b->vel.y, dt);

    if (i == g->held) {
        vec3_t target = v3_mad(player_eye(&g->pl), player_view(&g->pl).fwd, CUBE_HOLD);
        if (g->held_via >= 0) target = portal_map_point(&g->portals[g->held_via], &g->portals[g->held_via ^ 1], target);
        g->held_far = v3_len(v3_sub(target, body_center(b))) > CUBE_LETGO ? g->held_far + dt : 0.0f;
        if (g->held_far > CUBE_STUCK) drop(g);
    }

    // Lost in the goo, out of the world or through a fizzler: a new one
    // where it started.
    uint8_t const under =
        level_get(&g->lv, (int)floorf(b->pos.x), (int)floorf(b->pos.y - 0.05f), (int)floorf(b->pos.z));
    aabb_t const box  = body_aabb(b);
    bool const   fizz = swept_fizzler(&g->lv, &start, &box, pev & PHYS_TELEPORT);
    if (b->pos.y < -4.0f || (b->on_ground && under == MAT_GOO) || fizz) {
        if (i == g->held) drop(g);
        ev |= cube_respawn(g, i);
        if (fizz) ev |= GAME_EV_FIZZLE;
        return ev;
    }
    // A faith plate throws a cube that is not being carried.
    jump_t const* j = i != g->held && b->on_ground ? plate_under(&g->lv, b->pos) : NULL;
    if (j != NULL) {
        b->vel        = jump_velocity(b->pos, j->target);
        b->on_ground  = false;
        ev           |= GAME_EV_LAUNCH;
    }
    return ev;
}

// --- Lasers ---------------------------------------------------------------

// Whether `at`, on the wall a portal hangs on, is inside its oval.
static bool in_hole(portal_t const* p, vec3_t at) {
    vec3_t const l = portal_local(p, at);
    float const  u = l.x / PORTAL_HALF_W, v = l.y / PORTAL_HALF_H;
    return fabsf(l.z) < 0.02f && u * u + v * v <= 1.0f;
}

// Trace laser `k` from its emitter into g->beam: through glass, fizzlers
// and portals, turned by reflection cubes, stopped by anything else.
// Sets lit[b] for each catcher button whose block it ends on, and
// *player if it ends on the player.
static void trace_beam(game_t* g, int k, bool lit[LV_MAX_BUTTONS], bool* player) {
    emitter_t const* L    = &g->lv.lasers[k];
    vec3_t           d    = dir_vec(L->dir);
    vec3_t           o    = v3_mad(v3((float)L->x + 0.5f, (float)L->y + 0.5f, (float)L->z + 0.5f), d, 0.501f);
    float            left = 64.0f;
    int              skip = -1;  // the cube it just came out of
    g->beam_n[k]          = 0;
    for (int seg = 0; seg < BEAM_SEGS && left > 0.01f; seg++) {
        // The wall it reaches, looking past glass.
        ray_hit_t w    = {0};
        vec3_t    from = o;
        for (int i = 0; i < 32; i++) {
            float const gone = v3_dot(v3_sub(from, o), d);
            w                = level_raycast(&g->lv, from, d, left - gone);
            if (!w.hit) break;
            w.dist += gone;
            if (level_get(&g->lv, w.x, w.y, w.z) != MAT_GLASS) break;
            from  = v3_mad(o, d, w.dist + 1e-3f);
            w.hit = false;
        }
        float t    = w.hit ? w.dist : left;
        int   cube = -1;
        bool  pl = false, blocked = false;
        for (int i = 0; i < g->n_cubes; i++) {
            if (i == skip) continue;
            aabb_t const b  = cube_aabb(&g->cubes[i]);
            float const  tc = ray_aabb(o, d, &b);
            if (tc >= 0.0f && tc < t) t = tc, cube = i;
        }
        if (g->lv.n_platforms) {
            aabb_t const p  = platform_aabb(g);
            float const  tp = ray_aabb(o, d, &p);
            if (tp >= 0.0f && tp < t) t = tp, cube = -1, blocked = true;
        }
        aabb_t const pa = player_aabb(&g->pl);
        float const  tp = ray_aabb(o, d, &pa);
        if (tp >= 0.0f && tp < t) t = tp, cube = -1, blocked = false, pl = true;

        vec3_t const end            = v3_mad(o, d, t);
        g->beam[k][g->beam_n[k]++]  = (beam_seg_t){o, end};
        left                       -= t;
        if (pl) {
            *player = true;
            return;
        }
        if (cube >= 0) {
            if (!g->lv.cube_reflect[cube]) return;
            // Out of the reflection cube's middle, the way it faces.
            body_t const* b = &g->cubes[cube].body;
            o               = v3(b->pos.x, b->pos.y + CUBE_HALF, b->pos.z);
            d               = v3(sinf(g->cubes[cube].yaw), 0.0f, cosf(g->cubes[cube].yaw));
            skip            = cube;
            continue;
        }
        if (blocked || !w.hit) return;
        // A portal: on, out of the other one.
        bool through = false;
        for (int p = 0; p < 2 && !through; p++) {
            if (!g->portals[0].open || !g->portals[1].open || !in_hole(&g->portals[p], end)) continue;
            d       = portal_map_dir(&g->portals[p], &g->portals[p ^ 1], d);
            o       = v3_mad(portal_map_point(&g->portals[p], &g->portals[p ^ 1], end), d, 0.01f);
            skip    = -1;
            through = true;
        }
        if (through) continue;
        if (level_get(&g->lv, w.x, w.y, w.z) == MAT_CATCHER)
            for (int b = 0; b < g->lv.n_buttons; b++)
                if (g->lv.buttons[b].laser && g->lv.buttons[b].x == w.x && g->lv.buttons[b].y == w.y &&
                    g->lv.buttons[b].z == w.z)
                    lit[b] = true;
        return;
    }
}

// --- Light bridges ---------------------------------------------------------

// The bridge is traced this far above its surface: through a portal's
// oval, not along the very bottom of it.
#define BRIDGE_LIFT 0.25f

// Lay each light bridge out from its emitter: on through fizzlers and
// portal pairs, until a wall -- or a portal that would stand it on end.
static void trace_bridges(game_t* g) {
    for (int k = 0; k < g->lv.n_bridges; k++) {
        emitter_t const* E    = &g->lv.bridges[k];
        vec3_t           d    = dir_vec(E->dir);
        vec3_t           o    = v3_mad(v3((float)E->x + 0.5f, (float)E->y + BRIDGE_LIFT, (float)E->z + 0.5f), d, 0.5f);
        float            left = 64.0f;
        g->bridge_n[k]        = 0;
        for (int seg = 0; seg < BEAM_SEGS && left > 0.01f; seg++) {
            ray_hit_t const w   = level_raycast(&g->lv, o, d, left);
            float const     t   = w.hit ? w.dist : left;
            vec3_t const    end = v3_mad(o, d, t);
            g->bridge[k][g->bridge_n[k]++] =
                (beam_seg_t){v3(o.x, o.y - BRIDGE_LIFT, o.z), v3(end.x, end.y - BRIDGE_LIFT, end.z)};
            left -= t;
            if (!w.hit) break;
            bool through = false;
            for (int p = 0; p < 2 && !through; p++) {
                if (!g->portals[0].open || !g->portals[1].open || !in_hole(&g->portals[p], end)) continue;
                vec3_t const d2 = portal_map_dir(&g->portals[p], &g->portals[p ^ 1], d);
                if (fabsf(d2.y) > 0.5f) break;  // out of a floor or a ceiling: no bridge stands up
                d       = v3(roundf(d2.x), 0.0f, roundf(d2.z));
                o       = v3_mad(portal_map_point(&g->portals[p], &g->portals[p ^ 1], end), d, 0.01f);
                through = true;
            }
            if (!through) break;
        }
    }
}

// --- Gel --------------------------------------------------------------------

// A blob lands on (x, y, z) through `face`: it paints that cell and the
// ones round it in the face's plane, 3 x 3. True if anything changed.
static bool splat(level_t* lv, int x, int y, int z, int face, int gel) {
    int const a = face / 2, ua = (a + 1) % 3, va = (a + 2) % 3;
    bool      changed = false;
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            int c[3]  = {x, y, z};
            c[ua]    += i;
            c[va]    += j;
            changed   = level_set_paint(lv, c[0], c[1], c[2], gel) || changed;
        }
    return changed;
}

// Dispensers drip; blobs fall, through portals, and paint where they land.
static int step_gel(game_t* g, float dt) {
    int ev = 0;
    for (int k = 0; k < g->lv.n_gels; k++) {
        g->drip_t[k] -= dt;
        if (g->drip_t[k] > 0.0f) continue;
        g->drip_t[k] += GEL_DRIP;
        for (int i = 0; i < GEL_BLOBS; i++) {
            if (g->blobs[i].live) continue;
            gel_src_t const* s = &g->lv.gels[k];
            g->blobs[i] = (gel_blob_t){v3((float)s->x + 0.5f, (float)s->y - 0.05f, (float)s->z + 0.5f), v3(0, 0, 0),
                                       (uint8_t)s->gel, true};
            break;
        }
    }
    for (int i = 0; i < GEL_BLOBS; i++) {
        gel_blob_t* b = &g->blobs[i];
        if (!b->live) continue;
        b->vel.y  -= PHYS_GRAVITY * dt;
        float len  = v3_len(b->vel) * dt;
        if (len < 1e-6f) continue;
        vec3_t d = v3_scale(b->vel, dt / len);
        for (int hop = 0; hop < 3 && b->live; hop++) {
            ray_hit_t const w = level_raycast(&g->lv, b->pos, d, len);
            if (!w.hit) {
                b->pos = v3_mad(b->pos, d, len);
                break;
            }
            bool through = false;
            for (int p = 0; p < 2 && !through; p++) {
                if (!g->portals[0].open || !g->portals[1].open || !in_hole(&g->portals[p], w.point)) continue;
                d        = portal_map_dir(&g->portals[p], &g->portals[p ^ 1], d);
                b->vel   = portal_map_dir(&g->portals[p], &g->portals[p ^ 1], b->vel);
                b->pos   = v3_mad(portal_map_point(&g->portals[p], &g->portals[p ^ 1], w.point), d, 0.01f);
                len     -= w.dist;
                through  = true;
            }
            if (through) continue;
            if (splat(&g->lv, w.x, w.y, w.z, w.face, b->gel)) ev |= GAME_EV_PAINT;
            b->live = false;
        }
        if (b->pos.y < -4.0f) b->live = false;
    }
    return ev;
}

// --- Energy pellets -------------------------------------------------------

// The outward normal of box `b`'s face nearest point `p` on it.
static vec3_t box_normal(aabb_t const* b, vec3_t p) {
    float const d[6] = {fabsf(p.x - b->hi.x), fabsf(p.x - b->lo.x), fabsf(p.y - b->hi.y),
                        fabsf(p.y - b->lo.y), fabsf(p.z - b->hi.z), fabsf(p.z - b->lo.z)};
    int         best = 0;
    for (int i = 1; i < 6; i++)
        if (d[i] < d[best]) best = i;
    return dir_vec(best);
}

static vec3_t reflect(vec3_t v, vec3_t n) {
    return v3_sub(v, v3_scale(n, 2.0f * v3_dot(v, n)));
}

// Launchers fire; pellets fly straight, bounce off walls, glass, cubes and
// the platform, go through portals, kill the player, and are caught by a
// receiver -- which latches its button down and rests its launcher.
static int step_pellets(game_t* g, float dt) {
    int ev = 0;
    for (int k = 0; k < g->lv.n_launchers; k++) {
        pellet_t* p = &g->pellets[k];
        if (p->done) continue;
        if (!p->live) {
            p->wait -= dt;
            if (p->wait > 0.0f) continue;
            emitter_t const* L  = &g->lv.launchers[k];
            vec3_t const     d  = dir_vec(L->dir);
            p->pos              = v3_mad(v3((float)L->x + 0.5f, (float)L->y + 0.5f, (float)L->z + 0.5f), d, 0.6f);
            p->vel              = v3_scale(d, PELLET_SPEED);
            p->life             = PELLET_LIFE;
            p->live             = true;
            ev                 |= GAME_EV_PELLET;
            continue;
        }
        p->life -= dt;
        if (p->life <= 0.0f) {
            p->live = false;
            p->wait = PELLET_WAIT;
            continue;
        }
        float  left = PELLET_SPEED * dt;
        vec3_t d    = v3_scale(p->vel, 1.0f / PELLET_SPEED);
        for (int hop = 0; hop < 6 && left > 1e-5f && p->live; hop++) {
            ray_hit_t const w  = level_raycast(&g->lv, p->pos, d, left);
            float           t  = w.hit ? w.dist : left;
            // The player: dead. A cube or the platform: a bounce.
            aabb_t const    pa = player_aabb(&g->pl);
            float const     tp = ray_aabb(p->pos, d, &pa);
            if (tp >= 0.0f && tp < t) {
                ev      |= PL_EV_DIED;
                p->live  = false;
                p->wait  = PELLET_WAIT;
                break;
            }
            aabb_t box    = {0};
            bool   on_box = false;
            for (int i = 0; i < g->n_cubes; i++) {
                aabb_t const b  = cube_aabb(&g->cubes[i]);
                float const  tb = ray_aabb(p->pos, d, &b);
                if (tb > 1e-4f && tb < t) t = tb, box = b, on_box = true;
            }
            if (g->lv.n_platforms) {
                aabb_t const b  = platform_aabb(g);
                float const  tb = ray_aabb(p->pos, d, &b);
                if (tb > 1e-4f && tb < t) t = tb, box = b, on_box = true;
            }
            p->pos  = v3_mad(p->pos, d, t);
            left   -= t;
            if (on_box) {
                vec3_t const n  = box_normal(&box, p->pos);
                d               = reflect(d, n);
                p->pos          = v3_mad(p->pos, n, 0.002f);
                ev             |= GAME_EV_PELLET;
                continue;
            }
            if (!w.hit) break;
            bool through = false;
            for (int q = 0; q < 2 && !through; q++) {
                if (!g->portals[0].open || !g->portals[1].open || !in_hole(&g->portals[q], p->pos)) continue;
                d       = portal_map_dir(&g->portals[q], &g->portals[q ^ 1], d);
                p->pos  = v3_mad(portal_map_point(&g->portals[q], &g->portals[q ^ 1], p->pos), d, 0.01f);
                through = true;
            }
            if (through) continue;
            if (level_get(&g->lv, w.x, w.y, w.z) == MAT_RECEIVER) {
                for (int b = 0; b < g->lv.n_buttons; b++) {
                    button_t* bt = &g->lv.buttons[b];
                    if (bt->receiver && bt->x == w.x && bt->y == w.y && bt->z == w.z && !bt->pressed) {
                        bt->pressed  = true;
                        ev          |= GAME_EV_BUTTON | GAME_EV_BUTTON_DOWN;
                    }
                }
                p->live  = false;
                p->done  = true;
                ev      |= GAME_EV_CAUGHT;
                break;
            }
            vec3_t const n  = dir_vec(w.face);
            d               = reflect(d, n);
            p->pos          = v3_mad(p->pos, n, 0.002f);
            ev             |= GAME_EV_PELLET;
        }
        p->vel = v3_scale(d, PELLET_SPEED);
    }
    return ev;
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

// Where along its trip the platform is at time t: there, a pause, back, a pause.
static float trip(float t, float move) {
    float const cycle = 2.0f * (move + PLAT_PAUSE);
    float       u     = fmodf(t, cycle);
    if (u < PLAT_PAUSE) return 0.0f;
    u -= PLAT_PAUSE;
    if (u < move) return u / move;
    u -= move;
    if (u < PLAT_PAUSE) return 1.0f;
    u -= PLAT_PAUSE;
    return 1.0f - u / move;
}

// Standing on top of box `p`: resting on it, and over it.
static bool riding(aabb_t const* p, aabb_t const* b) {
    return fabsf(b->lo.y - p->hi.y) < 0.06f && b->lo.x < p->hi.x && b->hi.x > p->lo.x && b->lo.z < p->hi.z &&
           b->hi.z > p->lo.z;
}

// Glide the platform on, carrying what stands on it. It waits rather
// than move into anything that is not riding it.
static void move_platform(game_t* g, float dt) {
    if (!g->lv.n_platforms) return;
    // Driven by a button (a catcher, a relay, anything): it moves while all
    // of that button's group are down, and stands where it is otherwise.
    if (g->lv.platform_link >= 0)
        for (int b = 0; b < g->lv.n_buttons; b++)
            if (g->lv.buttons[b].link == g->lv.platform_link && !g->lv.buttons[b].pressed) return;
    platform_t const* p   = &g->lv.platform;
    float const       len = v3_len(p->travel);
    if (len < 0.01f) return;
    float const  t1   = g->plat_t + dt;
    vec3_t const at1  = v3_scale(p->travel, trip(t1, len / PLAT_SPEED));
    vec3_t const step = v3_sub(at1, g->plat_at);
    aabb_t const now  = platform_aabb(g);
    aabb_t const next = {v3_add(now.lo, step), v3_add(now.hi, step)};

    aabb_t const pa      = player_aabb(&g->pl);
    bool const   pl_ride = riding(&now, &pa);
    bool         cube_ride[LV_MAX_CUBES];
    for (int i = 0; i < g->n_cubes; i++) {
        aabb_t const c = cube_aabb(&g->cubes[i]);
        cube_ride[i]   = i != g->held && riding(&now, &c);
        if (!cube_ride[i] && i != g->held && aabb_overlap(&next, &c)) return;  // in the way: wait
    }
    if (!pl_ride && aabb_overlap(&next, &pa)) return;

    // Nor carry a rider into a wall or a ceiling: wait instead.
    aabb_t const pa1 = {v3_add(pa.lo, step), v3_add(pa.hi, step)};
    if (pl_ride && box_in_solid(&g->lv, &pa1)) return;
    for (int i = 0; i < g->n_cubes; i++) {
        if (!cube_ride[i]) continue;
        aabb_t const c  = cube_aabb(&g->cubes[i]);
        aabb_t const c1 = {v3_add(c.lo, step), v3_add(c.hi, step)};
        if (box_in_solid(&g->lv, &c1)) return;
    }

    g->plat_t  = t1;
    g->plat_at = at1;
    if (pl_ride) g->pl.pos = v3_add(g->pl.pos, step);
    for (int i = 0; i < g->n_cubes; i++)
        if (cube_ride[i]) g->cubes[i].body.pos = v3_add(g->cubes[i].body.pos, step);
}

int game_step(game_t* g, game_input_t const* in, float dt) {
    int ev = 0;
    if (dt <= 0.0f) return 0;
    if (dt > 0.1f) dt = 0.1f;

    g->pl.yaw   += in->dyaw;
    g->pl.pitch  = fmaxf(-PL_PITCH_MAX, fminf(PL_PITCH_MAX, g->pl.pitch + in->dpitch));
    for (int i = 0; i < 2; i++) {
        if (!in->fire[i]) continue;
        if (game_fire(g, i)) {
            ev |= GAME_EV_PORTAL | (i == 0 ? GAME_EV_SHOT_BLUE : GAME_EV_SHOT_ORANGE);
        } else {
            ev |= GAME_EV_SHOT_FAIL;
        }
    }
    if (in->use) ev |= game_use(g);

    move_platform(g, dt);
    // Crushers: down on whoever is under them.
    if (g->lv.n_crushers) {
        float const t0 = g->crush_t;
        g->crush_t     = fmodf(g->crush_t + dt, CRUSH_CYCLE * 4.0f);
        for (int k = 0; k < g->lv.n_crushers; k++) {
            crusher_t const* c  = &g->lv.crushers[k];
            float const      d0 = crush_depth(c, t0, k), d1 = crush_depth(c, g->crush_t, k);
            if (d1 <= d0) continue;  // only coming down crushes
            aabb_t const box = crusher_aabb(g, k), pa = player_aabb(&g->pl);
            if (aabb_overlap(&box, &pa)) ev |= PL_EV_DIED;
            for (int i = 0; i < g->n_cubes; i++) {
                aabb_t const cb = cube_aabb(&g->cubes[i]);
                if (!aabb_overlap(&box, &cb)) continue;
                if (i == g->held) drop(g);
                ev |= cube_respawn(g, i) | GAME_EV_FIZZLE;
            }
            if (d1 >= c->drop && d0 < c->drop) ev |= GAME_EV_CRUSH;
        }
    }
    trace_bridges(g);  // the portals may have moved
    ev |= step_gel(g, dt);
    ev |= step_pellets(g, dt);

    // The player, among the cubes.
    aabb_t               boxes[GAME_MAX_BOXES];
    int const            n      = gather_boxes(g, -1, boxes);
    phys_world_t const   w      = {&g->lv, g->portals, boxes, n};
    player_input_t const pin    = {.fwd = in->fwd, .strafe = in->strafe, .jump = in->jump};
    int                  via    = -1;
    aabb_t const         start  = player_aabb(&g->pl);
    ev                         |= player_update_in(&g->pl, &w, &pin, dt, &via);
    if ((ev & PL_EV_TELEPORT) && g->held >= 0) g->held_via = g->held_via < 0 ? (via ^ 1) : -1;

    // A fizzler: the portals close, and a cube carried in goes.
    aabb_t const pbox = player_aabb(&g->pl);
    if (swept_fizzler(&g->lv, &start, &pbox, ev & PL_EV_TELEPORT)) {
        if (g->portals[0].open || g->portals[1].open) {
            g->portals[0].open = g->portals[1].open  = false;
            ev                                      |= GAME_EV_PORTAL | GAME_EV_FIZZLE;
        }
        if (g->held >= 0) {
            int const c = g->held;
            drop(g);
            ev |= cube_respawn(g, c) | GAME_EV_FIZZLE;
        }
    }
    // A laser field: deadly to touch.
    if (swept_touch(&g->lv, MAT_FIELD, &start, &pbox, ev & PL_EV_TELEPORT)) ev |= PL_EV_DIED;
    // A faith plate.
    jump_t const* j = g->pl.on_ground ? plate_under(&g->lv, g->pl.pos) : NULL;
    if (j != NULL) {
        g->pl.vel        = jump_velocity(g->pl.pos, j->target);
        g->pl.on_ground  = false;
        ev              |= GAME_EV_LAUNCH;
    }

    for (int i = 0; i < g->n_cubes; i++) ev |= step_cube(g, i, dt);

    // Lasers: what they light, and whom they burn.
    bool lit[LV_MAX_BUTTONS] = {false}, burnt = false;
    for (int k = 0; k < g->lv.n_lasers; k++) trace_beam(g, k, lit, &burnt);
    // A relay is lit by any piece of beam through its post.
    for (int b = 0; b < g->lv.n_buttons; b++) {
        button_t const* bt = &g->lv.buttons[b];
        if (!bt->relay) continue;
        float const  x = (float)bt->x, y = (float)bt->y, z = (float)bt->z;
        aabb_t const post = {v3(x + 0.3f, y, z + 0.3f), v3(x + 0.7f, y + 1.0f, z + 0.7f)};
        for (int k = 0; k < g->lv.n_lasers && !lit[b]; k++)
            for (int i = 0; i < g->beam_n[k] && !lit[b]; i++) {
                vec3_t const d   = v3_sub(g->beam[k][i].b, g->beam[k][i].a);
                float const  len = v3_len(d);
                if (len < 1e-4f) continue;
                float const t = ray_aabb(g->beam[k][i].a, v3_scale(d, 1.0f / len), &post);
                lit[b]        = t >= 0.0f && t <= len;
            }
    }
    if (burnt) {
        if (g->burn_t == 0.0f) ev |= GAME_EV_BURN;
        g->burn_t += dt;
        if (g->burn_t > BEAM_BURN) ev |= PL_EV_DIED;
    } else {
        g->burn_t = 0.0f;
    }

    // Buttons: down under the player or a cube that is not being carried;
    // a pedestal button while its time runs, ticking each second.
    aabb_t const pa = player_aabb(&g->pl);
    for (int b = 0; b < g->lv.n_buttons; b++) {
        button_t* bt   = &g->lv.buttons[b];
        bool      down = false;
        if (bt->relay) {
            down = lit[b];
        } else if (bt->receiver) {
            down = bt->pressed;  // latched by step_pellets()
        } else if (bt->laser) {
            down = lit[b];
        } else if (bt->pedestal) {
            float const before = bt->timer_left;
            bt->timer_left     = fmaxf(0.0f, bt->timer_left - dt);
            down               = bt->timer_left > 0.0f;
            if (down && ceilf(bt->timer_left) != ceilf(before)) ev |= GAME_EV_TICK;
        } else {
            down = !bt->cube_only && on_button(bt, &pa);
            for (int i = 0; i < g->n_cubes && !down; i++) {
                aabb_t const c = cube_aabb(&g->cubes[i]);
                down           = i != g->held && on_button(bt, &c);
            }
        }
        if (down != bt->pressed) ev |= GAME_EV_BUTTON | (down ? GAME_EV_BUTTON_DOWN : GAME_EV_BUTTON_UP);
        bt->pressed = down;
    }

    // Doors: open while all of their buttons are down; they do not shut on
    // anything standing in them.
    for (int d = 0; d < g->lv.n_doors; d++) {
        // Open while ALL its buttons are down; a door with none stays shut.
        door_t* dr   = &g->lv.doors[d];
        int     n    = 0;
        bool    want = true;
        for (int b = 0; b < g->lv.n_buttons; b++) {
            if (g->lv.buttons[b].link != dr->link) continue;
            n++;
            if (!g->lv.buttons[b].pressed) want = false;
        }
        want            = want && n > 0;
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
