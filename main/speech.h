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

// Render one piece. Returns its sound, *n samples long -- SAM's own
// buffer, valid until the next speech_render() or speech_free() -- or
// NULL if SAM could not make anything of it.
uint8_t const* speech_render(char const* piece, int* n);
void           speech_free(void);
