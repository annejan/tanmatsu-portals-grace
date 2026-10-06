#include "recording.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// A chamber's steps, gathered as text and parsed once its section ends.
static bool close_run(recording_t* r, char const* text, char* err, size_t err_n) {
    if (r->n == 0) return true;
    recording_run_t* const run = &r->runs[r->n - 1];
    char                   why[96];
    int                    n = 0;
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
    memset(r, 0, sizeof(*r));
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
        } else if (strncmp(s, "step:", 5) == 0) {
            char* end = NULL;
            r->step   = strtof(s + 5, &end);
            if (end == s + 5 || !(r->step >= 0.005f && r->step <= 0.1f)) {
                snprintf(err, err_n, "line %d: step: seconds, from 0.005 to 0.1", line);
                return false;
            }
        } else if (strncmp(s, "chamber:", 8) == 0) {
            if (!close_run(r, section, err, err_n)) return false;
            if (r->n >= RECORDING_MAX) {
                snprintf(err, err_n, "line %d: more than %d chambers", line, RECORDING_MAX);
                return false;
            }
            snprintf(r->runs[r->n++].id, CHAMBER_ID_N, "%s", trim(s + 8));
            used       = 0;
            section[0] = '\0';
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
    char* text = size > 0 && size < 256 * 1024 ? malloc((size_t)size + 1) : NULL;
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
        char path[192];
        snprintf(path, sizeof(path), "%s/%s", dir, found[i]);
        char* const text = read_file(path);
        if (text == NULL) continue;
        char const* p = text;
        char        buf[256];
        for (int k = 0; k < 8 && line_of(&p, buf, sizeof(buf)); k++) {
            char* const s = trim(buf);
            if (strncmp(s, "name:", 5) == 0) {
                snprintf(names[i], RECORDING_NAME_N, "%s", trim(s + 5));
                break;
            }
        }
        free(text);
    }
    return n;
}
