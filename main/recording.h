#pragma once
// Recordings: a run through chambers, kept as a script for each, which
// the game plays back as the badge's frames come (watch.c: Esc -> Watch a
// recording). A recording is a text file in RECORDING_DIR:
//
//   name: TAS, Portals 0.10.2
//   version: 0.10.2 v0.10.2-3-g1a2b3c4d5e
//   chamber: 01-gap
//   shoot blue 1.0 1.9 3.5
//   ...
//   chamber: 02-ledge
//   ...
//
// the version the game's it was made for, the release and the build (on
// another, a run may go otherwise; a file giving only the release is
// matched on that), each chamber named by its id (a chamber file's name, the
// card's too), its steps a solution's (chambers/README.md). tools/tas.py writes the TAS
// as one.
//
// Or, for a run played on the badge and recorded there, its frames:
//
//   chamber: 01-gap
//   frames: 2
//   33012 1000 0 -2133 0 0
//   32950 1000 0 0 0 1
//
// a line each: the frame's time in microseconds, forward and strafe in
// thousandths (-1000 .. 1000), the turn and the tilt in microradians, and
// the keys pressed (REC_*). The game is played on these whole numbers as
// the frames are recorded, so a playback of them goes the same way.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "chamber.h"
#include "game.h"
#include "script.h"

#define RECORDING_DIR    "/sd/portals/recordings"
#define RECORDING_NAME_N 48
#define RECORDING_MAX    CHAMBER_MAX  // chambers in one
#define RECORDINGS_MAX   32           // recordings listed

// A frame of play, as recorded.
typedef struct {
    uint32_t dt_us;
    int32_t  dyaw_urad, dpitch_urad;
    int16_t  fwd, strafe;  // thousandths
    uint8_t  keys;         // REC_*
} recording_frame_t;

enum {
    REC_JUMP   = 1,  // held
    REC_BLUE   = 2,  // pressed this frame
    REC_ORANGE = 4,
    REC_USE    = 8,
};

typedef struct {
    char   id[CHAMBER_ID_N];
    step_t steps[SCRIPT_MAX_STEPS];  // ending with OP_END; or, recorded:
    int    frame0, frames;           // frames > 0: recording_t.frame[frame0 ..]
} recording_run_t;

typedef struct {
    char               name[RECORDING_NAME_N];
    char               version[48];  // the game's it was made for ("version:"), or ""
    int                n;
    recording_run_t    runs[RECORDING_MAX];
    recording_frame_t* frame;  // the recorded runs' frames, all of them (malloc'd)
    int                n_frames;
} recording_t;

// The frame `dt` and `in` make, in whole numbers.
recording_frame_t recording_frame(float dt, game_input_t const* in);
// The input and the time (returned) of a frame: what the game steps on,
// recording and playing back alike.
float             recording_frame_input(recording_frame_t const* f, game_input_t* in);

// False, and why in err, if it is not one. Frees what `r` held.
bool recording_parse(char const* text, recording_t* r, char* err, size_t err_n);
void recording_free(recording_t* r);

// Recording a run: the chambers done so far, each with its frames, and
// the one being played.
typedef struct {
    recording_t r;     // its runs: those done (n), and the one being played (open)
    bool        open;  // r.runs[r.n] is being played
    int         cap;   // frames r.frame has room for
} recording_capture_t;

void recording_capture_start(recording_capture_t* c);
// A chamber begins: its frames from now (one being played is dropped).
void recording_capture_chamber(recording_capture_t* c, char const* id);
// The chamber being played starts over: its frames so far are dropped.
void recording_capture_again(recording_capture_t* c);
// A frame of the chamber being played (none open: dropped). False if out
// of memory.
bool recording_capture_frame(recording_capture_t* c, recording_frame_t const* f);
// Its exit reached: kept.
void recording_capture_done(recording_capture_t* c);
// The chambers kept, written to `path` under `name`, recorded on the
// game's `version`. False if none, or it could not be written.
bool recording_capture_write(recording_capture_t const* c, char const* path, char const* name, char const* version);
void recording_capture_free(recording_capture_t* c);
// The recordings in `dir`: their files' ids (sorted) and names, at most
// RECORDINGS_MAX. Returns how many.
int  recording_list(char const* dir, char ids[][CHAMBER_ID_N], char names[][RECORDING_NAME_N]);
// Read and parse `dir`/`id`.txt.
bool recording_load(char const* dir, char const* id, recording_t* r, char* err, size_t err_n);
