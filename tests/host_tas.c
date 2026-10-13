// Host TAS check: when each demo reaches the exit, stepped at 50 a second
// as a tool-assisted run is (tools/tas.py).
//
//   host_tas [-dt SECONDS | -jitter SEED [LO HI] [-first S] [-hitch P S]] DEMO...
//
// at 50 steps a second; with -dt at another rate; with -jitter, each step
// a random 1/HI to 1/LO s (20 to 35 frames a second unless given), as the
// badge's frames come -- and the badge's long ones: with -first the first
// step is S seconds (the frame a chamber loaded in, before main.c timed it
// from the load), with -hitch each later step is S seconds one time in 1/P
// (a sentence of GLaDOS's being spoken, say: tas/README.md).
// prints, per demo, its time to the exit, or FAIL: if it never gets there,
// or the player dies first. PORTALS_CHAMBERS adds chambers from a
// directory, as the SD card would: the TAS chambers.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chamber.h"
#include "demo.h"

typedef struct {
    float exit;
    bool  died;
} run_t;

static void tick(game_t const* g, int ev, float now, void* ctx) {
    (void)g;
    run_t* r = ctx;
    if (r->exit >= 0.0f) return;
    if (ev & PL_EV_DIED) r->died = true;
    if ((ev & PL_EV_EXIT) && !r->died) r->exit = now;
}

int main(int argc, char** argv) {
    char const* dir = getenv("PORTALS_CHAMBERS");
    if (dir != NULL) chamber_load_dir(dir);
    int      fails  = 0;
    float    dt     = 1.0f / 50.0f;
    int      first  = 1;
    unsigned jitter = 0;
    float    lo = 20.0f, hi = 35.0f;  // frames a second, with -jitter
    float    first_s = 0.0f;          // -first: the first step this long
    float    hitch_p = 0.0f, hitch_s = 0.0f;  // -hitch: a step this long, this often
    if (argc > 2 && strcmp(argv[1], "-dt") == 0) {
        dt    = (float)atof(argv[2]);
        first = 3;
    } else if (argc > 2 && strcmp(argv[1], "-jitter") == 0) {
        jitter = (unsigned)atoi(argv[2]) * 2654435761u + 1u;
        first  = 3;
        if (argc > 4 && atof(argv[3]) > 0.0 && atof(argv[4]) > atof(argv[3])) {
            lo    = (float)atof(argv[3]);
            hi    = (float)atof(argv[4]);
            first = 5;
        }
        for (;;) {
            if (first + 1 < argc && strcmp(argv[first], "-first") == 0) {
                first_s  = (float)atof(argv[first + 1]);
                first   += 2;
            } else if (first + 2 < argc && strcmp(argv[first], "-hitch") == 0) {
                hitch_p  = (float)atof(argv[first + 1]);
                hitch_s  = (float)atof(argv[first + 2]);
                first   += 3;
            } else {
                break;
            }
        }
    }
    for (int a = first; a < argc; a++) {
        int const           i = demo_find(argv[a]);
        static demo_state_t st;
        run_t               r = {-1.0f, false};
        if (i >= 0 && jitter == 0) {
            demo_run(i, 120.0f, dt, &st, tick, &r, 0.0f);
        } else if (i >= 0) {
            // Frames as they come on the badge: each its own length.
            demo_player_t p;
            game_load(&st.g, demo_chamber(i));
            demo_player_start(&p, demo_steps(i));
            unsigned seed = jitter;
            int      k    = 0;
            for (float now = 0.0f; now < 120.0f && r.exit < 0.0f && !r.died; k++) {
                seed     = seed * 1103515245u + 12345u;
                float ft = 1.0f / hi + (1.0f / lo - 1.0f / hi) * (float)((seed >> 8) & 0xFFFF) / 65535.0f;
                if (k == 0 && first_s > 0.0f)
                    ft = first_s;
                else if (hitch_p > 0.0f && (float)((seed >> 4) & 0xFFF) / 4095.0f < hitch_p)
                    ft = hitch_s;
                int const   ev  = demo_player_step(&p, &st.g, ft, 0.0f);
                now            += ft;
                tick(&st.g, ev, now, &r);
            }
        }
        if (r.exit >= 0.0f) {
            printf("%s\t%.2f\n", argv[a], (double)r.exit);
        } else {
            printf("%s\tFAIL%s\n", argv[a], i < 0 ? " (no such demo)" : r.died ? " (died)" : "");
            fails++;
        }
    }
    return fails ? 1 : 0;
}
