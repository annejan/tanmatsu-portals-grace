#pragma once
// What is drawn over the chamber: the crosshair, the chamber's name and
// hint, the keys, the story line typing out, a turret's last words, a
// message, a recording's timer -- and the title screen's few words.

#include <stdbool.h>
#include <stdint.h>
#include "game.h"
#include "pax_gfx.h"

#define HUD_MESSAGE_S 2.5f  // a message is up this long
#define HUD_MESSAGE_N 64    // and holds this many bytes, its 0 included

// A message, centred, for HUD_MESSAGE_S.
void        hud_message(char const* text);
bool        hud_message_up(void);
char const* hud_message_text(void);
// The story line types out again from its first letter.
void        hud_story_start(void);
// No message and no turret's words; one already said is not shown later.
void        hud_quiet(void);
// A frame of `dt`: the message's time, the story's, and a turret's words.
void        hud_tick(float dt);
// A frame on the title screen: only the message's time.
void        hud_tick_message(float dt);

// Watching a recording: its timer, in place of the keys' help.
typedef struct {
    char const* name;   // the recording's
    float       total;  // the run so far
    float       run;    // this chamber's
} hud_timer_t;

typedef struct {
    float              fps;
    int                render_ms;
    bool               half, gyro;  // as the settings are
    bool               test;        // a device test's shots: nothing that varies run to run
    hud_timer_t const* timer;       // or NULL
} hud_info_t;

void hud_draw(pax_buf_t* fb, game_t const* g, hud_info_t const* info);
// The title screen's: the chamber playing behind it, and the message
// (`message`: the title itself is up, not a screen opened from it).
void hud_draw_title(pax_buf_t* fb, game_t const* g, bool message);
// Every pixel of an RGB565 frame scaled by `lit` (0 .. 1), in place.
void hud_fade(pax_buf_t* buf, float lit);
