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
    SND_TICK,         // a pedestal button's timer
    SND_DROPPER,      // a new cube out of a dropper
    SND_BURN,         // into a laser beam
    SND_BOUNCE,       // off blue gel
    SND_PELLET,       // an energy pellet fired, or bouncing
    SND_CAUGHT,       // a receiver caught a pellet
    SND_CRUSH,        // a crusher hits the floor
    SND_TURRET_SPOT,  // a turret sees you
    SND_TURRET_SHOT,  // one burst of a turret's fire
    SND_TOPPLE,       // a turret knocked over
    SND_DING,         // a lift's doors open: it is there
    SND_HUM,          // a lift on the move
    SND_COUNT,
} sound_t;

void sound_init(void);
void sound_play(sound_t s);
// The sounds for a game step's events (PL_EV_* | GAME_EV_*).
void sound_events(int ev);
// Gates, from the settings.
void sound_set_music(bool on);
// The music from its first bar again, as when the game starts.
void sound_restart_music(void);
void sound_set_effects(bool on);
void sound_set_voice(bool on);

// GLaDOS says a line (speech.c, SAM's voice), a sentence at a time; a new
// line cuts the old one off. NULL just stops.
void sound_say(char const* line);
// GLaDOS and the turrets, quiet: leaving the game for its title screen.
void sound_hush(void);
bool sound_saying(void);
// The line a turret said last (NULL before the first), and how many it
// has said: a new count is a new line, to show as a subtitle.
int  sound_turret_said(char const** line);
// Once a frame: the next sentence, when the last has been said.
void sound_update(void);
