#pragma once
// The player: an upright box that walks, jumps, falls and goes through
// portals with its momentum. Pure C, no engine.

#include <stdbool.h>
#include "level.h"
#include "physics.h"
#include "portal.h"
#include "vec.h"

#define PL_HALF_W    0.3f
#define PL_HEIGHT    1.75f
#define PL_EYE       1.6f
#define PL_PITCH_MAX 1.45f

typedef struct {
    vec3_t pos;  // feet, centre of the box's base
    vec3_t vel;
    float  yaw, pitch;
    bool   on_ground;
} player_t;

typedef struct {
    float  fwd, strafe;  // -1 .. 1
    bool   jump;
    bool   floating;  // in an excursion funnel: no gravity, no ground ...
    vec3_t carry;     // ... and carried along at this, steering a little
} player_input_t;

typedef enum {
    PL_EV_TELEPORT = 1 << 0,
    PL_EV_DIED     = 1 << 1,
    PL_EV_EXIT     = 1 << 2,
    PL_EV_LANDED   = 1 << 3,
    PL_EV_BOUNCE   = 1 << 4,  // off blue gel
} player_event_t;

void player_spawn(player_t* p, level_t const* lv);

// One step. `portals` is the pair; they connect only while both are open.
// Returns a mask of player_event_t.
int player_update(player_t* p, level_t const* lv, portal_t const portals[2], player_input_t const* in, float dt);
// The same among other solid boxes (cubes). On PL_EV_TELEPORT, `*through`
// (may be NULL) is the portal it went in by.
int player_update_in(player_t* p, phys_world_t const* w, player_input_t const* in, float dt, int* through);

vec3_t  player_eye(player_t const* p);
basis_t player_view(player_t const* p);
