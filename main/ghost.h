#pragma once
// Ghost races: each chamber's best runs, raced. Every attempt at a chamber
// is recorded as it is played (recording.h, the frames' whole numbers); one
// that reaches the exit faster than your best becomes your ghost of it,
// GHOST_DIR/<chamber>.txt. Rivals are other people's ghosts folders, copied
// to GHOST_DIR/rivals/<their nick>/: the fastest GHOST_RIVALS of them race
// too. Each racer's run is played again in a world of its own -- its own
// portals, cubes and buttons -- in step with the player's clock, and drawn
// where it is (render.h). Pure C: the host tests race it.

#include <stdbool.h>
#include "game.h"
#include "recording.h"

#define GHOST_DIR    "/sd/portals/ghosts"
#define GHOST_RIVALS 3
#define GHOST_NAME_N 24

typedef struct {
    char        name[GHOST_NAME_N];  // "you", or the rival's folder
    recording_t run;
    float       best_s;  // its time
    game_t*     world;   // its world (malloc'd once)
    int         at;      // its frames played ...
    float       t;       // ... and how long they took
    bool        live;    // has a world, and plays
} ghost_racer_t;

typedef struct {
    char                id[CHAMBER_ID_N];
    char                sig[48];  // what a run is good for: the release and the chamber as it is (ghost.c)
    ghost_racer_t       racer[1 + GHOST_RIVALS];
    int                 n;      // racers loaded
    bool                mine;   // racer[0] is your own best
    bool                stale;  // the runs to be read again (another chamber, a new best)
    float               now;    // this attempt's clock
    recording_capture_t cap;    // this attempt, as it goes
    bool                capturing;
    bool                lost;  // ... its capture ran out of memory: not kept
} ghost_t;

typedef struct {
    float time;    // this attempt's
    float before;  // your best before it, or -1 (none, or none for this chamber as it is now)
    bool  faster;  // a new best of yours ...
    bool  best;    // ... and saved as your ghost
    int   place;   // among the racers and you, from 1
    int   of;      // how many raced, you included
} ghost_result_t;

// An attempt at chamber `id` begins in level `lv` (as it starts), on game
// `release` ("0.12.0"): your best and the fastest rivals from `dir` to race
// -- runs made on this release in this chamber as it is now (a chamber
// edited, or a game whose physics may have changed, plays them otherwise).
// The runs are read once per chamber; a restart only sets them off again.
void           ghost_begin(ghost_t* g, char const* dir, char const* id, level_t const* lv, char const* release);
// A frame of the player's, as stepped: recorded, and every racer brought
// up to the same moment.
void           ghost_frame(ghost_t* g, recording_frame_t const* f);
// The exit reached: the time, against your best and the rivals -- a
// better one of yours written to `dir`, named for `nick`.
ghost_result_t ghost_finish(ghost_t* g, char const* dir, char const* nick);
// Racer i (0 .. ghost_count() - 1), to draw: false if it is not to be drawn
// now (gone through the exit a while ago).
int            ghost_count(ghost_t const* g);
bool           ghost_pose(ghost_t const* g, int i, player_t* out);
// No more racing: what it holds freed.
void           ghost_end(ghost_t* g);
