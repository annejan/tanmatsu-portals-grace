#pragma once
// One chamber in play: the player, the portals, the cubes, buttons and
// doors, stepped together. Pure C, no engine -- the badge, the scripted
// demos (demo.c) and the host tests all run this.

#include <stdbool.h>
#include "level.h"
#include "physics.h"
#include "player.h"
#include "portal.h"

#define CUBE_HALF   0.3f  // the cube is 0.6 m on a side
#define CUBE_HOLD   1.3f  // carried this far in front of the eye
#define CUBE_REACH  2.0f  // picked up from no further than this
#define CUBE_LETGO  2.5f  // dropped when this far from where it should be ...
#define CUBE_STUCK  0.4f  // ... for this long: stuck, not merely swinging round

typedef struct {
    body_t body;
} cube_t;

typedef struct {
    level_t  lv;
    player_t pl;
    portal_t portals[2];
    cube_t   cubes[LV_MAX_CUBES];
    int      n_cubes;
    int      held;      // the cube carried, or -1
    int      held_via;  // -1, or the portal i such that the cube is beyond it: see game.c
    float    held_far;  // seconds the carried cube has been too far from the hold point
    int      chamber;
} game_t;

typedef struct {
    float fwd, strafe;   // -1 .. 1
    float dyaw, dpitch;  // radians
    bool  jump;          // held
    bool  fire[2];       // pressed this step
    bool  use;           // pressed this step: pick up or put down
} game_input_t;

// Events, on top of player_event_t's.
enum {
    GAME_EV_PORTAL      = 1 << 8,   // a portal moved: re-mesh
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
};

// The velocity that carries a body from `from` (its base) to land on
// `to`: an arc peaking JUMP_APEX above the higher of the two.
#define JUMP_APEX 2.5f
vec3_t jump_velocity(vec3_t from, vec3_t to);

void game_load(game_t* g, int chamber);
// The same with a level from elsewhere (the editor's play-test).
void game_load_level(game_t* g, level_t const* lv);
// One step; returns PL_EV_* | GAME_EV_* bits.
int game_step(game_t* g, game_input_t const* in, float dt);

// The pieces of a step, for scripts that act between steps.
bool game_fire(game_t* g, int which);
int  game_use(game_t* g);

aabb_t cube_aabb(cube_t const* c);
