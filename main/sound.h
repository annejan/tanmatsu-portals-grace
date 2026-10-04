#pragma once
// Sound: short synthesised effects for what happens in a chamber, and
// calm procedural music. No samples on the SD card -- every effect is a
// few numbers (sound.c). Engine-side: runs on SynthEngine3D's mixer.

#include <stdbool.h>

typedef enum {
    SND_SHOT_BLUE,
    SND_SHOT_ORANGE,
    SND_SHOT_FAIL,  // the surface will not take a portal
    SND_TELEPORT,
    SND_LAND,
    SND_PICKUP,
    SND_DROP,
    SND_BUTTON_DOWN,
    SND_BUTTON_UP,
    SND_DOOR,
    SND_DEATH,
    SND_COMPLETE,
    SND_MENU,
    SND_FIZZLE,
    SND_LAUNCH,
    SND_COUNT,
} sound_t;

void sound_init(void);
void sound_play(sound_t s);
// The sounds for a game step's events (PL_EV_* | GAME_EV_*).
void sound_events(int ev);
// Gates, from the settings.
void sound_set_music(bool on);
void sound_set_effects(bool on);
