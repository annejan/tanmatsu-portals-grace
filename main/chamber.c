#include "chamber.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "ff.h"
#define LOGW(...) ESP_LOGW("chamber", __VA_ARGS__)
#define LOGI(...) ESP_LOGI("chamber", __VA_ARGS__)
#else
#define LOGW(...) (fprintf(stderr, "chamber: " __VA_ARGS__), fputc('\n', stderr))
#define LOGI(...) ((void)0)
#endif

#define DEG (3.14159265f / 180.0f)

// --- Parsing --------------------------------------------------------------

typedef struct {
    char*  err;
    size_t err_n;
    int    line;
} ctx_t;

static bool fail(ctx_t* c, char const* fmt, ...) {
    int const n = snprintf(c->err, c->err_n, "line %d: ", c->line);
    va_list   ap;
    va_start(ap, fmt);
    if (n >= 0 && (size_t)n < c->err_n) vsnprintf(c->err + n, c->err_n - (size_t)n, fmt, ap);
    va_end(ap);
    return false;
}

// The next line of `*p` into `out` (without its line ending); false at the end.
static bool next_line(char const** p, char* out, size_t n) {
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
    while (isspace((unsigned char)*s)) s++;
    char* e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static bool facing_of(char const* v, float* yaw) {
    static struct {
        char const* name;
        float       deg;
    } const names[] = {{"north", 0}, {"east", 90}, {"south", 180}, {"west", -90}};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strcmp(v, names[i].name) == 0) {
            *yaw = names[i].deg * DEG;
            return true;
        }
    // sscanf, not strtof: graceloader does not export strtof.
    float d;
    int   n = 0;
    if (sscanf(v, "%f %n", &d, &n) != 1 || v[n] != '\0') return false;
    *yaw = d * DEG;
    return true;
}

static bool portal_of(char const* w, int* which) {
    if (strcmp(w, "blue") == 0) *which = 0;
    else if (strcmp(w, "orange") == 0) *which = 1;
    else return false;
    return true;
}

// One line of a solution.
static bool parse_step(ctx_t* c, char* line, step_t* st) {
    char  verb[16] = {0}, word[16] = {0};
    float a = 0, b = 0, d = 0;
    int   n = 0;
    if (sscanf(line, "%15s", verb) != 1) return fail(c, "empty step");
    char const* args = line + strlen(verb);
    *st              = (step_t){0};
    if (strcmp(verb, "shoot") == 0) {
        if (sscanf(args, "%15s %f %f %f %n", word, &a, &b, &d, &n) != 4 || !portal_of(word, &st->which))
            return fail(c, "want: shoot blue|orange x y z");
        *st = (step_t){OP_SHOOT, st->which, a, b, d};
    } else if (strcmp(verb, "shoot_view") == 0) {
        if (sscanf(args, "%15s %n", word, &n) != 1 || !portal_of(word, &st->which))
            return fail(c, "want: shoot_view blue|orange");
        st->op = OP_SHOOT_VIEW;
    } else if (strcmp(verb, "face") == 0) {
        if (sscanf(args, "%f %f %n", &a, &b, &n) != 2) return fail(c, "want: face yaw_degrees pitch_degrees");
        *st = (step_t){OP_FACE, 0, a * DEG, b * DEG, 0};
    } else if (strcmp(verb, "look") == 0) {
        if (sscanf(args, "%f %f %f %n", &a, &b, &d, &n) != 3) return fail(c, "want: look x y z");
        *st = (step_t){OP_FACE_POINT, 0, a, b, d};
    } else if (strcmp(verb, "walk") == 0) {
        if (sscanf(args, "%f %n", &a, &n) != 1) return fail(c, "want: walk seconds");
        *st = (step_t){OP_WALK, 0, a, 0, 0};
    } else if (strcmp(verb, "walk_to") == 0) {
        float pace = 1.0f;
        int   got  = sscanf(args, "%f %f %f", &a, &d, &pace);
        if (got < 2) return fail(c, "want: walk_to x z [pace]");
        *st = (step_t){OP_WALK_TO, 0, a, got >= 3 ? pace : 1.0f, d};
        return true;
    } else if (strcmp(verb, "step_off") == 0) {
        if (sscanf(args, "%f %n", &a, &n) != 1) return fail(c, "want: step_off pace");
        *st = (step_t){OP_STEP_OFF, 0, a, 0, 0};
    } else if (strcmp(verb, "wait") == 0) {
        if (sscanf(args, "%f %n", &a, &n) != 1) return fail(c, "want: wait seconds");
        *st = (step_t){OP_WAIT, 0, a, 0, 0};
    } else if (strcmp(verb, "use") == 0) {
        st->op = OP_USE;
        n      = 0;
    } else {
        return fail(c, "unknown step \"%s\"", verb);
    }
    if (*trim((char*)args + n) != '\0') return fail(c, "too much after \"%s\"", verb);
    return true;
}

bool chamber_parse(char const* text, level_t* lv, step_t* steps, int* n_steps, char* err, size_t err_n) {
    ctx_t c = {err, err_n, 0};
    if (err_n) err[0] = '\0';
    memset(lv, 0, sizeof(*lv));
    snprintf(lv->name, sizeof(lv->name), "Untitled");
    int  ns        = 0;
    int  layer     = -1;  // the layer whose rows are being read
    int  row       = 0;
    bool in_sol    = false;
    bool have_size = false;
    int  spawns    = 0;
    char buf[128];
    char const* p = text;

    while (next_line(&p, buf, sizeof(buf))) {
        c.line++;
        if (layer >= 0) {
            // A row of the map: read as it stands, comments and all.
            if ((int)strlen(buf) > lv->w) return fail(&c, "row longer than the size's %d cells", lv->w);
            int const z = lv->d - 1 - row;
            for (int x = 0; x < lv->w; x++) {
                char const ch = x < (int)strlen(buf) ? buf[x] : '#';
                uint8_t    m  = MAT_AIR;
                switch (ch) {
                    case '#': m = MAT_METAL; break;
                    case 'W': m = MAT_WHITE; break;
                    case '~': m = MAT_GOO; break;
                    case 'E': m = MAT_EXIT; break;
                    case '.':
                    case ' ': break;
                    case 'S':
                        lv->spawn = v3((float)x + 0.5f, (float)layer, (float)z + 0.5f);
                        spawns++;
                        break;
                    case 'C':
                        if (lv->n_cubes >= LV_MAX_CUBES) return fail(&c, "more than %d cubes", LV_MAX_CUBES);
                        lv->cubes[lv->n_cubes++] = v3((float)x + 0.5f, (float)layer, (float)z + 0.5f);
                        break;
                    default:
                        if (ch >= 'a' && ch < 'a' + LV_MAX_DOORS) {
                            m = MAT_DOOR;
                        } else if (ch >= 'A' && ch < 'A' + LV_MAX_DOORS) {
                            if (lv->n_buttons >= LV_MAX_BUTTONS) return fail(&c, "more than %d buttons", LV_MAX_BUTTONS);
                            if (layer == 0) return fail(&c, "a button needs a cell under it");
                            lv->buttons[lv->n_buttons++] = (button_t){x, layer - 1, z, ch - 'A', false};
                        } else {
                            return fail(&c, "unknown cell '%c' at column %d", ch, x + 1);
                        }
                }
                level_set(lv, x, layer, z, m);
                // Doors: one per letter, the box its cells span (checked below).
                if (m == MAT_DOOR) {
                    int const k = ch - 'a';
                    door_t*   d = &lv->doors[k];
                    if (d->x1 == 0) *d = (door_t){x, layer, z, x + 1, layer + 1, z + 1, k, 0.0f};
                    if (x < d->x0) d->x0 = x;
                    if (layer < d->y0) d->y0 = layer;
                    if (z < d->z0) d->z0 = z;
                    if (x + 1 > d->x1) d->x1 = x + 1;
                    if (layer + 1 > d->y1) d->y1 = layer + 1;
                    if (z + 1 > d->z1) d->z1 = z + 1;
                    if (k + 1 > lv->n_doors) lv->n_doors = k + 1;
                }
            }
            if (++row == lv->d) layer = -1;
            continue;
        }

        char* s = trim(buf);
        if (*s == '\0' || strncmp(s, "//", 2) == 0) continue;
        if (in_sol) {
            // Checked even when not wanted, so a mistake in a solution is
            // reported when the file is loaded, not when it is played.
            step_t scratch;
            if (ns >= SCRIPT_MAX_STEPS - 1) return fail(&c, "more than %d steps", SCRIPT_MAX_STEPS - 1);
            if (!parse_step(&c, s, steps != NULL ? &steps[ns] : &scratch)) return false;
            ns++;
            continue;
        }
        if (strcmp(s, "solution") == 0) {
            in_sol = true;
            continue;
        }
        int y;
        if (sscanf(s, "layer %d", &y) == 1) {
            if (!have_size) return fail(&c, "layer before size");
            if (y < 0 || y >= lv->h) return fail(&c, "layer %d outside the size's %d", y, lv->h);
            layer = y;
            row   = 0;
            continue;
        }
        char* colon = strchr(s, ':');
        if (colon == NULL) return fail(&c, "expected \"key: value\", \"layer N\" or \"solution\"");
        *colon        = '\0';
        char* const k = trim(s);
        char* const v = trim(colon + 1);
        if (strcmp(k, "name") == 0) {
            snprintf(lv->name, sizeof(lv->name), "%s", v);
        } else if (strcmp(k, "hint") == 0) {
            snprintf(lv->hint, sizeof(lv->hint), "%s", v);
        } else if (strcmp(k, "size") == 0) {
            if (have_size) return fail(&c, "size given twice");
            if (sscanf(v, "%d %d %d", &lv->w, &lv->h, &lv->d) != 3 || lv->w < 3 || lv->h < 3 || lv->d < 3 ||
                lv->w > LV_MAX_W || lv->h > LV_MAX_H || lv->d > LV_MAX_D)
                return fail(&c, "size: want w h d, from 3 3 3 to %d %d %d", LV_MAX_W, LV_MAX_H, LV_MAX_D);
            // Everything is metal until a layer says otherwise.
            for (int i = 0; i < lv->w * lv->h * lv->d; i++) lv->cells[i] = MAT_METAL;
            have_size = true;
        } else if (strcmp(k, "facing") == 0) {
            if (!facing_of(v, &lv->spawn_yaw)) return fail(&c, "facing: north, east, south, west or degrees");
        } else {
            return fail(&c, "unknown key \"%s\"", k);
        }
    }
    if (layer >= 0) return fail(&c, "layer %d has %d of its %d rows", layer, row, lv->d);
    if (!have_size) return fail(&c, "no size");
    if (spawns != 1) return fail(&c, "want exactly one S, found %d", spawns);

    // Each door must fill its box, one cell thick across x or z.
    for (int i = 0; i < lv->n_doors; i++) {
        door_t const* d = &lv->doors[i];
        if (d->x1 == 0) return fail(&c, "door '%c' missing but a later one is there", 'a' + i);
        if (d->x1 - d->x0 != 1 && d->z1 - d->z0 != 1) return fail(&c, "door '%c' is not one cell thick", 'a' + i);
        for (int y = d->y0; y < d->y1; y++)
            for (int z = d->z0; z < d->z1; z++)
                for (int x = d->x0; x < d->x1; x++)
                    if (level_get(lv, x, y, z) != MAT_DOOR) return fail(&c, "door '%c' is not a box", 'a' + i);
    }
    for (int i = 0; i < lv->n_buttons; i++) {
        button_t const* b = &lv->buttons[i];
        uint8_t const   m = level_get(lv, b->x, b->y, b->z);
        if (m == MAT_AIR || m == MAT_DOOR) return fail(&c, "button '%c' has nothing under it", 'A' + b->link);
    }
    if (steps != NULL) {
        steps[ns] = (step_t){0};
        if (n_steps) *n_steps = ns;
    }
    return true;
}

// --- The list ---------------------------------------------------------

typedef struct {
    char        id[32];
    char const* text;
} entry_t;

static entry_t s_list[CHAMBER_MAX];
static int     s_n;
static bool    s_init;

static void init(void) {
    if (s_init) return;
    s_init = true;
    for (int i = 0; i < chamber_builtin_count && s_n < CHAMBER_MAX; i++) {
        snprintf(s_list[s_n].id, sizeof(s_list[s_n].id), "%s", chamber_builtins[i].id);
        s_list[s_n++].text = chamber_builtins[i].text;
    }
}

int chamber_count(void) {
    init();
    return s_n;
}

char const* chamber_id(int i) {
    init();
    return i >= 0 && i < s_n ? s_list[i].id : "?";
}

bool chamber_build(int i, level_t* lv, step_t* steps, int* n_steps) {
    init();
    if (i < 0 || i >= s_n) return false;
    char err[96];
    if (!chamber_parse(s_list[i].text, lv, steps, n_steps, err, sizeof(err))) {
        LOGW("%s: %s", s_list[i].id, err);
        return false;
    }
    return true;
}

static int by_name(void const* a, void const* b) {
    return strcmp(*(char const* const*)a, *(char const* const*)b);
}

static bool ends_txt(char const* name, size_t len) {
    if (len < 5) return false;
    char const* e = name + len - 4;
    return e[0] == '.' && tolower((unsigned char)e[1]) == 't' && tolower((unsigned char)e[2]) == 'x' &&
           tolower((unsigned char)e[3]) == 't';
}

static char s_found[CHAMBER_MAX][64];  // file names in the directory

#ifdef ESP_PLATFORM
// Graceloader exports no opendir / readdir: list through FatFs, whose
// paths are volume-relative. Try the plausible spellings of the VFS path
// and keep whichever opens (as SynthMiner's vfs_compat.c and the
// engine's se_mp3.c do, for the same reason).
static int list_dir(char const* dir) {
    char const* rel = dir;
    if (strncmp(dir, "/sd", 3) == 0) rel = dir + 3;
    else if (strncmp(dir, "/int", 4) == 0) rel = dir + 4;
    static FF_DIR  d;
    static FILINFO info;
    char           cand[160];
    bool           open = false;
    for (int i = 0; i < 4 && !open; i++) {
        switch (i) {
            case 0: snprintf(cand, sizeof(cand), "%s", rel); break;
            case 1: snprintf(cand, sizeof(cand), "0:%s", rel); break;
            case 2: snprintf(cand, sizeof(cand), "1:%s", rel); break;
            default: snprintf(cand, sizeof(cand), "%s", dir); break;
        }
        open = f_opendir(&d, cand) == FR_OK;
    }
    if (!open) return 0;
    int n = 0;
    while (n < CHAMBER_MAX && f_readdir(&d, &info) == FR_OK && info.fname[0] != '\0') {
        size_t const len = strlen(info.fname);
        if ((info.fattrib & AM_DIR) || len >= sizeof(s_found[0]) || !ends_txt(info.fname, len)) continue;
        memcpy(s_found[n++], info.fname, len + 1);
    }
    f_closedir(&d);
    return n;
}
#else
#include <dirent.h>
static int list_dir(char const* dir) {
    DIR* d = opendir(dir);
    if (d == NULL) return 0;
    int            n = 0;
    struct dirent* e;
    while (n < CHAMBER_MAX && (e = readdir(d)) != NULL) {
        size_t const len = strlen(e->d_name);
        if (len >= sizeof(s_found[0]) || !ends_txt(e->d_name, len)) continue;
        memcpy(s_found[n++], e->d_name, len + 1);
    }
    closedir(d);
    return n;
}
#endif

int chamber_load_dir(char const* dir) {
    init();
    char const* order[CHAMBER_MAX];
    int const   n = list_dir(dir);
    for (int i = 0; i < n; i++) order[i] = s_found[i];
    qsort(order, (size_t)n, sizeof(order[0]), by_name);

    int added = 0;
    for (int i = 0; i < n && s_n < CHAMBER_MAX; i++) {
        char path[192];
        snprintf(path, sizeof(path), "%s/%s", dir, order[i]);
        FILE* f = fopen(path, "rb");
        if (f == NULL) continue;
        fseek(f, 0, SEEK_END);
        long const size = ftell(f);
        fseek(f, 0, SEEK_SET);
        char* text = size > 0 && size < 64 * 1024 ? malloc((size_t)size + 1) : NULL;
        if (text == NULL || fread(text, 1, (size_t)size, f) != (size_t)size) {
            fclose(f);
            free(text);
            LOGW("%s: cannot read it", path);
            continue;
        }
        fclose(f);
        text[size] = '\0';
        static level_t check;  // static: a level is too big for the stack
        char           err[96];
        if (!chamber_parse(text, &check, NULL, NULL, err, sizeof(err))) {
            LOGW("%s: %s -- skipped", path, err);
            free(text);
            continue;
        }
        size_t const len = strlen(order[i]) - 4;
        snprintf(s_list[s_n].id, sizeof(s_list[s_n].id), "%.*s", (int)len, order[i]);
        s_list[s_n++].text = text;
        LOGI("%s: %s", path, check.name);
        added++;
    }
    return added;
}
