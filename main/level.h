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
    MAT_WHITE,     // portal-able panel
    MAT_METAL,     // takes no portal
    MAT_GOO,       // a floor that kills
    MAT_EXIT,      // a floor that ends the chamber
    MAT_DOOR,      // a door's cells: solid while it is shut (see door_t)
    MAT_GLASS,     // solid and see-through; takes no portal, stops a shot
    MAT_FIZZ,      // a fizzler: air that closes portals and destroys cubes
    MAT_JUMP,      // a faith plate: a floor that launches (see jump_t)
    MAT_PEDESTAL,  // a waist-high block a pedestal button stands on
    MAT_DROPPER,   // a hatch in the ceiling that cubes drop out of
    MAT_CUBEBASE,  // a floor a cube button stands on
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

#define LV_MAX_DOORS   8
#define LV_MAX_BUTTONS 8
#define LV_MAX_CUBES   4
#define LV_MAX_JUMPS   4

// A moving platform: the box [lo, hi) where it starts, and how far it
// travels; it glides there and back, pausing at each end.
typedef struct {
    vec3_t lo, hi;
    vec3_t travel;
} platform_t;

// A door fills the cells [x0, x1) x [y0, y1) x [z0, z1), one cell thick
// along x or z. It opens while all the buttons with its `link` are
// pressed; a door with no buttons stays shut.
typedef struct {
    int   x0, y0, z0, x1, y1, z1;
    int   link;
    float open;  // 0 shut .. 1 open: state, animated by the game
} door_t;

// A button on top of the solid cell (x, y, z). On the floor it is pressed
// while the player or a cube stands on it. On a pedestal (MAT_PEDESTAL
// under it) it is pressed with Use, and stays down for the level's
// `timer` seconds. On a cube base (MAT_CUBEBASE) only a cube presses it.
typedef struct {
    int   x, y, z;
    int   link;
    bool  pressed;     // state, set by the game
    bool  pedestal;    // on a pedestal: pressed by hand, for a while
    bool  cube_only;   // on a cube base: the player does not press it
    float timer_left;  // state: a pedestal button's seconds still to go
} button_t;

#define LV_TIMER_S    4.0f  // a pedestal button's time, unless a chamber says
// A dropper's cube appears with its base this far under the hatch.
#define LV_DROP_DEPTH 0.7f

// A faith plate: the MAT_JUMP cell (x, y, z), and where it lands what
// stands on it.
typedef struct {
    int    x, y, z;
    vec3_t target;
} jump_t;

typedef struct {
    char       name[32];
    char       hint[80];
    int        w, h, d;
    uint8_t    cells[LV_MAX_W * LV_MAX_H * LV_MAX_D];
    vec3_t     spawn;  // feet
    float      spawn_yaw;
    door_t     doors[LV_MAX_DOORS];
    int        n_doors;
    button_t   buttons[LV_MAX_BUTTONS];
    int        n_buttons;
    vec3_t     cubes[LV_MAX_CUBES];      // where each cube starts, its base
    bool       cube_drop[LV_MAX_CUBES];  // ... out of a dropper's hatch
    int        n_cubes;
    char       story[160];  // shown as the chamber starts
    float      timer;       // seconds a pedestal button stays down
    jump_t     jumps[LV_MAX_JUMPS];
    int        n_jumps;
    platform_t platform;  // M cells and the N cell; none when n_platforms is 0
    int        n_platforms;
} level_t;

// The chambers (chamber.h): the built-in ones, then any from the SD card.
int  level_count(void);
// Build chamber `index` (0-based) into `lv`. False past the last one, or
// if its file does not parse.
bool level_load(level_t* lv, int index);

uint8_t level_get(level_t const* lv, int x, int y, int z);
void    level_set(level_t* lv, int x, int y, int z, uint8_t m);
// Solid to bodies and shots: anything but air, a fizzler and an open door.
bool    level_solid(level_t const* lv, int x, int y, int z);
// The door whose cells hold (x, y, z), or -1.
int     level_door_at(level_t const* lv, int x, int y, int z);
// A door counts as open, to walk or shoot through, from this far open.
#define DOOR_PASSABLE 0.9f

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

// What one chamber may need drawn, at most: rectangles of wall, and faces
// of glass and fizzler (drawn on their own, see render.c). A chamber
// needing more is refused when it is read, rather than drawn with holes.
#define LV_MAX_QUADS 1024
#define LV_MAX_CLEAR 512

// A face the mesh leaves out, because a portal sits on it.
typedef struct {
    int x, y, z, face;
} hole_t;

// Returns how many rectangles the chamber needs, writing at most `max_out`
// of them (`out` may be NULL to only count).
int level_mesh(level_t const* lv, hole_t const* holes, int n_holes, mquad_t* out, int max_out);
// The faces of glass and fizzler a chamber needs drawn.
int level_clear_faces(level_t const* lv);
