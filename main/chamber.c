#include "chamber.h"
#include <ctype.h>
#include <math.h>
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

// --- The legend -----------------------------------------------------------

chamber_glyph_t const chamber_legend[] = {
    {'#', "metal"},        {'W', "white panel"},  {'.', "air"},          {' ', "air"},          {'~', "goo"},
    {'E', "exit"},         {'G', "glass"},        {'F', "fizzler"},      {'J', "faith plate"},  {'T', "plate target"},
    {'M', "platform"},     {'N', "platform end"}, {'S', "start"},        {'C', "cube"},         {'a', "door a"},
    {'b', "door b"},       {'c', "door c"},       {'d', "door d"},       {'e', "door e"},       {'f', "door f"},
    {'g', "door g"},       {'h', "door h"},       {'1', "button for a"}, {'2', "button for b"}, {'3', "button for c"},
    {'4', "button for d"}, {'5', "button for e"}, {'6', "button for f"}, {'7', "button for g"}, {'8', "button for h"},
    {'A', "old button 1"}, {'B', "old button 2"}, {'D', "old button 4"},
};
int const chamber_legend_n = (int)(sizeof(chamber_legend) / sizeof(chamber_legend[0]));

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
    // Not NaN or infinity: either would reach the player's position, and
    // from there the physics' loops over cells.
    if (sscanf(v, "%f %n", &d, &n) != 1 || v[n] != '\0' || !isfinite(d)) return false;
    *yaw = fmodf(d, 360.0f) * DEG;
    return true;
}

static bool portal_of(char const* w, int* which) {
    if (strcmp(w, "blue") == 0)
        *which = 0;
    else if (strcmp(w, "orange") == 0)
        *which = 1;
    else
        return false;
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
        int   k    = 0;
        if (sscanf(args, "%f %f %n", &a, &d, &n) != 2) return fail(c, "want: walk_to x z [pace]");
        if (sscanf(args + n, "%f %n", &pace, &k) == 1) n += k;
        *st = (step_t){OP_WALK_TO, 0, a, pace, d};
    } else if (strcmp(verb, "step_off") == 0) {
        if (sscanf(args, "%f %n", &a, &n) != 1) return fail(c, "want: step_off pace");
        *st = (step_t){OP_STEP_OFF, 0, a, 0, 0};
    } else if (strcmp(verb, "wait") == 0) {
        if (sscanf(args, "%f %n", &a, &n) != 1) return fail(c, "want: wait seconds");
        *st = (step_t){OP_WAIT, 0, a, 0, 0};
    } else if (strcmp(verb, "use") == 0) {
        st->op = OP_USE;
        n      = 0;
    } else if (strcmp(verb, "grab") == 0) {
        st->op = OP_GRAB;
        n      = 0;
    } else {
        return fail(c, "unknown step \"%s\"", verb);
    }
    if (*trim((char*)args + n) != '\0') return fail(c, "too much after \"%s\"", verb);
    if (!isfinite(st->a) || !isfinite(st->b) || !isfinite(st->c)) return fail(c, "\"%s\": not a number", verb);
    return true;
}

// Where a cell comes in a file written the usual way: layer by layer
// upwards, each layer's rows from the far side (highest z) to the near
// one, left to right.
static int reading_order(int x, int y, int z) {
    return (y * LV_MAX_D + (LV_MAX_D - 1 - z)) * LV_MAX_W + x;
}

static int target_order(vec3_t t) {
    return reading_order((int)floorf(t.x), (int)floorf(t.y), (int)floorf(t.z));
}

bool chamber_parse(char const* text, level_t* lv, step_t* steps, int* n_steps, char* err, size_t err_n) {
    ctx_t c = {err, err_n, 0};
    if (err_n) err[0] = '\0';
    memset(lv, 0, sizeof(*lv));
    snprintf(lv->name, sizeof(lv->name), "Untitled");
    int         ns                   = 0;
    int         layer                = -1;  // the layer whose rows are being read
    int         row                  = 0;
    bool        in_sol               = false;
    bool        have_size            = false;
    int         spawns               = 0;
    bool        layer_seen[LV_MAX_H] = {false};
    // Faith plates and their targets, paired in reading order (below).
    jump_t      plates[LV_MAX_JUMPS];
    vec3_t      targets[LV_MAX_JUMPS];
    int         n_plates = 0, n_targets = 0;
    // The moving platform: the box its M cells span, and the N cell.
    int         m_box[6] = {0}, n_cell[3] = {0}, n_m = 0;
    bool        have_m = false, have_n = false;
    char        buf[128];
    char const* p = text;
    // A byte-order mark, as Windows editors write one, is not text.
    if (strncmp(p, "\xEF\xBB\xBF", 3) == 0) p += 3;

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
                    case '#':
                        m = MAT_METAL;
                        break;
                    case 'W':
                        m = MAT_WHITE;
                        break;
                    case '~':
                        m = MAT_GOO;
                        break;
                    case 'G':
                        m = MAT_GLASS;
                        break;
                    case 'F':
                        m = MAT_FIZZ;
                        break;
                    case 'J':
                        if (n_plates >= LV_MAX_JUMPS) return fail(&c, "more than %d faith plates", LV_MAX_JUMPS);
                        m                  = MAT_JUMP;
                        plates[n_plates++] = (jump_t){x, layer, z, v3(0, 0, 0)};
                        break;
                    case 'M':
                        if (!have_m) {
                            m_box[0] = m_box[3] = x, m_box[1] = m_box[4] = layer, m_box[2] = m_box[5] = z;
                            have_m = true;
                        }
                        if (x < m_box[0]) m_box[0] = x;
                        if (layer < m_box[1]) m_box[1] = layer;
                        if (z < m_box[2]) m_box[2] = z;
                        if (x > m_box[3]) m_box[3] = x;
                        if (layer > m_box[4]) m_box[4] = layer;
                        if (z > m_box[5]) m_box[5] = z;
                        n_m++;
                        break;
                    case 'N':
                        if (have_n) return fail(&c, "more than one N");
                        n_cell[0] = x, n_cell[1] = layer, n_cell[2] = z;
                        have_n = true;
                        break;
                    case 'T':
                        if (n_targets >= LV_MAX_JUMPS) return fail(&c, "more than %d targets", LV_MAX_JUMPS);
                        targets[n_targets++] = v3((float)x + 0.5f, (float)layer, (float)z + 0.5f);
                        break;
                    case 'E':
                        m = MAT_EXIT;
                        break;
                    case '.':
                    case ' ':
                        break;
                    case 'S':
                        lv->spawn = v3((float)x + 0.5f, (float)layer, (float)z + 0.5f);
                        spawns++;
                        break;
                    case 'C':
                        if (lv->n_cubes >= LV_MAX_CUBES) return fail(&c, "more than %d cubes", LV_MAX_CUBES);
                        lv->cubes[lv->n_cubes++] = v3((float)x + 0.5f, (float)layer, (float)z + 0.5f);
                        break;
                    default:
                        // Buttons are digits; A, B and D are how older files
                        // wrote buttons 1, 2 and 4.
                        int const button = chamber_is_button(ch) ? ch - '1'
                                           : ch == 'A'           ? 0
                                           : ch == 'B'           ? 1
                                           : ch == 'D'           ? 3
                                                                 : -1;
                        if (chamber_is_door(ch)) {
                            m = MAT_DOOR;
                        } else if (button >= 0) {
                            if (lv->n_buttons >= LV_MAX_BUTTONS)
                                return fail(&c, "more than %d buttons", LV_MAX_BUTTONS);
                            if (layer == 0) return fail(&c, "a button needs a cell under it");
                            lv->buttons[lv->n_buttons++] = (button_t){x, layer - 1, z, button, false};
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
            // Twice, its cubes and buttons would add up, and its cells not.
            if (layer_seen[y]) return fail(&c, "layer %d given twice", y);
            layer_seen[y] = true;
            layer         = y;
            row           = 0;
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
    if (n_plates != n_targets) return fail(&c, "%d faith plate(s) (J) but %d target(s) (T)", n_plates, n_targets);
    // The first plate throws to the first target, and so on, in reading
    // order: whatever order the layers came in, as the writers put them
    // in this order.
    for (int i = 1; i < n_plates; i++)
        for (int j = i; j > 0 && reading_order(plates[j].x, plates[j].y, plates[j].z) <
                                     reading_order(plates[j - 1].x, plates[j - 1].y, plates[j - 1].z);
             j--) {
            jump_t const t = plates[j];
            plates[j]      = plates[j - 1];
            plates[j - 1]  = t;
        }
    for (int i = 1; i < n_targets; i++)
        for (int j = i; j > 0 && target_order(targets[j]) < target_order(targets[j - 1]); j--) {
            vec3_t const t = targets[j];
            targets[j]     = targets[j - 1];
            targets[j - 1] = t;
        }
    for (int i = 0; i < n_plates; i++) {
        plates[i].target = targets[i];
        lv->jumps[i]     = plates[i];
    }
    lv->n_jumps = n_plates;
    if (have_m != have_n) return fail(&c, "a moving platform needs its M cells and one N");
    if (have_m) {
        int const sx = m_box[3] - m_box[0] + 1, sy = m_box[4] - m_box[1] + 1, sz = m_box[5] - m_box[2] + 1;
        if (n_m != sx * sy * sz) return fail(&c, "the M cells are not a box");
        if (n_cell[0] >= m_box[0] && n_cell[0] <= m_box[3] && n_cell[1] >= m_box[1] && n_cell[1] <= m_box[4] &&
            n_cell[2] >= m_box[2] && n_cell[2] <= m_box[5])
            return fail(&c, "the N is inside the platform");
        lv->platform = (platform_t){
            v3((float)m_box[0], (float)m_box[1], (float)m_box[2]),
            v3((float)m_box[3] + 1, (float)m_box[4] + 1, (float)m_box[5] + 1),
            v3((float)(n_cell[0] - m_box[0]), (float)(n_cell[1] - m_box[1]), (float)(n_cell[2] - m_box[2])),
        };
        lv->n_platforms = 1;
    }

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
    // ... and no two boxes overlap. A cell of door b in door a's box makes
    // the boxes overlap, so then every box holds its own letter only.
    for (int i = 0; i < lv->n_doors; i++)
        for (int j = i + 1; j < lv->n_doors; j++) {
            door_t const* a = &lv->doors[i];
            door_t const* b = &lv->doors[j];
            if (a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 && b->y0 < a->y1 && a->z0 < b->z1 && b->z0 < a->z1)
                return fail(&c, "doors '%c' and '%c' overlap", 'a' + i, 'a' + j);
        }
    for (int i = 0; i < lv->n_buttons; i++) {
        button_t const* b = &lv->buttons[i];
        uint8_t const   m = level_get(lv, b->x, b->y, b->z);
        if (m == MAT_AIR || m == MAT_DOOR || m == MAT_FIZZ)
            return fail(&c, "button '%c' has nothing under it", chamber_button_char(b->link));
        if (b->link >= lv->n_doors || lv->doors[b->link].x1 == 0)
            return fail(&c, "button '%c' has no door '%c'", chamber_button_char(b->link), chamber_door_char(b->link));
    }
    // More than the renderer holds: refused here, not drawn with holes.
    int const quads = level_mesh(lv, NULL, 0, NULL, 0);
    if (quads > LV_MAX_QUADS) return fail(&c, "too detailed: %d wall faces to draw, at most %d", quads, LV_MAX_QUADS);
    int const clear = level_clear_faces(lv);
    if (clear > LV_MAX_CLEAR)
        return fail(&c, "too much glass and fizzler: %d faces to draw, at most %d", clear, LV_MAX_CLEAR);
    if (steps != NULL) {
        steps[ns] = (step_t){0};
        if (n_steps) *n_steps = ns;
    }
    return true;
}

// --- Writing --------------------------------------------------------------

typedef struct {
    char*  p;
    size_t n, len;
    bool   full;
} sink_t;

static void put(sink_t* o, char const* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int const k = vsnprintf(o->p + o->len, o->len < o->n ? o->n - o->len : 0, fmt, ap);
    va_end(ap);
    if (k < 0 || o->len + (size_t)k >= o->n) {
        o->full = true;
        return;
    }
    o->len += (size_t)k;
}

char chamber_cell_char(level_t const* lv, int x, int y, int z) {
    for (int i = 0; i < lv->n_buttons; i++)
        if (lv->buttons[i].x == x && lv->buttons[i].y + 1 == y && lv->buttons[i].z == z)
            return chamber_button_char(lv->buttons[i].link);
    for (int i = 0; i < lv->n_cubes; i++)
        if ((int)floorf(lv->cubes[i].x) == x && (int)floorf(lv->cubes[i].y) == y && (int)floorf(lv->cubes[i].z) == z)
            return 'C';
    if ((int)floorf(lv->spawn.x) == x && (int)floorf(lv->spawn.y) == y && (int)floorf(lv->spawn.z) == z) return 'S';
    if (lv->n_platforms) {
        platform_t const* p = &lv->platform;
        if ((float)x >= p->lo.x && (float)x < p->hi.x && (float)y >= p->lo.y && (float)y < p->hi.y &&
            (float)z >= p->lo.z && (float)z < p->hi.z)
            return 'M';
        if (x == (int)(p->lo.x + p->travel.x) && y == (int)(p->lo.y + p->travel.y) && z == (int)(p->lo.z + p->travel.z))
            return 'N';
    }
    for (int i = 0; i < lv->n_jumps; i++)
        if ((int)floorf(lv->jumps[i].target.x) == x && (int)floorf(lv->jumps[i].target.y) == y &&
            (int)floorf(lv->jumps[i].target.z) == z)
            return 'T';
    switch (level_get(lv, x, y, z)) {
        case MAT_GLASS:
            return 'G';
        case MAT_FIZZ:
            return 'F';
        case MAT_JUMP:
            return 'J';
        case MAT_AIR:
            return '.';
        case MAT_WHITE:
            return 'W';
        case MAT_GOO:
            return '~';
        case MAT_EXIT:
            return 'E';
        case MAT_DOOR: {
            int const d = level_door_at(lv, x, y, z);
            return chamber_door_char(d >= 0 ? lv->doors[d].link : 0);
        }
        default:
            return '#';
    }
}

char const* chamber_facing_name(float yaw) {
    float const deg = yaw / DEG;
    long const  r   = lroundf(deg);
    if (fabsf(deg - (float)r) > 0.001f) return NULL;  // 89.6 is not east
    int const d = (int)(((r % 360) + 360) % 360);
    return d == 0 ? "north" : d == 90 ? "east" : d == 180 ? "south" : d == 270 ? "west" : NULL;
}

int chamber_write(level_t const* lv, step_t const* steps, int n_steps, char* out, size_t out_n) {
    sink_t o = {out, out_n, 0, false};
    if (out_n) out[0] = '\0';
    put(&o, "name: %s\n", lv->name);
    if (lv->hint[0]) put(&o, "hint: %s\n", lv->hint);
    put(&o, "size: %d %d %d\n", lv->w, lv->h, lv->d);
    char const* f = chamber_facing_name(lv->spawn_yaw);
    if (f)
        put(&o, "facing: %s\n", f);
    else
        put(&o, "facing: %g\n", (double)(lv->spawn_yaw / DEG));
    for (int y = 0; y < lv->h; y++) {
        bool all_metal = true;
        for (int z = 0; z < lv->d && all_metal; z++)
            for (int x = 0; x < lv->w && all_metal; x++)
                if (chamber_cell_char(lv, x, y, z) != '#') all_metal = false;
        if (all_metal) continue;  // a missing layer is metal
        put(&o, "\nlayer %d\n", y);
        for (int z = lv->d - 1; z >= 0; z--) {
            char row[LV_MAX_W + 2];
            for (int x = 0; x < lv->w; x++) row[x] = chamber_cell_char(lv, x, y, z);
            row[lv->w]     = '\n';
            row[lv->w + 1] = '\0';
            put(&o, "%s", row);
        }
    }
    if (steps != NULL && n_steps > 0) {
        static char const* const colour[2] = {"blue", "orange"};
        put(&o, "\nsolution\n");
        for (int i = 0; i < n_steps; i++) {
            step_t const* s = &steps[i];
            switch (s->op) {
                case OP_SHOOT:
                    put(&o, "shoot %s %g %g %g\n", colour[s->which & 1], (double)s->a, (double)s->b, (double)s->c);
                    break;
                case OP_SHOOT_VIEW:
                    put(&o, "shoot_view %s\n", colour[s->which & 1]);
                    break;
                case OP_FACE:
                    put(&o, "face %g %g\n", (double)(s->a / DEG), (double)(s->b / DEG));
                    break;
                case OP_FACE_POINT:
                    put(&o, "look %g %g %g\n", (double)s->a, (double)s->b, (double)s->c);
                    break;
                case OP_WALK:
                    put(&o, "walk %g\n", (double)s->a);
                    break;
                case OP_WALK_TO:
                    put(&o, "walk_to %g %g %g\n", (double)s->a, (double)s->c, (double)s->b);
                    break;
                case OP_STEP_OFF:
                    put(&o, "step_off %g\n", (double)s->a);
                    break;
                case OP_WAIT:
                    put(&o, "wait %g\n", (double)s->a);
                    break;
                case OP_USE:
                    put(&o, "use\n");
                    break;
                case OP_GRAB:
                    put(&o, "grab\n");
                    break;
                default:
                    break;
            }
        }
    }
    return o.full ? -1 : (int)o.len;
}

// --- The list ---------------------------------------------------------

typedef struct {
    char        id[CHAMBER_ID_N];
    char const* text;
    bool        owned;  // read from a file: ours to free
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

int chamber_find(char const* id) {
    init();
    for (int i = 0; i < s_n; i++)
        if (strcmp(s_list[i].id, id) == 0) return i;
    return -1;
}

char const* chamber_text(int i) {
    init();
    return i >= 0 && i < s_n ? s_list[i].text : "";
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
static int  s_listed;                  // .txt files the last list_dir saw, kept or not

// Keep `name` if it is among the CHAMBER_MAX first by name so far: the
// directory hands names out in its own order, and the cut must not depend
// on that.
static void keep_name(int* n, char const* name, size_t len) {
    s_listed++;
    int slot = *n;
    if (*n == CHAMBER_MAX) {
        slot = 0;
        for (int i = 1; i < *n; i++)
            if (strcmp(s_found[i], s_found[slot]) > 0) slot = i;
        if (strcmp(name, s_found[slot]) >= 0) return;
    } else {
        (*n)++;
    }
    memcpy(s_found[slot], name, len + 1);
}

#ifdef ESP_PLATFORM
// Graceloader exports no opendir / readdir: list through FatFs, whose
// paths are volume-relative. Try the plausible spellings of the VFS path
// and keep whichever opens (as SynthMiner's vfs_compat.c and the
// engine's se_mp3.c do, for the same reason).
static int list_dir(char const* dir) {
    char const* rel = dir;
    if (strncmp(dir, "/sd", 3) == 0)
        rel = dir + 3;
    else if (strncmp(dir, "/int", 4) == 0)
        rel = dir + 4;
    static FF_DIR  d;
    static FILINFO info;
    char           cand[160];
    bool           open = false;
    for (int i = 0; i < 4 && !open; i++) {
        switch (i) {
            case 0:
                snprintf(cand, sizeof(cand), "%s", rel);
                break;
            case 1:
                snprintf(cand, sizeof(cand), "0:%s", rel);
                break;
            case 2:
                snprintf(cand, sizeof(cand), "1:%s", rel);
                break;
            default:
                snprintf(cand, sizeof(cand), "%s", dir);
                break;
        }
        open = f_opendir(&d, cand) == FR_OK;
    }
    s_listed = 0;
    if (!open) return 0;
    int n = 0;
    while (f_readdir(&d, &info) == FR_OK && info.fname[0] != '\0') {
        size_t const len = strlen(info.fname);
        if ((info.fattrib & AM_DIR) || len >= sizeof(s_found[0]) || !ends_txt(info.fname, len)) continue;
        keep_name(&n, info.fname, len);
    }
    f_closedir(&d);
    return n;
}
#else
#include <dirent.h>
static int list_dir(char const* dir) {
    s_listed = 0;
    DIR* d   = opendir(dir);
    if (d == NULL) return 0;
    int            n = 0;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        size_t const len = strlen(e->d_name);
        if (len >= sizeof(s_found[0]) || !ends_txt(e->d_name, len)) continue;
        keep_name(&n, e->d_name, len);
    }
    closedir(d);
    return n;
}
#endif

int chamber_builtin_n(void) {
    return chamber_builtin_count < CHAMBER_MAX ? chamber_builtin_count : CHAMBER_MAX;
}

int chamber_reload_dir(char const* dir) {
    init();
    for (int i = chamber_builtin_n(); i < s_n; i++)
        if (s_list[i].owned) free((void*)s_list[i].text);
    s_n = chamber_builtin_n();
    return chamber_load_dir(dir);
}

int chamber_load_dir(char const* dir) {
    init();
    char const* order[CHAMBER_MAX];
    int const   n = list_dir(dir);
    for (int i = 0; i < n; i++) order[i] = s_found[i];
    qsort(order, (size_t)n, sizeof(order[0]), by_name);

    int added = 0, i = 0;
    for (; i < n && s_n < CHAMBER_MAX; i++) {
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
        s_list[s_n].owned  = true;
        s_list[s_n++].text = text;
        LOGI("%s: %s", path, check.name);
        added++;
    }
    int const left = (n - i) + (s_listed - n);
    if (left > 0) LOGW("%s: %d more chamber files than the list has room for -- not loaded", dir, left);
    return added;
}
