#pragma once
// The desk (desk.h) on the badge: its keys from the keyboard, and the
// terminal drawn, 80 by 25 in the mono Hershey font.

#include "bsp/input.h"
#include "desk.h"
#include "pax_gfx.h"

// An input event at the desk: what the desk wants done (desk_action_t).
desk_action_t deskview_event(desk_t* k, bsp_input_event_t const* ev);
void          deskview_draw(pax_buf_t* fb, desk_t const* k);
