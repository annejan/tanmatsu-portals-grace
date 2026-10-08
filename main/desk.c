#include "desk.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chamber.h"

// --- The desk's files --------------------------------------------------------

// A file into the arena, whole; NULL if it is not there or does not fit.
static char* slurp(desk_data_t* d, char const* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return NULL;
    size_t const room = sizeof(d->arena) - d->used;
    size_t const got  = room > 0 ? fread(d->arena + d->used, 1, room - 1, f) : 0;
    char         probe;  // one byte more: it did not fit (Graceloader exports no fgetc)
    bool const   more = fread(&probe, 1, 1, f) == 1;
    fclose(f);
    if (more || room == 0) return NULL;
    char* const t  = d->arena + d->used;
    t[got]         = '\0';
    d->used       += got + 1;
    return t;
}

static char* trim(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    char* e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = '\0';
    return s;
}

static int minutes(char const* v) {
    int h = 0, m = 0;
    if (sscanf(v, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return -1;
    return h * 60 + m;
}

// "round 3", "done 1", "mail boss", "time 16:50"; false if not one.
static bool when_of(char const* v, desk_when_t* w) {
    *w    = (desk_when_t){0};
    int n = 0;
    if (sscanf(v, "round %d", &n) == 1 && n >= 0) {
        w->kind = WHEN_ROUND;
    } else if (sscanf(v, "done %d", &n) == 1 && n >= 0) {
        w->kind = WHEN_DONE;
    } else if (strncmp(v, "mail ", 5) == 0 && strlen(trim((char*)v + 5)) < sizeof(w->mail)) {
        w->kind = WHEN_MAIL;
        snprintf(w->mail, sizeof(w->mail), "%s", trim((char*)v + 5));
    } else if (strncmp(v, "time ", 5) == 0 && (n = minutes(v + 5)) >= 0) {
        w->kind = WHEN_TIME;
    } else {
        return false;
    }
    w->n = (int16_t)n;
    return true;
}

// The header lines (`key: value`) at a text's top, up to the first blank
// line or one that is not a header: each to `fn`. Returns the rest.
typedef bool (*header_fn)(char const* key, char* value, void* ctx);
static char const* headers(char* text, header_fn fn, void* ctx, bool* ok) {
    char* p = text;
    while (*p) {
        char* e    = strchr(p, '\n');
        char* next = e ? e + 1 : p + strlen(p);
        if (e) *e = '\0';
        char* const s     = trim(p);
        char* const colon = strchr(s, ':');
        bool        key   = colon != NULL && colon > s;
        for (char* c = s; key && c < colon; c++) key = isalpha((unsigned char)*c) != 0;
        if (*s == '\0' || !key) {
            if (e) *e = '\n';  // not a header: the text starts here
            return *s == '\0' ? next : p;
        }
        *colon = '\0';
        if (!fn(s, trim(colon + 1), ctx)) *ok = false;
        p = next;
    }
    return p;
}

static bool day_key(char const* k, char* v, void* ctx) {
    desk_data_t* d = ctx;
    if (strcmp(k, "user") == 0)
        snprintf(d->user, sizeof(d->user), "%s", v);
    else if (strcmp(k, "host") == 0)
        snprintf(d->host, sizeof(d->host), "%s", v);
    else if (strcmp(k, "prompt") == 0)
        snprintf(d->prompt, sizeof(d->prompt), "%s", v);
    else if (strcmp(k, "start") == 0)
        return (d->start = (int16_t)minutes(v)) >= 0;
    else if (strcmp(k, "step") == 0) {
        int h = 0, m = 0;
        if (sscanf(v, "%d:%d", &h, &m) != 2 || h < 0 || m < 0 || m > 59) return false;
        d->step = (int16_t)(h * 60 + m);
    } else
        return false;
    return true;
}

static bool file_key(char const* k, char* v, void* ctx) {
    return strcmp(k, "after") == 0 && when_of(v, ctx);
}

static bool mail_key(char const* k, char* v, void* ctx) {
    desk_mail_t* m = ctx;
    if (strcmp(k, "from") == 0)
        snprintf(m->from, sizeof(m->from), "%s", v);
    else if (strcmp(k, "subject") == 0)
        snprintf(m->subject, sizeof(m->subject), "%s", v);
    else if (strcmp(k, "after") == 0)
        return when_of(v, &m->after);
    else
        return false;
    return true;
}

static bool fail(char* err, size_t n, char const* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
    return false;
}

bool desk_load(desk_data_t* d, char const* pack_dir, char* err, size_t err_n) {
    memset(d, 0, offsetof(desk_data_t, arena));
    d->used = 0;
    snprintf(d->user, sizeof(d->user), "USER");
    snprintf(d->host, sizeof(d->host), "TERMINAL");
    snprintf(d->prompt, sizeof(d->prompt), "C:\\>");
    d->start = 9 * 60;
    d->step  = 30;
    d->boot  = "";
    if (err_n) err[0] = '\0';
    char path[256];

    snprintf(path, sizeof(path), "%.200s/desk/day.txt", pack_dir);
    char* t = slurp(d, path);
    if (t != NULL) {
        bool ok = true;
        d->boot = headers(t, day_key, d, &ok);
        if (!ok) return fail(err, err_n, "desk/day.txt: a header it does not know");
    }

    // The files, by name.
    char        dir[224];
    char const* names[DESK_MAILS + 1];
    snprintf(dir, sizeof(dir), "%.200s/desk/files", pack_dir);
    int n = chamber_list_dir(dir, names, DESK_FILES + 1);
    if (n > DESK_FILES) return fail(err, err_n, "desk/files: more than %d", DESK_FILES);
    static char keep[DESK_MAILS + 1][64];  // the listing's names: overwritten by the next
    for (int i = 0; i < n; i++) snprintf(keep[i], sizeof(keep[i]), "%s", names[i]);
    for (int i = 0; i < n; i++) {
        desk_file_t* f = &d->files[d->n_files];
        if (strlen(keep[i]) >= sizeof(f->name)) return fail(err, err_n, "desk/files/%s: name too long", keep[i]);
        for (size_t c = 0; keep[i][c]; c++) f->name[c] = (char)toupper((unsigned char)keep[i][c]);
        snprintf(path, sizeof(path), "%.190s/%.60s", dir, keep[i]);
        char* const body = slurp(d, path);
        if (body == NULL) return fail(err, err_n, "desk/files/%s: does not fit", keep[i]);
        // Its first line may say when it shows.
        f->text = body;
        if (strncmp(body, "after:", 6) == 0) {
            bool ok = true;
            f->text = headers(body, file_key, &f->after, &ok);
            if (!ok) return fail(err, err_n, "desk/files/%s: after: round N, done N, mail ID or time HH:MM", keep[i]);
        }
        d->n_files++;
    }

    // The mail, by name: NN-id.txt.
    snprintf(dir, sizeof(dir), "%.200s/desk/mail", pack_dir);
    n = chamber_list_dir(dir, names, DESK_MAILS + 1);
    if (n > DESK_MAILS) return fail(err, err_n, "desk/mail: more than %d", DESK_MAILS);
    for (int i = 0; i < n; i++) snprintf(keep[i], sizeof(keep[i]), "%s", names[i]);
    for (int i = 0; i < n; i++) {
        desk_mail_t* m    = &d->mails[d->n_mails];
        char const*  dash = strchr(keep[i], '-');
        char const*  id   = dash ? dash + 1 : keep[i];
        if (strlen(id) - 4 >= sizeof(m->id)) return fail(err, err_n, "desk/mail/%s: id too long", keep[i]);
        snprintf(m->id, sizeof(m->id), "%.*s", (int)strlen(id) - 4, id);
        snprintf(path, sizeof(path), "%.190s/%.60s", dir, keep[i]);
        char* const body = slurp(d, path);
        if (body == NULL) return fail(err, err_n, "desk/mail/%s: does not fit", keep[i]);
        bool ok = true;
        m->text = headers(body, mail_key, m, &ok);
        if (!ok) return fail(err, err_n, "desk/mail/%s: from:, subject:, after:", keep[i]);
        if (!m->subject[0]) return fail(err, err_n, "desk/mail/%s: no subject", keep[i]);
        d->n_mails++;
    }
    for (int i = 0; i < d->n_mails; i++) {
        desk_when_t const* w     = &d->mails[i].after;
        bool               known = w->kind != WHEN_MAIL;
        for (int j = 0; j < d->n_mails && !known; j++) known = strcmp(d->mails[j].id, w->mail) == 0;
        if (!known)
            return fail(err, err_n, "desk/mail: %s waits for mail %s, which is not there", d->mails[i].id, w->mail);
    }

    // The calendar: events, each `event:` then its keys.
    snprintf(path, sizeof(path), "%.200s/desk/calendar.txt", pack_dir);
    t = slurp(d, path);
    for (char* p = t; p != NULL && *p;) {
        char* e    = strchr(p, '\n');
        char* next = e ? e + 1 : p + strlen(p);
        if (e) *e = '\0';
        char* const s = trim(p);
        p             = next;
        if (*s == '\0' || strncmp(s, "//", 2) == 0) continue;
        char* colon = strchr(s, ':');
        if (colon == NULL) return fail(err, err_n, "desk/calendar.txt: \"%.30s\"?", s);
        *colon           = '\0';
        char* const   v  = trim(colon + 1);
        desk_event_t* ev = d->n_events > 0 ? &d->events[d->n_events - 1] : NULL;
        if (strcmp(s, "event") == 0) {
            if (d->n_events >= DESK_EVENTS)
                return fail(err, err_n, "desk/calendar.txt: more than %d events", DESK_EVENTS);
            ev = &d->events[d->n_events++];
            snprintf(ev->what, sizeof(ev->what), "%s", v);
            ev->at = -1;
        } else if (ev == NULL) {
            return fail(err, err_n, "desk/calendar.txt: %s: before any event", s);
        } else if (strcmp(s, "at") == 0) {
            if ((ev->at = (int16_t)minutes(v)) < 0) return fail(err, err_n, "desk/calendar.txt: at: HH:MM");
        } else if (strcmp(s, "moves") == 0) {
            // "<when> HH:MM" or "<when> never": the last word is where to.
            char* const last = strrchr(v, ' ');
            if (last == NULL || ev->n_moves >= DESK_MOVES)
                return fail(err, err_n, "desk/calendar.txt: moves: <when> HH:MM, at most %d", DESK_MOVES);
            *last        = '\0';
            int const to = strcmp(last + 1, "never") == 0 ? -1 : minutes(last + 1);
            if ((to < 0 && strcmp(last + 1, "never") != 0) || !when_of(trim(v), &ev->when[ev->n_moves]))
                return fail(err, err_n, "desk/calendar.txt: moves: <when> HH:MM or never");
            ev->to[ev->n_moves++] = (int16_t)to;
        } else {
            return fail(err, err_n, "desk/calendar.txt: %s: not known", s);
        }
    }
    for (int i = 0; i < d->n_events; i++)
        if (d->events[i].at < 0) return fail(err, err_n, "desk/calendar.txt: %s has no at:", d->events[i].what);

    // The outro's cards.
    snprintf(path, sizeof(path), "%.200s/desk/outro.txt", pack_dir);
    t = slurp(d, path);
    for (char* p = t; p != NULL && *p;) {
        char* e    = strchr(p, '\n');
        char* next = e ? e + 1 : p + strlen(p);
        if (e) *e = '\0';
        char* const s = trim(p);
        p             = next;
        if (*s == '\0' || strncmp(s, "//", 2) == 0) continue;
        if (strncmp(s, "card:", 5) == 0 && d->n_cards < DESK_CARDS)
            d->cards[d->n_cards++] = trim(s + 5);
        else if (strncmp(s, "reveal:", 7) == 0)
            d->reveal = trim(s + 7);
        else if (strncmp(s, "coda:", 5) == 0 && d->n_coda < 4)
            d->coda[d->n_coda++] = trim(s + 5);
        else
            return fail(err, err_n, "desk/outro.txt: card:, reveal: or coda: (at most %d cards, 4 coda lines)",
                        DESK_CARDS);
    }
    return true;
}

// --- The terminal ----------------------------------------------------------

int desk_clock(desk_t const* k) {
    int const t = k->d->start + k->d->step * k->s->plays;
    return t < 24 * 60 - 1 ? t : 24 * 60 - 1;
}

bool desk_done(desk_t const* k) {
    return k->s->at >= k->n_rounds;
}

static bool holds(desk_t const* k, desk_when_t const* w) {
    switch (w->kind) {
        case WHEN_ROUND:
            return k->s->plays >= w->n;
        case WHEN_DONE:
            return k->s->at >= w->n;
        case WHEN_TIME:
            return desk_clock(k) >= w->n;
        case WHEN_MAIL:
            for (int i = 0; i < k->d->n_mails; i++)
                if (strcmp(k->d->mails[i].id, w->mail) == 0) return (k->read & (1u << i)) != 0;
            return false;
        default:
            return true;
    }
}

static bool arrived(desk_t const* k, int i) {
    return holds(k, &k->d->mails[i].after);
}

// A line into the scrollback (cut at 80).
static void out(desk_t* k, char const* fmt, ...) {
    char    buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    // Wrapped at 80, at a space where there is one.
    char const* p = buf;
    do {
        size_t len = strlen(p);
        if (len > DESK_COLS) {
            len = DESK_COLS;
            for (size_t c = DESK_COLS; c > DESK_COLS / 2; c--)
                if (p[c] == ' ') {
                    len = c;
                    break;
                }
        }
        snprintf(k->lines[k->n_lines % DESK_LINES], DESK_COLS + 1, "%.*s", (int)len, p);
        k->n_lines++;
        p += len;
        while (*p == ' ') p++;
    } while (*p);
}

// Mail that has come in since last told.
static void tell_mail(desk_t* k) {
    for (int i = 0; i < k->d->n_mails; i++)
        if (!(k->told & (1u << i)) && arrived(k, i)) {
            k->told |= 1u << i;
            out(k, "* New mail from %s: %s", k->d->mails[i].from, k->d->mails[i].subject);
        }
}

static void text_out(desk_t* k, char const* text) {
    for (char const* p = text; *p;) {
        char const* e = strchr(p, '\n');
        size_t      n = e ? (size_t)(e - p) : strlen(p);
        if (n > 0 && p[n - 1] == '\r') n--;
        out(k, "%.*s", (int)n, p);
        p = e ? e + 1 : p + strlen(p);
    }
}

void desk_begin(desk_t* k, desk_data_t const* d, story_t* s, int n_rounds, bool fresh) {
    if (fresh || k->d != d) {
        memset(k, 0, sizeof(*k));
        k->d        = d;
        k->s        = s;
        k->n_rounds = n_rounds;
        text_out(k, d->boot);
        out(k, "");
        out(k, "Type HELP for a list of commands.");
    }
    k->d        = d;
    k->s        = s;
    k->n_rounds = n_rounds;
    k->view     = DV_PROMPT;
    tell_mail(k);
}

void desk_after_round(desk_t* k, story_result_t const* r) {
    out(k, "");
    out(k, "[%02d:%02d] Round %d: %s", desk_clock(k) / 60, desk_clock(k) % 60, r->round, r->headline);
    out(k, "        Score: %d", k->s->score);
    if (desk_done(k)) out(k, "Review queue empty. Nothing left to review today.");
    tell_mail(k);
    k->view = DV_PROMPT;
}

// --- Pages: TYPE, and a mail opened ------------------------------------------

// `text` wrapped into the page, a line per 81 bytes.
static void page_open(desk_t* k, char const* title, char const* text, uint8_t back) {
    int       n                 = 0;
    int const max               = (int)(sizeof(k->page) / (DESK_COLS + 1));
    char (*rows)[DESK_COLS + 1] = (char (*)[DESK_COLS + 1]) k->page;
    if (title && n < max) snprintf(rows[n++], DESK_COLS + 1, "%s", title);
    if (title && n < max) rows[n++][0] = '\0';
    for (char const* p = text; *p && n < max;) {
        char const* e   = strchr(p, '\n');
        size_t      len = e ? (size_t)(e - p) : strlen(p);
        if (len > 0 && p[len - 1] == '\r') len--;
        // One source line, wrapped.
        char const* q    = p;
        size_t      left = len;
        do {
            size_t take = left;
            if (take > DESK_COLS) {
                take = DESK_COLS;
                for (size_t c = DESK_COLS; c > DESK_COLS / 2; c--)
                    if (q[c] == ' ') {
                        take = c;
                        break;
                    }
            }
            snprintf(rows[n++], DESK_COLS + 1, "%.*s", (int)take, q);
            q    += take;
            left -= take;
            while (left > 0 && *q == ' ') q++, left--;
        } while (left > 0 && n < max);
        p = e ? e + 1 : p + strlen(p);
    }
    k->page_lines = n;
    k->page_top   = 0;
    k->page_back  = back;
    k->view       = DV_PAGE;
}

// --- Commands --------------------------------------------------------------

static void upcase(char* s) {
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

static bool file_shown(desk_t const* k, desk_file_t const* f) {
    return holds(k, &f->after);
}

static void cmd_dir(desk_t* k) {
    out(k, " Volume in drive C is APERTURE");
    out(k, " Directory of C:\\RLHF");
    out(k, "");
    int n = 0;
    for (int i = 0; i < k->d->n_files; i++)
        if (file_shown(k, &k->d->files[i])) {
            char name[16], ext[8] = "";
            snprintf(name, sizeof(name), "%s", k->d->files[i].name);
            char* dot = strchr(name, '.');
            if (dot) {
                snprintf(ext, sizeof(ext), "%.3s", dot + 1);
                *dot = '\0';
            }
            out(k, "%-8.8s %-3s %8d", name, ext, (int)strlen(k->d->files[i].text));
            n++;
        }
    out(k, "GLADOS   EXE   262144");
    out(k, "MAIL     EXE    32768");
    out(k, "CAL      EXE    16384");
    out(k, "%9d file(s)", n + 3);
}

static void cmd_type(desk_t* k, char const* arg) {
    if (*arg == '\0') {
        out(k, "Required parameter missing");
        return;
    }
    char want[32];
    snprintf(want, sizeof(want), "%s", arg);
    upcase(want);
    for (int i = 0; i < k->d->n_files; i++) {
        desk_file_t const* f = &k->d->files[i];
        char               bare[16];
        snprintf(bare, sizeof(bare), "%s", f->name);
        char* dot = strchr(bare, '.');
        if (dot) *dot = '\0';
        if (file_shown(k, f) && (strcmp(want, f->name) == 0 || strcmp(want, bare) == 0)) {
            page_open(k, f->name, f->text, DV_PROMPT);
            return;
        }
    }
    if (strstr(want, ".EXE") != NULL)
        out(k, "Cannot TYPE a program. Run it.");
    else
        out(k, "File not found");
}

static void cmd_cal(desk_t* k) {
    int const now = desk_clock(k);
    out(k, "CALENDAR                                                      Today  %02d:%02d", now / 60, now % 60);
    out(k, "------------------------------------------------------------------------------");
    if (k->d->n_events == 0) out(k, "(nothing today)");
    for (int i = 0; i < k->d->n_events; i++) {
        desk_event_t const* ev = &k->d->events[i];
        int                 at = ev->at;
        for (int m = 0; m < ev->n_moves; m++)
            if (holds(k, &ev->when[m])) at = ev->to[m];
        if (at < 0)
            out(k, "--:--  %s  (cancelled)", ev->what);
        else if (at != ev->at)
            out(k, "%02d:%02d  %s  (moved, was %02d:%02d)", at / 60, at % 60, ev->what, ev->at / 60, ev->at % 60);
        else
            out(k, "%02d:%02d  %s", at / 60, at % 60, ev->what);
    }
}

static void cmd_help(desk_t* k) {
    out(k, "DIR          the files here");
    out(k, "TYPE file    show a file");
    out(k, "MAIL         your mail");
    out(k, "CAL          today's calendar");
    out(k, "GLADOS       the review queue: the next draft");
    out(k, "STATUS       where the review stands");
    out(k, "TIME         the time");
    out(k, "CLS          clear the screen");
    out(k, "EXIT         leave, once the queue is empty");
}

static void cmd_status(desk_t* k) {
    out(k, "Reviewer %s at %s.  Round %d.  Score %d.", k->d->user, k->d->host, k->s->round, k->s->score);
    if (desk_done(k))
        out(k, "Queue: empty.");
    else
        out(k, "Queue: chamber %d of %d, draft %d.", k->s->at + 1, k->n_rounds, k->s->drafts);
}

static desk_action_t command(desk_t* k, char* line) {
    char* const s = trim(line);
    char        cmd[16];
    int         n = 0;
    while (s[n] && s[n] != ' ' && n < (int)sizeof(cmd) - 1) cmd[n] = s[n], n++;
    cmd[n] = '\0';
    upcase(cmd);
    char const* arg = trim(s + n);
    if (cmd[0] == '\0') return DESK_NONE;
    if (strcmp(cmd, "DIR") == 0 || strcmp(cmd, "LS") == 0) {
        cmd_dir(k);
    } else if (strcmp(cmd, "TYPE") == 0 || strcmp(cmd, "CAT") == 0) {
        cmd_type(k, arg);
    } else if (strcmp(cmd, "MAIL") == 0 || strcmp(cmd, "MAIL.EXE") == 0) {
        k->view     = DV_MAIL;
        k->mail_cur = 0;
    } else if (strcmp(cmd, "CAL") == 0 || strcmp(cmd, "CAL.EXE") == 0) {
        cmd_cal(k);
    } else if (strcmp(cmd, "GLADOS") == 0 || strcmp(cmd, "GLADOS.EXE") == 0) {
        if (desk_done(k)) {
            out(k, "GLaDOS: There is nothing left for you to review. Go home.");
            return DESK_NONE;
        }
        out(k, "Loading draft %d of chamber %d...", k->s->drafts, k->s->at + 1);
        return DESK_PLAY;
    } else if (strcmp(cmd, "STATUS") == 0) {
        cmd_status(k);
    } else if (strcmp(cmd, "TIME") == 0) {
        out(k, "Current time is %02d:%02d", desk_clock(k) / 60, desk_clock(k) % 60);
    } else if (strcmp(cmd, "CLS") == 0) {
        k->n_lines = 0;
    } else if (strcmp(cmd, "HELP") == 0 || strcmp(cmd, "?") == 0) {
        cmd_help(k);
    } else if (strcmp(cmd, "EXIT") == 0 || strcmp(cmd, "LOGOUT") == 0) {
        if (!desk_done(k)) {
            out(k, "The review queue is not empty. The assignment is due today.");
            return DESK_NONE;
        }
        return DESK_LEAVE;
    } else {
        out(k, "Bad command or file name");
    }
    return DESK_NONE;
}

// --- Keys ----------------------------------------------------------------------

// The mail that has come in, newest first: index into mails[], or -1.
static int inbox(desk_t const* k, int row) {
    for (int i = k->d->n_mails - 1; i >= 0; i--)
        if (arrived(k, i) && row-- == 0) return i;
    return -1;
}

static int inbox_n(desk_t const* k) {
    int n = 0;
    while (inbox(k, n) >= 0) n++;
    return n;
}

desk_action_t desk_key(desk_t* k, int key, char ch) {
    if (k->view == DV_PAGE) {
        int const room = DESK_ROWS - 1;
        if (key == DK_ESC || key == DK_BACK) {
            k->view = k->page_back;
        } else if (key == DK_UP) {
            if (k->page_top > 0) k->page_top--;
        } else if (key == DK_DOWN) {
            if (k->page_top + room < k->page_lines) k->page_top++;
        } else if (key == DK_PGUP) {
            k->page_top = k->page_top > room ? k->page_top - room : 0;
        } else if (key == DK_ENTER || key == DK_PGDN || (key == DK_CHAR && ch == ' ')) {
            // On a page, or back at its end.
            if (k->page_top + room >= k->page_lines)
                k->view = k->page_back;
            else
                k->page_top += room;
        }
        return DESK_NONE;
    }
    if (k->view == DV_MAIL) {
        int const n = inbox_n(k);
        if (key == DK_ESC || key == DK_BACK || (key == DK_CHAR && (ch == 'q' || ch == 'Q'))) {
            k->view = DV_PROMPT;
        } else if (key == DK_UP) {
            if (k->mail_cur > 0) k->mail_cur--;
        } else if (key == DK_DOWN) {
            if (k->mail_cur + 1 < n) k->mail_cur++;
        } else if (key == DK_ENTER && n > 0) {
            int const          i = inbox(k, k->mail_cur);
            char               head[DESK_COLS * 2];
            desk_mail_t const* m = &k->d->mails[i];
            snprintf(head, sizeof(head), "From: %.38s   Subject: %.36s", m->from, m->subject);
            k->read |= 1u << i;
            page_open(k, head, m->text, DV_MAIL);
            tell_mail(k);  // a mail may bring another
        }
        return DESK_NONE;
    }
    // The prompt.
    switch (key) {
        case DK_CHAR:
            if (ch >= ' ' && ch < 0x7F && k->in_len < DESK_COLS - (int)strlen(k->d->prompt) - 1) {
                memmove(k->in + k->in_cur + 1, k->in + k->in_cur, (size_t)(k->in_len - k->in_cur + 1));
                k->in[k->in_cur++] = ch;
                k->in_len++;
            }
            break;
        case DK_BACK:
            if (k->in_cur > 0) {
                memmove(k->in + k->in_cur - 1, k->in + k->in_cur, (size_t)(k->in_len - k->in_cur + 1));
                k->in_cur--;
                k->in_len--;
            }
            break;
        case DK_LEFT:
            if (k->in_cur > 0) k->in_cur--;
            break;
        case DK_RIGHT:
            if (k->in_cur < k->in_len) k->in_cur++;
            break;
        case DK_UP:
        case DK_DOWN: {
            // The commands typed before.
            int const n = k->n_hist < 8 ? k->n_hist : 8;
            if (n == 0) break;
            if (key == DK_UP && k->at_hist < n) k->at_hist++;
            if (key == DK_DOWN && k->at_hist > 0) k->at_hist--;
            if (k->at_hist == 0)
                k->in[0] = '\0';
            else
                snprintf(k->in, sizeof(k->in), "%s", k->hist[(k->n_hist - k->at_hist) % 8]);
            k->in_len = k->in_cur = (int)strlen(k->in);
            break;
        }
        case DK_ESC:
            if (k->in_len == 0) return DESK_TITLE;
            k->in[0]  = '\0';
            k->in_len = k->in_cur = 0;
            break;
        case DK_ENTER: {
            char line[DESK_COLS + 1];
            snprintf(line, sizeof(line), "%s", k->in);
            out(k, "%s%s", k->d->prompt, line);
            if (k->in_len > 0) snprintf(k->hist[k->n_hist++ % 8], DESK_COLS + 1, "%s", line);
            k->in[0]  = '\0';
            k->in_len = k->in_cur = 0;
            k->at_hist            = 0;
            return command(k, line);
        }
        default:
            break;
    }
    return DESK_NONE;
}

// --- The screen --------------------------------------------------------------

void desk_screen(desk_t const* k, char rows[DESK_ROWS][DESK_COLS + 1], int* cur_row, int* cur_col, int* hl) {
    for (int r = 0; r < DESK_ROWS; r++) rows[r][0] = '\0';
    *cur_row = *cur_col = *hl = -1;
    if (k->view == DV_PAGE) {
        char const(*page)[DESK_COLS + 1] = (char const(*)[DESK_COLS + 1]) k->page;
        for (int r = 0; r < DESK_ROWS - 1 && k->page_top + r < k->page_lines; r++)
            snprintf(rows[r], DESK_COLS + 1, "%s", page[k->page_top + r]);
        bool const more = k->page_top + DESK_ROWS - 1 < k->page_lines;
        snprintf(rows[DESK_ROWS - 1], DESK_COLS + 1, "%s",
                 more ? "-- More --  (Space: on   Esc: back)" : "-- End --  (Space or Esc: back)");
        *hl = DESK_ROWS - 1;
        return;
    }
    if (k->view == DV_MAIL) {
        int const n = inbox_n(k);
        snprintf(rows[0], DESK_COLS + 1, "MAIL  %s@%s", k->d->user, k->d->host);
        snprintf(rows[1], DESK_COLS + 1,
                 "------------------------------------------------------------------------------");
        if (n == 0) snprintf(rows[2], DESK_COLS + 1, "(no mail)");
        int const top = k->mail_cur > DESK_ROWS - 6 ? k->mail_cur - (DESK_ROWS - 6) : 0;
        for (int r = 0; r < DESK_ROWS - 4 && top + r < n; r++) {
            int const          i = inbox(k, top + r);
            desk_mail_t const* m = &k->d->mails[i];
            snprintf(rows[2 + r], DESK_COLS + 1, "%c %-20.20s %-.55s", (k->read & (1u << i)) ? ' ' : '*', m->from,
                     m->subject);
            if (top + r == k->mail_cur) *hl = 2 + r;
        }
        snprintf(rows[DESK_ROWS - 1], DESK_COLS + 1, "Up/Down: choose   Enter: read   Esc: back");
        return;
    }
    // The prompt: the scrollback's last lines, then the input line.
    int const shown = k->n_lines < DESK_LINES ? k->n_lines : DESK_LINES;
    int const above = shown < DESK_ROWS - 1 ? shown : DESK_ROWS - 1;
    for (int r = 0; r < above; r++)
        snprintf(rows[r], DESK_COLS + 1, "%s", k->lines[(k->n_lines - above + r) % DESK_LINES]);
    snprintf(rows[above], DESK_COLS + 1, "%.23s%.57s", k->d->prompt, k->in);
    *cur_row = above;
    *cur_col = (int)strlen(k->d->prompt) + k->in_cur;
}

// --- Saves -----------------------------------------------------------------

int desk_save_text(desk_t const* k, char* out_, size_t n) {
    story_t const* s = k->s;
    int            len =
        snprintf(out_, n, "at: %d\nfound: %u\nnovel: %d\nround: %d\nscore: %d\ndrafts: %d\nplays: %d\nread:", s->at,
                 (unsigned)s->found, s->novel ? 1 : 0, s->round, s->score, s->drafts, s->plays);
    for (int i = 0; i < k->d->n_mails && len >= 0 && (size_t)len < n; i++)
        if (k->read & (1u << i)) len += snprintf(out_ + len, n - (size_t)len, " %s", k->d->mails[i].id);
    if (len >= 0 && (size_t)len < n) len += snprintf(out_ + len, n - (size_t)len, "\n");
    return len >= 0 && (size_t)len < n ? len : -1;
}

bool desk_load_save(char const* text, story_t* s, desk_data_t const* d, uint32_t* read) {
    story_t     t    = *s;
    int         seen = 0;
    char        line[512];
    char const* p = text;
    *read         = 0;
    while (*p) {
        char const* e = strchr(p, '\n');
        size_t      n = e ? (size_t)(e - p) : strlen(p);
        snprintf(line, sizeof(line), "%.*s", (int)(n < sizeof(line) ? n : sizeof(line) - 1), p);
        p          = e ? e + 1 : p + n;
        int      v = 0;
        unsigned u = 0;
        if (sscanf(line, "at: %d", &v) == 1 && v >= 0)
            t.at = v, seen |= 1;
        else if (sscanf(line, "found: %u", &u) == 1)
            t.found = u, seen |= 2;
        else if (sscanf(line, "novel: %d", &v) == 1)
            t.novel = v != 0, seen |= 4;
        else if (sscanf(line, "round: %d", &v) == 1 && v >= 1)
            t.round = v, seen |= 8;
        else if (sscanf(line, "score: %d", &v) == 1 && v >= 0)
            t.score = v, seen |= 16;
        else if (sscanf(line, "drafts: %d", &v) == 1 && v >= 1)
            t.drafts = v, seen |= 32;
        else if (sscanf(line, "plays: %d", &v) == 1 && v >= 0)
            t.plays = v, seen |= 64;
        else if (strncmp(line, "read:", 5) == 0) {
            // Mail ids, a space between.
            for (char* w = line + 5; *w;) {
                while (*w == ' ') w++;
                char* end = w;
                while (*end && *end != ' ') end++;
                char const keep = *end;
                *end            = '\0';
                for (int i = 0; i < d->n_mails && *w; i++)
                    if (strcmp(d->mails[i].id, w) == 0) *read |= 1u << i;
                *end = keep;
                w    = end;
            }
        }
    }
    if (seen != 127) return false;
    *s = t;
    return true;
}

void desk_print(desk_t* k, char const* text) {
    text_out(k, text);
}
