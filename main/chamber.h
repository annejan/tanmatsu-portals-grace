#pragma once
// Chambers as text files: chambers/*.txt, built into the app, and any
// more the player puts in /sd/portals/chambers, which come after them.
// Pure C -- the host tests read the same files. The directory is listed
// with dirent on the host and FatFs on the badge (graceloader exports no
// opendir).
//
// The format (chambers/README.md has the whole of it):
//
//   name: 06  Two buttons        header lines, `key: value`
//   hint: Both doors need a cube.
//   size: 12 6 14                cells across (x), up (y), deep (z)
//   facing: north                north +z, east +x, south, west, or degrees
//   story: Words, as it starts.  optional; also `timer: 4`, a pedestal
//                                button's seconds
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
#include <stdint.h>
#include "level.h"
#include "script.h"

#define CHAMBER_MAX      40
#define CHAMBER_ID_N     64             // an id is its file's name without .txt
#define CHAMBER_FILE_MAX (1024 * 1024)  // a chamber file at most: a 128 x 32 x 128 one is about 540 KB

typedef struct {
    char const* id;  // file name without .txt, e.g. "01-gap"
    char const* text;
} chamber_builtin_t;

// The built-in chambers, generated from chambers/*.txt at build time
// (tools/embed_chambers.py).
extern chamber_builtin_t const chamber_builtins[];
extern int const               chamber_builtin_count;

// Parse a chamber file. On failure false, with a message ("line 12: ...")
// in `err`. `steps` may be NULL to skip the solution.
bool chamber_parse(char const* text, level_t* lv, step_t* steps, int* n_steps, char* err, size_t err_n);
// A script on its own, as a solution is written (a "solution" line may
// head it): tas/NN-name.txt, a route for the chamber of that name.
bool chamber_parse_steps(char const* text, step_t* steps, int* n_steps, char* err, size_t err_n);

// Write `lv` out in the file format, solution and all (`steps` may be
// NULL). Returns the length, or -1 if `out` was too small.
int chamber_write(level_t const* lv, step_t const* steps, int n_steps, char* out, size_t out_n);

// The map's characters: one table, which the parser, both writers and
// the editor all go by, and one meaning each (the host tests check that no
// two share a character or an editor key). Doors are a-h, and the buttons
// that open them the digits 1-8: button 1 opens door a, 2 opens b, and so
// on; a door opens while ALL its buttons are down. Older files wrote
// buttons A, B, D; those still read, as 1, 2 and 4 (C was never possible:
// it is a cube).
typedef enum {
    GLYPH_CELL,          // a cell of `mat`, nothing more
    GLYPH_PLATE,         // a faith plate (MAT_JUMP), paired with a target
    GLYPH_TARGET,        // where a faith plate lands what it throws
    GLYPH_START,         // the player's start
    GLYPH_CUBE,          // a cube, in the cell where it starts
    GLYPH_PLATFORM,      // a cell of the moving platform's box
    GLYPH_PLATFORM_END,  // the cell the platform's lowest corner goes to
    GLYPH_DOOR,          // a cell of door `link`
    GLYPH_BUTTON,        // a button for door `link`, above a solid cell
    GLYPH_DROPPER,       // a ceiling hatch (MAT_DROPPER) with its own cube
    GLYPH_REFLECT,       // a reflection cube, in the cell where it starts
    GLYPH_SPHERE,        // a sphere, in the cell where it starts
    GLYPH_TURRET,        // a turret, in the cell where it starts, facing the player's start
} glyph_kind_t;

typedef struct {
    char        ch;
    char const* what;    // in the legend and the editor
    uint8_t     kind;    // glyph_kind_t
    uint8_t     mat;     // the cell's material: air for what stands in one
    int8_t      link;    // a door's, or the door a button opens: 0 is a
    char        key;     // the editor's key for it, 0 for none
    uint32_t    colour;  // in the editor's map; 0 draws it as air
} chamber_glyph_t;

// The keys the editor keeps for itself (editor.c): no glyph may use one.
#define CHAMBER_EDITOR_KEYS "QEBRPFWASD"

extern chamber_glyph_t const chamber_legend[];
extern int const             chamber_legend_n;
// The glyph for character `c`, or NULL if it is not a map character.
chamber_glyph_t const*       chamber_glyph(char c);

static inline bool chamber_is_door(char c) {
    return c >= 'a' && c < 'a' + LV_MAX_DOORS;
}
static inline bool chamber_is_button(char c) {
    return c >= '1' && c < '1' + LV_MAX_BUTTONS;
}
static inline char chamber_door_char(int link) {
    return (char)('a' + link);
}
static inline char chamber_button_char(int link) {
    return (char)('1' + link);
}

// The character a cell is written as ('#', 'W', 'S', 'a', '1', ...).
char        chamber_cell_char(level_t const* lv, int x, int y, int z);
// "north", "east", "south" or "west" for a whole quarter turn, else NULL
// (written in degrees).
char const* chamber_facing_name(float yaw);

// The list: built-ins first, then what chamber_load_dir() added.
int         chamber_count(void);
char const* chamber_id(int i);
char const* chamber_text(int i);           // the file as it was read
int         chamber_find(char const* id);  // its index in the list, or -1
bool        chamber_build(int i, level_t* lv, step_t* steps, int* n_steps);
// Add every *.txt in `dir` that parses, in name order. Returns how many;
// a file that does not parse is skipped, and logged.
int         chamber_load_dir(char const* dir);
// The .txt files in `dir`, by name, at most `max`: names valid until the
// next listing.
int         chamber_list_dir(char const* dir, char const** names, int max);
// Delete a file on the card: Graceloader exports no remove(); through FatFs.
bool        chamber_remove_file(char const* path);
// Forget the chambers chamber_load_dir() added, and read `dir` again.
int         chamber_reload_dir(char const* dir);
// How many of the list are built in.
int         chamber_builtin_n(void);
