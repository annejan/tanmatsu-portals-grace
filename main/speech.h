#pragma once
// GLaDOS, played by SAM, the C64's Software Automatic Mouth
// (third_party/sam): a line of text to 8-bit sound at 22050 Hz, a piece at
// a time. Pure C: the host tests speak too.

#include <stdbool.h>
#include <stdint.h>

#define SPEECH_RATE 22050  // Hz, unsigned 8-bit mono
#define SPEECH_MAX  96     // characters in one piece: SAM's buffers hold that, phonemes and all

// Split a line into pieces SAM can say: its sentences, and a long one at
// its commas. Returns how many (at most `max`).
int speech_split(char const* text, char out[][SPEECH_MAX], int max);

// Who speaks: GLaDOS in SAM's own voice, a turret higher and smaller.
typedef enum {
    SPEECH_GLADOS = 0,
    SPEECH_TURRET,
} speech_voice_t;

// What turrets say: when one sees you, and when one is knocked over.
extern char const* const speech_turret_spot[];
extern char const* const speech_turret_down[];
#define SPEECH_TURRET_LINES 3

// Render one piece in a voice. Returns its sound, *n samples long --
// SAM's own buffer, valid until the next speech_render() or speech_free()
// -- or NULL if SAM could not make anything of it.
uint8_t const* speech_render(char const* piece, int voice, int* n);
void           speech_free(void);
