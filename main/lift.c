#include "lift.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "player.h"

#define PI        3.14159265f
#define SIDE      (PI / 4.0f)  // each side's arc
#define R_MIN     0.46f        // the narrowest a station gets: a closet
#define R_MAX     0.9f
#define GLIDE_V   3.0f         // m/s, into the middle
#define DOOR_S    0.3f         // a door, shutting or opening
#define TURN_S    0.6f         // turning to look back out, at least
#define LOOK_DOWN (-0.1f)      // ... and a little up
#define DT_MAX    0.05f

static float smooth(float k) {
    k = k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k;
    return k * k * (3.0f - 2.0f * k);
}

static float wrap(float a) {  // to -PI .. PI
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}

// --- Where the stations go ------------------------------------------------

// Room for a station: air, or a door (open or shut, it is the lift's front).
static bool open_at(level_t const* lv, float x, float y, float z) {
    int const cx = (int)floorf(x), cy = (int)floorf(y), cz = (int)floorf(z);
    return !level_solid(lv, cx, cy, cz) || level_get(lv, cx, cy, cz) == MAT_DOOR;
}

// Whether an ellipse rx, rz round (x, z) on floor y is clear at the feet
// and at the head, its edge (a little out) and its inside, and stands on
// floor all round: not over a drop, goo or a platform's dock.
static bool fits(level_t const* lv, float x, float y, float z, float rx, float rz) {
    int const fy = (int)floorf(y - 0.05f);
    for (int i = 0; i < 16; i++) {
        float const a = (float)i * PI / 8.0f;
        int const   cx = (int)floorf(x + (rx + 0.03f) * sinf(a)), cz = (int)floorf(z + (rz + 0.03f) * cosf(a));
        if (!level_solid(lv, cx, fy, cz) || level_get(lv, cx, fy, cz) == MAT_GOO) return false;
    }
    float const hy[2] = {y + 0.05f, y + 1.7f};
    for (int h = 0; h < 2; h++) {
        if (!open_at(lv, x, hy[h], z)) return false;
        for (int i = 0; i < 16; i++) {
            float const a = (float)i * PI / 8.0f, sa = sinf(a), ca = cosf(a);
            if (!open_at(lv, x + (rx + 0.03f) * sa, hy[h], z + (rz + 0.03f) * ca)) return false;
            if (i % 2 == 0 && !open_at(lv, x + rx * 0.5f * sa, hy[h], z + rz * 0.5f * ca)) return false;
        }
    }
    return true;
}

// Its ceiling, hatch, shaft and the glass that stays.
static void finish(level_t const* lv, lift_site_t* s) {
    float const px[5] = {0.0f, 0.7f, -0.7f, 0.7f, -0.7f}, pz[5] = {0.0f, 0.7f, 0.7f, -0.7f, -0.7f};
    float       ceil  = 40.0f;
    for (int k = 0; k < 5; k++) {
        int const x = (int)floorf(s->x + px[k] * s->rx), z = (int)floorf(s->z + pz[k] * s->rz);
        int       c = (int)floorf(s->y + 0.01f);
        while ((float)c - s->y < 40.0f && !level_solid(lv, x, c, z)) c++;
        ceil = fminf(ceil, (float)c - s->y);
    }
    s->ceil  = ceil;
    s->cb    = fmaxf(1.85f, fminf(2.2f, ceil - 0.15f));
    s->hatch = ceil <= 8.0f;
    s->mouth = s->hatch ? ceil : 6.0f;
    s->rise  = fmaxf(2.5f, s->mouth - 0.1f);
    s->fixed = 0;
    for (int i = 0; i < LIFT_SIDES; i++) {
        bool wall = true;
        for (int k = 0; k < 3 && wall; k++) {
            float const a  = ((float)i + (k == 0 ? 0.08f : k == 1 ? 0.5f : 0.92f)) * SIDE;
            float const x  = s->x + (s->rx + 0.3f) * sinf(a), z = s->z + (s->rz + 0.3f) * cosf(a);
            wall          &= !open_at(lv, x, s->y + 0.05f, z) && !open_at(lv, x, s->y + 1.7f, z);  // a door is a way in
        }
        if (wall) s->fixed |= (uint8_t)(1u << i);
    }
}

bool lift_site_at(level_t const* lv, float x, float y, float z, lift_site_t* out) {
    static float const r[] = {0.72f, 0.65f, 0.6f, 0.55f, 0.5f, R_MIN};
    // The point itself, else moved up to 0.2 m off a wall -- the nearest first.
    static float const off[][2] = {{0, 0},       {0.1f, 0},     {-0.1f, 0},     {0, 0.1f},     {0, -0.1f},
                                   {0.1f, 0.1f}, {0.1f, -0.1f}, {-0.1f, 0.1f},  {-0.1f, -0.1f}, {0.2f, 0},
                                   {-0.2f, 0},   {0, 0.2f},     {0, -0.2f}};
    memset(out, 0, sizeof(*out));
    for (size_t i = 0; i < sizeof(r) / sizeof(r[0]); i++)
        for (size_t k = 0; k < sizeof(off) / sizeof(off[0]); k++) {
            float const o = sqrtf(off[k][0] * off[k][0] + off[k][1] * off[k][1]);
            if (r[i] - o < 0.42f) continue;  // the point well inside it
            if (!fits(lv, x + off[k][0], y, z + off[k][1], r[i], r[i])) continue;
            *out = (lift_site_t){.on = true, .x = x + off[k][0], .y = y, .z = z + off[k][1], .rx = r[i], .rz = r[i]};
            finish(lv, out);
            return true;
        }
    return false;
}

// The exit's cells with room above them, the biggest group of them on one
// layer, and in it the block a station fits best.
#define EXIT_CELLS 512

typedef struct {
    int16_t x, y, z;
} cell_t;

static bool in_group(cell_t const* c, int n, int x, int y, int z) {
    for (int i = 0; i < n; i++)
        if (c[i].x == x && c[i].y == y && c[i].z == z) return true;
    return false;
}

static bool exit_site(level_t const* lv, lift_site_t* out) {
    static cell_t e[EXIT_CELLS], g[EXIT_CELLS], best[EXIT_CELLS];
    static bool   used[EXIT_CELLS];
    int           ne = 0, nbest = 0;
    for (int y = 0; y < lv->h; y++)
        for (int z = 0; z < lv->d; z++)
            for (int x = 0; x < lv->w; x++)
                if (ne < EXIT_CELLS && level_get(lv, x, y, z) == MAT_EXIT && !level_solid(lv, x, y + 1, z))
                    e[ne++] = (cell_t){(int16_t)x, (int16_t)y, (int16_t)z};
    memset(used, 0, sizeof(used));
    for (int i = 0; i < ne; i++) {
        if (used[i]) continue;
        // A group: four-connected, on one layer.
        int n = 0;
        g[n++]  = e[i];
        used[i] = true;
        for (int k = 0; k < n; k++)
            for (int j = 0; j < ne; j++)
                if (!used[j] && e[j].y == g[k].y && abs(e[j].x - g[k].x) + abs(e[j].z - g[k].z) == 1) {
                    used[j] = true;
                    g[n++]  = e[j];
                }
        if (n > nbest) {
            memcpy(best, g, sizeof(cell_t) * (size_t)n);
            nbest = n;
        }
    }
    if (nbest == 0) return false;
    static int const   blocks[][2] = {{2, 2}, {2, 1}, {1, 2}, {1, 1}};
    static float const s[]         = {1.44f, 1.3f, 1.15f, 1.0f, 0.92f, 0.0f};  // 0: the narrowest
    lift_site_t        site        = {0};
    float              b_area = 0, b_far = 0;
    int                b_walls = 0;
    for (int b = 0; b < 4; b++) {
        int const w = blocks[b][0], d = blocks[b][1];
        for (int i = 0; i < nbest; i++) {
            int const x0 = best[i].x, y = best[i].y, z0 = best[i].z;
            bool      whole = true;
            for (int dx = 0; dx < w && whole; dx++)
                for (int dz = 0; dz < d && whole; dz++) whole = in_group(best, nbest, x0 + dx, y, z0 + dz);
            if (!whole) continue;
            float const cx = (float)x0 + (float)w * 0.5f, cz = (float)z0 + (float)d * 0.5f, fy = (float)(y + 1);
            float       rx = 0, rz = 0;
            for (int a = 0; a < 6; a++)
                for (int c = 0; c < 6; c++) {
                    float const tx = s[a] > 0.0f ? fminf(R_MAX, fminf((float)w * 0.5f + 0.1f, (float)w * 0.5f * s[a]))
                                                 : R_MIN;
                    float const tz = s[c] > 0.0f ? fminf(R_MAX, fminf((float)d * 0.5f + 0.1f, (float)d * 0.5f * s[c]))
                                                 : R_MIN;
                    if (tx * tz > rx * rz && fits(lv, cx, fy, cz, tx, tz)) {
                        rx = tx;
                        rz = tz;
                    }
                }
            if (rx <= 0.0f) continue;
            float far = 0;
            for (int k = 0; k < nbest; k++)
                far = fmaxf(far, hypotf((float)best[k].x + 0.5f - cx, (float)best[k].z + 0.5f - cz));
            int walls = 0;
            for (int dx = -1; dx <= w; dx++)
                for (int dz = -1; dz <= d; dz++)
                    if ((dx < 0 || dx >= w || dz < 0 || dz >= d) && level_solid(lv, x0 + dx, y + 1, z0 + dz)) walls++;
            float const area = (float)(w * d);
            bool const  better = !site.on || area > b_area ||
                                (area == b_area && (far < b_far - 1e-3f || (fabsf(far - b_far) <= 1e-3f && walls > b_walls)));
            if (!better) continue;
            site    = (lift_site_t){.on = true, .x = cx, .y = fy, .z = cz, .rx = rx, .rz = rz};
            b_area  = area;
            b_far   = far;
            b_walls = walls;
        }
        if (site.on) break;  // the biggest block that fits: no smaller one
    }
    if (!site.on) return false;
    finish(lv, &site);
    *out = site;
    return true;
}

void lift_sites(level_t const* lv, lift_sites_t* out) {
    memset(out, 0, sizeof(*out));
    exit_site(lv, &out->exit);
    lift_site_at(lv, lv->spawn.x, lv->spawn.y, lv->spawn.z, &out->start);
}

bool lift_exit_for(level_t const* lv, lift_sites_t const* s, vec3_t p, lift_site_t* out) {
    if (s->exit.on && hypotf(p.x - s->exit.x, p.z - s->exit.z) <= 3.0f && fabsf(p.y - s->exit.y) < 0.5f) {
        *out = s->exit;
        return true;
    }
    return lift_site_at(lv, p.x, p.y, p.z, out);
}

// --- The ride ---------------------------------------------------------------

static float ride_s(lift_site_t const* s) {
    return 1.2f + 0.15f * s->rise;
}

void lift_enter(lift_t* l, game_t* g, lift_site_t const* exit, bool stay) {
    memset(l, 0, sizeof(*l));
    l->phase    = LIFT_SHUT;
    l->car      = *exit;
    l->car_exit = true;
    l->stay     = stay;
    l->p0       = g->pl.pos;
    l->v0       = v3(g->pl.vel.x, 0.0f, g->pl.vel.z);
    float const v = v3_len(l->v0);
    if (v > 4.5f) l->v0 = v3_scale(l->v0, 4.5f / v);
    float const dx = l->p0.x - exit->x, dz = l->p0.z - exit->z, d = hypotf(dx, dz);
    l->tg          = fminf(1.2f, fmaxf(0.25f, d / GLIDE_V));
    l->td0         = fmaxf(0.0f, l->tg - 0.3f);
    // The side they came in by: where they are, else where they came from,
    // else behind them.
    if (d > 0.05f)
        l->phi = atan2f(dx, dz);
    else if (v > 0.1f)
        l->phi = atan2f(-l->v0.x, -l->v0.z);
    else
        l->phi = wrap(g->pl.yaw + PI);
    l->yaw0      = g->pl.yaw;
    l->dyaw      = wrap(l->phi - g->pl.yaw);
    l->pitch0    = g->pl.pitch;
    l->hide_cube = g->held;
    l->pending   = g->held >= 0 ? LIFT_EV_FIZZLE : 0;
    g->pl.vel    = v3(0, 0, 0);
}

void lift_down(lift_t* l, game_t* g, lift_site_t const* start) {
    memset(l, 0, sizeof(*l));
    if (!start->on) return;  // no room for one: there at once
    l->phase     = LIFT_DOWN;
    l->car       = *start;
    l->snap      = g->pl;
    l->dy        = start->rise;
    l->phi       = g->pl.yaw;  // its front: the way the player faces
    l->shut      = true;
    l->hide_cube = -1;
    for (int i = 0; i < LIFT_SIDES; i++) l->closed[i] = 1.0f;
    g->pl.pos.y += l->dy;
    g->pl.vel    = v3(0, 0, 0);
    l->pending   = LIFT_EV_RIDE;
}

int lift_step(lift_t* l, game_t* g, float dt, bool wait, float dyaw, float dpitch, float speed) {
    if (l->phase == LIFT_NONE) return 0;
    int ev     = l->pending;
    l->pending = 0;
    dt         = fminf(dt, DT_MAX) * speed;
    g->pl.vel  = v3(0, 0, 0);
    // Looking round, in the chamber being left: theirs from then on. Not
    // coming down -- the player lands as the chamber loaded them.
    if (l->phase == LIFT_SHUT || l->phase == LIFT_HOLD || l->phase == LIFT_UP) {
        if (dyaw != 0.0f || dpitch != 0.0f) l->own_look = true;
        if (l->own_look) {
            g->pl.yaw   += dyaw;
            g->pl.pitch  = fmaxf(-PL_PITCH_MAX, fminf(PL_PITCH_MAX, g->pl.pitch + dpitch));
        }
    }
    switch (l->phase) {
        case LIFT_SHUT: {
            float const before = l->t;
            l->t += dt;
            // In: from where the run ended, at its own pace, to the middle at rest.
            float const  s   = fminf(1.0f, l->t / l->tg), s2 = s * s, s3 = s2 * s;
            float const  h00 = 2 * s3 - 3 * s2 + 1, h10 = s3 - 2 * s2 + s, h01 = -2 * s3 + 3 * s2;
            vec3_t const c   = v3(l->car.x, l->car.y, l->car.z);
            g->pl.pos = v3_add(v3_add(v3_scale(l->p0, h00), v3_scale(l->v0, h10 * l->tg)), v3_scale(c, h01));
            float const turn = fmaxf(l->tg, TURN_S);
            if (!l->own_look) {
                float const k = smooth(l->t / turn);
                g->pl.yaw     = l->yaw0 + l->dyaw * k;
                g->pl.pitch   = l->pitch0 + (LOOK_DOWN - l->pitch0) * k;
            }
            // The doors: the far side first, the side they came in by last.
            if (before <= l->td0 && l->t > l->td0) ev |= LIFT_EV_DOOR;
            for (int i = 0; i < LIFT_SIDES; i++) {
                float const d0 = l->td0 + 0.25f * (1.0f - fabsf(wrap(((float)i + 0.5f) * SIDE - l->phi)) / PI);
                l->closed[i]   = fminf(1.0f, fmaxf(0.0f, (l->t - d0) / DOOR_S));
            }
            if (l->t >= fmaxf(l->td0 + 0.25f + DOOR_S, fmaxf(l->tg, turn))) {
                for (int i = 0; i < LIFT_SIDES; i++) l->closed[i] = 1.0f;
                l->shut  = true;
                l->phase = LIFT_HOLD;
                l->t     = 0.0f;
                ev |= LIFT_EV_SHUT;
            }
            break;
        }
        case LIFT_HOLD:
            l->t += dt;
            if (l->stay || wait) break;
            l->phase = LIFT_UP;
            l->t     = 0.0f;
            ev |= LIFT_EV_RIDE;
            break;
        case LIFT_UP: {
            l->t          += dt;
            l->dy          = l->car.rise * smooth(l->t / ride_s(&l->car));
            g->pl.pos      = v3(l->car.x, l->car.y + l->dy, l->car.z);
            if (l->t >= ride_s(&l->car)) ev |= LIFT_EV_TOP;  // and here it waits, for the next chamber
            break;
        }
        case LIFT_DOWN:
            l->t        += dt;
            l->dy        = l->car.rise * (1.0f - smooth(l->t / ride_s(&l->car)));
            g->pl        = l->snap;
            g->pl.pos.y += l->dy;
            if (l->t >= ride_s(&l->car)) {
                l->dy    = 0.0f;
                l->phase = LIFT_OPEN;
                l->t     = 0.0f;
                ev |= LIFT_EV_LAND | LIFT_EV_DOOR;
            }
            break;
        case LIFT_OPEN:
            l->t += dt;
            // The front first.
            for (int i = 0; i < LIFT_SIDES; i++) {
                float const d0 = 0.15f * fabsf(wrap(((float)i + 0.5f) * SIDE - l->phi)) / PI;
                l->closed[i]   = 1.0f - fminf(1.0f, fmaxf(0.0f, (l->t - d0) / DOOR_S));
            }
            g->pl = l->snap;
            if (l->t >= 0.15f + DOOR_S) {
                // There: the player exactly as the chamber loaded them.
                g->pl    = l->snap;
                l->phase = LIFT_NONE;
                ev |= LIFT_EV_LANDED;
            }
            break;
        case LIFT_NONE:
            break;
    }
    return ev;
}

bool lift_on(lift_t const* l) {
    return l->phase != LIFT_NONE;
}

float lift_lit(lift_t const* l) {
    if (l->phase != LIFT_UP && l->phase != LIFT_DOWN) return 1.0f;
    // Dark as the eye goes up into the shaft.
    float const eye = l->dy + PL_EYE;
    return 1.0f - smooth((eye - (l->car.mouth - 0.8f)) / 1.8f);
}

// --- What to draw -------------------------------------------------------------

static void still(lift_station_view_t* v, lift_site_t const* s, bool exit) {
    memset(v, 0, sizeof(*v));
    v->site = *s;
    v->exit = exit;
}

void lift_view(lift_t const* l, lift_sites_t const* s, lift_view_t* out) {
    memset(out, 0, sizeof(*out));
    out->hide_cube = -1;
    if (l == NULL || l->phase == LIFT_NONE) {
        if (s->exit.on) still(&out->st[out->n++], &s->exit, true);
        if (s->start.on) still(&out->st[out->n++], &s->start, false);
        return;
    }
    lift_station_view_t* v = &out->st[out->n++];
    still(v, &l->car, l->car_exit);
    v->dy    = l->dy;
    v->shaft = l->phase == LIFT_UP || l->phase == LIFT_DOWN;
    int shut = 0, doors = 0;
    for (int i = 0; i < LIFT_SIDES; i++) {
        v->closed[i] = l->closed[i];
        if (l->car.fixed & (1u << i)) continue;
        doors++;
        shut += l->closed[i] > 0.5f;
    }
    v->shut_light  = doors > 0 ? shut * 2 > doors : l->phase != LIFT_OPEN;
    out->hide_cube = l->hide_cube;
    // The chamber's other station, as it stands.
    lift_site_t const* other = l->car_exit ? &s->start : &s->exit;
    if (other->on) still(&out->st[out->n++], other, !l->car_exit);
}
