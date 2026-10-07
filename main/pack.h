#pragma once
// Story packs: chambers that belong together, played one after another as
// a story -- each a folder on the card, PACK_DIR/<pack>/, to share by
// copying it:
//
//   pack.txt      name: After hours
//                 author: annejan
//                 about: The tests are over. The testing is not.
//                 ending: You may go home now. You may not.
//                 chamber: overtime
//                 chamber: momentum
//   overtime.txt  the chambers (chambers/README.md), in the order pack.txt
//   momentum.txt  gives -- or, without "chamber:" lines, by file name.
//   replays/      recordings to watch with it (Watch a recording lists them),
//                 naming its chambers by their files alone if they like.
//
// Each chamber joins the chamber list (chamber.h) as "<pack>/<file>", after
// the chambers played in order; the title screen's Stories lists the packs.
//
// A story told from a desk (review.h) says so, and gives its rounds:
//
//   frame: desk        played from an Aperture terminal's desk
//   round: pressure    review rounds, in order: chambers with review keys
//   round: momentum
//   outro: walkout     the ending's scene
//   chamber: needs-update
//
// A build that knows rounds plays them and passes over the "chamber:" lines;
// an older one knows only those, and plays a room that says to update.

#include <stdbool.h>
#include "chamber.h"

#define PACK_DIR      "/sd/portals/dlc"
#define PACK_MAX      16
#define PACK_CHAMBERS 24

typedef struct {
    char id[32];  // its folder's name
    char name[48];
    char author[32];
    char about[96];
    char ending[160];  // GLaDOS's, when its last chamber is done
    int  n;
    int  chamber[PACK_CHAMBERS];  // in the chamber list, in order: a desk story's rounds
    bool desk;                    // frame: desk -- its chambers are review rounds
    char outro[CHAMBER_ID_N];     // the outro's file, "" for none
    int  outro_chamber;           // ... in the chamber list, or -1
} pack_t;

// Read every pack in `dir`, adding their chambers to the chamber list
// (packs read before are forgotten). Returns how many packs.
int           pack_load(char const* dir);
int           pack_count(void);
pack_t const* pack_get(int i);
// The pack chamber `chamber` is in, and its place in it; -1 if none.
int           pack_of(int chamber, int* at);
// Parse a pack.txt's text into `p` (its chambers by file id, in `files`:
// a desk story's rounds); false if it names no chamber. For the tests too.
bool          pack_parse(char const* text, pack_t* p, char files[][CHAMBER_ID_N], int* n_files);
