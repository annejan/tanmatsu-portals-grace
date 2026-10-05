// Host TAS check: when each demo reaches the exit, stepped at 50 a second
// as a tool-assisted run is (tools/tas.py).
//
//   host_tas DEMO...
//
// prints, per demo, its time to the exit, or FAIL: if it never gets there,
// or the player dies first. PORTALS_CHAMBERS adds chambers from a
// directory, as the SD card would: the TAS chambers.

#include <stdio.h>
#include <stdlib.h>
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
    int fails = 0;
    for (int a = 1; a < argc; a++) {
        int const           i = demo_find(argv[a]);
        static demo_state_t st;
        run_t               r = {-1.0f, false};
        if (i >= 0) demo_run(i, 120.0f, 1.0f / 50.0f, &st, tick, &r, 0.0f);
        if (r.exit >= 0.0f) {
            printf("%s\t%.2f\n", argv[a], (double)r.exit);
        } else {
            printf("%s\tFAIL%s\n", argv[a], i < 0 ? " (no such demo)" : r.died ? " (died)" : "");
            fails++;
        }
    }
    return fails ? 1 : 0;
}
