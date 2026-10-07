#pragma once
// Scripted runs through the chambers: shoot here, walk there. Each is a
// pure function of time -- demo_eval() replays the script from its start
// in fixed steps -- so the badge (device tests, main/testkit) and the
// host (tests/host_*.c) see the same instant the same way.

#include <stdbool.h>
#include "game.h"
#include "script.h"

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

// A script played a step at a time, at whatever pace the caller's frames
// come: the badge's TAS mode (watch.c) steps one with each frame's own
// time, as a player at the keys would; demo_run() is the same at a fixed
// step.
typedef struct {
    step_t const* steps;
    int           k;        // the step running
    float         in_step;  // seconds into it
    bool          walked;   // OP_STEP_OFF: has been walking on the ground
    bool          jump;     // OP_JUMP: on the next step
    int           paced;    // the act step last paused before ...
    float         hold;     // ... and how much of that pause is left
} demo_player_t;

// Demo i's script, until the next call (the chamber's solution, parsed).
step_t const* demo_steps(int i);
// `steps` ends with OP_END, and must outlive the playing.
void          demo_player_start(demo_player_t* p, step_t const* steps);
// One step of `dt` seconds: the script's input for it, and game_step();
// returns the step's events, the script's shots and Use included.
int           demo_player_step(demo_player_t* p, game_t* g, float dt, float pace);
// Whether the script has run out (the player stands still from then on).
bool          demo_player_done(demo_player_t const* p);
