#pragma once
// A story told from a desk (pack.h): its review rounds, played in order.
// Each round file is GLaDOS's draft of a chamber (review.h). A flawed
// draft wants cheesing: each known flaw found is logged and patched, and
// the next draft is the same chamber with that fix in; a way she did not
// know of is a novel exploit. Solved as she meant it, a flawed draft has
// taught her nothing: again. Once every known flaw is patched, the draft
// is final: solved as meant, it is approved, and the next file's round
// begins. Pure C -- the host tests play it.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "draft.h"
#include "game.h"
#include "review.h"

#define STORY_LINE_N 160  // as level_t.story

typedef struct {
    int      pack;    // pack.h's index
    int      at;      // the round file being played, in the pack's order
    uint32_t found;   // its known flaws found so far (bit i: flaws[i])
    bool     novel;   // a novel exploit logged for it
    int      round;   // drafts played, from 1: the round number on the desk
    int      score;   // flaws logged: 1 a known one, 2 a novel one
    int      drafts;  // drafts of this file so far, from 1
} story_t;

typedef enum {
    STORY_AGAIN,    // the same draft once more
    STORY_PATCHED,  // the same chamber, a flaw patched: the next round
    STORY_NEXT,     // approved: the next file's round
    STORY_DONE,     // approved, and that was the last
} story_outcome_t;

typedef struct {
    uint8_t outcome;             // story_outcome_t
    int     points;              // added to the score
    char    headline[48];        // a word or two: "Flaw logged", "Approved"
    char    line[STORY_LINE_N];  // GLaDOS's, said and typed out
} story_result_t;

void story_begin(story_t* s, int pack);

// Whether the round in play is final: every known flaw patched (a final
// or broken file is final from the start).
bool story_final(story_t const* s, review_t const* r);

// The exit reached in the round in play, its review `r` and what the run
// did: what that means, and the story moved on accordingly.
story_result_t story_exit(story_t* s, review_t const* r, level_t const* lv, track_t const* t, int n_rounds);

// The round's chamber: the file's text patched with the flaws found so
// far. Through `scratch` (big: the caller's). Its length, or -1 with the
// reason in `err`.
int story_round_text(story_t const* s, char const* text, review_t const* r, draft_t* scratch, char* out, size_t n,
                     char* err, size_t err_n);
