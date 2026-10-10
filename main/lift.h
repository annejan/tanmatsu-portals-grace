#pragma once
// The lift between chambers: up out of one, in the dark the next loaded,
// down onto its start -- no stop between them. The player stands still in
// it (no physics steps: the run ended on the exit, and the next begins at
// the foot of the lift), a cube carried comes up too. Pure C: main.c plays
// it between chambers, watch.c between a recording's; the host tests ride
// it.

#include <stdbool.h>
#include "game.h"

#define LIFT_S    1.1f  // each way
#define LIFT_RISE 2.5f  // at most this far up from the feet

typedef enum {
    LIFT_NONE,
    LIFT_UP,    // rising out of the chamber, the lights going down
    LIFT_DOWN,  // falling into the next, the lights coming up
} lift_phase_t;

typedef struct {
    lift_phase_t phase;
    float        t;              // seconds into this way
    float        x, z;           // the shaft
    float        from, to;       // the player's feet, this way
} lift_t;

enum {
    LIFT_EV_TOP    = 1,  // at the top, in the dark: load the next, then lift_down()
    LIFT_EV_LANDED = 2,  // down: play on
};

// Up from where the player stands in `g`.
void  lift_up(lift_t* l, game_t const* g);
// Down onto where the player stands in `g` (the next chamber, just loaded):
// the player is put at the top of the shaft.
void  lift_down(lift_t* l, game_t* g);
// Its next `dt` seconds, the player (and a cube carried) moved in `g`;
// with `wait`, it stays at the top, in the dark, until it is false.
// LIFT_EV_*.
int   lift_step(lift_t* l, game_t* g, float dt, bool wait);
bool  lift_on(lift_t const* l);
// How lit the screen is: 1 at the foot of the shaft, 0 at the top.
float lift_lit(lift_t const* l);
// The tube to draw, feet to above the eye at the top (render_set_lift).
void  lift_tube(lift_t const* l, float* x, float* y0, float* y1, float* z);
// How far up from feet at `p` there is room for the eye to rise, at most
// `most`: to a hand under the first solid cell above.
float lift_room(level_t const* lv, vec3_t p, float most);
