#pragma once
// Scripted runs through the chambers: shoot here, walk there. Each is a
// pure function of time -- demo_eval() replays the script from its start
// in fixed steps -- so the badge (device tests, main/testkit) and the
// host (tests/host_*.c) see the same instant the same way.

#include <stdbool.h>
#include "game.h"

typedef struct {
    game_t g;
    int    events;  // PL_EV_* | GAME_EV_*, all of them so far
} demo_state_t;

int         demo_count(void);
int         demo_find(char const* name);  // -1 if none
char const* demo_name(int i);
float       demo_duration(int i);
// The chamber a demo plays in; whether it has a script (a chamber file's
// solution may be missing).
int         demo_chamber(int i);
bool        demo_has_solution(int i);
// The state `t` seconds into demo `i`, stepped at 50 fps: what the
// device tests and the screenshots replay.
void        demo_eval(int i, float t, demo_state_t* out);
// The same stepped at `dt`, as the badge's frame rate would: the tests
// check every solution at 15 to 50 fps.
void        demo_eval_dt(int i, float t, float dt, demo_state_t* out);
// The same, calling `tick` after every step with the state and that
// step's events -- its shots and Use included: what a recording needs
// (tests/host_movie.c). With `pace`, the player stands still that long
// before each shot and Use, as a person would look first; a solution timed
// to the second may then fail.
typedef void (*demo_tick_fn)(game_t const* g, int events, float now, void* ctx);
void demo_run(int i, float t, float dt, demo_state_t* out, demo_tick_fn tick, void* ctx, float pace);
