#pragma once
// Ghost races: each chamber's best run, raced. Every attempt at a chamber
// is recorded as it is played (recording.h, the frames' whole numbers); one
// that reaches the exit faster than the best so far becomes the chamber's
// ghost, GHOST_DIR/<chamber>.txt. Next time, the ghost's run is played
// again in a world of its own -- its own portals, cubes and buttons --
// in step with the player's clock, and drawn where it is (render.h).
// Pure C: the host tests race it.

#include <stdbool.h>
#include "game.h"
#include "recording.h"

#define GHOST_DIR "/sd/portals/ghosts"

typedef struct {
    char                id[CHAMBER_ID_N];
    bool                racing;  // a ghost is playing: there is a best run
    game_t*             world;   // its world (malloc'd once)
    recording_t         best;    // its run
    int                 at;      // its frames played ...
    float               t;       // ... and how long they took
    float               best_s;  // the best time, or -1
    float               now;     // this attempt's clock
    recording_capture_t cap;     // this attempt, as it goes
    bool                capturing;
    bool                lost;     // ... its capture ran out of memory: not kept
    char                sig[48];  // what the run is good for: the release and the chamber as it is (ghost.c)
} ghost_t;

typedef struct {
    float time;    // this attempt's
    float before;  // the best before it, or -1 (none, or none for this chamber as it is now)
    bool  faster;  // a new best ...
    bool  best;    // ... and saved as the ghost
} ghost_result_t;

// An attempt at chamber `id` begins in level `lv` (as it starts), on game
// `release` ("0.12.0"): the best run from `dir` to race -- if there is one
// made on this release in this chamber as it is now (a chamber edited, or
// a game whose physics may have changed, plays it otherwise).
void           ghost_begin(ghost_t* g, char const* dir, char const* id, level_t const* lv, char const* release);
// A frame of the player's, as stepped: recorded, and the ghost brought up
// to the same moment.
void           ghost_frame(ghost_t* g, recording_frame_t const* f);
// The exit reached: the time, against the best -- a better one written
// to `dir` as the new ghost.
ghost_result_t ghost_finish(ghost_t* g, char const* dir);
// Where the ghost is, to draw it: false if there is none to draw.
bool           ghost_pose(ghost_t const* g, player_t* out);
// No more racing: what it holds freed.
void           ghost_end(ghost_t* g);
