#pragma once
// Lifts between chambers, as places: every chamber has a lift station at
// its start and one at its exit, worked out from the level as it loads (no
// glyph, nothing in the file). A station is a glowing collar on the floor,
// rails up to a hatch in the ceiling, a halo ring whose lower edge is the
// door light, and glass where a wall stands behind it; its open sides are
// doors, folded up into the halo while it waits.
//
// The run ends on the exit exactly as it always has (PL_EV_EXIT); what
// follows is the lift's: the player glides to the middle and turns to look
// back out, the doors drop shut -- the side they came in by last -- and the
// car rises through the chamber just solved, up through the hatch into a
// lit shaft as the screen goes dark. In the next chamber it comes down out
// of the hatch onto the start, the doors open, and play goes on. Nothing
// of it collides, and nothing is written into the game but the player's
// place and view while riding -- put back exactly as loaded before the
// first step: TAS, ghost and recording times are the same with lifts or
// without. Pure C: main.c rides it between chambers, watch.c between a
// recording's; the host tests check every chamber's stations and rides.

#include <stdbool.h>
#include <stdint.h>
#include "game.h"

#define LIFT_SIDES 8  // a station is an eight-sided ellipse; side i spans i*45 .. (i+1)*45 degrees, 0 = +z

typedef struct {
    bool    on;
    float   x, z, y;  // its middle; y: the floor, the player's feet
    float   rx, rz;   // half its width along x and z, 0.46 .. 0.9 m
    float   ceil;     // the first solid above the floor, m up from it
    float   cb;       // the halo's lower edge: min(2.2, ceil - 0.15), never under 1.85
    float   mouth;    // where the shaft begins: the ceiling, or 6 m up under a high one
    float   rise;     // how far the car goes up (and comes down)
    bool    hatch;    // a hatch in the ceiling (not over 8 m)
    uint8_t fixed;    // bit i: side i is glass that stays, a wall right behind it
} lift_site_t;

typedef struct {
    lift_site_t exit, start;
} lift_sites_t;

typedef enum {
    LIFT_NONE,
    LIFT_SHUT,  // to the middle, turned to look out, the doors dropping
    LIFT_HOLD,  // shut, waiting: a message to read, or a pack's end
    LIFT_UP,    // rising out of the chamber, into the dark
    LIFT_DOWN,  // coming down into the next
    LIFT_OPEN,  // the doors going up
} lift_phase_t;

typedef struct {
    lift_phase_t phase;
    float        t;          // seconds into this phase
    lift_site_t  car;        // the station it rides from, or to
    bool         car_exit;   // ... the chamber's exit (else its start)
    vec3_t       p0, v0;     // the glide in: from where, how fast
    float        tg, td0;    // the glide's length; when the doors start
    float        phi;        // the side the player came in by (angle, as the sides)
    float        yaw0, dyaw, pitch0;
    bool         own_look;   // the player looks round themselves: no turn for them
    float        dy;         // the car above its floor
    float        closed[LIFT_SIDES];  // each door: 0 open .. 1 shut
    bool         stay;       // shut, and no further (a pack's last chamber)
    bool         shut;       // its doors are all shut (LIFT_EV_SHUT said)
    int          hide_cube;  // a cube carried in: gone in the grill, -1 none
    player_t     snap;       // the player as the next chamber loaded: put back on landing
    int          pending;    // LIFT_EV_* to say on the next step
} lift_t;

enum {
    LIFT_EV_TOP    = 1,   // at the top, in the dark: load the next, then lift_down()
    LIFT_EV_LANDED = 2,   // down and open: play on
    LIFT_EV_SHUT   = 4,   // the doors shut
    LIFT_EV_DOOR   = 8,   // the doors begin to move
    LIFT_EV_LAND   = 16,  // the car touches down
    LIFT_EV_FIZZLE = 32,  // a cube carried in fizzled
    LIFT_EV_RIDE   = 64,  // the car begins to move
};

// Level `lv`'s stations, as it starts (spawn, exit cells): either may be
// off (no exit, no room).
void  lift_sites(level_t const* lv, lift_sites_t* out);
// A station fitted round point (x, y, z): for an exit too far from its own.
bool  lift_site_at(level_t const* lv, float x, float y, float z, lift_site_t* out);
// The station for a run that ended at `p`: the exit's, if it is within
// reach (3 m, on its floor), else one fitted where the player stands.
// False if there is no room for one there either.
bool  lift_exit_for(level_t const* lv, lift_sites_t const* s, vec3_t p, lift_site_t* out);
// The run ended on the exit: into station `exit`; with `stay`, it shuts
// and goes no further.
void  lift_enter(lift_t* l, game_t* g, lift_site_t const* exit, bool stay);
// The next `dt` seconds of it: the player moved and turned in `g`. With
// `wait` it stays shut until that is false; `dyaw`, `dpitch` are the
// player's own look (they steer while riding); `speed` (1, or more to
// hurry it) runs the ride faster. LIFT_EV_*.
int   lift_step(lift_t* l, game_t* g, float dt, bool wait, float dyaw, float dpitch, float speed);
// At the top: down into the chamber just loaded in `g`, at its start
// station `start`. The player as loaded is kept, and put back on landing.
void  lift_down(lift_t* l, game_t* g, lift_site_t const* start);
bool  lift_on(lift_t const* l);
// How lit the screen is: dark as the eye goes up into the shaft.
float lift_lit(lift_t const* l);

// What to draw (render_set_lift): up to two stations.
typedef struct {
    lift_site_t site;
    float       dy;                  // the car above its floor
    float       closed[LIFT_SIDES];  // each side's door, 0 .. 1 (fixed glass not counted)
    bool        shut_light;          // the door light orange (shut), else blue
    bool        shaft;               // riding: the shaft above the mouth
    bool        exit;                // the exit's station (else the start's)
    bool        empty;               // its car gone up into the hatch: collar, rails and hatch only
} lift_station_view_t;

typedef struct {
    int                 n;
    lift_station_view_t st[2];
    int                 hide_cube;  // a cube not to draw, or -1
} lift_view_t;

// The start station's car once the player has walked out of it: its doors
// shut, and it goes back up into its hatch. Only to look at.
typedef struct {
    float away;  // seconds the player has been clear of it
    float t;     // seconds since it began to go, or < 0 not yet
    float dy;    // the car above its floor
    float closed;
} lift_depart_t;

// Back in place, its doors open: a chamber (re)loaded.
void  lift_depart_reset(lift_depart_t* d);
// The next `dt` seconds of it, the player's feet at `p`: LIFT_EV_DOOR as its
// doors begin to shut, LIFT_EV_RIDE as it begins to rise.
int   lift_depart_step(lift_depart_t* d, lift_site_t const* start, vec3_t p, float dt);

// `l` riding (or not) among the stations `s` of the chamber in play, the
// start's car gone as `d` says (NULL: there).
void  lift_view(lift_t const* l, lift_sites_t const* s, lift_depart_t const* d, lift_view_t* out);
