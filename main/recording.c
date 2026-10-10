#include "recording.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// One line of `*p` into out[n], cut short if long; false at the end.
static bool line_of(char const** p, char* out, size_t n) {
    if (**p == '\0') return false;
    size_t k = 0;
    while (**p != '\0' && **p != '\n') {
        if (**p != '\r' && k + 1 < n) out[k++] = **p;
        (*p)++;
    }
    if (**p == '\n') (*p)++;
    out[k] = '\0';
    return true;
}

static char* trim(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    char* e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
    return s;
}

// --- Frames ---------------------------------------------------------------

static int32_t whole(float v, float unit) {
    return (int32_t)lroundf(v / unit);
}

recording_frame_t recording_frame(float dt, game_input_t const* in) {
    float const dt_c = dt < 0.0f ? 0.0f : dt > 1.0f ? 1.0f : dt;
    return (recording_frame_t){
        .dt_us       = (uint32_t)whole(dt_c, 1e-6f),
        .dyaw_urad   = whole(in->dyaw, 1e-6f),
        .dpitch_urad = whole(in->dpitch, 1e-6f),
        .fwd         = (int16_t)whole(fmaxf(-1.0f, fminf(1.0f, in->fwd)), 1e-3f),
        .strafe      = (int16_t)whole(fmaxf(-1.0f, fminf(1.0f, in->strafe)), 1e-3f),
        .keys = (uint8_t)((in->jump ? REC_JUMP : 0) | (in->fire[0] ? REC_BLUE : 0) | (in->fire[1] ? REC_ORANGE : 0) |
                          (in->use ? REC_USE : 0)),
    };
}

float recording_frame_input(recording_frame_t const* f, game_input_t* in) {
    *in = (game_input_t){
        .fwd    = (float)f->fwd * 1e-3f,
        .strafe = (float)f->strafe * 1e-3f,
        .dyaw   = (float)f->dyaw_urad * 1e-6f,
        .dpitch = (float)f->dpitch_urad * 1e-6f,
        .jump   = (f->keys & REC_JUMP) != 0,
        .fire   = {(f->keys & REC_BLUE) != 0, (f->keys & REC_ORANGE) != 0},
        .use    = (f->keys & REC_USE) != 0,
    };
    return (float)f->dt_us * 1e-6f;
}

void recording_free(recording_t* r) {
    free(r->frame);
    r->frame    = NULL;
    r->n_frames = 0;
}

// Room in r->frame for `more`, with `*cap` what it has now.
static bool frames_room(recording_t* r, int* cap, int more) {
    if (r->n_frames + more <= *cap) return true;
    int n = *cap > 0 ? *cap : 1024;
    while (n < r->n_frames + more) n *= 2;
    recording_frame_t* const f = realloc(r->frame, (size_t)n * sizeof(*f));
    if (f == NULL) return false;
    r->frame = f;
    *cap     = n;
    return true;
}

// A frame's line: its six whole numbers, in the order recording.h gives.
static bool parse_frame(char const* s, recording_frame_t* f) {
    long v[6];
    for (int i = 0; i < 6; i++) {
        char* end;
        v[i] = strtol(s, &end, 10);
        if (end == s) return false;
        s = end;
    }
    while (*s == ' ' || *s == '\t') s++;
    if (*s != '\0' || v[0] < 0 || v[0] > 1000000 || v[1] < -1000 || v[1] > 1000 || v[2] < -1000 || v[2] > 1000 ||
        v[5] < 0 || v[5] > 15)
        return false;
    *f = (recording_frame_t){
        .dt_us       = (uint32_t)v[0],
        .fwd         = (int16_t)v[1],
        .strafe      = (int16_t)v[2],
        .dyaw_urad   = (int32_t)v[3],
        .dpitch_urad = (int32_t)v[4],
        .keys        = (uint8_t)v[5],
    };
    return true;
}

// --- Parsing --------------------------------------------------------------

// A chamber's steps, gathered as text and parsed once its section ends.
static bool close_run(recording_t* r, char const* text, char* err, size_t err_n) {
    if (r->n == 0) return true;
    recording_run_t* const run = &r->runs[r->n - 1];
    if (run->frames > 0) {
        if (text[0] != '\0') {
            snprintf(err, err_n, "chamber %s: steps and frames both", run->id);
            return false;
        }
        return true;
    }
    char why[96];
    int  n = 0;
    if (!chamber_parse_steps(text, run->steps, &n, why, sizeof(why))) {
        snprintf(err, err_n, "chamber %s: %s", run->id, why);
        return false;
    }
    if (n == 0) {
        snprintf(err, err_n, "chamber %s: no steps", run->id);
        return false;
    }
    return true;
}

bool recording_parse(char const* text, recording_t* r, char* err, size_t err_n) {
    recording_free(r);
    memset(r, 0, sizeof(*r));
    int cap = 0;  // frames r->frame has room for
    if (err_n > 0) err[0] = '\0';
    static char section[8192];  // the chamber's steps so far
    size_t      used = 0;
    section[0]       = '\0';
    char        buf[256];
    char const* p    = text;
    int         line = 0;
    if (strncmp(p, "\xEF\xBB\xBF", 3) == 0) p += 3;
    while (line_of(&p, buf, sizeof(buf))) {
        line++;
        char* const s = trim(buf);
        if (strncmp(s, "name:", 5) == 0) {
            snprintf(r->name, sizeof(r->name), "%s", trim(s + 5));
        } else if (strncmp(s, "version:", 8) == 0) {
            snprintf(r->version, sizeof(r->version), "%s", trim(s + 8));
        } else if (strncmp(s, "chamber:", 8) == 0) {
            if (!close_run(r, section, err, err_n)) return false;
            if (r->n >= RECORDING_MAX) {
                snprintf(err, err_n, "line %d: more than %d chambers", line, RECORDING_MAX);
                return false;
            }
            snprintf(r->runs[r->n++].id, CHAMBER_ID_N, "%s", trim(s + 8));
            used       = 0;
            section[0] = '\0';
        } else if (strncmp(s, "frames:", 7) == 0) {
            // A recorded run: its frames follow, one a line.
            recording_run_t* const run = r->n > 0 ? &r->runs[r->n - 1] : NULL;
            long const             n   = strtol(s + 7, NULL, 10);
            if (run == NULL || run->frames > 0 || used > 0 || n <= 0 || n > 1000000) {
                snprintf(err, err_n, "line %d: \"frames:\" out of place", line);
                return false;
            }
            if (!frames_room(r, &cap, (int)n)) {
                snprintf(err, err_n, "line %d: no memory for %ld frames", line, n);
                return false;
            }
            run->frame0 = r->n_frames;
            for (long k = 0; k < n; k++) {
                if (!line_of(&p, buf, sizeof(buf)) || !parse_frame(trim(buf), &r->frame[r->n_frames])) {
                    snprintf(err, err_n, "line %ld: not a frame", (long)line + k + 1);
                    return false;
                }
                r->n_frames++;
            }
            line        += (int)n;
            run->frames  = (int)n;
        } else if (*s != '\0' && strncmp(s, "//", 2) != 0) {
            if (r->n == 0) {
                snprintf(err, err_n, "line %d: a step before any \"chamber:\"", line);
                return false;
            }
            size_t const len = strlen(s);
            if (used + len + 2 > sizeof(section)) {
                snprintf(err, err_n, "line %d: chamber %s is too long", line, r->runs[r->n - 1].id);
                return false;
            }
            memcpy(section + used, s, len);
            used            += len;
            section[used++]  = '\n';
            section[used]    = '\0';
        }
    }
    if (!close_run(r, section, err, err_n)) return false;
    if (r->n == 0) {
        snprintf(err, err_n, "no chambers in it");
        return false;
    }
    return true;
}

static char* read_file(char const* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long const size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = size > 0 && size < 4 * 1024 * 1024 ? malloc((size_t)size + 1) : NULL;  // a recorded hour: 2.5 MB
    if (text != NULL && fread(text, 1, (size_t)size, f) == (size_t)size) {
        text[size] = '\0';
    } else {
        free(text);
        text = NULL;
    }
    fclose(f);
    return text;
}

bool recording_load(char const* dir, char const* id, recording_t* r, char* err, size_t err_n) {
    char path[192];
    snprintf(path, sizeof(path), "%s/%s.txt", dir, id);
    char* const text = read_file(path);
    if (text == NULL) {
        snprintf(err, err_n, "cannot read %s", path);
        return false;
    }
    bool const ok = recording_parse(text, r, err, err_n);
    free(text);
    return ok;
}

int recording_list(char const* dir, char ids[][CHAMBER_ID_N], char names[][RECORDING_NAME_N]) {
    char const* found[RECORDINGS_MAX];
    int const   n = chamber_list_dir(dir, found, RECORDINGS_MAX);
    for (int i = 0; i < n; i++) {
        snprintf(ids[i], CHAMBER_ID_N, "%.*s", (int)strlen(found[i]) - 4, found[i]);
        // Its name, from its first lines; else its file's.
        snprintf(names[i], RECORDING_NAME_N, "%s", ids[i]);
        // Only its first lines: a recorded run can be megabytes long.
        char path[192];
        snprintf(path, sizeof(path), "%s/%s", dir, found[i]);
        FILE* const f = fopen(path, "rb");
        if (f == NULL) continue;
        char         head[1024];
        size_t const got = fread(head, 1, sizeof(head) - 1, f);
        fclose(f);
        head[got]     = '\0';
        char const* p = head;
        char        buf[256];
        for (int k = 0; k < 8 && line_of(&p, buf, sizeof(buf)); k++) {
            char* const s = trim(buf);
            if (strncmp(s, "name:", 5) == 0) {
                snprintf(names[i], RECORDING_NAME_N, "%s", trim(s + 5));
                break;
            }
        }
    }
    return n;
}

// --- Recording a run ------------------------------------------------------

void recording_capture_start(recording_capture_t* c) {
    recording_capture_free(c);
}

void recording_capture_chamber(recording_capture_t* c, char const* id) {
    recording_capture_again(c);
    if (c->r.n >= RECORDING_MAX) return;
    recording_run_t* const run = &c->r.runs[c->r.n];
    snprintf(run->id, CHAMBER_ID_N, "%s", id);
    run->steps[0].op = OP_END;
    run->frame0      = c->r.n_frames;
    run->frames      = 0;
    c->open          = true;
}

void recording_capture_again(recording_capture_t* c) {
    if (!c->open) return;
    c->r.n_frames            = c->r.runs[c->r.n].frame0;
    c->r.runs[c->r.n].frames = 0;
}

bool recording_capture_frame(recording_capture_t* c, recording_frame_t const* f) {
    if (!c->open) return true;  // between chambers: nothing to keep
    if (!frames_room(&c->r, &c->cap, 1)) return false;
    c->r.frame[c->r.n_frames++] = *f;
    c->r.runs[c->r.n].frames++;
    return true;
}

void recording_capture_done(recording_capture_t* c) {
    if (!c->open) return;
    c->open = false;
    c->r.n++;
}

#ifdef ESP_PLATFORM
// `errno` is a TLS variable an app under Graceloader cannot bind (it needs
// __tls_get_addr); the libc's __errno() is exported and is the same thing.
extern int* __errno(void);
#define CARD_ERRNO (*__errno())
#else
#include <errno.h>
#define CARD_ERRNO errno
#endif

int       recording_write_failed;
int       recording_write_errno;
uintptr_t recording_write_probe;

int recording_errno(void) {
    return CARD_ERRNO;
}

// One go at the file: the RECORDING_FAILED_* it stopped at, 0 if on the card.
static int write_once(recording_capture_t const* c, char const* path, char const* name, char const* version) {
    // f_open mallocs a 512-byte name buffer, then this file's 512-byte sector
    // buffer, which the card DMAs from: where a second 512 lands now is
    // where that one will. Outside DMA-capable RAM, the card cannot take it.
    void* volatile const name_buf = malloc(512);  // volatile: kept, not optimised out
    void* const probe     = malloc(512);
    recording_write_probe = (uintptr_t)probe;
    free(probe);
    free(name_buf);
    CARD_ERRNO = 0;
    FILE* f    = fopen(path, "w");
    if (f == NULL) {
        recording_write_errno = CARD_ERRNO;
        return RECORDING_FAILED_OPEN;
    }
    // fprintf() fills a stdio buffer; a card error shows as a negative return
    // when it spills, so `< 0`, not "== 0", is the test.
    bool ok = fprintf(f, "name: %s\nversion: %s\n", name, version) >= 0;
    for (int k = 0; k < c->r.n && ok; k++) {
        recording_run_t const* const run = &c->r.runs[k];
        ok = fprintf(f, "\nchamber: %s\nframes: %d\n", run->id, run->frames) >= 0;
        for (int i = 0; i < run->frames && ok; i++) {
            recording_frame_t const* const q = &c->r.frame[run->frame0 + i];
            ok = fprintf(f, "%lu %d %d %ld %ld %d\n", (unsigned long)q->dt_us, q->fwd, q->strafe,
                         (long)q->dyaw_urad, (long)q->dpitch_urad, q->keys) >= 0;
        }
    }
    int const write_errno = CARD_ERRNO;
    CARD_ERRNO            = 0;
    bool const closed     = fclose(f) == 0;  // the last sector reaches the card here
    if (ok && closed) return 0;
    recording_write_errno = ok ? CARD_ERRNO : write_errno;
    return ok ? RECORDING_FAILED_CLOSE : RECORDING_FAILED_WRITE;
}

static int text_once(char const* path, char const* text) {
    CARD_ERRNO = 0;
    FILE* f    = fopen(path, "w");
    if (f == NULL) {
        recording_write_errno = CARD_ERRNO;
        return RECORDING_FAILED_OPEN;
    }
    size_t const n           = strlen(text);
    bool const   ok          = fwrite(text, 1, n, f) == n;
    int const    write_errno = CARD_ERRNO;
    CARD_ERRNO               = 0;
    bool const closed        = fclose(f) == 0;
    if (ok && closed) return 0;
    recording_write_errno = ok ? CARD_ERRNO : write_errno;
    return ok ? RECORDING_FAILED_CLOSE : RECORDING_FAILED_WRITE;
}

bool recording_write_text(char const* path, char const* text) {
    recording_write_errno = 0;
    for (int tries = 0; tries < 2; tries++) {
        if (tries > 0) usleep(200 * 1000);
        recording_write_failed = text_once(path, text);
        if (recording_write_failed == 0) return true;
    }
    return false;
}

bool recording_capture_write(recording_capture_t const* c, char const* path, char const* name, char const* version) {
    recording_write_failed = RECORDING_FAILED_EMPTY;
    recording_write_errno  = 0;
    if (c->r.n == 0) return false;
    // Twice: after one failed card write FatFs refuses that file until it is
    // opened again, and a transaction the card missed usually goes through
    // a moment later.
    for (int tries = 0; tries < 2; tries++) {
        if (tries > 0) usleep(200 * 1000);
        recording_write_failed = write_once(c, path, name, version);
        if (recording_write_failed == 0) return true;
        chamber_remove_file(path);  // not half a run on the card
    }
    return false;
}

void recording_capture_free(recording_capture_t* c) {
    recording_free(&c->r);
    memset(c, 0, sizeof(*c));
}
