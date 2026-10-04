#pragma once
// A chamber being edited: the characters of its file (chambers/README.md),
// one per cell, and its header. Edited as characters, so what is saved and
// play-tested always goes through the one parser (chamber.c). Pure C.

#include <stdbool.h>
#include <stddef.h>
#include "level.h"

typedef struct {
    char  id[32];    // file name without .txt
    char  name[32];
    char  hint[80];
    int   w, h, d;
    float yaw;       // the start's facing, radians
    char  grid[LV_MAX_H][LV_MAX_D][LV_MAX_W];  // [y][z][x]
    char  solution[2048];  // the file's solution section, kept as it was
} draft_t;

// A box of metal with white walls, a white floor and air inside, the
// start in the middle.
void draft_new(draft_t* d, char const* id, int w, int h, int dep);
bool draft_from_text(draft_t* d, char const* id, char const* text);
// The file. Returns its length, or -1 if `out` was too small.
int  draft_text(draft_t const* d, char* out, size_t n);
// Parse it into a level, as the game would. On failure the parser's
// message in `err`.
bool draft_level(draft_t const* d, level_t* lv, char* err, size_t err_n);

void draft_resize(draft_t* d, int w, int h, int dep);
// Set one cell. There is one start: painting 'S' moves it.
void draft_paint(draft_t* d, int x, int y, int z, char ch);
char draft_get(draft_t const* d, int x, int y, int z);
