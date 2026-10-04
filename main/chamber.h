#pragma once
// Chambers as text files: chambers/*.txt, built into the app, and any
// more the player puts in /sd/portals/chambers, which come after them.
// Pure C (stdio and dirent only) -- the host tests read the same files.
//
// The format (chambers/README.md has the whole of it):
//
//   name: 06  Two buttons        header lines, `key: value`
//   hint: Both doors need a cube.
//   size: 12 6 14                cells across (x), up (y), deep (z)
//   facing: north                north +z, east +x, south, west, or degrees
//
//   layer 1                      one per height y; a missing one is metal
//   ############                 a map from above: the top line is the
//   #S...C...a.#                 far side (highest z), x runs left to right
//   ...
//
//   solution                     optional: a scripted run that solves it
//   shoot blue 1.0 1.9 4.5
//   walk_to 12 4.5
//
//   // a comment, anywhere outside a layer's lines

#include <stdbool.h>
#include <stddef.h>
#include "level.h"
#include "script.h"

#define CHAMBER_MAX 40

typedef struct {
    char const* id;    // file name without .txt, e.g. "01-gap"
    char const* text;
} chamber_builtin_t;

// The built-in chambers, generated from chambers/*.txt at build time
// (tools/embed_chambers.py).
extern chamber_builtin_t const chamber_builtins[];
extern int const               chamber_builtin_count;

// Parse a chamber file. On failure false, with a message ("line 12: ...")
// in `err`. `steps` may be NULL to skip the solution.
bool chamber_parse(char const* text, level_t* lv, step_t* steps, int* n_steps, char* err, size_t err_n);

// The list: built-ins first, then what chamber_load_dir() added.
int         chamber_count(void);
char const* chamber_id(int i);
bool        chamber_build(int i, level_t* lv, step_t* steps, int* n_steps);
// Add every *.txt in `dir` that parses, in name order. Returns how many;
// a file that does not parse is skipped, and logged.
int chamber_load_dir(char const* dir);
