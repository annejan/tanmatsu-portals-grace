#pragma once
// The chamber editor on the badge: a map of one layer at a time, painted
// with the characters of the chamber file format. Saves to the SD card,
// and play-tests through the game.

#include <stdbool.h>
#include "bsp/input.h"
#include "level.h"
#include "pax_gfx.h"

typedef enum {
    EDITOR_CMD_NONE = 0,
    EDITOR_CMD_PLAYTEST,  // editor_level() holds the chamber to play
    EDITOR_CMD_QUIT,
} editor_cmd_t;

// Edit chamber `index` from the list, or a new empty one for -1. A
// built-in chamber is saved as a copy, my-<its id>.txt.
void editor_open(int index, char const* chamber_dir);
bool editor_active(void);
void editor_close(void);
void editor_event(bsp_input_event_t const* ev);
editor_cmd_t editor_update(float dt);
void         editor_draw(pax_buf_t* fb);
// The chamber as it stands, parsed: what a play-test plays.
level_t const* editor_level(void);
// Back from a play-test.
void editor_resume(void);
