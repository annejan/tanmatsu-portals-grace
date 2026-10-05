#include "draft.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "chamber.h"

#define DEG (3.14159265f / 180.0f)

static bool inside(draft_t const* d, int x, int y, int z) {
    return x >= 0 && y >= 0 && z >= 0 && x < d->w && y < d->h && z < d->d;
}

char draft_get(draft_t const* d, int x, int y, int z) {
    return inside(d, x, y, z) ? d->grid[y][z][x] : '#';
}

void draft_paint(draft_t* d, int x, int y, int z, char ch) {
    if (!inside(d, x, y, z)) return;
    if (ch == 'S')
        for (int yy = 0; yy < d->h; yy++)
            for (int zz = 0; zz < d->d; zz++)
                for (int xx = 0; xx < d->w; xx++)
                    if (d->grid[yy][zz][xx] == 'S') d->grid[yy][zz][xx] = '.';
    d->grid[y][z][x] = ch;
}

void draft_new(draft_t* d, char const* id, int w, int h, int dep) {
    memset(d, 0, sizeof(*d));
    snprintf(d->id, sizeof(d->id), "%s", id);
    snprintf(d->name, sizeof(d->name), "%s", id);
    d->timer = LV_TIMER_S;
    d->w     = w;
    d->h     = h;
    d->d     = dep;
    for (int y = 0; y < h; y++)
        for (int z = 0; z < dep; z++)
            for (int x = 0; x < w; x++) {
                bool const edge  = x == 0 || z == 0 || x == w - 1 || z == dep - 1;
                bool const floor = y == 0, roof = y == h - 1;
                char       c = '#';
                if (!edge && !floor && !roof)
                    c = '.';
                else if (floor && !edge)
                    c = 'W';
                else if (edge && !floor && !roof && !(x == 0 && z == 0) && !(x == 0 && z == dep - 1) &&
                         !(x == w - 1 && z == 0) && !(x == w - 1 && z == dep - 1))
                    c = 'W';  // the walls, not the corner columns
                d->grid[y][z][x] = c;
            }
    draft_paint(d, w / 2, 1, dep / 2, 'S');
}

// The solution section: from the line that reads "solution" and nothing
// else, as the parser finds it. (A map row cannot read that: 's' is not a
// cell, so in a file that parses this is the one.)
static char const* solution_line(char const* text) {
    for (char const* line = text; *line != '\0';) {
        char const* const end = strchr(line, '\n');
        char const*       a   = line;
        char const*       b   = end != NULL ? end : line + strlen(line);
        while (a < b && isspace((unsigned char)*a)) a++;
        while (b > a && isspace((unsigned char)b[-1])) b--;
        if (b - a == 8 && strncmp(a, "solution", 8) == 0) return line;
        if (end == NULL) break;
        line = end + 1;
    }
    return NULL;
}

bool draft_from_text(draft_t* d, char const* id, char const* text, char* err, size_t err_n) {
    static level_t lv;  // static: a level is too big for the stack
    if (!chamber_parse(text, &lv, NULL, NULL, err, err_n)) return false;
    // The solution, kept as text: the editor does not change it. Cut short,
    // the file would no longer read back once saved.
    char const* const sol = solution_line(text);
    if (sol != NULL && strlen(sol) >= sizeof(d->solution)) {
        snprintf(err, err_n, "its solution is longer than the editor keeps (%d bytes)", (int)sizeof(d->solution) - 1);
        return false;
    }
    memset(d, 0, sizeof(*d));
    snprintf(d->id, sizeof(d->id), "%s", id);
    snprintf(d->name, sizeof(d->name), "%s", lv.name);
    snprintf(d->hint, sizeof(d->hint), "%s", lv.hint);
    snprintf(d->story, sizeof(d->story), "%s", lv.story);
    d->timer = lv.timer;
    d->w     = lv.w;
    d->h     = lv.h;
    d->d     = lv.d;
    d->yaw   = lv.spawn_yaw;
    for (int y = 0; y < d->h; y++)
        for (int z = 0; z < d->d; z++)
            for (int x = 0; x < d->w; x++) d->grid[y][z][x] = chamber_cell_char(&lv, x, y, z);
    if (sol != NULL) snprintf(d->solution, sizeof(d->solution), "%s", sol);
    return true;
}

static int write_text(draft_t const* d, char* out, size_t n, bool solution) {
    size_t len = 0;
#define PUT(...)                                                                \
    do {                                                                        \
        int const k_ = snprintf(out + len, len < n ? n - len : 0, __VA_ARGS__); \
        if (k_ < 0 || len + (size_t)k_ >= n) return -1;                         \
        len += (size_t)k_;                                                      \
    } while (0)
    PUT("name: %s\n", d->name);
    if (d->hint[0]) PUT("hint: %s\n", d->hint);
    if (d->story[0]) PUT("story: %s\n", d->story);
    if (d->timer != LV_TIMER_S) PUT("timer: %g\n", (double)d->timer);
    PUT("size: %d %d %d\n", d->w, d->h, d->d);
    char const* const facing = chamber_facing_name(d->yaw);
    if (facing != NULL)
        PUT("facing: %s\n", facing);
    else
        PUT("facing: %g\n", (double)(d->yaw / DEG));
    for (int y = 0; y < d->h; y++) {
        bool metal = true;
        for (int z = 0; z < d->d && metal; z++)
            for (int x = 0; x < d->w && metal; x++)
                if (d->grid[y][z][x] != '#') metal = false;
        if (metal) continue;  // a missing layer is metal
        PUT("\nlayer %d\n", y);
        for (int z = d->d - 1; z >= 0; z--) PUT("%.*s\n", d->w, d->grid[y][z]);
    }
    if (solution && d->solution[0]) PUT("\n%s", d->solution);
#undef PUT
    return (int)len;
}

int draft_text(draft_t const* d, char* out, size_t n) {
    return write_text(d, out, n, true);
}

int draft_save_text(draft_t const* d, char* out, size_t n, char* err, size_t err_n) {
    static level_t check;  // static: a level is too big for the stack
    int const      len = write_text(d, out, n, true);
    if (len < 0) {
        snprintf(err, err_n, "too big to write out");
        return -1;
    }
    return chamber_parse(out, &check, NULL, NULL, err, err_n) ? len : -1;
}

void draft_fresh_id(char const* dir, char const* base, char* out, size_t n) {
    for (int k = 0; k < 100; k++) {
        if (base != NULL && k == 0)
            snprintf(out, n, "my-%s", base);
        else
            snprintf(out, n, "my-%02d", k + (base == NULL ? 1 : 0));
        char        path[192];
        struct stat st;
        snprintf(path, sizeof(path), "%s/%s.txt", dir, out);
        if (chamber_find(out) < 0 && stat(path, &st) != 0) return;
    }
}

bool draft_level(draft_t const* d, level_t* lv, char* err, size_t err_n) {
    static char text[24 * 1024];
    if (write_text(d, text, sizeof(text), false) < 0) {
        snprintf(err, err_n, "too big to write out");
        return false;
    }
    return chamber_parse(text, lv, NULL, NULL, err, err_n);
}

void draft_resize(draft_t* d, int w, int h, int dep) {
    if (w < 3) w = 3;
    if (h < 3) h = 3;
    if (dep < 3) dep = 3;
    if (w > LV_MAX_W) w = LV_MAX_W;
    if (h > LV_MAX_H) h = LV_MAX_H;
    if (dep > LV_MAX_D) dep = LV_MAX_D;
    // New cells are metal, and so are the ones cut off: growing it again
    // later must not bring back what was there.
    for (int y = 0; y < LV_MAX_H; y++)
        for (int z = 0; z < LV_MAX_D; z++)
            for (int x = 0; x < LV_MAX_W; x++)
                if (x >= d->w || y >= d->h || z >= d->d || x >= w || y >= h || z >= dep) d->grid[y][z][x] = '#';
    d->w = w;
    d->h = h;
    d->d = dep;
}
