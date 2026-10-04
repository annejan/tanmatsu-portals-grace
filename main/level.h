#pragma once
// The test chamber: a grid of one-metre cells, each air or a solid of
// some material. Pure C, no engine -- the host tests build it too.
//
// World axes are the engine's: x across, y up, z forward. Cell (x, y, z)
// spans [x, x+1) x [y, y+1) x [z, z+1). Outside the grid is solid metal.

#include <stdbool.h>
#include <stdint.h>
#include "vec.h"

#define LV_MAX_W 24
#define LV_MAX_H 16
#define LV_MAX_D 24

typedef enum {
    MAT_AIR = 0,
    MAT_WHITE,  // portal-able panel
    MAT_METAL,  // takes no portal
    MAT_GOO,    // a floor that kills
    MAT_EXIT,   // a floor that ends the chamber
    MAT_COUNT,
} material_t;

// The six face directions. A face's direction is its outward normal: the
// side the air is on.
typedef enum {
    DIR_PX = 0,
    DIR_NX,
    DIR_PY,
    DIR_NY,
    DIR_PZ,
    DIR_NZ,
} dir_t;

static inline vec3_t dir_vec(int d) {
    static vec3_t const v[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    return v[d];
}
static inline void dir_step(int d, int* dx, int* dy, int* dz) {
    vec3_t const v = dir_vec(d);
    *dx            = (int)v.x;
    *dy            = (int)v.y;
    *dz            = (int)v.z;
}

typedef struct {
    char const* name;
    char const* hint;
    int         w, h, d;
    uint8_t     cells[LV_MAX_W * LV_MAX_H * LV_MAX_D];
    vec3_t      spawn;  // feet
    float       spawn_yaw;
} level_t;

int  level_count(void);
// Build chamber `index` (0-based) into `lv`. False past the last one.
bool level_load(level_t* lv, int index);

uint8_t level_get(level_t const* lv, int x, int y, int z);
void    level_set(level_t* lv, int x, int y, int z, uint8_t m);
static inline bool level_solid(level_t const* lv, int x, int y, int z) {
    return level_get(lv, x, y, z) != MAT_AIR;
}

// --- Raycast ------------------------------------------------------------
typedef struct {
    bool   hit;
    int    x, y, z;  // the solid cell hit
    int    face;     // dir_t of the face entered through
    float  dist;
    vec3_t point;
} ray_hit_t;

ray_hit_t level_raycast(level_t const* lv, vec3_t from, vec3_t dir, float max_dist);

// --- Mesh ---------------------------------------------------------------
//
// The visible faces, greedily merged into rectangles of one material.
// A quad is origin + s*du + t*dv for s, t in [0, 1]; du and dv are whole
// cells long, so (su, sv) -- their lengths -- tile a texture once a cell.
typedef struct {
    vec3_t  origin, du, dv, n;
    float   su, sv;
    uint8_t mat;
} mquad_t;

#define LV_MAX_QUADS 1024

// A face the mesh leaves out, because a portal sits on it.
typedef struct {
    int x, y, z, face;
} hole_t;

int level_mesh(level_t const* lv, hole_t const* holes, int n_holes, mquad_t* out, int max_out);
