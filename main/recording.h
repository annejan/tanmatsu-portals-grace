#pragma once
// Recordings: a run through chambers, kept as a script for each, which
// the game plays back as the badge's frames come (main.c: Esc -> Watch a
// recording). A recording is a text file in RECORDING_DIR:
//
//   name: TAS, Portals 0.10.2
//   step: 0.1
//   chamber: 01-gap
//   shoot blue 1.0 1.9 3.5
//   ...
//   chamber: 02-ledge
//   ...
//
// each chamber named by its id (a chamber file's name, the card's too),
// its steps a solution's (chambers/README.md). With `step:`, the game is
// stepped in exactly that many seconds at a time, however fast the badge
// draws: a run made so plays out the same on every badge, and on the PC
// (a tool-assisted run is). Without it, each frame is one step of its own
// length, as a player at the keys. tools/tas.py writes the TAS as one.

#include <stdbool.h>
#include <stddef.h>
#include "chamber.h"
#include "script.h"

#define RECORDING_DIR    "/sd/portals/recordings"
#define RECORDING_NAME_N 48
#define RECORDING_MAX    CHAMBER_MAX  // chambers in one
#define RECORDINGS_MAX   16           // recordings listed

typedef struct {
    char   id[CHAMBER_ID_N];
    step_t steps[SCRIPT_MAX_STEPS];  // ending with OP_END
} recording_run_t;

typedef struct {
    char            name[RECORDING_NAME_N];
    float           step;  // s a step, or 0: one a frame
    int             n;
    recording_run_t runs[RECORDING_MAX];
} recording_t;

// False, and why in err, if it is not one.
bool recording_parse(char const* text, recording_t* r, char* err, size_t err_n);
// The recordings in `dir`: their files' ids (sorted) and names, at most
// RECORDINGS_MAX. Returns how many.
int  recording_list(char const* dir, char ids[][CHAMBER_ID_N], char names[][RECORDING_NAME_N]);
// Read and parse `dir`/`id`.txt.
bool recording_load(char const* dir, char const* id, recording_t* r, char* err, size_t err_n);
