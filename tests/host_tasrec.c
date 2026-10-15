// Host check of a TAS played as a player's inputs (demo_player_play), as
// Watch records it on the badge: each chamber's route on uneven frames,
// each input rounded as a recording keeps it and played so, and kept. The
// recording is written out, for the board's replay (host_runcheck) to play
// back -- which must go the same way, to the same times.
//
//   host_tasrec SEED OUT DEMO...
//
// SEED picks the frames (20 to 35 a second, as host_tas -jitter); each
// DEMO is a chamber whose solution is the route (tools/tas.py's tas-<id>
// files, in PORTALS_CHAMBERS), recorded under the chamber's own id (the
// part after "tas-"). Prints each chamber's time to its exit, as the
// badge's Watch would time it, or FAIL.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chamber.h"
#include "demo.h"
#include "recording.h"

static recording_capture_t s_cap;
static float               s_now;

// One frame: rounded as a recording keeps it, played so, and kept.
static int play(game_t* g, game_input_t const* in, float dt, void* ctx) {
    (void)ctx;
    recording_frame_t const f = recording_frame(dt, in);
    game_input_t            q;
    float const             fd = recording_frame_input(&f, &q);
    recording_capture_frame(&s_cap, &f);
    s_now += fd < 0.1f ? fd : 0.1f;
    return game_step(g, &q, fd);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: host_tasrec SEED OUT DEMO...\n");
        return 2;
    }
    char const* dir = getenv("PORTALS_CHAMBERS");
    if (dir != NULL) chamber_load_dir(dir);
    unsigned seed  = (unsigned)atoi(argv[1]) * 2654435761u + 1u;
    int      fails = 0;
    recording_capture_start(&s_cap);
    for (int a = 3; a < argc; a++) {
        int const           i  = demo_find(argv[a]);
        char const*         id = strncmp(argv[a], "tas-", 4) == 0 ? argv[a] + 4 : argv[a];
        static demo_state_t st;
        float               exit_t = -1.0f;
        bool                died   = false;
        if (i >= 0) {
            demo_player_t p;
            game_load(&st.g, demo_chamber(i));
            demo_player_start(&p, demo_steps(i));
            recording_capture_chamber(&s_cap, id);
            s_now = 0.0f;
            while (s_now < 120.0f && exit_t < 0.0f && !died) {
                seed           = seed * 1103515245u + 12345u;
                float const ft = 1.0f / 35.0f + (1.0f / 20.0f - 1.0f / 35.0f) * (float)((seed >> 8) & 0xFFFF) / 65535.0f;
                int const   ev = demo_player_play(&p, &st.g, ft, play, NULL);
                if (ev & PL_EV_EXIT) exit_t = s_now;
                if (ev & PL_EV_DIED) died = true;
            }
        }
        if (exit_t >= 0.0f) {
            recording_capture_done(&s_cap);
            printf("%s\t%.3f\n", id, (double)exit_t);
        } else {
            printf("%s\tFAIL%s\n", id, i < 0 ? " (no such demo)" : died ? " (died)" : "");
            fails++;
        }
    }
    if (!recording_capture_write(&s_cap, argv[2], "TAS, played as inputs", "0.13.0 host_tasrec")) {
        fprintf(stderr, "%s: not written\n", argv[2]);
        return 1;
    }
    recording_capture_free(&s_cap);
    return fails ? 1 : 0;
}
