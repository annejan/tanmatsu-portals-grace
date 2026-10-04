#include "level.h"
#include "chamber.h"
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
    if (m == MAT_AIR || m == MAT_FIZZ) return false;
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

int level_count(void) {
    return chamber_count();
}

bool level_load(level_t* lv, int index) {
    return chamber_build(index, lv, NULL, NULL);
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
                    // Doors, glass and fizzlers are drawn by the game, not the
                    // mesh: their cells are open space here, so what is
                    // round and behind them shows.
                    uint8_t const n    = level_get(lv, e[0], e[1], e[2]);
                    bool const    vis  = m != MAT_AIR && m != MAT_DOOR && m != MAT_GLASS && m != MAT_FIZZ &&
                                     (n == MAT_AIR || n == MAT_DOOR || n == MAT_GLASS || n == MAT_FIZZ) &&
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
