#pragma once
// The title screen's attract mode: behind the title the chambers play
// themselves -- each one's solution, the player looking before each shot
// as a person would, one chamber after another in a shuffled order. It
// plays in the caller's game_t; the caller draws it, fading it by
// attract_lit(), and keeps it quiet.

#include <stdbool.h>
#include <stdint.h>
#include "game.h"

// The shuffle's seed: before the first run.
void  attract_seed(uint32_t seed);
// The chamber list has changed (the editor saved): counted again.
void  attract_recount(void);
// The next run, from its start. The caller remeshes the level.
void  attract_begin(game_t* g);
// One frame of `dt` seconds; true if the level must be remeshed (a
// portal, gel, or the next run begun).
bool  attract_update(game_t* g, float dt);
// How lit the run is: 0 black .. 1 full, fading in as it starts and out
// as it ends.
float attract_lit(void);
// For the tests: runs begun, and how many of them reached the exit.
int   attract_runs(void);
int   attract_exits(void);
