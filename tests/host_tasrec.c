// Host check of a TAS played as a player's inputs (demo_player_play), as
// Watch records it on the badge: each chamber's route on uneven frames,
// each input rounded as a recording keeps it and played so, and kept. The
// recording is written out, for the board's replay (host_runcheck) to play
// back -- which must go the same way, to the same times.
//
//   host_tasrec SEED OUT DEMO...
//
// SEED picks the frames (20 to 35 a second, as host_tas -jitter, or
// TASREC_FPS="LO HI"; or TASREC_FRAMES=FILE[:FILE...], runs kept on a badge: each
// chamber played on that chamber's own frame lengths there, in order --
// its acts' 1 us frames left out -- and on their median once they run
// out; with SEED not 0, drawn at random from that chamber's frames there,
// another attempt on the same badge); each
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

// TASREC_FRAMES: a kept run's frame lengths, chamber by chamber.
#define KEPT_CHAMBERS 64
#define KEPT_FRAMES   4096
static struct {
    char  id[48];
    int   n;
    float dt[KEPT_FRAMES];
    float median;
} s_kept[KEPT_CHAMBERS];
static int s_n_kept;

static int cmp_float(void const* a, void const* b) {
    float const x = *(float const*)a, y = *(float const*)b;
    return x < y ? -1 : x > y;
}

static int kept_find(char const* id) {
    for (int i = 0; i < s_n_kept; i++)
        if (strcmp(s_kept[i].id, id) == 0) return i;
    return -1;
}

// One kept run's frames, added to the chambers' pools.
static bool kept_read(char const* path) {
    FILE* f = fopen(path, "r");
    if (f == NULL) return false;
    char line[256];
    int  in = -1;  // the chamber being read
    while (fgets(line, sizeof(line), f) != NULL) {
        long us;
        if (strncmp(line, "chamber:", 8) == 0) {
            char id[48] = "";
            sscanf(line + 8, " %47s", id);
            in = kept_find(id);  // another run's frames of it: pooled
            if (in < 0 && s_n_kept < KEPT_CHAMBERS) {
                in = s_n_kept++;
                snprintf(s_kept[in].id, sizeof(s_kept[in].id), "%s", id);
            }
        } else if (in >= 0 && line[0] >= '0' && line[0] <= '9' && sscanf(line, "%ld", &us) == 1 && us > 1000 &&
                   s_kept[in].n < KEPT_FRAMES) {
            s_kept[in].dt[s_kept[in].n++] = (float)us * 1e-6f;
        }
    }
    fclose(f);
    return true;
}

static void kept_medians(void) {
    for (int i = 0; i < s_n_kept; i++) {
        static float sorted[KEPT_FRAMES];
        memcpy(sorted, s_kept[i].dt, sizeof(float) * (size_t)s_kept[i].n);
        qsort(sorted, (size_t)s_kept[i].n, sizeof(float), cmp_float);
        s_kept[i].median = s_kept[i].n ? sorted[s_kept[i].n / 2] : 1.0f / 30.0f;
    }
}


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
    float    lo = 20.0f, hi = 35.0f;  // frames a second; TASREC_FPS="LO HI" for one badge's band
    if (getenv("TASREC_FPS") != NULL) sscanf(getenv("TASREC_FPS"), "%f %f", &lo, &hi);
    // Several kept runs, FILE:FILE...: their frames pooled (in order, the first's, then the next's).
    char const* kept = getenv("TASREC_FRAMES");
    if (kept != NULL) {
        char list[1024];
        snprintf(list, sizeof(list), "%s", kept);
        for (char* path = strtok(list, ":"); path != NULL; path = strtok(NULL, ":"))
            if (!kept_read(path)) {
                fprintf(stderr, "%s: not read\n", path);
                return 2;
            }
        kept_medians();
    }
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
            s_now        = 0.0f;
            int const kc = kept != NULL ? kept_find(id) : -1;
            int       kf = 0;
            while (s_now < 120.0f && exit_t < 0.0f && !died) {
                seed     = seed * 1103515245u + 12345u;
                float ft = 1.0f / hi + (1.0f / lo - 1.0f / hi) * (float)((seed >> 8) & 0xFFFF) / 65535.0f;
                if (kc >= 0 && atoi(argv[1]) != 0)
                    ft = s_kept[kc].dt[(seed >> 8) % (unsigned)s_kept[kc].n];
                else if (kc >= 0)
                    ft = kf < s_kept[kc].n ? s_kept[kc].dt[kf++] : s_kept[kc].median;
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
