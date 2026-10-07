#pragma once
// The title screen, the pause menu and their screens, on the engine's
// se_ui list menus: title, pause, settings, controls (rebinding through
// se_ui_capture_key), chamber select and recordings.

#include <stdbool.h>
#include "bsp/input.h"
#include "pax_gfx.h"
#include "recording.h"

typedef enum {
    MENU_CMD_NONE = 0,
    MENU_CMD_RESUME,
    MENU_CMD_RESTART,
    MENU_CMD_CHAMBER,   // load menu_cmd_t.chamber
    MENU_CMD_NEW_GAME,  // the first chamber, as the game begins
    MENU_CMD_EDITOR,    // open the chamber editor on menu_cmd_t.chamber
    MENU_CMD_WATCH,     // play back the recording menu_cmd_t.recording
    MENU_CMD_TITLE,     // back to the title screen
    MENU_CMD_QUIT,
} menu_cmd_kind_t;

typedef struct {
    menu_cmd_kind_t kind;
    int             chamber;
    char const*     recording;  // its id: until the menu next opens
} menu_cmd_t;

void       menu_open(int current_chamber);
// The title screen, the game's first: Continue goes back to
// `continue_chamber`, or there is no Continue if it is -1.
void       menu_title(int continue_chamber);
// Whether the title screen, or a screen opened from it, is up.
bool       menu_on_title(void);
// Whether the title screen itself is up.
bool       menu_title_shown(void);
// Whatever is up, closed: a device test is taking over the screen.
void       menu_close(void);
bool       menu_active(void);
// Every input event while the menu is open; also Esc, to open it.
void       menu_event(bsp_input_event_t const* ev);
// True if `ev` is the key that opens the menu.
bool       menu_is_open_key(bsp_input_event_t const* ev);
// Once a frame while open: act on the keys this frame brought.
menu_cmd_t menu_update(void);
void       menu_draw(pax_buf_t* fb);
