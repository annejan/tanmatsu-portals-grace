// Host run check: a recording replayed as the badge plays it (main/watch.c),
// for the speedrun board (scene.rs/runs): what it was made on, each
// chamber's time, and whether it reached every exit -- worked out here,
// not taken from the file.
//
//   host_runcheck FILE [PACKS_DIR]
//
// FILE is a recording (main/recording.h): a run recorded on the badge,
// frame by frame -- each frame stepped with its own time and input, the
// clock its frames' (at most 0.1 s each, as watch.c counts) until the exit
// -- or a script, a TAS, stepped at 30 frames a second as `make tas`
// times it. PACKS_DIR holds story packs, as /sd/portals/dlc does, for runs
// through their chambers ("pack/file", or the file alone).
//
// Prints one JSON object: {"ok", "error", "name", "version", "physics",
// "kind" ("frames", "script" or "mixed"), "complete", "total", "runs":
// [{"id", "chamber" (its full id: "pack/file" for a story's), "name",
// "kind", "time", "exit", "why"}]}. Exit status 0 when it
// read; whether it reached the exits is in the JSON.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chamber.h"
#include "demo.h"
#include "game.h"
#include "pack.h"
#include "recording.h"

#define SCRIPT_DT   (1.0f / 30.0f)  // a script's frames, as `make tas` times the TAS
#define GIVE_UP_S   60.0f           // a chamber this long has lost its way (watch.c's REC_GIVE_S)
#define MAX_FILE    (512 * 1024)
#define MAX_FRAMES  400000           // about three hours at 35 frames a second

static void json_str(char const* s) {
    putchar('"');
    for (; *s; s++) {
        unsigned char const c = (unsigned char)*s;
        if (c == '"' || c == '\\')
            printf("\\%c", c);
        else if (c < 0x20 || c == 0x7f)
            printf("\\u%04x", c);
        else
            putchar(c);
    }
    putchar('"');
}

static int fail(char const* why) {
    printf("{\"ok\":false,\"error\":");
    json_str(why);
    printf(",\"physics\":");
    json_str(GAME_PHYSICS);
    printf("}\n");
    return 0;
}

static char* slurp(char const* path, long* n) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (*n < 0 || *n > MAX_FILE) {
        fclose(f);
        return NULL;
    }
    char* const t = malloc((size_t)*n + 1);
    if (t == NULL || fread(t, 1, (size_t)*n, f) != (size_t)*n) {
        fclose(f);
        free(t);
        return NULL;
    }
    fclose(f);
    t[*n] = '\0';
    return t;
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: host_runcheck FILE [PACKS_DIR]\n");
        return 2;
    }
    if (argc == 3) pack_load(argv[2]);
    long        n    = 0;
    char* const text = slurp(argv[1], &n);
    if (text == NULL) return fail("cannot read it, or it is over 512 KB");
    for (long i = 0; i < n; i++)
        if (text[i] == '\0') {
            free(text);
            return fail("not a text file");
        }
    static recording_t r;
    char               err[160];
    bool const         read = recording_parse(text, &r, err, sizeof(err));
    free(text);
    if (!read) return fail(err);
    if (r.n_frames > MAX_FRAMES) return fail("too many frames");

    static game_t g;
    static float  times[RECORDING_MAX];
    static bool   exits[RECORDING_MAX];
    static char   why[RECORDING_MAX][48];
    static char   names[RECORDING_MAX][48];
    static char   found[RECORDING_MAX][CHAMBER_ID_N];  // the chamber it is, by its full id
    float         total    = 0.0f;
    bool          complete = true;
    int           scripts = 0, recorded = 0;
    for (int k = 0; k < r.n; k++) {
        recording_run_t const* const run = &r.runs[k];
        times[k]                         = 0.0f;
        exits[k]                         = false;
        why[k][0]                        = '\0';
        snprintf(names[k], sizeof(names[k]), "%s", run->id);
        found[k][0] = '\0';
        int c = chamber_find(run->id);
        // A pack's replay names its chamber by the file alone: the first pack that has it.
        for (int p = 0; c < 0 && p < pack_count(); p++) {
            char full[96];
            snprintf(full, sizeof(full), "%.31s/%.63s", pack_get(p)->id, run->id);
            c = chamber_find(full);
        }
        if (c < 0) {
            snprintf(why[k], sizeof(why[k]), "no such chamber");
            complete = false;
            continue;
        }
        chamber_name(c, names[k], sizeof(names[k]));
        snprintf(found[k], sizeof(found[k]), "%s", chamber_id(c));
        if (!game_load(&g, c)) {
            snprintf(why[k], sizeof(why[k]), "the chamber does not load");
            complete = false;
            continue;
        }
        float t  = 0.0f;
        int   ev = 0;
        if (run->frames > 0) {
            recorded++;
            for (int i = 0; i < run->frames && !(ev & (PL_EV_EXIT | PL_EV_DIED)); i++) {
                recording_frame_t const* const f  = &r.frame[run->frame0 + i];
                float const                    fd = (float)f->dt_us * 1e-6f;
                game_input_t                   in;
                recording_frame_input(f, &in);
                ev |= game_step(&g, &in, fd);
                t  += fminf(fd, 0.1f);
            }
        } else {
            scripts++;
            static demo_player_t pl;
            demo_player_start(&pl, run->steps);
            while (!(ev & (PL_EV_EXIT | PL_EV_DIED)) && t < GIVE_UP_S) {
                ev |= demo_player_step(&pl, &g, SCRIPT_DT, 0.0f);
                t  += SCRIPT_DT;
            }
        }
        times[k] = t;
        exits[k] = (ev & PL_EV_EXIT) != 0;
        if (!exits[k]) {
            snprintf(why[k], sizeof(why[k]), "%s",
                     (ev & PL_EV_DIED)                ? "died"
                     : run->frames > 0                ? "its frames ran out short of the exit"
                                                      : "lost its way");
            complete = false;
        } else {
            total += t;
        }
    }

    printf("{\"ok\":true,\"name\":");
    json_str(r.name);
    printf(",\"version\":");
    json_str(r.version);
    printf(",\"physics\":");
    json_str(GAME_PHYSICS);
    printf(",\"kind\":\"%s\",\"complete\":%s,\"total\":%.3f,\"runs\":[",
           scripts && recorded ? "mixed" : scripts ? "script" : "frames", complete ? "true" : "false",
           (double)total);
    for (int k = 0; k < r.n; k++) {
        printf("%s{\"id\":", k ? "," : "");
        json_str(r.runs[k].id);
        printf(",\"chamber\":");
        json_str(found[k]);
        printf(",\"name\":");
        json_str(names[k]);
        printf(",\"kind\":\"%s\",\"time\":%.3f,\"exit\":%s,\"why\":", r.runs[k].frames > 0 ? "frames" : "script",
               (double)times[k], exits[k] ? "true" : "false");
        json_str(why[k]);
        printf("}");
    }
    printf("]}\n");
    recording_free(&r);
    return 0;
}
