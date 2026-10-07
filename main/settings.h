#pragma once
// The player's settings, kept in NVS so they outlast a reboot. The key
// bindings are kept by the engine (se_bindings, same namespace).

#include <stdbool.h>

#define SETTINGS_NVS_NAMESPACE "portals"

void settings_load(void);

bool        settings_gyro(void);
void        settings_set_gyro(bool on);
bool        settings_half_res(void);
void        settings_set_half_res(bool on);
bool        settings_music(void);
void        settings_set_music(bool on);
bool        settings_effects(void);
void        settings_set_effects(bool on);
bool        settings_voice(void);  // GLaDOS reads the story lines out
void        settings_set_voice(bool on);
bool        settings_leds(void);  // the portals on user LEDs A and B
void        settings_set_leds(bool on);
int         settings_portal_depth(void);
void        settings_set_portal_depth(int depth);
bool        settings_frame_times(void);  // a recording's summary shows the frame rate in each chamber
void        settings_set_frame_times(bool on);
// The chamber play was last in, by its id ("" if none yet): Continue, on
// the title screen. Written only when it changes.
char const* settings_chamber(void);
void        settings_set_chamber(char const* id);
