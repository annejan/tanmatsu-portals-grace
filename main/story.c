#include "story.h"
#include <stdio.h>
#include <string.h>

void story_begin(story_t* s, int pack) {
    memset(s, 0, sizeof(*s));
    s->pack   = pack;
    s->round  = 1;
    s->drafts = 1;
}

static uint32_t all_flaws(review_t const* r) {
    return r->n_flaws >= 32 ? 0xFFFFFFFFu : (1u << r->n_flaws) - 1u;
}

bool story_final(story_t const* s, review_t const* r) {
    return r->kind != REVIEW_FLAWED || (s->found & all_flaws(r)) == all_flaws(r);
}

// On to the next draft: the same file's, or the next file's first.
static void next_draft(story_t* s, bool next_file) {
    s->round++;
    if (next_file) {
        s->at++;
        s->found  = 0;
        s->novel  = false;
        s->drafts = 1;
    } else {
        s->drafts++;
    }
}

story_result_t story_exit(story_t* s, review_t const* r, level_t const* lv, track_t const* t, int n_rounds) {
    story_result_t         out = {0};
    review_verdict_t const v   = review_judge(r, lv, t);
    bool const             fin = story_final(s, r);

    if (r->kind == REVIEW_BROKEN || r->kind == REVIEW_NONE) {
        // Through to the exit at all: it works now.
        out.outcome = s->at + 1 >= n_rounds ? STORY_DONE : STORY_NEXT;
        snprintf(out.headline, sizeof(out.headline), "%s", r->kind == REVIEW_BROKEN ? "Repaired" : "Done");
        snprintf(out.line, sizeof(out.line), "It can be solved. I will take the credit for that.");
        next_draft(s, true);
        return out;
    }
    if (fin) {
        if (v.intended && v.flaws == 0) {
            out.outcome = s->at + 1 >= n_rounds ? STORY_DONE : STORY_NEXT;
            snprintf(out.headline, sizeof(out.headline), "Approved");
            snprintf(out.line, sizeof(out.line),
                     "Solved exactly as designed. The test subjects will hate it. Approved.");
            next_draft(s, true);
            return out;
        }
        // A way round the final draft: logged once, but approval wants the
        // meant way.
        out.outcome = STORY_AGAIN;
        if (!s->novel) {
            s->novel    = true;
            out.points  = 2;
            s->score   += 2;
            snprintf(out.headline, sizeof(out.headline), "Exploit logged, +2");
        } else {
            snprintf(out.headline, sizeof(out.headline), "Already logged");
        }
        snprintf(out.line, sizeof(out.line),
                 "That is not how my final draft is solved. Noted. Now solve it properly, so I can approve it.");
        return out;
    }
    uint32_t const fresh = v.flaws & ~s->found;
    if (fresh) {
        // A known flaw, found: logged, and patched for the next draft.
        int n = 0, first = -1;
        for (int i = 0; i < r->n_flaws; i++)
            if (fresh & (1u << i)) {
                if (first < 0) first = i;
                n++;
            }
        s->found    |= fresh;
        out.points   = n;
        s->score    += n;
        out.outcome  = STORY_PATCHED;
        snprintf(out.headline, sizeof(out.headline), "Flaw %c logged, +%d", r->flaws[first].id, n);
        snprintf(out.line, sizeof(out.line), "%s Patched. Again.", r->flaws[first].about);
        next_draft(s, false);
        return out;
    }
    if (v.flaws) {
        // A flaw already patched, still there: the patch failed.
        out.outcome = STORY_AGAIN;
        snprintf(out.headline, sizeof(out.headline), "Already logged");
        snprintf(out.line, sizeof(out.line), "I patched that. Apparently not. Find me something new.");
        return out;
    }
    if (v.intended) {
        out.outcome = STORY_AGAIN;
        snprintf(out.headline, sizeof(out.headline), "No feedback");
        snprintf(out.line, sizeof(out.line),
                 "You solved it the way I meant. I have learned nothing. The draft is flawed: find the flaw.");
        return out;
    }
    // Neither the meant way nor a known flaw: one she did not know of.
    out.outcome = STORY_AGAIN;
    if (!s->novel) {
        s->novel    = true;
        out.points  = 2;
        s->score   += 2;
        snprintf(out.headline, sizeof(out.headline), "Novel exploit, +2");
        snprintf(out.line, sizeof(out.line),
                 "I did not know that was possible. Logged. I cannot patch what I do not understand: find another.");
    } else {
        snprintf(out.headline, sizeof(out.headline), "Already logged");
        snprintf(out.line, sizeof(out.line), "That one again. I logged it the first time. Find another.");
    }
    return out;
}

int story_round_text(story_t const* s, char const* text, review_t const* r, draft_t* scratch, char* out, size_t n,
                     char* err, size_t err_n) {
    if (s->found == 0) {
        // Nothing patched yet: the file as it is.
        size_t const len = strlen(text);
        if (len >= n) {
            snprintf(err, err_n, "longer than %d bytes", (int)n - 1);
            return -1;
        }
        memcpy(out, text, len + 1);
        return (int)len;
    }
    return review_patch(text, r, s->found, false, scratch, out, n, err, err_n);
}
