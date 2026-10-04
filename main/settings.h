#pragma once
// The player's settings, kept in NVS so they outlast a reboot. The key
// bindings are kept by the engine (se_bindings, same namespace).

#include <stdbool.h>

#define SETTINGS_NVS_NAMESPACE "portals"

void settings_load(void);

bool settings_gyro(void);
void settings_set_gyro(bool on);
bool settings_half_res(void);
void settings_set_half_res(bool on);
bool settings_music(void);
void settings_set_music(bool on);
bool settings_effects(void);
void settings_set_effects(bool on);
int  settings_portal_depth(void);
void settings_set_portal_depth(int depth);
