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
//
// A pack may name its music (sound.h), a tune and how to play it:
//
//   music: office             or hackfest, overtime, calm (the game's own)
//   music: overtime key=D bpm=80 seed=7
//
// Without it, the game's own tune -- but for the packs made with the game,
// which know theirs when a card's copy is older than the line. Every build
// before reads past a key it does not know: an older one plays its music.
//
// Rounds may branch: from a `route:` line on, rounds belong to that route,
// until the next `route:` or a `join` line; the player picks one route
// (the desk's ROUTE) and plays only its rounds, then the shared ones after.
//
//   choose: How do you get to Enschede?
//   route: train | By train | The intercity, and a change at Amersfoort.
//   round: platform-11b
//   route: bike | By bike | 160 km. How hard can it be?
//   round: headwind
//   join
//   round: arrival

#include <stdbool.h>
#include <stdint.h>
#include "chamber.h"

#define PACK_DIR      "/sd/portals/dlc"
#define PACK_MAX      16
#define PACK_CHAMBERS 24
#define PACK_ROUTES   4
#define PACK_SAVE_DIR "/sd/portals/saves"  // a desk story's save: PACK_SAVE_DIR/<pack>.txt

typedef struct {
    char id[16];  // what the player types: ROUTE TRAIN
    char title[32];
    char about[96];
} pack_route_t;

typedef struct {
    char id[32];      // its folder's name
    char folder[64];  // where its files are, under PACK_DIR: the id, or id/id (unzipped into a folder of its own)
    char name[48];
    char author[32];
    char about[96];
    char ending[160];  // GLaDOS's, when its last chamber is done
    int  n;
    int  chamber[PACK_CHAMBERS];  // in the chamber list, in order: a desk story's rounds
    bool desk;                    // frame: desk -- its chambers are review rounds
    char outro[CHAMBER_ID_N];     // the outro's file, "" for none
    int  outro_chamber;           // ... in the chamber list, or -1
    // Routes (above): each round's, 0 for one every route plays, else 1 +
    // its index in routes[].
    uint8_t      route_of[PACK_CHAMBERS];
    pack_route_t routes[PACK_ROUTES];
    int          n_routes;
    char         choose[80];  // the question the routes answer
    char         music[48];   // its tune (sound_music_tune), "" for the game's own
} pack_t;

// Read every pack in `dir`, adding their chambers to the chamber list
// (packs read before are forgotten). Returns how many packs.
int           pack_load(char const* dir);
// What pack_load found in `dir` but could not play, so the Stories screen
// can say: a folder with no chamber in it, or a zip not unzipped. `name` is
// the folder's or the zip's, `why` a few words for the list; NULL past the
// last.
#define PACK_SKIPPED 8
typedef struct {
    char name[32];
    char why[24];
} pack_skip_t;
int                pack_skipped_count(void);
pack_skip_t const* pack_skipped(int i);
int           pack_count(void);
pack_t const* pack_get(int i);
// The pack chamber `chamber` is in, and its place in it; -1 if none.
int           pack_of(int chamber, int* at);
// Where desk story `p` is saved (main.c writes it at the desk, deletes it
// when the story ends), and whether there is a save there now.
void          pack_save_path(pack_t const* p, char* out, size_t n);
bool          pack_has_save(pack_t const* p);
// Parse a pack.txt's text into `p` (its chambers by file id, in `files`:
// a desk story's rounds); false if it names no chamber. For the tests too.
bool          pack_parse(char const* text, pack_t* p, char files[][CHAMBER_ID_N], int* n_files);
