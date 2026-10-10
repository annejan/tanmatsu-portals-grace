#include "pack.h"
#include "card.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#define LOGW(...) ESP_LOGW("pack", __VA_ARGS__)
#define LOGI(...) ESP_LOGI("pack", __VA_ARGS__)
#else
#define LOGW(...) (fprintf(stderr, "pack: " __VA_ARGS__), fputc('\n', stderr))
#define LOGI(...) ((void)0)
#endif

static pack_t s_packs[PACK_MAX];
static int    s_n;

int pack_count(void) {
    return s_n;
}

pack_t const* pack_get(int i) {
    return i >= 0 && i < s_n ? &s_packs[i] : NULL;
}

int pack_of(int chamber, int* at) {
    for (int i = 0; i < s_n; i++)
        for (int k = 0; k < s_packs[i].n; k++)
            if (s_packs[i].chamber[k] == chamber) {
                if (at != NULL) *at = k;
                return i;
            }
    return -1;
}

static char* trim(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    char* e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = '\0';
    return s;
}

bool pack_parse(char const* text, pack_t* p, char files[][CHAMBER_ID_N], int* n_files) {
    *n_files = 0;
    // A desk story's rounds, until it is known to be one.
    static char rounds[PACK_CHAMBERS][CHAMBER_ID_N];
    int         n_rounds = 0, route = 0;
    p->desk              = false;
    p->outro[0]          = '\0';
    p->n_routes          = 0;
    p->choose[0]         = '\0';
    memset(p->route_of, 0, sizeof(p->route_of));
    char        line[256];
    char const* at = text;
    if (strncmp(at, "\xEF\xBB\xBF", 3) == 0) at += 3;
    while (*at != '\0') {
        size_t k = 0;
        while (*at != '\0' && *at != '\n') {
            if (k + 1 < sizeof(line)) line[k++] = *at;
            at++;
        }
        if (*at == '\n') at++;
        line[k]       = '\0';
        char* const s = trim(line);
        if (strncmp(s, "name:", 5) == 0)
            snprintf(p->name, sizeof(p->name), "%s", trim(s + 5));
        else if (strncmp(s, "author:", 7) == 0)
            snprintf(p->author, sizeof(p->author), "%s", trim(s + 7));
        else if (strncmp(s, "about:", 6) == 0)
            snprintf(p->about, sizeof(p->about), "%s", trim(s + 6));
        else if (strncmp(s, "ending:", 7) == 0)
            snprintf(p->ending, sizeof(p->ending), "%s", trim(s + 7));
        else if (strncmp(s, "chamber:", 8) == 0 && *n_files < PACK_CHAMBERS)
            snprintf(files[(*n_files)++], CHAMBER_ID_N, "%s", trim(s + 8));
        else if (strncmp(s, "round:", 6) == 0 && n_rounds < PACK_CHAMBERS) {
            p->route_of[n_rounds] = (uint8_t)route;
            snprintf(rounds[n_rounds++], CHAMBER_ID_N, "%s", trim(s + 6));
        } else if (strncmp(s, "choose:", 7) == 0)
            snprintf(p->choose, sizeof(p->choose), "%s", trim(s + 7));
        else if (strncmp(s, "route:", 6) == 0) {
            // id | title | about
            if (p->n_routes >= PACK_ROUTES) {
                LOGW("more than %d routes -- the rest left out", PACK_ROUTES);
                route = 255;  // its rounds belong to no route anyone can pick
                continue;
            }
            pack_route_t* r     = &p->routes[p->n_routes++];
            char*         title = strchr(s + 6, '|');
            char*         about = title ? strchr(title + 1, '|') : NULL;
            if (title) *title++ = '\0';
            if (about) *about++ = '\0';
            snprintf(r->id, sizeof(r->id), "%s", trim(s + 6));
            if (title)
                snprintf(r->title, sizeof(r->title), "%s", trim(title));
            else
                memcpy(r->title, r->id, sizeof(r->id));
            snprintf(r->about, sizeof(r->about), "%s", about ? trim(about) : "");
            for (char* u = r->id; *u; u++)
                if (*u >= 'a' && *u <= 'z') *u = (char)(*u - 'a' + 'A');
            route = p->n_routes;
        } else if (strcmp(s, "join") == 0)
            route = 0;
        else if (strncmp(s, "outro:", 6) == 0)
            snprintf(p->outro, sizeof(p->outro), "%s", trim(s + 6));
        else if (strncmp(s, "frame:", 6) == 0) {
            // A frame this build does not know: its "chamber:" lines, as an
            // older build would.
            p->desk = strcmp(trim(s + 6), "desk") == 0;
            if (!p->desk) LOGW("frame: %s -- not known to this build", trim(s + 6));
        }
    }
    if (p->desk && n_rounds > 0) {
        memcpy(files, rounds, sizeof(rounds[0]) * (size_t)n_rounds);
        *n_files = n_rounds;
    } else {
        p->desk     = false;
        p->outro[0] = '\0';
    }
    return *n_files > 0;
}

// The pack in folder `dir`/`id`: its pack.txt, and its chambers added.
static bool load_one(char const* dir, char const* id, pack_t* p) {
    memset(p, 0, sizeof(*p));
    p->outro_chamber = -1;
    snprintf(p->id, sizeof(p->id), "%.31s", id);
    snprintf(p->name, sizeof(p->name), "%.47s", id);
    char        path[320];
    char        folder[200];
    static char files[PACK_CHAMBERS][CHAMBER_ID_N];
    int         n = 0;
    snprintf(folder, sizeof(folder), "%.150s/%.31s", dir, id);
    // pack.txt, if there is one.
    snprintf(path, sizeof(path), "%s/pack.txt", folder);
    FILE* f = card_fopen(path, "rb");
    if (f != NULL) {
        static char  text[4096];
        size_t const got = fread(text, 1, sizeof(text) - 1, f);
        fclose(f);
        text[got] = '\0';
        pack_parse(text, p, files, &n);
    }
    // Without "chamber:" lines: every chamber file there, by name.
    if (n == 0) {
        char const* names[PACK_CHAMBERS + 1];
        int const   k = chamber_list_dir(folder, names, PACK_CHAMBERS + 1);
        for (int i = 0; i < k && n < PACK_CHAMBERS; i++)
            if (strcmp(names[i], "pack.txt") != 0)
                snprintf(files[n++], CHAMBER_ID_N, "%.*s", (int)strlen(names[i]) - 4, names[i]);
    }
    for (int i = 0; i < n; i++) {
        // Its id "<pack>/<file>" must fit whole: cut short, two could clash.
        if (strlen(id) + 1 + strlen(files[i]) >= CHAMBER_ID_N) {
            LOGW("%s/%s: name too long -- skipped", folder, files[i]);
            continue;
        }
        char cid[32 + CHAMBER_ID_N];  // under CHAMBER_ID_N: checked above
        snprintf(path, sizeof(path), "%s/%.63s.txt", folder, files[i]);
        snprintf(cid, sizeof(cid), "%.31s/%.63s", id, files[i]);
        int const c     = chamber_find(cid) >= 0 ? chamber_find(cid) : chamber_load_file(path, cid);
        bool      twice = false;  // the same chamber twice: the story would never end
        for (int k = 0; k < p->n; k++) twice |= p->chamber[k] == c;
        if (twice) LOGW("%s: %s twice -- once is enough", folder, files[i]);
        if (c >= 0 && !twice) {
            p->route_of[p->n] = p->route_of[i];  // p->n <= i: read before it is written over
            p->chamber[p->n++] = c;
        }
    }
    if (p->outro[0] != '\0') {
        char cid[32 + CHAMBER_ID_N];
        if (strlen(id) + 1 + strlen(p->outro) >= CHAMBER_ID_N) {
            LOGW("%s/%s: name too long -- no outro", folder, p->outro);
        } else {
            snprintf(path, sizeof(path), "%s/%.63s.txt", folder, p->outro);
            snprintf(cid, sizeof(cid), "%.31s/%.63s", id, p->outro);
            p->outro_chamber = chamber_find(cid) >= 0 ? chamber_find(cid) : chamber_load_file(path, cid);
        }
    }
    if (p->n == 0) LOGW("%s: no chamber in it", folder);
    return p->n > 0;
}

int pack_load(char const* dir) {
    s_n = 0;
    char const* names[PACK_MAX];
    static char ids[PACK_MAX][32];
    int const   n = chamber_list_subdirs(dir, names, PACK_MAX);
    for (int i = 0; i < n; i++)
        snprintf(ids[i], sizeof(ids[i]), "%s", names[i]);  // names are overwritten as packs load
    for (int i = 0; i < n && s_n < PACK_MAX; i++)
        if (strlen(names[i]) >= sizeof(ids[i]))
            LOGW("%s/%s: folder name too long -- skipped", dir, names[i]);
        else if (load_one(dir, ids[i], &s_packs[s_n])) {
            LOGI("%s: %s, %d chambers", ids[i], s_packs[s_n].name, s_packs[s_n].n);
            s_n++;
        }
    return s_n;
}
