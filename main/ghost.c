#include "ghost.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chamber.h"

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
    uint32_t             h      = 2166136261u;
    size_t const         n      = (size_t)lv->w * (size_t)lv->h * (size_t)lv->d;
    int const            dims[] = {lv->w,
                                   lv->h,
                                   lv->d,
                                   (int)(lv->spawn.x * 16),
                                   (int)(lv->spawn.y * 16),
                                   (int)(lv->spawn.z * 16),
                                   (int)(lv->spawn_yaw * 1000),
                                   lv->n_cubes,
                                   lv->n_buttons};
    unsigned char const* b      = (unsigned char const*)dims;
    for (size_t i = 0; i < sizeof(dims); i++) h = (h ^ b[i]) * 16777619u;
    for (size_t i = 0; i < n; i++) h = (h ^ lv->cells[i]) * 16777619u;
    return h;
}

// A run from `dir`/`fid`.txt into `r`, if it is one for this chamber as it
// is (`sig`): its time, or -1.
static float load_run(char const* dir, char const* fid, char const* sig, recording_t* r) {
    char err[96];
    if (!recording_load(dir, fid, r, err, sizeof(err)) || r->n < 1 || r->runs[0].frames <= 0 ||
        strcmp(r->version, sig) != 0) {
        recording_free(r);
        return -1.0f;
    }
    return run_time(r);
}

static void drop_runs(ghost_t* g) {
    for (int i = 0; i < 1 + GHOST_RIVALS; i++) {
        recording_free(&g->racer[i].run);
        g->racer[i].live = false;
    }
    g->n    = 0;
    g->mine = false;
}

// Your best, and the fastest GHOST_RIVALS of the rivals', read.
static void load_runs(ghost_t* g, char const* dir) {
    drop_runs(g);
    char fid[CHAMBER_ID_N];
    file_id(g->id, fid, sizeof(fid));
    ghost_racer_t* r = &g->racer[0];
    if ((r->best_s = load_run(dir, fid, g->sig, &r->run)) >= 0.0f) {
        snprintf(r->name, sizeof(r->name), "you");
        g->mine = true;
        g->n    = 1;
    }
    // Rivals: each folder in rivals/, its file for this chamber; the
    // fastest kept, at most GHOST_RIVALS.
    char rdir[160];
    snprintf(rdir, sizeof(rdir), "%.120s/rivals", dir);
    // Every folder (as many as a listing holds), by its whole name: a name
    // cut short would be another folder, and its runs never found.
    static char const* names[CHAMBER_MAX];
    static char        keep[CHAMBER_MAX][CHAMBER_ID_N];
    int const          n = chamber_list_subdirs(rdir, names, CHAMBER_MAX);
    for (int i = 0; i < n; i++) snprintf(keep[i], sizeof(keep[i]), "%s", names[i]);
    static recording_t cand;
    for (int i = 0; i < n; i++) {
        char d[240];
        snprintf(d, sizeof(d), "%.160s/%.63s", rdir, keep[i]);
        float const t = load_run(d, fid, g->sig, &cand);
        if (t < 0.0f) continue;
        // Its place among the rivals so far: the slowest goes if it is full.
        int const first = g->mine ? 1 : 0;
        int       at    = g->n;
        if (g->n - first >= GHOST_RIVALS) {
            int slow = first;
            for (int k = first; k < g->n; k++)
                if (g->racer[k].best_s > g->racer[slow].best_s) slow = k;
            if (g->racer[slow].best_s <= t) {
                recording_free(&cand);
                continue;
            }
            recording_free(&g->racer[slow].run);
            at = slow;
        } else {
            g->n++;
        }
        ghost_racer_t* rr = &g->racer[at];
        rr->run           = cand;
        memset(&cand, 0, sizeof(cand));  // its frames are the racer's now
        rr->best_s = t;
        snprintf(rr->name, sizeof(rr->name), "%.23s", keep[i]);
    }
    g->stale = false;
}

void ghost_begin(ghost_t* g, char const* dir, char const* id, level_t const* lv, char const* release) {
    // The attempt before: dropped.
    if (g->capturing) recording_capture_free(&g->cap);
    char sig[48];
    snprintf(sig, sizeof(sig), "%.24s L%08x", release, (unsigned)level_sig(lv));
    // The runs: read again only for another chamber, or after a new best.
    if (g->stale || strcmp(g->id, id) != 0 || strcmp(g->sig, sig) != 0) {
        snprintf(g->id, sizeof(g->id), "%s", id);
        snprintf(g->sig, sizeof(g->sig), "%s", sig);
        load_runs(g, dir);
    }
    g->now  = 0.0f;
    g->lost = false;
    recording_capture_start(&g->cap);
    recording_capture_chamber(&g->cap, id);
    g->capturing = true;
    // Each racer off again, in a world of its own; one there is no room
    // for does not race.
    for (int i = 0; i < g->n; i++) {
        ghost_racer_t* r = &g->racer[i];
        if (r->world == NULL) r->world = malloc(sizeof(*r->world));
        r->live = r->world != NULL;
        r->at   = 0;
        r->t    = 0.0f;
        if (r->live) game_load_level(r->world, lv);
    }
}

void ghost_frame(ghost_t* g, recording_frame_t const* f) {
    if (!g->capturing) return;
    // Out of memory: this attempt is not kept -- not a run with a gap in it.
    if (!g->lost && !recording_capture_frame(&g->cap, f)) {
        g->lost = true;
        recording_capture_free(&g->cap);
    }
    g->now += (float)f->dt_us * 1e-6f;
    // Each racer's frames, up to the player's clock: its world stepped on
    // each, as it was when it was played.
    for (int i = 0; i < g->n; i++) {
        ghost_racer_t* r = &g->racer[i];
        if (!r->live) continue;
        recording_run_t const* run = &r->run.runs[0];
        while (r->at < run->frames) {
            recording_frame_t const* gf = &r->run.frame[run->frame0 + r->at];
            float const              dt = (float)gf->dt_us * 1e-6f;
            if (r->t + dt > g->now + 1e-6f) break;
            game_input_t in;
            float const  st = recording_frame_input(gf, &in);
            game_step(r->world, &in, st);
            r->t += dt;
            r->at++;
        }
    }
}

ghost_result_t ghost_finish(ghost_t* g, char const* dir, char const* nick) {
    float const    mine = g->mine ? g->racer[0].best_s : -1.0f;
    ghost_result_t r    = {g->now, mine, mine < 0.0f || g->now < mine, false, 1, 1 + g->n};
    for (int i = 0; i < g->n; i++)
        if (g->racer[i].best_s < g->now) r.place++;
    if (!g->capturing) return r;
    recording_capture_done(&g->cap);
    if (r.faster && !g->lost) {
        char fid[CHAMBER_ID_N], path[192], name[RECORDING_NAME_N];
        file_id(g->id, fid, sizeof(fid));
        snprintf(path, sizeof(path), "%s/%s.txt", dir, fid);
        snprintf(name, sizeof(name), "%.24s, %.2f s", nick != NULL && nick[0] ? nick : "you", (double)g->now);
        r.best = recording_capture_write(&g->cap, path, name, g->sig);
        if (r.best) g->stale = true;  // the new best, raced next time
    }
    recording_capture_free(&g->cap);
    g->capturing = false;
    return r;
}

int ghost_count(ghost_t const* g) {
    return g->n;
}

bool ghost_pose(ghost_t const* g, int i, player_t* out) {
    if (i < 0 || i >= g->n || !g->racer[i].live) return false;
    ghost_racer_t const* r = &g->racer[i];
    // At the end of its run it has gone through the exit: it waits there
    // a moment, then is gone.
    if (r->at >= r->run.runs[0].frames && g->now > r->t + 1.0f) return false;
    *out = r->world->pl;
    return true;
}

void ghost_end(ghost_t* g) {
    if (g->capturing) recording_capture_free(&g->cap);
    g->capturing = false;
    drop_runs(g);
    for (int i = 0; i < 1 + GHOST_RIVALS; i++) {
        free(g->racer[i].world);
        g->racer[i].world = NULL;
    }
    g->id[0] = '\0';
    g->stale = true;
}
