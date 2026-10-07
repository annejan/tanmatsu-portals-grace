#pragma once
// The test chamber: a grid of one-metre cells, each air or a solid of
// some material. Pure C, no engine -- the host tests build it too.
//
// World axes are the engine's: x across, y up, z forward. Cell (x, y, z)
// spans [x, x+1) x [y, y+1) x [z, z+1). Outside the grid is solid metal.

#include <stdbool.h>
#include <stdint.h>
#include "vec.h"

// The biggest chamber, in cells (one metre each), its walls included.
// Each cell takes two bytes in a level_t (its solid, and its gel), and
// the app keeps several levels (in play, being parsed, listed, edited):
// about 1 MB each at this size. A cell's x, y and z must each fit a
// byte (chamber.c's crusher flood fill).
#define LV_MAX_W 128
#define LV_MAX_H 32
#define LV_MAX_D 128
_Static_assert(LV_MAX_W <= 256 && LV_MAX_H <= 256 && LV_MAX_D <= 256, "a cell's x, y, z must each fit a byte");
#define LV_PAINT_LOG 256  // cells painted, remembered (level_t.paint_log)
// The farthest a portal shot, a laser, a light bridge or a funnel goes, in
// all its pieces through portals: across the biggest chamber (its diagonal
// is 184 m) and on. It was 64 m, which a 128 m chamber outgrew.
#define LV_REACH     256.0f

typedef enum {
    MAT_AIR = 0,
    MAT_WHITE,        // portal-able panel
    MAT_METAL,        // takes no portal
    MAT_GOO,          // a floor that kills
    MAT_EXIT,         // a floor that ends the chamber
    MAT_DOOR,         // a door's cells: solid while it is shut (see door_t)
    MAT_GLASS,        // solid and see-through; takes no portal, stops a shot
    MAT_FIZZ,         // a fizzler: air that closes portals and destroys cubes
    MAT_JUMP,         // a faith plate: a floor that launches (see jump_t)
    MAT_PEDESTAL,     // a waist-high block a pedestal button stands on
    MAT_DROPPER,      // a hatch in the ceiling that cubes drop out of
    MAT_CUBEBASE,     // a floor a cube button stands on
    MAT_EMITTER,      // a laser emitter: fires out of its one open side (see laser_t)
    MAT_CATCHER,      // a laser catcher: a block whose button is down while lit
    MAT_BRIDGE,       // a light bridge emitter: a walkable strip out of its open side
    MAT_DISP_BLUE,    // gel dispensers, in a ceiling: they drip their gel
    MAT_DISP_ORANGE,  // (see gel_src_t)
    MAT_DISP_WHITE,
    MAT_LAUNCHER,  // an energy pellet launcher: fires out of its one open side
    MAT_RECEIVER,  // a pellet receiver: a block whose button latches when a pellet arrives
    MAT_RELAY,     // a laser relay: a slim post a beam passes through, lighting its button
    MAT_FIELD,     // a laser field: a red sheet like a fizzler, deadly to touch
    MAT_CRUSHER,   // while parsing only: a crusher's cells (then crusher_t, and air)
    MAT_CUP,       // a floor a sphere button stands on: only a sphere presses it, and it holds the sphere
    MAT_FUNNEL,    // an excursion funnel emitter: a tractor beam out of its one open side
    // Never in a cell: what a painted face is meshed and drawn as.
    MAT_PAINT_BLUE,
    MAT_PAINT_ORANGE,
    MAT_PAINT_WHITE,
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

#define LV_MAX_DOORS    8
#define LV_MAX_BUTTONS  8
#define LV_MAX_CUBES    4
#define LV_MAX_JUMPS    4
#define LV_MAX_LASERS   4
#define LV_MAX_BRIDGES  2
#define LV_MAX_GELS     4
#define LV_MAX_PELLETS  2
#define LV_MAX_CRUSHERS 4
#define LV_MAX_FUNNELS  2

// A crusher: a block hanging in the ceiling, [lo, hi), that slams down
// `drop` metres onto the floor under it and rises again, over and over.
typedef struct {
    vec3_t lo, hi;
    float  drop;
} crusher_t;

// Gel, as paint on a cell: blue bounces, orange speeds you up, white
// takes portals. Only metal and white panels take paint.
typedef enum {
    GEL_NONE = 0,
    GEL_BLUE,
    GEL_ORANGE,
    GEL_WHITE,
} gel_t;

// A gel dispenser: its ceiling cell, dripping `gel` out of its underside.
typedef struct {
    int x, y, z;
    int gel;
} gel_src_t;

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
// On a laser catcher (MAT_CATCHER) it is down while a beam hits the block.
// On a cup (MAT_CUP) only a sphere presses it.
typedef struct {
    int   x, y, z;
    int   link;
    bool  pressed;      // state, set by the game
    bool  pedestal;     // on a pedestal: pressed by hand, for a while
    bool  cube_only;    // on a cube base: the player does not press it
    bool  sphere_only;  // in a cup: only a sphere presses it
    bool  laser;        // on a laser catcher: down while a beam hits it
    bool  receiver;     // on a pellet receiver: down for good once a pellet arrives
    bool  relay;        // on a laser relay: down while a beam passes through it
    float timer_left;   // state: a pedestal button's seconds still to go
} button_t;

#define LV_TIMER_S    4.0f  // a pedestal button's time, unless a chamber says
// A dropper's cube appears with its base this far under the hatch.
#define LV_DROP_DEPTH 0.7f

// An emitter -- of a laser (MAT_EMITTER), a light bridge (MAT_BRIDGE) or
// an excursion funnel (MAT_FUNNEL):
// its cell (x, y, z), and the side it emits out of (a dir_t), its only
// open one.
typedef struct {
    int x, y, z;
    int dir;
} emitter_t;

// A faith plate: the MAT_JUMP cell (x, y, z), and where it lands what
// stands on it.
typedef struct {
    int    x, y, z;
    vec3_t target;
} jump_t;

typedef struct {
    char      name[32];
    char      hint[80];
    int       w, h, d;
    uint8_t   cells[LV_MAX_W * LV_MAX_H * LV_MAX_D];
    vec3_t    spawn;  // feet
    float     spawn_yaw;
    door_t    doors[LV_MAX_DOORS];
    int       n_doors;
    button_t  buttons[LV_MAX_BUTTONS];
    int       n_buttons;
    vec3_t    cubes[LV_MAX_CUBES];         // where each cube starts, its base
    bool      cube_drop[LV_MAX_CUBES];     // ... out of a dropper's hatch
    bool      cube_reflect[LV_MAX_CUBES];  // a reflection cube: sends a laser on
    bool      cube_sphere[LV_MAX_CUBES];   // a sphere: it rolls, and walking into it pushes it
    bool      cube_turret[LV_MAX_CUBES];   // a turret: it shoots the player it sees
    float     cube_yaw[LV_MAX_CUBES];      // the way a turret faces at the start: towards the player's
    int       n_cubes;
    char      story[160];  // shown as the chamber starts
    float     timer;       // seconds a pedestal button stays down
    jump_t    jumps[LV_MAX_JUMPS];
    int       n_jumps;
    emitter_t lasers[LV_MAX_LASERS];
    int       n_lasers;
    emitter_t bridges[LV_MAX_BRIDGES];  // a bridge's surface is level with its cell's bottom
    int       n_bridges;
    emitter_t funnels[LV_MAX_FUNNELS];  // excursion funnels
    int       n_funnels;
    int funnel_link;  // -1: they pull away from their emitters; else, while that link's buttons are all down, towards
    crusher_t  crushers[LV_MAX_CRUSHERS];
    int        n_crushers;
    emitter_t  launchers[LV_MAX_PELLETS];  // energy pellet launchers
    int        n_launchers;
    gel_src_t  gels[LV_MAX_GELS];
    int        n_gels;
    uint8_t    paint[LV_MAX_W * LV_MAX_H * LV_MAX_D];  // gel_t per cell: state, painted in play
    // For render.c, which meshes only what changed: which cells these are
    // (a new serial whenever a cell is set; a copy keeps it), and the
    // cells painted, the last LV_PAINT_LOG of paint_n so far.
    uint32_t   serial;
    uint32_t   paint_n;
    uint32_t   paint_log[LV_PAINT_LOG];
    platform_t platform;       // M cells and the N cell; none when n_platforms is 0
    int        platform_link;  // -1: it always moves; else only while that link's buttons are all down
    int        n_platforms;
} level_t;

// Open space, as drawn: what the faces of solid cells next to it show to.
// Doors, fizzlers and laser fields are drawn by the game; pedestals and
// relays are slim posts in a cell of their own.
static inline bool level_open(uint8_t m) {
    return m == MAT_AIR || m == MAT_DOOR || m == MAT_FIZZ || m == MAT_FIELD || m == MAT_PEDESTAL || m == MAT_RELAY;
}

// A sheet through the middle of its cell: a fizzler or a laser field.
static inline bool level_sheet(uint8_t m) {
    return m == MAT_FIZZ || m == MAT_FIELD;
}

// The chambers (chamber.h): the built-in ones, then any from the SD card.
int  level_count(void);
// Build chamber `index` (0-based) into `lv`. False past the last one, or
// if its file does not parse.
bool level_load(level_t* lv, int index);

uint8_t  level_get(level_t const* lv, int x, int y, int z);
void     level_set(level_t* lv, int x, int y, int z, uint8_t m);
// Solid to bodies and shots: anything but air, a fizzler and an open door.
bool     level_solid(level_t const* lv, int x, int y, int z);
// The gel on a cell, and painting it (a cell that takes no paint keeps none).
int      level_paint(level_t const* lv, int x, int y, int z);
// A level to parse into for a moment -- to check a file, read a chamber's
// name, build its solution -- shared: at the biggest size a level is a
// megabyte, and every caller had its own. Nothing may keep it, or call
// anything that uses it, while it is in use.
level_t* level_scratch(void);
bool     level_set_paint(level_t* lv, int x, int y, int z, int gel);
// A portal sticks to it: a white panel, or anything painted white.
bool     level_portalable(level_t const* lv, int x, int y, int z);
// The door whose cells hold (x, y, z), or -1.
int      level_door_at(level_t const* lv, int x, int y, int z);
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
#define LV_MAX_QUADS 2048
#define LV_MAX_CLEAR 1024

// A face the mesh leaves out, because a portal sits on it.
typedef struct {
    int x, y, z, face;
} hole_t;

// Returns how many rectangles the chamber needs, writing at most `max_out`
// of them (`out` may be NULL to only count).
int level_mesh(level_t const* lv, hole_t const* holes, int n_holes, mquad_t* out, int max_out);
// The same, one slice at a time -- the faces facing `face` (DIR_*) in
// slice `s` along its axis, 0 .. level_mesh_slices() - 1 -- in the order
// level_mesh() gives them: what to re-mesh when a portal moves (render.c).
int level_mesh_slice(level_t const* lv, hole_t const* holes, int n_holes, int face, int s, mquad_t* out, int max_out);
int level_mesh_slices(level_t const* lv, int face);
// The faces of glass and fizzler a chamber needs drawn.
int level_clear_faces(level_t const* lv);
