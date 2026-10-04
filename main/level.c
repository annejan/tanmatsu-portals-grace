#include "level.h"
#include <string.h>

// --- Building -----------------------------------------------------------

static int idx(level_t const* lv, int x, int y, int z) {
    return (y * lv->d + z) * lv->w + x;
}

uint8_t level_get(level_t const* lv, int x, int y, int z) {
    if (x < 0 || y < 0 || z < 0 || x >= lv->w || y >= lv->h || z >= lv->d) return MAT_METAL;
    return lv->cells[idx(lv, x, y, z)];
}

int level_door_at(level_t const* lv, int x, int y, int z) {
    for (int i = 0; i < lv->n_doors; i++) {
        door_t const* d = &lv->doors[i];
        if (x >= d->x0 && x < d->x1 && y >= d->y0 && y < d->y1 && z >= d->z0 && z < d->z1) return i;
    }
    return -1;
}

bool level_solid(level_t const* lv, int x, int y, int z) {
    uint8_t const m = level_get(lv, x, y, z);
    if (m == MAT_AIR) return false;
    if (m == MAT_DOOR) {
        int const d = level_door_at(lv, x, y, z);
        return d < 0 || lv->doors[d].open < DOOR_PASSABLE;
    }
    return true;
}

void level_set(level_t* lv, int x, int y, int z, uint8_t m) {
    if (x < 0 || y < 0 || z < 0 || x >= lv->w || y >= lv->h || z >= lv->d) return;
    lv->cells[idx(lv, x, y, z)] = m;
}

// Every cell in [x0, x1) x [y0, y1) x [z0, z1) becomes `m`.
static void box(level_t* lv, int x0, int y0, int z0, int x1, int y1, int z1, uint8_t m) {
    for (int y = y0; y < y1; y++)
        for (int z = z0; z < z1; z++)
            for (int x = x0; x < x1; x++)
                if (x >= 0 && y >= 0 && z >= 0 && x < lv->w && y < lv->h && z < lv->d) lv->cells[idx(lv, x, y, z)] = m;
}

static void begin(level_t* lv, int w, int h, int d) {
    memset(lv, 0, sizeof(*lv));
    lv->w = w;
    lv->h = h;
    lv->d = d;
    box(lv, 0, 0, 0, w, h, d, MAT_METAL);
}

// 1. Walk across a pit of goo by going round it.
static void chamber_gap(level_t* lv) {
    begin(lv, 10, 7, 16);
    lv->name = "01  The gap";
    lv->hint = "Portals stick to white panels.";
    box(lv, 0, 1, 0, 10, 6, 16, MAT_WHITE);  // all four walls
    box(lv, 1, 0, 1, 9, 1, 15, MAT_WHITE);   // floor
    box(lv, 1, 1, 1, 9, 6, 15, MAT_AIR);
    box(lv, 1, 0, 5, 9, 1, 10, MAT_GOO);
    box(lv, 4, 0, 12, 6, 1, 14, MAT_EXIT);
    lv->spawn     = v3(5.0f, 1.0f, 2.5f);
    lv->spawn_yaw = 0.0f;
}

// 2. Up onto a ledge no jump reaches.
static void chamber_ledge(level_t* lv) {
    begin(lv, 10, 10, 14);
    lv->name = "02  The ledge";
    lv->hint = "Only some panels take a portal.";
    box(lv, 1, 1, 1, 9, 9, 13, MAT_AIR);
    box(lv, 1, 1, 9, 9, 5, 13, MAT_METAL);  // the ledge, 4 m up
    box(lv, 4, 4, 11, 6, 5, 12, MAT_EXIT);
    box(lv, 0, 1, 2, 1, 4, 6, MAT_WHITE);    // low on the left wall
    box(lv, 2, 5, 13, 8, 9, 14, MAT_WHITE);  // high on the back wall
    lv->spawn     = v3(5.0f, 1.0f, 2.5f);
    lv->spawn_yaw = 0.0f;
}

// 3. Speedy thing goes in, speedy thing comes out.
static void chamber_fling(level_t* lv) {
    begin(lv, 23, 15, 9);
    lv->name = "03  The fling";
    lv->hint = "Fall far into one, fly out of the other.";
    box(lv, 1, 1, 1, 22, 14, 8, MAT_AIR);
    box(lv, 1, 1, 1, 9, 11, 3, MAT_METAL);   // the balcony, 10 m up
    box(lv, 1, 0, 3, 9, 1, 8, MAT_WHITE);    // the pit floor below it
    box(lv, 0, 8, 3, 1, 10, 8, MAT_WHITE);   // a strip high on the west wall
    box(lv, 9, 0, 1, 15, 1, 8, MAT_GOO);
    box(lv, 15, 1, 1, 22, 3, 8, MAT_METAL);  // the far platform
    box(lv, 19, 2, 3, 21, 3, 6, MAT_EXIT);
    lv->spawn     = v3(6.5f, 11.0f, 2.0f);
    lv->spawn_yaw = -1.5707963f;  // facing west
}

static void door(level_t* lv, int x0, int y0, int z0, int x1, int y1, int z1, int link) {
    box(lv, x0, y0, z0, x1, y1, z1, MAT_DOOR);
    lv->doors[lv->n_doors++] = (door_t){x0, y0, z0, x1, y1, z1, link, 0.0f};
}

static void button(level_t* lv, int x, int y, int z, int link) {
    lv->buttons[lv->n_buttons++] = (button_t){x, y, z, link, false};
}

static void cube(level_t* lv, float x, float y, float z) {
    lv->cubes[lv->n_cubes++] = v3(x, y, z);
}

// 4. A cube, a button, a door.
static void chamber_button(level_t* lv) {
    begin(lv, 12, 6, 14);
    lv->name = "04  The button";
    lv->hint = "Pick up a cube. Buttons hold doors open.";
    box(lv, 1, 1, 1, 11, 5, 13, MAT_AIR);
    box(lv, 1, 1, 8, 11, 5, 9, MAT_METAL);  // the wall across the room
    door(lv, 5, 1, 8, 7, 3, 9, 0);
    box(lv, 5, 0, 10, 7, 1, 12, MAT_EXIT);
    button(lv, 3, 0, 4, 0);
    cube(lv, 8.5f, 1.0f, 3.5f);
    lv->spawn     = v3(6.0f, 1.0f, 2.0f);
    lv->spawn_yaw = 0.0f;
}

// 5. The cube is up on a ledge; the button is down here.
static void chamber_delivery(level_t* lv) {
    begin(lv, 12, 9, 14);
    lv->name = "05  Delivery";
    lv->hint = "Carry the cube through a portal.";
    box(lv, 1, 1, 1, 11, 8, 13, MAT_AIR);
    box(lv, 1, 1, 9, 5, 5, 13, MAT_METAL);    // the ledge, 4 m up
    box(lv, 1, 5, 13, 5, 8, 14, MAT_WHITE);   // the wall above it
    box(lv, 11, 1, 2, 12, 4, 7, MAT_WHITE);   // low on the east wall
    box(lv, 6, 1, 9, 7, 5, 13, MAT_METAL);    // the exit alcove's walls
    box(lv, 7, 1, 8, 11, 5, 9, MAT_METAL);
    door(lv, 8, 1, 8, 10, 3, 9, 0);
    box(lv, 8, 0, 11, 10, 1, 12, MAT_EXIT);
    button(lv, 8, 0, 5, 0);
    cube(lv, 3.0f, 5.0f, 11.0f);
    lv->spawn     = v3(6.0f, 1.0f, 2.0f);
    lv->spawn_yaw = 0.0f;
}

typedef void (*builder_t)(level_t*);
static builder_t const s_chambers[] = {chamber_gap, chamber_ledge, chamber_fling, chamber_button, chamber_delivery};

int level_count(void) {
    return (int)(sizeof(s_chambers) / sizeof(s_chambers[0]));
}

bool level_load(level_t* lv, int index) {
    if (index < 0 || index >= level_count()) return false;
    s_chambers[index](lv);
    return true;
}

// --- Raycast (Amanatides & Woo) ----------------------------------------

ray_hit_t level_raycast(level_t const* lv, vec3_t from, vec3_t dir, float max_dist) {
    ray_hit_t r = {0};
    int       c[3]    = {(int)floorf(from.x), (int)floorf(from.y), (int)floorf(from.z)};
    float     o[3]    = {from.x, from.y, from.z};
    float     d[3]    = {dir.x, dir.y, dir.z};
    int       step[3] = {0};
    float     tmax[3], tdelta[3];
    for (int a = 0; a < 3; a++) {
        if (d[a] > 0.0f) {
            step[a]   = 1;
            tdelta[a] = 1.0f / d[a];
            tmax[a]   = ((float)(c[a] + 1) - o[a]) / d[a];
        } else if (d[a] < 0.0f) {
            step[a]   = -1;
            tdelta[a] = -1.0f / d[a];
            tmax[a]   = (o[a] - (float)c[a]) / -d[a];
        } else {
            tdelta[a] = 1e30f;
            tmax[a]   = 1e30f;
        }
    }
    float t = 0.0f;
    while (t <= max_dist) {
        int a = 0;
        if (tmax[1] < tmax[a]) a = 1;
        if (tmax[2] < tmax[a]) a = 2;
        t = tmax[a];
        c[a] += step[a];
        tmax[a] += tdelta[a];
        if (t > max_dist) break;
        if (level_solid(lv, c[0], c[1], c[2])) {
            r.hit   = true;
            r.x     = c[0];
            r.y     = c[1];
            r.z     = c[2];
            r.face  = a * 2 + (step[a] > 0 ? 1 : 0);  // entered moving +a: the -a face
            r.dist  = t;
            r.point = v3_mad(from, dir, t);
            return r;
        }
    }
    return r;
}

// --- Greedy mesh -------------------------------------------------------

static bool is_hole(hole_t const* holes, int n, int x, int y, int z, int face) {
    for (int i = 0; i < n; i++)
        if (holes[i].x == x && holes[i].y == y && holes[i].z == z && holes[i].face == face) return true;
    return false;
}

int level_mesh(level_t const* lv, hole_t const* holes, int n_holes, mquad_t* out, int max_out) {
    int const dims[3] = {lv->w, lv->h, lv->d};
    int       n       = 0;
    uint8_t   mask[LV_MAX_W * LV_MAX_D];  // the largest u x v plane

    for (int face = 0; face < 6; face++) {
        int const a    = face / 2;
        int const sign = (face % 2 == 0) ? 1 : -1;
        int const ua   = (a + 1) % 3;
        int const va   = (a + 2) % 3;
        int const nu   = dims[ua];
        int const nv   = dims[va];
        for (int s = 0; s < dims[a]; s++) {
            for (int j = 0; j < nv; j++) {
                for (int i = 0; i < nu; i++) {
                    int c[3];
                    c[a]  = s;
                    c[ua] = i;
                    c[va] = j;
                    uint8_t const m = level_get(lv, c[0], c[1], c[2]);
                    int           e[3] = {c[0], c[1], c[2]};
                    e[a] += sign;
                    // A door is drawn by the game, not the mesh: its cells
                    // are open space here, so the frame round it shows.
                    uint8_t const n    = level_get(lv, e[0], e[1], e[2]);
                    bool const    vis  = m != MAT_AIR && m != MAT_DOOR && (n == MAT_AIR || n == MAT_DOOR) &&
                                     !is_hole(holes, n_holes, c[0], c[1], c[2], face);
                    mask[j * nu + i] = vis ? m : MAT_AIR;
                }
            }
            for (int j = 0; j < nv; j++) {
                for (int i = 0; i < nu;) {
                    uint8_t const m = mask[j * nu + i];
                    if (m == MAT_AIR) {
                        i++;
                        continue;
                    }
                    int w = 1;
                    while (i + w < nu && mask[j * nu + i + w] == m) w++;
                    int  h    = 1;
                    bool grow = true;
                    while (grow && j + h < nv) {
                        for (int k = 0; k < w; k++)
                            if (mask[(j + h) * nu + i + k] != m) {
                                grow = false;
                                break;
                            }
                        if (grow) h++;
                    }
                    for (int jj = 0; jj < h; jj++)
                        for (int k = 0; k < w; k++) mask[(j + jj) * nu + i + k] = MAT_AIR;

                    if (n < max_out) {
                        float o[3], du[3] = {0}, dv[3] = {0};
                        o[a]   = (float)(s + (sign > 0 ? 1 : 0));
                        o[ua]  = (float)i;
                        o[va]  = (float)j;
                        du[ua] = (float)w;
                        dv[va] = (float)h;
                        out[n] = (mquad_t){
                            .origin = v3(o[0], o[1], o[2]),
                            .du     = v3(du[0], du[1], du[2]),
                            .dv     = v3(dv[0], dv[1], dv[2]),
                            .n      = dir_vec(face),
                            .su     = (float)w,
                            .sv     = (float)h,
                            .mat    = m,
                        };
                        n++;
                    }
                    i += w;
                }
            }
        }
    }
    return n;
}
