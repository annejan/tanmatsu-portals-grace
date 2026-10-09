#include "ghost.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A chamber's ghost file: its id with '/' (a pack's chamber) made '~'.
static void file_id(char const* id, char* out, size_t n) {
    snprintf(out, n, "%s", id);
    for (char* c = out; *c; c++)
        if (*c == '/') *c = '~';
}

static float run_time(recording_t const* r) {
    if (r->n < 1 || r->runs[0].frames <= 0) return -1.0f;
    double t = 0;
    for (int i = 0; i < r->runs[0].frames; i++) t += (double)r->frame[r->runs[0].frame0 + i].dt_us * 1e-6;
    return (float)t;
}

// The chamber as it is: its cells, its size, its start, hashed (FNV-1a).
static uint32_t level_sig(level_t const* lv) {
    uint32_t           h      = 2166136261u;
    size_t const       n      = (size_t)lv->w * (size_t)lv->h * (size_t)lv->d;
    int const          dims[] = {lv->w, lv->h, lv->d, (int)(lv->spawn.x * 16), (int)(lv->spawn.y * 16),
                                 (int)(lv->spawn.z * 16), (int)(lv->spawn_yaw * 1000), lv->n_cubes, lv->n_buttons};
    unsigned char const* b    = (unsigned char const*)dims;
    for (size_t i = 0; i < sizeof(dims); i++) h = (h ^ b[i]) * 16777619u;
    for (size_t i = 0; i < n; i++) h = (h ^ lv->cells[i]) * 16777619u;
    return h;
}

void ghost_begin(ghost_t* g, char const* dir, char const* id, level_t const* lv, char const* release) {
    // The attempt before: dropped.
    if (g->capturing) recording_capture_free(&g->cap);
    recording_free(&g->best);
    snprintf(g->id, sizeof(g->id), "%s", id);
    g->racing = false;
    g->at     = 0;
    g->t      = 0.0f;
    g->now    = 0.0f;
    g->best_s = -1.0f;
    g->lost   = false;
    snprintf(g->sig, sizeof(g->sig), "%.24s L%08x", release, (unsigned)level_sig(lv));
    // This attempt, recorded as it goes.
    recording_capture_start(&g->cap);
    recording_capture_chamber(&g->cap, id);
    g->capturing = true;
    // The best, if there is one, and its world.
    char fid[CHAMBER_ID_N], err[96];
    file_id(id, fid, sizeof(fid));
    // One that does not read, or was made for another release or another
    // layout of the chamber, is no best: the next exit replaces it.
    if (!recording_load(dir, fid, &g->best, err, sizeof(err)) || g->best.n < 1 || g->best.runs[0].frames <= 0 ||
        strcmp(g->best.version, g->sig) != 0) {
        recording_free(&g->best);
        return;
    }
    g->best_s = run_time(&g->best);
    // No room for its world: the best still stands, unraced.
    if (g->world == NULL) g->world = malloc(sizeof(*g->world));
    if (g->world == NULL) {
        recording_free(&g->best);
        return;
    }
    game_load_level(g->world, lv);
    g->racing = true;
}

void ghost_frame(ghost_t* g, recording_frame_t const* f) {
    if (!g->capturing) return;
    // Out of memory: this attempt is not kept -- not a run with a gap in it.
    if (!g->lost && !recording_capture_frame(&g->cap, f)) {
        g->lost = true;
        recording_capture_free(&g->cap);
    }
    g->now += (float)f->dt_us * 1e-6f;
    if (!g->racing) return;
    // The ghost's frames, up to the player's clock: its world stepped on
    // each, as it was when it was played.
    recording_run_t const* run = &g->best.runs[0];
    while (g->at < run->frames) {
        recording_frame_t const* gf = &g->best.frame[run->frame0 + g->at];
        float const              dt = (float)gf->dt_us * 1e-6f;
        if (g->t + dt > g->now + 1e-6f) break;
        game_input_t in;
        float const  st = recording_frame_input(gf, &in);
        game_step(g->world, &in, st);
        g->t += dt;
        g->at++;
    }
}

ghost_result_t ghost_finish(ghost_t* g, char const* dir) {
    ghost_result_t r = {g->now, g->best_s, g->best_s < 0.0f || g->now < g->best_s, false};
    if (!g->capturing) return r;
    recording_capture_done(&g->cap);
    if (r.faster && !g->lost) {
        char fid[CHAMBER_ID_N], path[192], name[RECORDING_NAME_N];
        file_id(g->id, fid, sizeof(fid));
        snprintf(path, sizeof(path), "%s/%s.txt", dir, fid);
        snprintf(name, sizeof(name), "best, %.2f s", (double)g->now);
        r.best = recording_capture_write(&g->cap, path, name, g->sig);
    }
    recording_capture_free(&g->cap);
    g->capturing = false;
    return r;
}

bool ghost_pose(ghost_t const* g, player_t* out) {
    if (!g->racing || g->world == NULL) return false;
    // At the end of its run it has gone through the exit: it waits there
    // a moment, then is gone.
    if (g->at >= g->best.runs[0].frames && g->now > g->t + 1.0f) return false;
    *out = g->world->pl;
    return true;
}

void ghost_end(ghost_t* g) {
    if (g->capturing) recording_capture_free(&g->cap);
    g->capturing = false;
    recording_free(&g->best);
    free(g->world);
    g->world  = NULL;
    g->racing = false;
}
