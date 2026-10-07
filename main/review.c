#include "review.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "chamber.h"

typedef enum {
    K_EVER,    // a bit of track_t.ever
    K_SEEN,    // a bit of track_t.seen
    K_SHOTS,   // at most N portals placed
    K_BUTTON,  // button N [by ...]
    K_DOOR,    // door X
    K_PLATE,   // launch [N]
    K_CUBE_PLATE,
} kind_t;

typedef struct {
    char const* word;
    uint8_t     kind;
    uint32_t    bit;
} term_def_t;

// Longest first where one begins another ("cube portal", "portal").
static term_def_t const s_terms[] = {
    {"cube portal", K_SEEN, TRACK_CUBE_PORTAL},
    {"cube launch", K_CUBE_PLATE, 0},
    {"ride holding", K_SEEN, TRACK_RIDE_HOLD},
    {"portal", K_EVER, GAME_EV_SHOT_BLUE | GAME_EV_SHOT_ORANGE},
    {"shots", K_SHOTS, 0},
    {"pickup", K_EVER, GAME_EV_PICKUP},
    {"press", K_EVER, GAME_EV_PRESS},
    {"dropper", K_EVER, GAME_EV_DROPPER},
    {"button", K_BUTTON, 0},
    {"door", K_DOOR, 0},
    {"launch", K_PLATE, 0},
    {"bounce", K_SEEN, TRACK_BOUNCE},
    {"paint", K_EVER, GAME_EV_PAINT},
    {"pellet", K_EVER, GAME_EV_CAUGHT},
    {"topple", K_EVER, GAME_EV_TOPPLE},
    {"spotted", K_EVER, GAME_EV_SPOTTED},
    {"fizzle", K_EVER, GAME_EV_FIZZLE},
    {"burn", K_EVER, GAME_EV_BURN},
    {"speed", K_SEEN, TRACK_SPEED},
    {"float", K_SEEN, TRACK_FLOAT},
    {"ride", K_SEEN, TRACK_RIDE},
    {"bridge", K_SEEN, TRACK_BRIDGE},
    {"platform", K_SEEN, TRACK_PLATFORM},
};
#define N_TERMS ((int)(sizeof(s_terms) / sizeof(s_terms[0])))

static char const* const s_by[] = {"player", "cube", "sphere", "turret", "fallen", "beam", "pellet", "hand"};

typedef struct {
    char*  err;
    size_t n;
    int    line;
} ctx_t;

static bool fail(ctx_t* c, char const* fmt, ...) {
    if (c->n == 0) return false;
    int k = c->line > 0 ? snprintf(c->err, c->n, "line %d: ", c->line) : 0;
    if (k < 0 || (size_t)k >= c->n) return false;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->err + k, c->n - (size_t)k, fmt, ap);
    va_end(ap);
    return false;
}

static char const* skip(char const* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

// `word` at s, as a whole word: past it, or NULL.
static char const* word(char const* s, char const* w) {
    size_t const n = strlen(w);
    if (strncmp(s, w, n) != 0 || isalnum((unsigned char)s[n])) return NULL;
    return skip(s + n);
}

static char const* number(char const* s, int* out) {
    if (!isdigit((unsigned char)*s)) return NULL;
    int v = 0;
    while (isdigit((unsigned char)*s) && v < 10000) v = v * 10 + (*s++ - '0');
    if (isdigit((unsigned char)*s)) return NULL;
    *out = v;
    return skip(s);
}

// One term, from `s` to `end`.
static bool parse_term(ctx_t* c, char const* s, char const* end, review_term_t* t) {
    char buf[64];
    while (end > s && (end[-1] == ' ' || end[-1] == '\t')) end--;
    s = skip(s);
    if (end - s <= 0) return fail(c, "an empty term");
    if (end - s >= (long)sizeof(buf)) return fail(c, "a term longer than %d characters", (int)sizeof(buf) - 1);
    memcpy(buf, s, (size_t)(end - s));
    buf[end - s]  = '\0';
    char const* p = buf;
    *t            = (review_term_t){.arg = -1};
    char const* q = word(p, "no");
    if (q != NULL) t->no = true, p = q;
    int k = 0;
    for (; k < N_TERMS; k++)
        if ((q = word(p, s_terms[k].word)) != NULL) break;
    if (k == N_TERMS) return fail(c, "unknown term \"%s\"", p);
    t->what = (uint8_t)k;
    p       = q;
    int n   = 0;
    switch (s_terms[k].kind) {
        case K_SHOTS:
            if ((p = number(p, &n)) == NULL || n > 100) return fail(c, "shots: how many, at most, 0-100");
            t->arg = (int8_t)n;
            break;
        case K_BUTTON:
            if ((p = number(p, &n)) == NULL || n < 1 || n > LV_MAX_BUTTONS)
                return fail(c, "button: its number, 1-%d", LV_MAX_BUTTONS);
            t->arg = (int8_t)(n - 1);
            if ((q = word(p, "by")) != NULL) {
                p     = q;
                int b = 0;
                for (; b < (int)(sizeof(s_by) / sizeof(s_by[0])); b++)
                    if ((q = word(p, s_by[b])) != NULL) break;
                if (b == (int)(sizeof(s_by) / sizeof(s_by[0])))
                    return fail(c, "button %d by: player, cube, sphere, turret, fallen, beam, pellet or hand", n);
                t->by = (uint8_t)(1u << b);
                p     = q;
            }
            break;
        case K_DOOR:
            if (!chamber_is_door(*p) || isalnum((unsigned char)p[1]))
                return fail(c, "door: its letter, a-%c", chamber_door_char(LV_MAX_DOORS - 1));
            t->arg = (int8_t)(*p - 'a');
            p      = skip(p + 1);
            break;
        case K_PLATE:
        case K_CUBE_PLATE:
            if (*p != '\0') {
                if ((p = number(p, &n)) == NULL || n < 1 || n > LV_MAX_JUMPS)
                    return fail(c, "launch: which faith plate, 1-%d, in reading order; or none for any", LV_MAX_JUMPS);
                t->arg = (int8_t)(n - 1);
            }
            break;
        default:
            break;
    }
    if (*p != '\0') return fail(c, "\"%s\" after the term", p);
    return true;
}

// Terms, comma-separated, from `s` to `end`.
static bool parse_cond(ctx_t* c, char const* s, char const* end, review_cond_t* cond) {
    cond->n = 0;
    while (s < end) {
        char const* comma = memchr(s, ',', (size_t)(end - s));
        if (comma == NULL) comma = end;
        if (cond->n >= REVIEW_TERMS) return fail(c, "more than %d terms", REVIEW_TERMS);
        if (!parse_term(c, s, comma, &cond->t[cond->n++])) return false;
        if (comma < end && comma + 1 >= end) return fail(c, "an empty term");  // a comma at the end
        s = comma + 1;
    }
    if (cond->n == 0) return fail(c, "no terms");
    return true;
}

// x, y or z: N or lo-hi, below `max`.
static char const* range(char const* s, int max, uint8_t* lo, uint8_t* hi) {
    int a = 0, b = 0;
    if (!isdigit((unsigned char)*s)) return NULL;
    while (isdigit((unsigned char)*s) && a < 1000) a = a * 10 + (*s++ - '0');
    b = a;
    if (*s == '-') {
        s++;
        if (!isdigit((unsigned char)*s)) return NULL;
        b = 0;
        while (isdigit((unsigned char)*s) && b < 1000) b = b * 10 + (*s++ - '0');
    }
    if ((*s != ' ' && *s != '\t') || a > b || b >= max) return NULL;
    *lo = (uint8_t)a;
    *hi = (uint8_t)b;
    return skip(s);
}

// "x y z FT": cells, from F to T.
static bool parse_cells(ctx_t* c, char const* s, review_fix_t* f, char const* key) {
    if ((s = range(s, LV_MAX_W, &f->x0, &f->x1)) == NULL || (s = range(s, LV_MAX_H, &f->y0, &f->y1)) == NULL ||
        (s = range(s, LV_MAX_D, &f->z0, &f->z1)) == NULL)
        return fail(c, "%s: x y z (each N or lo-hi), then the cell before and after, as in \".#\"", key);
    if (chamber_glyph(s[0]) == NULL || chamber_glyph(s[1]) == NULL || s[0] == s[1] || *skip(s + 2) != '\0')
        return fail(c, "%s: the cell before and after, two map characters, as in \".#\"", key);
    f->from = s[0];
    f->to   = s[1];
    return true;
}

static bool next_line(char const** p, char* out, size_t n, bool* too_long) {
    if (**p == '\0') return false;
    char const* e = strchr(*p, '\n');
    size_t      k = e != NULL ? (size_t)(e - *p) : strlen(*p);
    *too_long     = k >= n;
    if (k >= n) k = n - 1;
    memcpy(out, *p, k);
    out[k] = '\0';
    if (k > 0 && out[k - 1] == '\r') out[k - 1] = '\0';
    *p = e != NULL ? e + 1 : *p + strlen(*p);
    return true;
}

bool review_parse(char const* text, review_t* r, char* err, size_t err_n) {
    ctx_t c = {err, err_n, 0};
    if (err_n) err[0] = '\0';
    memset(r, 0, sizeof(*r));
    char        buf[256];
    bool        too_long      = false;
    bool        have_intended = false, have_debug = false;
    char const* p = text;
    if (strncmp(p, "\xEF\xBB\xBF", 3) == 0) p += 3;
    while (next_line(&p, buf, sizeof(buf), &too_long)) {
        c.line++;
        char const* s = skip(buf);
        // The header ends at the map or the solution; review keys are
        // header keys.
        if (strncmp(s, "layer", 5) == 0 || strncmp(s, "solution", 8) == 0) break;
        if (too_long) continue;  // chamber_parse() says so
        char const* colon = strchr(s, ':');
        if (colon == NULL || strncmp(s, "//", 2) == 0) continue;
        char key[16];
        int  kn = (int)(colon - s);
        while (kn > 0 && (s[kn - 1] == ' ' || s[kn - 1] == '\t')) kn--;
        if (kn >= (int)sizeof(key)) continue;
        memcpy(key, s, (size_t)kn);
        key[kn] = '\0';
        if (!chamber_review_key(key)) continue;
        char const* v   = skip(colon + 1);
        char const* end = v + strlen(v);
        while (end > v && (end[-1] == ' ' || end[-1] == '\t')) end--;

        if (strcmp(key, "review") == 0) {
            if (r->kind != REVIEW_NONE) return fail(&c, "review: given twice");
            if (end - v == 6 && strncmp(v, "flawed", 6) == 0)
                r->kind = REVIEW_FLAWED;
            else if (end - v == 5 && strncmp(v, "final", 5) == 0)
                r->kind = REVIEW_FINAL;
            else if (end - v == 6 && strncmp(v, "broken", 6) == 0)
                r->kind = REVIEW_BROKEN;
            else
                return fail(&c, "review: flawed, final or broken");
        } else if (strcmp(key, "intended") == 0) {
            if (have_intended) return fail(&c, "intended: given twice");
            if (!parse_cond(&c, v, end, &r->intended)) return false;
            have_intended = true;
        } else if (strcmp(key, "flaw") == 0) {
            if (r->n_flaws >= REVIEW_FLAWS) return fail(&c, "more than %d flaws", REVIEW_FLAWS);
            review_flaw_t* f = &r->flaws[r->n_flaws];
            if (!islower((unsigned char)v[0]) || (v[1] != ' ' && v[1] != '\t'))
                return fail(&c, "flaw: its letter, a-z, then its terms");
            if (review_flaw(r, v[0]) >= 0) return fail(&c, "flaw %c given twice", v[0]);
            f->id              = v[0];
            char const* dashes = strstr(v, "--");
            if (dashes == NULL || dashes > end) return fail(&c, "flaw %c: its terms, then -- and what it is", v[0]);
            if (!parse_cond(&c, v + 2, dashes, &f->when)) return false;
            char const* about = skip(dashes + 2);
            if (end - about <= 0) return fail(&c, "flaw %c: what it is, after --", v[0]);
            if (end - about >= REVIEW_ABOUT)
                return fail(&c, "flaw %c: what it is, longer than %d characters", v[0], REVIEW_ABOUT - 1);
            memcpy(f->about, about, (size_t)(end - about));
            r->n_flaws++;
        } else if (strcmp(key, "fix") == 0) {
            if (r->n_fixes >= REVIEW_FIXES) return fail(&c, "more than %d fixes", REVIEW_FIXES);
            review_fix_t* f = &r->fixes[r->n_fixes];
            if (!islower((unsigned char)v[0]) || (v[1] != ' ' && v[1] != '\t'))
                return fail(&c, "fix: the flaw's letter, then x y z and the cell before and after");
            f->flaw = v[0];
            if (!parse_cells(&c, skip(v + 1), f, "fix")) return false;
            r->n_fixes++;
        } else if (strcmp(key, "debug") == 0) {
            if (have_debug) return fail(&c, "debug: given twice");
            have_debug    = true;
            int         n = 0;
            char const* q = number(v, &n);
            if (q == NULL || n < 1 || n > 99)
                return fail(&c, "debug: how many changes (1-99), then what may change, as in \"#W\"");
            r->debug_budget = n;
            while (*q != '\0') {
                if (r->n_debug >= REVIEW_DEBUGS) return fail(&c, "debug: more than %d kinds of change", REVIEW_DEBUGS);
                if (chamber_glyph(q[0]) == NULL || chamber_glyph(q[1]) == NULL || q[0] == q[1] ||
                    (q[2] != '\0' && q[2] != ' ' && q[2] != '\t'))
                    return fail(&c, "debug: a change is the cell before and after, two map characters, as in \"#W\"");
                r->debug[r->n_debug][0] = q[0];
                r->debug[r->n_debug][1] = q[1];
                r->n_debug++;
                q = skip(q + 2);
            }
            if (r->n_debug == 0) return fail(&c, "debug: what may change, as in \"#W\"");
        } else {  // repair
            if (r->n_repairs >= REVIEW_FIXES) return fail(&c, "more than %d repairs", REVIEW_FIXES);
            if (!parse_cells(&c, v, &r->repairs[r->n_repairs], "repair")) return false;
            r->n_repairs++;
        }
    }

    c.line         = 0;  // what follows is about the file, not a line of it
    bool const any = have_intended || r->n_flaws || r->n_fixes || have_debug || r->n_repairs;
    if (r->kind == REVIEW_NONE) {
        if (any) return fail(&c, "review keys, but no \"review:\"");
        return true;
    }
    if (r->kind != REVIEW_BROKEN && !have_intended) return fail(&c, "no \"intended:\"");
    if (r->kind == REVIEW_FLAWED && r->n_flaws == 0) return fail(&c, "review: flawed, but no \"flaw:\"");
    if (r->kind != REVIEW_FLAWED && (r->n_flaws || r->n_fixes))
        return fail(&c, "flaws and fixes are for \"review: flawed\"");
    if (r->kind == REVIEW_BROKEN && !have_debug) return fail(&c, "review: broken, but no \"debug:\"");
    if (r->kind != REVIEW_BROKEN && (have_debug || r->n_repairs))
        return fail(&c, "debug and repair are for \"review: broken\"");
    for (int i = 0; i < r->n_fixes; i++)
        if (review_flaw(r, r->fixes[i].flaw) < 0)
            return fail(&c, "a fix for flaw %c, which is not given", r->fixes[i].flaw);
    for (int i = 0; i < r->n_flaws; i++) {
        bool fixed = false;
        for (int j = 0; j < r->n_fixes; j++) fixed |= r->fixes[j].flaw == r->flaws[i].id;
        if (!fixed) return fail(&c, "flaw %c has no fix", r->flaws[i].id);
    }
    for (int i = 0; i < r->n_repairs; i++)
        if (!review_debug_allows(r, r->repairs[i].from, r->repairs[i].to))
            return fail(&c, "a repair from '%c' to '%c', which the debugger does not allow", r->repairs[i].from,
                        r->repairs[i].to);
    return true;
}

int review_flaw(review_t const* r, char id) {
    for (int i = 0; i < r->n_flaws; i++)
        if (r->flaws[i].id == id) return i;
    return -1;
}

bool review_debug_allows(review_t const* r, char from, char to) {
    for (int i = 0; i < r->n_debug; i++)
        if (r->debug[i][0] == from && r->debug[i][1] == to) return true;
    return false;
}

static bool term_holds(review_term_t const* t, level_t const* lv, track_t const* tr) {
    term_def_t const* d = &s_terms[t->what];
    switch (d->kind) {
        case K_EVER:
            return (tr->ever & d->bit) != 0;
        case K_SEEN:
            return (tr->seen & d->bit) != 0;
        case K_SHOTS:
            return tr->shots <= t->arg;
        case K_BUTTON:
            for (int b = 0; b < lv->n_buttons; b++)
                if (lv->buttons[b].link == t->arg && (tr->by[b] & (t->by ? t->by : 0xFF)) != 0) return true;
            return false;
        case K_DOOR:
            for (int k = 0; k < lv->n_doors; k++)
                if (lv->doors[k].link == t->arg && (tr->doors & (1u << k))) return true;
            return false;
        case K_PLATE:
            return t->arg < 0 ? tr->plates != 0 : (tr->plates & (1u << t->arg)) != 0;
        case K_CUBE_PLATE:
            return t->arg < 0 ? tr->cube_plates != 0 : (tr->cube_plates & (1u << t->arg)) != 0;
    }
    return false;
}

bool review_holds(review_cond_t const* c, level_t const* lv, track_t const* t) {
    for (int i = 0; i < c->n; i++)
        if (term_holds(&c->t[i], lv, t) == c->t[i].no) return false;
    return true;
}

review_verdict_t review_judge(review_t const* r, level_t const* lv, track_t const* t) {
    review_verdict_t v = {review_holds(&r->intended, lv, t), 0};
    for (int i = 0; i < r->n_flaws; i++)
        if (review_holds(&r->flaws[i].when, lv, t)) v.flaws |= 1u << i;
    return v;
}

static bool apply(draft_t* d, review_fix_t const* f, char* err, size_t err_n) {
    for (int y = f->y0; y <= f->y1; y++)
        for (int z = f->z0; z <= f->z1; z++)
            for (int x = f->x0; x <= f->x1; x++) {
                char const now = draft_get(d, x, y, z);
                if (x >= d->w || y >= d->h || z >= d->d || now != f->from) {
                    snprintf(err, err_n, "%s %d %d %d: '%c' there, not '%c'", f->flaw ? "a fix at" : "a repair at", x,
                             y, z, now, f->from);
                    return false;
                }
                draft_paint(d, x, y, z, f->to);
            }
    return true;
}

int review_patch(char const* text, review_t const* r, uint32_t flaws, bool repair, draft_t* scratch, char* out,
                 size_t out_n, char* err, size_t err_n) {
    if (err_n) err[0] = '\0';
    if (!draft_from_text(scratch, "", text, err, err_n)) return -1;
    for (int i = 0; i < r->n_fixes; i++) {
        int const k = review_flaw(r, r->fixes[i].flaw);
        if (k >= 0 && (flaws & (1u << k)) && !apply(scratch, &r->fixes[i], err, err_n)) return -1;
    }
    if (repair)
        for (int i = 0; i < r->n_repairs; i++)
            if (!apply(scratch, &r->repairs[i], err, err_n)) return -1;
    int const n = draft_text(scratch, out, out_n);
    if (n < 0) snprintf(err, err_n, "patched, longer than %d bytes", (int)out_n - 1);
    return n;
}

int review_describe(level_t const* lv, track_t const* t, char* out, size_t n) {
    size_t len = 0;
#define ADD(...)                                                                     \
    do {                                                                             \
        int const k_ = snprintf(out + len, len < n ? n - len : 0, __VA_ARGS__);      \
        if (k_ > 0) len = len + (size_t)k_ < n ? len + (size_t)k_ : (n ? n - 1 : 0); \
    } while (0)
    if (n) out[0] = '\0';
    ADD("shots %d", t->shots);
    for (int k = 0; k < N_TERMS; k++) {
        term_def_t const* d = &s_terms[k];
        if ((d->kind == K_EVER && (t->ever & d->bit)) || (d->kind == K_SEEN && (t->seen & d->bit)))
            ADD(", %s", d->word);
    }
    for (int link = 0; link < LV_MAX_BUTTONS; link++) {
        uint8_t by = 0;  // a door's buttons are one term
        for (int b = 0; b < lv->n_buttons; b++)
            if (lv->buttons[b].link == link) by |= t->by[b];
        for (int i = 0; i < 8; i++)
            if (by & (1u << i)) ADD(", button %d by %s", link + 1, s_by[i]);
    }
    for (int k = 0; k < lv->n_doors; k++)
        if (t->doors & (1u << k)) ADD(", door %c", chamber_door_char(lv->doors[k].link));
    for (int j = 0; j < LV_MAX_JUMPS; j++) {
        if (t->plates & (1u << j)) ADD(", launch %d", j + 1);
        if (t->cube_plates & (1u << j)) ADD(", cube launch %d", j + 1);
    }
#undef ADD
    return (int)len;
}
