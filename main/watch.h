#pragma once
// Esc -> Watch a recording: a recording from the card (recording.h) played
// back chamber by chamber, each script stepped with each frame's own time,
// as a player at the keys would be, and timed by the game's clock: the run
// as this badge plays it -- the TAS's times, on the hardware. The times go
// to /sd/portals/<recording>-times.txt.

#include <stdbool.h>
#include "hud.h"

// Watch recording `id`; afterwards back to the title, or else to chamber
// `back` (its id) -- fresh, and saving nothing, so Continue stays where
// play was. False, with a message up saying why, if it could not be read.
bool watch_start(char const* id, bool to_title, char const* back);
bool watch_on(void);
// Esc: stopped, and back.
void watch_stop(void);
// Whether Esc stopped a watch just now: the built-in keyboard sends Esc
// twice, and the second is not the menu's.
bool watch_just_stopped(void);
void watch_update(float dt);
// The timer, while watching.
bool watch_timer(hud_timer_t* out);
