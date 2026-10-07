#pragma once
// A camera for a scripted run, worked out ahead: what the films
// (tests/host_movie.c) and the title screen's attract mode (attract.c)
// look through instead of the player's own view.
//
// A script turns the player in an instant, and fires the same step: on
// screen the portal would appear before the view swung to it. So a run is
// played twice. The first time logs the view at every step; from that the
// camera is worked out: it is on the aim a moment before every shot,
// pickup, put-down and press, it looks where it walks rather than at the
// floor, it eases into every turn from both sides, and it sways a little,
// as a head does. Through a portal it cuts, as the view does.

#include <stdbool.h>

// A step of the first run, as the player was after it.
typedef struct {
    float yaw, pitch;
    float vx, vy, vz;
    bool  ground;
    bool  held;  // carrying something: it is held in the view, so the view stays
    int   ev;    // the step's PL_EV_* | GAME_EV_*
} cine_view_t;

// The camera for `n` steps of `tick` seconds logged in `log`: yaw and
// pitch per step into `cam`. `want` and `aimed` are the caller's scratch,
// `n` long like `cam`.
void cine_plan(cine_view_t const* log, int n, float tick, float (*cam)[2], float (*want)[2], bool* aimed);
