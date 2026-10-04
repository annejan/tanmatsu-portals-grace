#pragma once
// Scripted runs through the chambers: shoot here, walk there. Each is a
// pure function of time -- demo_eval() replays the script from its start
// in fixed steps -- so the badge (device tests, main/testkit) and the
// host (tests/host_*.c) see the same instant the same way.

#include "level.h"
#include "player.h"
#include "portal.h"

typedef struct {
    level_t  lv;
    player_t pl;
    portal_t portals[2];
    int      events;  // player_event_t, all of them so far
} demo_state_t;

int         demo_count(void);
int         demo_find(char const* name);  // -1 if none
char const* demo_name(int i);
float       demo_duration(int i);
// The state `t` seconds into demo `i`.
void demo_eval(int i, float t, demo_state_t* out);
