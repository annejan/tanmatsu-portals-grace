#pragma once
// One chamber in play: the player, the portals, the cubes, buttons and
// doors, stepped together. Pure C, no engine -- the badge, the scripted
// demos (demo.c) and the host tests all run this.

#include <stdbool.h>
#include "level.h"
#include "physics.h"
#include "player.h"
#include "portal.h"

// How runs play: a ghost (ghost.h) races only on the physics it was made
// on. Bump it when a change to the game, the player, the physics or the
// portals makes a recorded run go otherwise -- not for each release, which
// would retire every ghost. make check holds it to tests/physics.txt.
#define GAME_PHYSICS "0.12.0"

#define CUBE_HALF      0.3f  // the cube is 0.6 m on a side
#define CUBE_HOLD      1.3f  // carried this far in front of the eye
#define CUBE_REACH     2.0f  // picked up from no further than this
#define CUBE_LETGO     2.5f  // dropped when this far from where it should be ...
#define CUBE_STUCK     0.4f  // ... for this long: stuck, not merely swinging round
#define BEAM_SEGS      8     // pieces of one laser beam: bounces and portals
#define BEAM_BURN      0.5f  // seconds in a laser beam a player survives
// Every solid box a body can meet besides the grid: the other cubes, the
// player, the platform, and the pieces of the light bridges.
#define GAME_MAX_BOXES (LV_MAX_CUBES + 2 + LV_MAX_BRIDGES * BEAM_SEGS + LV_MAX_BUTTONS + LV_MAX_CRUSHERS)
// A crusher's cycle, in seconds: up, the slam, down, the rise. Several in
// a chamber run a quarter cycle apart, each after the one before it.
#define CRUSH_UP       1.5f
#define CRUSH_SLAM     0.25f
#define CRUSH_DOWN     0.5f
#define CRUSH_RISE     0.75f
#define CRUSH_CYCLE    (CRUSH_UP + CRUSH_SLAM + CRUSH_DOWN + CRUSH_RISE)
#define GEL_BLOBS      48  // gel in flight, at most
// A turret: 0.5 m across, 1 m tall. It sees the player in front of it and
// fires after a moment; anything landing on it, or it landing hard,
// knocks it over.
#define TURRET_HALF    0.25f
#define TURRET_H       1.0f
#define TURRET_EYE     0.75f  // its eye, above its base
#define TURRET_RANGE   15.0f  // m
#define TURRET_CONE    0.6f   // the cosine of the angle off its front it sees at, at most (53 degrees)
#define TURRET_WAKE    0.6f   // s it sees you before it fires
#define TURRET_KILL    1.0f   // s of fire a player survives
#define TURRET_KNOCK   3.0f   // m/s: landing this fast on a turret, or as one, knocks it over
#define TURRET_BURST   0.1f   // s between bursts of fire
#define GEL_DRIP       0.25f  // a dispenser lets a blob go this often (s)

#define FUNNEL_SPEED 3.0f  // m/s an excursion funnel carries things along it
#define FUNNEL_PULL  3.0f  // per second: how hard it draws them to its middle

#define PELLET_SPEED 6.0f   // m/s, straight, no gravity
#define PELLET_LIFE  10.0f  // s before it fizzles out (longer, to cross a big chamber) ...
#define PELLET_WAIT  1.5f   // ... and before its launcher fires the next

// An energy pellet, one per launcher: in flight, waiting to be fired, or
// done -- caught by a receiver, after which its launcher rests.
typedef struct {
    vec3_t pos, vel;
    float  life;  // seconds it has left in flight
    float  wait;  // seconds until the launcher fires again
    bool   live;
    bool   done;
} pellet_t;

// A blob of gel in flight, from a dispenser: it paints where it lands.
typedef struct {
    vec3_t  pos, vel;
    uint8_t gel;  // gel_t
    bool    live;
} gel_blob_t;

typedef struct {
    body_t body;
    float  yaw;      // where a reflection cube sends a laser, or a turret looks: it faces this way
    vec3_t spin[2];  // a sphere's own two axes, turned as it rolls (it is drawn by them)
    float  seen;     // a turret: seconds it has seen the player; it fires from TURRET_WAKE
    bool   down;     // a turret knocked over: it does nothing more
    bool   gone;     // a turret lost: in the goo, a fizzler, a crusher. It does not come back.
} cube_t;

// A piece of a laser beam, from a to b.
typedef struct {
    vec3_t a, b;
} beam_seg_t;

// What a run has done so far, for a review of how it was solved (review.h):
// it only watches, and never changes what happens. A new run (load,
// restart, death) starts it afresh.
enum {
    TRACK_FLOAT       = 1 << 0,  // carried by an excursion funnel
    TRACK_RIDE        = 1 << 1,  // carried by the moving platform
    TRACK_RIDE_HOLD   = 1 << 2,  // ... holding something
    TRACK_BRIDGE      = 1 << 3,  // stood on a light bridge
    TRACK_SPEED       = 1 << 4,  // ran on orange gel, fast
    TRACK_CUBE_PORTAL = 1 << 5,  // a cube went through a portal
    TRACK_PLATFORM    = 1 << 6,  // the platform moved
    TRACK_BOUNCE      = 1 << 7,  // thrown up by blue gel: a bounce, or a jump off it
    TRACK_GEL_PORTAL  = 1 << 8,  // gel went through a portal
};
enum {  // what held a button down (track_t.by)
    BY_PLAYER = 1 << 0,
    BY_CUBE   = 1 << 1,
    BY_SPHERE = 1 << 2,
    BY_TURRET = 1 << 3,  // standing
    BY_FALLEN = 1 << 4,  // knocked over
    BY_BEAM   = 1 << 5,  // a laser: a catcher or a relay
    BY_PELLET = 1 << 6,  // a receiver
    BY_HAND   = 1 << 7,  // a pedestal, pressed
};
typedef struct {
    uint32_t ever;                 // every PL_EV_* | GAME_EV_* seen
    uint16_t seen;                 // TRACK_*
    uint8_t  by[LV_MAX_BUTTONS];   // BY_*: what has held each button down
    uint16_t doors;                // each door that opened (past DOOR_PASSABLE)
    uint8_t  plates, cube_plates;  // each faith plate that threw the player, a cube
    uint16_t shots;                // portals placed
} track_t;

typedef struct {
    level_t    lv;
    player_t   pl;
    portal_t   portals[2];
    cube_t     cubes[LV_MAX_CUBES];
    int        n_cubes;
    int        held;      // the cube carried, or -1
    int        held_via;  // -1, or the portal i such that the cube is beyond it: see game.c
    float      held_far;  // seconds the carried cube has been too far from the hold point
    int        chamber;
    float      plat_t;                          // the moving platform's clock: where it is in its trip
    vec3_t     plat_at;                         // ... and how far from where it started
    beam_seg_t beam[LV_MAX_LASERS][BEAM_SEGS];  // each laser's beam, as traced last step
    int        beam_n[LV_MAX_LASERS];
    float      burn_t;                             // seconds the player has stood in a beam
    beam_seg_t bridge[LV_MAX_BRIDGES][BEAM_SEGS];  // each light bridge's centre line, on its surface
    int        bridge_n[LV_MAX_BRIDGES];
    gel_blob_t blobs[GEL_BLOBS];
    float      drip_t[LV_MAX_GELS];                // each dispenser: seconds to its next blob
    pellet_t   pellets[LV_MAX_PELLETS];            // one per launcher
    float      crush_t;                            // the crushers' clock
    beam_seg_t funnel[LV_MAX_FUNNELS][BEAM_SEGS];  // each excursion funnel's middle, from its emitter on
    int        funnel_n[LV_MAX_FUNNELS];
    float      shot_t;   // seconds the player has been under a turret's fire
    float      burst_t;  // the turrets' fire: its clock
    track_t    track;    // what this run has done (review.h)
} game_t;

typedef struct {
    float fwd, strafe;   // -1 .. 1
    float dyaw, dpitch;  // radians
    bool  jump;          // held
    bool  fire[2];       // pressed this step
    bool  use;           // pressed this step: pick up, put down, or press
} game_input_t;

// Events, on top of player_event_t's.
enum {
    GAME_EV_PORTAL      = 1 << 8,  // a portal moved: re-mesh
    GAME_EV_PICKUP      = 1 << 9,
    GAME_EV_DROP        = 1 << 10,
    GAME_EV_BUTTON      = 1 << 11,  // a button went down or up
    GAME_EV_BUTTON_DOWN = 1 << 12,
    GAME_EV_BUTTON_UP   = 1 << 13,
    GAME_EV_DOOR        = 1 << 14,  // a door started to open or to shut
    GAME_EV_SHOT_BLUE   = 1 << 15,  // a portal placed
    GAME_EV_SHOT_ORANGE = 1 << 16,
    GAME_EV_SHOT_FAIL   = 1 << 17,  // a shot the surface would not take
    GAME_EV_FIZZLE      = 1 << 18,  // a fizzler took the portals, or a cube
    GAME_EV_LAUNCH      = 1 << 19,  // a faith plate threw something
    GAME_EV_TICK        = 1 << 20,  // a pedestal button's timer: another second gone
    GAME_EV_PRESS       = 1 << 21,  // a pedestal button pressed by hand
    GAME_EV_DROPPER     = 1 << 22,  // a dropper let a new cube out
    GAME_EV_BURN        = 1 << 23,  // the player stepped into a laser beam
    GAME_EV_PAINT       = 1 << 24,  // gel painted something new: re-mesh
    GAME_EV_PELLET      = 1 << 25,  // a launcher fired, or a pellet bounced
    GAME_EV_CAUGHT      = 1 << 26,  // a receiver caught a pellet
    GAME_EV_CRUSH       = 1 << 27,  // a crusher hit the floor
    GAME_EV_SPOTTED     = 1 << 28,  // a turret saw the player
    GAME_EV_SHOOT       = 1 << 29,  // a burst of a turret's fire
    GAME_EV_TOPPLE      = 1 << 30,  // a turret knocked over
};

// False, and g as it was, if the chamber cannot be read (a file on the card).
bool game_load(game_t* g, int chamber);
// The same with a level from elsewhere (the editor's play-test).
void game_load_level(game_t* g, level_t const* lv);
// One step; returns PL_EV_* | GAME_EV_* bits.
int  game_step(game_t* g, game_input_t const* in, float dt);

// The pieces of a step, for scripts that act between steps.
bool game_fire(game_t* g, int which);
int  game_use(game_t* g);

aabb_t cube_aabb(cube_t const* c);
// Whether the funnels pull towards their emitters, their button down.
bool   funnel_reversed(game_t const* g);
// Whether the point `c` is in a funnel, and if so, the velocity it is
// carried at.
bool   funnel_carry(game_t const* g, vec3_t c, vec3_t* carry);
// Whether turret `i` is firing at the player.
bool   turret_firing(game_t const* g, int i);
// Crusher `k` where it is now.
aabb_t crusher_aabb(game_t const* g, int k);
// The moving platform where it is now (only if lv.n_platforms).
aabb_t platform_aabb(game_t const* g);
