#pragma once
// Esc -> Watch a recording: a recording from the card (recording.h) played
// back chamber by chamber, each script stepped with each frame's own time,
// as a player at the keys would be, and timed by the game's clock: the run
// as this badge plays it -- the TAS's times, on the hardware. The times go
// to /sd/portals/<recording>-times.txt.

#include <stdbool.h>
#include "hud.h"
#include "lift.h"

// Watch recording `id` in `dir`; afterwards back to the title, or else to
// chamber `back` (its id) -- fresh, and saving nothing, so Continue stays
// where play was. A story pack's replay (`pack` its id, else "") may name
// the pack's chambers by their files alone. False, with a message up
// saying why, if it could not be read.
bool watch_start(char const* dir, char const* id, char const* pack, bool to_title, char const* back);
bool watch_on(void);
// The lift between its chambers (Settings -> Lifts), to draw; NULL if not watching.
lift_t const* watch_lift(void);
// Esc: stopped, and back.
void watch_stop(void);
// Whether Esc stopped a watch just now: the built-in keyboard sends Esc
// twice, and the second is not the menu's.
bool watch_just_stopped(void);
void watch_update(float dt);
// The timer, while watching.
bool watch_timer(hud_timer_t* out);
