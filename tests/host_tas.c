// Host TAS check: when each demo reaches the exit, stepped at 50 a second
// as a tool-assisted run is (tools/tas.py).
//
//   host_tas [-dt SECONDS | -jitter SEED] DEMO...
//
// at 50 steps a second; with -dt at another rate; with -jitter, each step
// a random 1/30 to 1/8 s, as the badge's frames come (it plays the TAS
// at about 10 a second).
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
    if (argc > 2 && strcmp(argv[1], "-dt") == 0) {
        dt    = (float)atof(argv[2]);
        first = 3;
    } else if (argc > 2 && strcmp(argv[1], "-jitter") == 0) {
        jitter = (unsigned)atoi(argv[2]) * 2654435761u + 1u;
        first  = 3;
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
            for (float now = 0.0f; now < 120.0f && r.exit < 0.0f && !r.died;) {
                seed           = seed * 1103515245u + 12345u;
                float const ft = 1.0f / 30.0f + (1.0f / 8.0f - 1.0f / 30.0f) * (float)((seed >> 8) & 0xFFFF) / 65535.0f;
                int const   ev = demo_player_step(&p, &st.g, ft, 0.0f);
                now += ft;
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
