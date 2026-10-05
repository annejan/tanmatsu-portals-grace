#include "speech.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "../third_party/sam/reciter.h"
#include "../third_party/sam/sam.h"

int debug = 0;  // SAM's own switch for its trace output: off

static char* s_last;  // SAM's buffer from the last render: SAM allocates a new one each time

void speech_free(void) {
    free(s_last);
    s_last = NULL;
}

// Copy text[from, to) into a piece, trimmed; false if nothing is left.
static bool piece(char const* text, int from, int to, char* out) {
    while (from < to && isspace((unsigned char)text[from])) from++;
    while (to > from && isspace((unsigned char)text[to - 1])) to--;
    if (to - from <= 0) return false;
    int const n = to - from < SPEECH_MAX - 1 ? to - from : SPEECH_MAX - 1;
    memcpy(out, text + from, (size_t)n);
    out[n] = '\0';
    return true;
}

int speech_split(char const* text, char out[][SPEECH_MAX], int max) {
    int       n = 0, start = 0, comma = -1;
    int const len = (int)strlen(text);
    for (int i = 0; i <= len && n < max; i++) {
        char const c   = text[i];
        bool const end = c == '\0' || c == '.' || c == '!' || c == '?';
        if (c == ',') comma = i;
        // Too long for one piece: break at the last comma, else at a space.
        if (!end && i - start >= SPEECH_MAX - 8) {
            int cut = comma > start ? comma + 1 : i;
            if (comma <= start)
                for (int j = i; j > start + 1; j--)
                    if (text[j - 1] == ' ') {
                        cut = j;
                        break;
                    }
            if (piece(text, start, cut, out[n])) n++;
            start = cut;
            comma = -1;
            continue;
        }
        if (end) {
            if (piece(text, start, c == '\0' ? i : i + 1, out[n])) n++;
            start = i + 1;
            comma = -1;
        }
    }
    return n;
}

uint8_t const* speech_render(char const* text, int* n) {
    *n = 0;
    // What the reciter reads: capitals, digits and the punctuation it knows;
    // anything else a pause. "[" ends its input.
    unsigned char in[256];
    int           k = 0;
    for (int i = 0; text[i] != '\0' && k < SPEECH_MAX; i++) {
        unsigned char const c = (unsigned char)toupper((unsigned char)text[i]);
        in[k++]               = (isalnum(c) || strchr(" .,?!'", c) != NULL) ? c : ' ';
    }
    in[k++] = '[';
    in[k]   = '\0';
    if (!TextToPhonemes(in)) return NULL;
    speech_free();
    SetInput((char*)in);
    if (!SAMMain()) {
        s_last = GetBuffer();
        return NULL;
    }
    s_last = GetBuffer();
    *n     = GetBufferLength() / 50;
    if (*n > SAM_BUFFER_BYTES) *n = SAM_BUFFER_BYTES;
    return (uint8_t const*)s_last;
}
