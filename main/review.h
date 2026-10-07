#pragma once
// A chamber under review, as a story pack's rounds have them (pack.h):
// GLaDOS's draft of it, and what reaching its exit says about the way the
// player went. Pure C -- the host tests judge the scripted routes.
//
// The keys, in a chamber's header (chambers/README.md has the whole of it):
//
//   review: flawed               flawed: cheese it; final: solve it as
//                                meant; broken: fix it, in the debugger
//   intended: button 1 by cube, door a, no launch
//                                the meant way: every term held (AND)
//   flaw: a no button 1 by cube -- The door opens without its cube.
//                                a known flaw: its terms, and what it is
//   fix: a 4 1 3-5 .#            what patches flaw a: cells x y z (a range
//                                lo-hi each), from '.' to '#'
//   debug: 2 #W .#               the debugger: 2 changes, each one of these
//   repair: 4 2 7 #W             a repair that works (checked by the tests)
//
// After the solution, `cheese a` and the like: a scripted route that
// cheeses it so (chamber.h).
//
// A term is one of these, `no ` before it to say it did not happen:
//   portal, cube portal, shots N (at most N), pickup, press, dropper,
//   button N [by player|cube|sphere|turret|fallen|beam|pellet|hand],
//   door X, launch [N], cube launch [N], bounce, paint, pellet, topple,
//   spotted, fizzle, burn, speed, float, ride, ride holding, bridge,
//   platform

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "draft.h"
#include "game.h"
#include "level.h"

#define REVIEW_TERMS  12  // in one condition
#define REVIEW_FLAWS  6
#define REVIEW_FIXES  24
#define REVIEW_DEBUGS 8  // the changes the debugger allows
#define REVIEW_ABOUT  120

typedef enum {
    REVIEW_NONE,  // not a round
    REVIEW_FLAWED,
    REVIEW_FINAL,
    REVIEW_BROKEN,
} review_kind_t;

typedef struct {
    uint8_t what;  // review.c's term table
    bool    no;
    int8_t  arg;  // a button's, door's or plate's number (from 0), shots; -1: any
    uint8_t by;   // BY_*, 0: anything
} review_term_t;

typedef struct {
    review_term_t t[REVIEW_TERMS];
    int           n;
} review_cond_t;

typedef struct {
    char          id;  // 'a'..'z'
    review_cond_t when;
    char          about[REVIEW_ABOUT];
} review_flaw_t;

typedef struct {
    char    flaw;                    // the flaw it patches; 0 for a repair
    uint8_t x0, y0, z0, x1, y1, z1;  // inclusive
    char    from, to;
} review_fix_t;

typedef struct {
    uint8_t       kind;  // review_kind_t
    review_cond_t intended;
    review_flaw_t flaws[REVIEW_FLAWS];
    int           n_flaws;
    review_fix_t  fixes[REVIEW_FIXES];
    int           n_fixes;
    int           debug_budget;             // changes the debugger allows, 0: none
    char          debug[REVIEW_DEBUGS][2];  // each: a cell from [0] to [1]
    int           n_debug;
    review_fix_t  repairs[REVIEW_FIXES];
    int           n_repairs;
} review_t;

// A chamber file's review keys (the rest is the chamber's: chamber.c skips
// these). A file with none is REVIEW_NONE. False with "line 4: ..." in
// `err` for one that does not read.
bool review_parse(char const* text, review_t* r, char* err, size_t err_n);

// A condition, after a run: all its terms held.
bool review_holds(review_cond_t const* c, level_t const* lv, track_t const* t);

typedef struct {
    bool     intended;  // the meant way
    uint32_t flaws;     // bit i: flaws[i] held
} review_verdict_t;

review_verdict_t review_judge(review_t const* r, level_t const* lv, track_t const* t);

// The flaw with id `id`, its index, or -1.
int review_flaw(review_t const* r, char id);

// The chamber patched: the fixes of each flaw in `flaws` (bit i:
// flaws[i]), and with `repair` its repairs too. Through `scratch`, a draft
// (big: the caller's). Its length in `out`, or -1 with the reason in `err`
// -- a fix that finds another cell than its `from` there, for one.
int review_patch(char const* text, review_t const* r, uint32_t flaws, bool repair, draft_t* scratch, char* out,
                 size_t out_n, char* err, size_t err_n);

// The debugger: may a cell go from `from` to `to`?
bool review_debug_allows(review_t const* r, char from, char to);

// What a run did, as terms ("portal, shots 4, button 1 by cube, door a"):
// for a person writing a review, and the debug log. Its length.
int review_describe(level_t const* lv, track_t const* t, char* out, size_t n);
