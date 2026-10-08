// Host review check: play a chamber file's routes and judge each one as a
// story round would (review.h).
//
//   host_review [-dt SECONDS | -jitter SEED] [-patch LETTERS] [-repair] FILE [SECTION...]
//   host_review selftest
//   host_review desk PACK_DIR      its desk/ read, as the badge reads it
//
// Each SECTION ("solution", "cheese a", ...; all of them if none given; or
// a route file, tas/NN-name.txt, a script on its own) is
// played at 50 steps a second, -dt at another, or with -jitter at the
// badge's uneven frames. -patch plays the chamber with those flaws' fixes; -repair with
// its repairs. Prints, per section:
//
//   SECTION <TAB> seconds to the exit, or FAIL <TAB> verdict <TAB> terms
//
// the verdict being "intended", "flaw a c" (the known flaws that held) or
// "novel" (neither). Exits 1 if a file does not read or a section fails.
// selftest checks the review core on the built-in chambers (their
// solutions against the faster tas/ routes, which cheese several of them)
// and on tests/review/: `make check`.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chamber.h"
#include "demo.h"
#include "desk.h"
#include "review.h"
#include "story.h"

static char* slurp(char const* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long const n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* t = malloc((size_t)n + 1);
    if (t == NULL || fread(t, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(t);
        return NULL;
    }
    t[n] = '\0';
    fclose(f);
    return t;
}

// The sections a file has, in order: "solution", "cheese a", ...
static int sections(char const* text, char out[][128], int max) {
    int n = 0;
    for (char const* p = text; *p && n < max;) {
        char const* e = strchr(p, '\n');
        size_t      k = e ? (size_t)(e - p) : strlen(p);
        while (k > 0 && (p[k - 1] == '\r' || p[k - 1] == ' ')) k--;
        if ((k == 8 && strncmp(p, "solution", 8) == 0) || (k == 8 && strncmp(p, "cheese ", 7) == 0))
            snprintf(out[n++], 128, "%.*s", (int)k, p);
        p = e ? e + 1 : p + strlen(p);
    }
    return n;
}

typedef struct {
    float exit;  // seconds to the exit, -1 if never
    bool  died;
} run_t;

// Play `steps` in `lv`: in steps of `dt`, or with `jitter` (a seed) at the
// badge's uneven frames. The tracker is left in `g`.
static run_t play(level_t const* lv, step_t const* steps, float dt, unsigned jitter, game_t* g) {
    demo_player_t p;
    game_load_level(g, lv);
    demo_player_start(&p, steps);
    run_t r   = {-1.0f, false};
    float now = 0.0f;
    while (now < 300.0f && r.exit < 0.0f && !r.died) {
        float ft = dt;
        if (jitter) {
            jitter = jitter * 1103515245u + 12345u;
            ft     = 1.0f / 35.0f + (1.0f / 20.0f - 1.0f / 35.0f) * (float)((jitter >> 8) & 0xFFFF) / 65535.0f;
        }
        int const ev  = demo_player_step(&p, g, ft, 0.0f);
        now          += ft;
        if (ev & PL_EV_DIED) r.died = true;
        if ((ev & PL_EV_EXIT) && !r.died) r.exit = now;
    }
    return r;
}

// "intended", "flaw a c" or "novel"; "-" for no review, or no exit.
static void verdict_of(review_t const* r, level_t const* lv, track_t const* t, run_t run, char* out, size_t n) {
    snprintf(out, n, "-");
    if (r->kind == REVIEW_NONE || run.exit < 0.0f) return;
    review_verdict_t const v = review_judge(r, lv, t);
    if (v.flaws) {
        int k = snprintf(out, n, "flaw");
        for (int i = 0; i < r->n_flaws && k > 0 && (size_t)k < n; i++)
            if (v.flaws & (1u << i)) k += snprintf(out + k, n - (size_t)k, " %c", r->flaws[i].id);
    } else {
        snprintf(out, n, "%s", v.intended ? "intended" : "novel");
    }
}

// The flaws named by `letters`, as review_patch() wants them; -1 for one
// not given.
static int64_t flaws_of(review_t const* r, char const* letters) {
    uint32_t m = 0;
    for (char const* l = letters; *l; l++) {
        int const k = review_flaw(r, *l);
        if (k < 0) return -1;
        m |= 1u << k;
    }
    return m;
}

// A route: a section of `text`, or a file of steps on its own (".txt").
static bool route_of(char const* text, char const* section, step_t* steps, char* err, size_t n) {
    size_t const fl    = strlen(section);
    char*        route = fl > 4 && strcmp(section + fl - 4, ".txt") == 0 ? slurp(section) : NULL;
    if (fl > 4 && strcmp(section + fl - 4, ".txt") == 0 && route == NULL) {
        snprintf(err, n, "cannot read");
        return false;
    }
    bool const ok = route != NULL ? chamber_parse_steps(route, steps, NULL, err, n)
                                  : chamber_parse_section(text, section, steps, NULL, err, n);
    free(route);
    return ok;
}

// ---- selftest ----

static int s_fail;

#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
            s_fail++;                                   \
        }                                               \
    } while (0)

// `text` (patched with `letters`' fixes, its repairs with `repair`), the
// route `section` played: its verdict, "FAIL" if it never reaches the
// exit, or "error: ..." -- and what it did in `terms`.
static char const* judge(char const* text, char const* letters, bool repair, char const* section, float dt,
                         unsigned jitter, char* terms, size_t terms_n) {
    static char     out[192];
    static review_t r;
    static level_t  lv;
    static step_t   steps[SCRIPT_MAX_STEPS];
    static game_t   g;
    static draft_t  scratch;
    static char     patched[CHAMBER_FILE_MAX];
    char            err[160];
    if (terms_n) terms[0] = '\0';
    if (!review_parse(text, &r, err, sizeof(err))) {
        snprintf(out, sizeof(out), "error: %s", err);
        return out;
    }
    int64_t const m = flaws_of(&r, letters);
    if (m < 0) return "error: no such flaw";
    if (m || repair) {
        if (review_patch(text, &r, (uint32_t)m, repair, &scratch, patched, sizeof(patched), err, sizeof(err)) < 0) {
            snprintf(out, sizeof(out), "error: %s", err);
            return out;
        }
        text = patched;
    }
    if (!chamber_parse(text, &lv, NULL, NULL, err, sizeof(err)) || !route_of(text, section, steps, err, sizeof(err))) {
        snprintf(out, sizeof(out), "error: %s", err);
        return out;
    }
    run_t const run = play(&lv, steps, dt, jitter, &g);
    review_describe(&lv, &g.track, terms, terms_n);
    if (run.exit < 0.0f) return "FAIL";
    verdict_of(&r, &lv, &g.track, run, out, sizeof(out));
    return out;
}

// A built-in chamber with review keys put in front.
static char* with_review(char const* id, char const* keys) {
    int const i = chamber_find(id);
    if (i < 0) return NULL;
    char const* const body = chamber_text(i);
    size_t const      n    = strlen(keys) + strlen(body) + 2;
    char*             t    = malloc(n);
    snprintf(t, n, "%s\n%s", keys, body);
    return t;
}

// The built-in chambers' solutions are the meant way; their tas/ routes,
// faster, cheese several of them. Each review here says which.
static void test_builtins(void) {
    static struct {
        char const* id;
        char const* keys;
        char const* solution;  // its verdict
        char const* tas;
    } const cases[] = {
        {"01-gap", "review: final\nintended: no shots 2", "intended", "novel"},
        {"04-button", "review: final\nintended: pickup, button 1 by cube, door a", "intended", "intended"},
        {"05-delivery",
         "review: flawed\nintended: cube portal, button 1 by cube, door a\n"
         "flaw: a no door a -- Out over the door.\nfix: a 0 0 0 #W",
         "intended", "flaw a"},
        {"06-faith-plate", "review: final\nintended: launch 1, no launch 2, shots 0, no portal", "intended",
         "intended"},
        {"07-the-grill", "review: final\nintended: fizzle, cube portal, button 1 by cube", "intended", "intended"},
        {"09-the-ferry",
         "review: flawed\nintended: ride, platform, no portal\n"
         "flaw: a portal, no ride -- Portals past the ferry.\nfix: a 0 0 0 #W",
         "intended", "flaw a"},
        {"10-two-buttons",
         "review: flawed\nintended: button 1 by player, button 1 by cube, door a, no cube portal\n"
         "flaw: a no button 1 by player -- The cube did for both buttons.\nfix: a 0 0 0 #W",
         "intended", "flaw a"},
        {"11-against-the-clock", "review: final\nintended: press, button 1 by hand, button 1 by cube, door a",
         "intended", "intended"},
        {"12-redirection", "review: final\nintended: button 1 by beam, pickup, no button 1 by cube", "intended",
         "novel"},
        {"13-hard-light", "review: final\nintended: bridge, no bounce", "intended", "intended"},
        {"14-repulsion",
         "review: flawed\nintended: bounce, paint\n"
         "flaw: a no paint -- No gel at all.\n"
         "flaw: b paint, no bounce -- Gel, but never thrown up by it.\nfix: a 0 0 0 #W\nfix: b 0 0 0 #W",
         "intended", "flaw b"},
        {"15-catch", "review: final\nintended: pellet, button 1 by pellet, no button 1 by player", "intended",
         "intended"},
        {"16-relay",
         "review: flawed\nintended: button 1 by beam, ride, platform\n"
         "flaw: a no portal -- Not a portal in sight.\nfix: a 0 0 0 #W",
         "intended", "flaw a"},
        {"18-edgeless",
         "review: flawed\nintended: button 1 by sphere, door a\n"
         "flaw: a portal, no door a -- Around the door.\nfix: a 0 0 0 #W",
         "intended", "flaw a"},
        {"19-sentry",
         "review: flawed\nintended: topple, no spotted\n"
         "flaw: a spotted, no topple -- Walked past it, under fire.\nfix: a 0 0 0 #W",
         "intended", "flaw a"},
        {"20-excursion", "review: final\nintended: float, shots 2", "intended", "novel"},
    };
    char terms[512];
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char* const t = with_review(cases[i].id, cases[i].keys);
        CHECK(t != NULL, "%s: no such chamber", cases[i].id);
        if (t == NULL) continue;
        char tas[64];
        snprintf(tas, sizeof(tas), "tas/%s.txt", cases[i].id);
        // At 30 frames a second, as tools/tas.py times them.
        char const* v = judge(t, "", false, "solution", 1.0f / 30.0f, 0, terms, sizeof(terms));
        CHECK(strcmp(v, cases[i].solution) == 0, "%s: solution judged %s, not %s (%s)", cases[i].id, v,
              cases[i].solution, terms);
        v = judge(t, "", false, tas, 1.0f / 30.0f, 0, terms, sizeof(terms));
        CHECK(strcmp(v, cases[i].tas) == 0, "%s: tas route judged %s, not %s (%s)", cases[i].id, v, cases[i].tas,
              terms);
        free(t);
    }
}

// GLaDOS's Pressure draft: two flaws known, a third left to be found.
// Each fix closes its own flaw's route and no other, and the meant way
// still works in every patched variant, at the badge's frame rates too.
static void test_pressure(void) {
    char* const t = slurp("tests/review/pressure.txt");
    CHECK(t != NULL, "tests/review/pressure.txt: cannot read");
    if (t == NULL) return;
    static struct {
        char const* patch;
        char const* section;
        char const* verdict;
    } const cases[] = {
        {"", "solution", "intended"},   {"", "cheese a", "flaw a"},    {"", "cheese b", "novel"},
        {"", "cheese c", "flaw c"},     {"a", "solution", "intended"}, {"a", "cheese a", "FAIL"},
        {"a", "cheese c", "flaw c"},    {"c", "cheese a", "flaw a"},   {"c", "cheese c", "FAIL"},
        {"ac", "solution", "intended"}, {"ac", "cheese b", "novel"},
    };
    char terms[512];
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        for (unsigned j = 0; j < 3; j++) {
            char const* const v =
                judge(t, cases[i].patch, false, cases[i].section, 1.0f / 50.0f, j * 7u, terms, sizeof(terms));
            CHECK(strcmp(v, cases[i].verdict) == 0, "pressure patched \"%s\", %s (jitter %u): %s, not %s (%s)",
                  cases[i].patch, cases[i].section, j * 7u, v, cases[i].verdict, terms);
        }
    free(t);
}

// Every term the tracker keeps, seen on some route.
static void test_terms(void) {
    static struct {
        char const* file;
        char const* section;
        char const* term;
    } const cases[] = {
        {"dlc/after-hours/momentum.txt", "solution", ", speed"},
        {"dlc/after-hours/overtime.txt", "solution", ", burn"},
        {"dlc-src/shop-floor/cheese-grab.txt", "solution", ", cube launch 2"},
        {"dlc-src/shop-floor/fixed.txt", "solution", ", dropper"},
        {"tests/review/pressure.txt", "cheese b", ", button 1 by fallen"},
        {"tests/review/pressure.txt", "solution", ", button 1 by turret"},
        {"tests/review/pressure.txt", "cheese c", ", ride holding"},
        {"tests/review/pressure.txt", "solution", ", button 2 by pellet"},
        {"tests/review/pressure.txt", "solution", ", door c"},
        {"dlc/after-hours/spire.txt", "solution", ", launch 3"},
        {"tests/review/carry.txt", "solution", ", cube portal"},
    };
    char terms[512];
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char* const t = slurp(cases[i].file);
        CHECK(t != NULL, "%s: cannot read", cases[i].file);
        if (t == NULL) continue;
        char const* const v = judge(t, "", false, cases[i].section, 1.0f / 50.0f, 0, terms, sizeof(terms));
        CHECK(strcmp(v, "FAIL") != 0 && strncmp(v, "error", 5) != 0, "%s: %s", cases[i].file, v);
        size_t const n  = strlen(cases[i].term);
        char const*  at = strstr(terms, cases[i].term);
        CHECK(at != NULL && (at[n] == ',' || at[n] == '\0'), "%s %s: no \"%s\" in \"%s\"", cases[i].file,
              cases[i].section, cases[i].term + 2, terms);
        free(t);
    }
}

// Review keys that do not read, and why.
static void test_parse_errors(void) {
    static struct {
        char const* keys;
        char const* says;
    } const cases[] = {
        {"review: sloppy", "flawed, final or broken"},
        {"review: final\nreview: final\nintended: portal", "given twice"},
        {"intended: portal", "no \"review:\""},
        {"review: final", "no \"intended:\""},
        {"review: flawed\nintended: portal", "no \"flaw:\""},
        {"review: final\nintended: portal, wiggle", "unknown term \"wiggle\""},
        {"review: final\nintended: portal,", "an empty term"},
        {"review: final\nintended: button 9", "button: its number"},
        {"review: final\nintended: button 1 by ghost", "by: player"},
        {"review: final\nintended: door z", "door: its letter"},
        {"review: final\nintended: launch 7", "launch: which faith plate"},
        {"review: final\nintended: shots", "shots: how many"},
        {"review: final\nintended: portal now", "\"now\" after the term"},
        {"review: flawed\nintended: portal\nflaw: a portal", "-- and what it is"},
        {"review: flawed\nintended: portal\nflaw: a portal --", "what it is, after --"},
        {"review: flawed\nintended: portal\nflaw: a portal -- x\nflaw: a portal -- y\nfix: a 0 0 0 #W",
         "flaw a given twice"},
        {"review: flawed\nintended: portal\nflaw: a portal -- x", "flaw a has no fix"},
        {"review: flawed\nintended: portal\nflaw: a portal -- x\nfix: a 0 0 0 #W\nfix: b 0 0 0 #W",
         "flaw b, which is not given"},
        {"review: flawed\nintended: portal\nflaw: a portal -- x\nfix: a 0 0 0 #", "the cell before and after"},
        {"review: flawed\nintended: portal\nflaw: a portal -- x\nfix: a 3-1 0 0 #W", "x y z"},
        {"review: flawed\nintended: portal\nflaw: a portal -- x\nfix: a 0 0 0 ##", "the cell before and after"},
        {"review: final\nintended: portal\nflaw: a portal -- x\nfix: a 0 0 0 #W", "for \"review: flawed\""},
        {"review: broken", "no \"debug:\""},
        {"review: broken\ndebug: 0 #W", "how many changes"},
        {"review: broken\ndebug: 2 #", "two map characters"},
        {"review: broken\ndebug: 2 #W\nrepair: 0 0 0 .#", "which the debugger does not allow"},
        {"review: final\nintended: portal\ndebug: 2 #W", "for \"review: broken\""},
    };
    static char const body[] = "size: 3 3 3\nlayer 1\n...\n.S.\n...\n";
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char text[512];
        snprintf(text, sizeof(text), "%s\n%s", cases[i].keys, body);
        static review_t r;
        char            err[160] = "";
        bool const      ok       = review_parse(text, &r, err, sizeof(err));
        CHECK(!ok && strstr(err, cases[i].says) != NULL, "\"%s\": %s, not \"...%s...\"", cases[i].keys,
              ok ? "reads" : err, cases[i].says);
        // The chamber itself reads, review keys and all: they are the
        // review's to check.
        static level_t lv;
        CHECK(chamber_parse(text, &lv, NULL, NULL, err, sizeof(err)), "\"%s\": the chamber: %s", cases[i].keys, err);
    }
    // And ones that do, every term.
    static review_t   r;
    char              err[160] = "";
    static char const good[] =
        "review: flawed\n"
        "intended: portal, cube portal, shots 3, pickup, press, dropper, button 1, button 2 by cube, door b,"
        " launch, launch 2\n"
        "flaw: a cube launch, cube launch 1, bounce, paint, pellet, topple, spotted, fizzle -- One.\n"
        "flaw: c burn, speed, float, ride, ride holding, bridge, platform, no portal -- Two.\n"
        "fix: a 0-2 1 0-1 .#\n"
        "fix: c 1 1 1 SW\n"
        "size: 3 3 3\nlayer 1\n...\n.S.\n...\n";
    CHECK(review_parse(good, &r, err, sizeof(err)), "good keys: %s", err);
    CHECK(r.kind == REVIEW_FLAWED && r.intended.n == 11 && r.n_flaws == 2 && r.flaws[0].when.n == 8 &&
              r.flaws[1].when.n == 8 && r.flaws[1].when.t[7].no && r.n_fixes == 2 &&
              strcmp(r.flaws[1].about, "Two.") == 0,
          "good keys read wrong");
    CHECK(r.fixes[0].x0 == 0 && r.fixes[0].x1 == 2 && r.fixes[0].z1 == 1 && r.fixes[0].from == '.' &&
              r.fixes[0].to == '#',
          "a fix's range read wrong");
    // No review keys: not a round.
    CHECK(review_parse("name: x\nsize: 3 3 3\n", &r, err, sizeof(err)) && r.kind == REVIEW_NONE, "plain chamber");
}

// Patching: a fix that finds another cell there is refused; a broken
// chamber's repairs mend it.
static void test_patch(void) {
    static char const text[] =
        "review: broken\ndebug: 1 #W .W\nrepair: 1 1 1 .W\n"
        "size: 3 3 3\nlayer 1\n...\n.S.\n...\n\nsolution\nwait 1\n";
    static review_t r;
    static draft_t  d;
    static char     out[4096];
    char            err[160];
    CHECK(review_parse(text, &r, err, sizeof(err)), "broken: %s", err);
    CHECK(review_debug_allows(&r, '#', 'W') && review_debug_allows(&r, '.', 'W') && !review_debug_allows(&r, 'W', '#'),
          "debug changes");
    // (1 1 1) is the S: not '.'.
    CHECK(review_patch(text, &r, 0, true, &d, out, sizeof(out), err, sizeof(err)) < 0 && strstr(err, "'S' there"),
          "a repair over the wrong cell: %s", err);
    static char const text2[] =
        "review: broken\ndebug: 1 .W\nrepair: 0 1 0-2 .W\n"
        "size: 3 3 3\nlayer 1\n...\n.S.\n...\n\nsolution\nwait 1\n\ncheese a\nwait 2\n";
    CHECK(review_parse(text2, &r, err, sizeof(err)), "broken 2: %s", err);
    int const n = review_patch(text2, &r, 0, true, &d, out, sizeof(out), err, sizeof(err));
    CHECK(n > 0, "repair: %s", err);
    CHECK(strstr(out, "layer 1\n...\nWS.\n...") == NULL && strstr(out, "layer 1\nW..\nWS.\nW..\n") != NULL,
          "repaired: %s", out);
    // Its review keys, solution and cheese kept.
    static review_t r2;
    CHECK(review_parse(out, &r2, err, sizeof(err)) && r2.kind == REVIEW_BROKEN && r2.n_repairs == 1, "kept keys: %s",
          err);
    static step_t steps[SCRIPT_MAX_STEPS];
    int           ns = 0;
    CHECK(chamber_parse_section(out, "cheese a", steps, &ns, err, sizeof(err)) && ns == 1, "kept cheese: %s", err);
}

// Cheese sections: kept out of the solution, each checked, once each.
static void test_sections(void) {
    static char const head[] = "size: 3 3 3\nlayer 1\n...\n.S.\n...\n\nsolution\nwait 1\nwait 2\n";
    char              text[512];
    static level_t    lv;
    static step_t     steps[SCRIPT_MAX_STEPS];
    int               ns = 0;
    char              err[160];
    snprintf(text, sizeof(text), "%s\ncheese a\nwait 3\n\ncheese b\nwait 4\nwait 5\nwait 6\n", head);
    CHECK(chamber_parse(text, &lv, steps, &ns, err, sizeof(err)) && ns == 2, "solution with cheese: %d %s", ns, err);
    CHECK(
        chamber_parse_steps(text + strlen(head) - strlen("wait 1\nwait 2\n"), steps, &ns, err, sizeof(err)) && ns == 2,
        "steps stop at a cheese: %d", ns);
    CHECK(chamber_parse_section(text, "solution", steps, &ns, err, sizeof(err)) && ns == 2, "solution: %d", ns);
    CHECK(chamber_parse_section(text, "cheese a", steps, &ns, err, sizeof(err)) && ns == 1 && steps[0].a == 3.0f,
          "cheese a: %d", ns);
    CHECK(chamber_parse_section(text, "cheese b", steps, &ns, err, sizeof(err)) && ns == 3, "cheese b: %d", ns);
    CHECK(!chamber_parse_section(text, "cheese c", steps, &ns, err, sizeof(err)) && strstr(err, "no \"cheese c\""),
          "no cheese c: %s", err);
    snprintf(text, sizeof(text), "%s\ncheese a\nwait 3\ncheese a\nwait 4\n", head);
    CHECK(!chamber_parse(text, &lv, NULL, NULL, err, sizeof(err)) && strstr(err, "cheese a given twice"), "twice: %s",
          err);
    snprintf(text, sizeof(text), "%s\ncheese a\nwiggle 3\n", head);
    CHECK(!chamber_parse(text, &lv, NULL, NULL, err, sizeof(err)) && strstr(err, "line 12"), "a bad cheese step: %s",
          err);
}

// The editor keeps a chamber's review keys and cheese, as it keeps its
// solution.
static void test_draft(void) {
    char* const    t = slurp("tests/review/pressure.txt");
    static draft_t d;
    static char    out[CHAMBER_FILE_MAX];
    char           err[160];
    CHECK(t != NULL && draft_from_text(&d, "pressure", t, err, sizeof(err)), "draft: %s", err);
    CHECK(draft_save_text(&d, out, sizeof(out), err, sizeof(err)) > 0, "save: %s", err);
    static review_t a, b;
    CHECK(review_parse(t, &a, err, sizeof(err)) && review_parse(out, &b, err, sizeof(err)) &&
              memcmp(&a, &b, sizeof(a)) == 0,
          "review keys kept: %s", err);
    static step_t steps[SCRIPT_MAX_STEPS];
    int           n1 = 0, n2 = 0;
    CHECK(chamber_parse_section(t, "cheese c", steps, &n1, err, sizeof(err)) &&
              chamber_parse_section(out, "cheese c", steps, &n2, err, sizeof(err)) && n1 == n2 && n1 > 10,
          "cheese kept: %d %d", n1, n2);
    free(t);
}

// A story's rounds on the Pressure draft, played as the badge plays them:
// each draft the file patched with the flaws found so far.
static story_result_t play_round(story_t* s, char const* text, review_t const* r, char const* route, bool* exited) {
    static draft_t scratch;
    static char    round[CHAMBER_FILE_MAX];
    static level_t lv;
    static step_t  steps[SCRIPT_MAX_STEPS];
    static game_t  g;
    char           err[160];
    *exited = false;
    CHECK(story_round_text(s, text, r, &scratch, round, sizeof(round), err, sizeof(err)) > 0, "round text: %s", err);
    CHECK(chamber_parse(round, &lv, NULL, NULL, err, sizeof(err)), "round: %s", err);
    CHECK(chamber_parse_section(text, route, steps, NULL, err, sizeof(err)), "%s: %s", route, err);
    run_t const run = play(&lv, steps, 1.0f / 30.0f, 0, &g);
    if (run.exit < 0.0f) return (story_result_t){0};
    *exited = true;
    return story_exit(s, r, &lv, &g.track, 2);
}

static void test_story(void) {
    char* const     t = slurp("tests/review/pressure.txt");
    static review_t r;
    char            err[160];
    CHECK(t != NULL && review_parse(t, &r, err, sizeof(err)), "pressure: %s", err);
    if (t == NULL) return;
    story_t s;
    story_begin(&s, 0);
    static struct {
        char const* route;
        bool        exits;
        int         outcome;
        char const* headline;
        int         score, round, at;
    } const steps[] = {
        // The meant way first: nothing learned.
        {"solution", true, STORY_AGAIN, "No feedback", 0, 1, 0},
        // The toppled turret: not a flaw she knows. Once.
        {"cheese b", true, STORY_AGAIN, "Novel exploit, +2", 2, 1, 0},
        {"cheese b", true, STORY_AGAIN, "Already logged", 2, 1, 0},
        // Flaw a: logged, patched; its route then fails.
        {"cheese a", true, STORY_PATCHED, "Flaw a logged, +1", 3, 2, 0},
        {"cheese a", false, 0, "", 3, 2, 0},
        {"solution", true, STORY_AGAIN, "No feedback", 3, 2, 0},
        // Flaw c: the last one known, so the next draft is final.
        {"cheese c", true, STORY_PATCHED, "Flaw c logged, +1", 4, 3, 0},
        {"cheese c", false, 0, "", 4, 3, 0},
        // The turret still topples, in the final: logged already.
        {"cheese b", true, STORY_AGAIN, "Already logged", 4, 3, 0},
        // As meant: approved, on to the next file's round.
        {"solution", true, STORY_NEXT, "Approved", 4, 4, 1},
    };
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        bool                 exited = false;
        story_result_t const res    = play_round(&s, t, &r, steps[i].route, &exited);
        CHECK(exited == steps[i].exits, "story step %zu (%s): %s the exit", i, steps[i].route,
              exited ? "reached" : "never reached");
        if (exited)
            CHECK(res.outcome == steps[i].outcome && strcmp(res.headline, steps[i].headline) == 0,
                  "story step %zu (%s): %d \"%s\", not %d \"%s\"", i, steps[i].route, res.outcome, res.headline,
                  steps[i].outcome, steps[i].headline);
        CHECK(s.score == steps[i].score && s.round == steps[i].round && s.at == steps[i].at,
              "story step %zu (%s): score %d round %d at %d", i, steps[i].route, s.score, s.round, s.at);
        if (i == 6) CHECK(story_final(&s, &r), "every known flaw patched: final");
        if (i == 0) CHECK(!story_final(&s, &r), "a flawed draft is not final");
    }
    // The next file starts afresh.
    CHECK(s.found == 0 && !s.novel && s.drafts == 1, "the next file: nothing found yet");
    // The last file approved: done.
    story_t last;
    story_begin(&last, 0);
    last.at                     = 1;
    last.found                  = 3;
    bool                 exited = false;
    story_result_t const res    = play_round(&last, t, &r, "solution", &exited);
    CHECK(exited && res.outcome == STORY_DONE, "the last round approved: done (%d)", res.outcome);
    // A patched flaw that still held (a fix that did not take): not logged
    // again, not patched again.
    static level_t lv;
    CHECK(chamber_parse(t, &lv, NULL, NULL, err, sizeof(err)), "pressure: %s", err);
    track_t tr = {0};
    for (int b = 0; b < lv.n_buttons; b++)
        if (lv.buttons[b].link == 2) tr.by[b] = BY_PLAYER;  // button 3 by player: flaw a
    story_t again;
    story_begin(&again, 0);
    again.found               = 1;  // flaw a, patched
    story_result_t const res2 = story_exit(&again, &r, &lv, &tr, 2);
    CHECK(res2.outcome == STORY_AGAIN && res2.points == 0 && again.score == 0 && again.round == 1,
          "a patched flaw again: %d \"%s\"", res2.outcome, res2.headline);
    free(t);
}

// The desk: its screen as one text, to search.
static char const* screen_text(desk_t const* k) {
    static char text[DESK_ROWS * (DESK_COLS + 2)];
    char        rows[DESK_ROWS][DESK_COLS + 1];
    int         cr, cc, hl;
    desk_screen(k, rows, &cr, &cc, &hl);
    text[0] = '\0';
    for (int r = 0; r < DESK_ROWS; r++) {
        strcat(text, rows[r]);
        strcat(text, "\n");
    }
    return text;
}

static desk_action_t type(desk_t* k, char const* line) {
    for (char const* c = line; *c; c++) desk_key(k, DK_CHAR, *c);
    return desk_key(k, DK_ENTER, 0);
}

static void test_desk(void) {
    static desk_data_t d;
    static desk_t      k;
    char               err[160];
    CHECK(desk_load(&d, "tests/review/desk-pack", err, sizeof(err)), "desk: %s", err);
    CHECK(d.n_files == 2 && d.n_mails == 3 && d.n_events == 2 && strcmp(d.user, "DRATTMANN") == 0 &&
              d.start == 9 * 60 && d.step == 40 && strstr(d.boot, "ENRICHMENT OS") != NULL,
          "desk read: %d files, %d mails, %d events", d.n_files, d.n_mails, d.n_events);
    story_t s;
    story_begin(&s, 0);
    desk_begin(&k, &d, &s, 2, true);
    CHECK(strstr(screen_text(&k), "ENRICHMENT OS") && strstr(screen_text(&k), "New mail from HR: Welcome aboard") &&
              !strstr(screen_text(&k), "Overtime"),
          "booted, the first mail told:\n%s", screen_text(&k));
    CHECK(strstr(screen_text(&k), "C:\\RLHF>") != NULL, "the prompt");
    // DIR: the files that are out yet.
    CHECK(type(&k, "dir") == DESK_NONE && strstr(screen_text(&k), "ASSIGN   TXT") && !strstr(screen_text(&k), "SECRET"),
          "dir:\n%s", screen_text(&k));
    type(&k, "type secret.txt");
    CHECK(strstr(screen_text(&k), "File not found") != NULL, "a hidden file is not there");
    type(&k, "TYPE assign");
    CHECK(k.view == DV_PAGE && strstr(screen_text(&k), "ASSIGN.TXT") && strstr(screen_text(&k), "Cheese them."),
          "type:\n%s", screen_text(&k));
    desk_key(&k, DK_ESC, 0);
    CHECK(k.view == DV_PROMPT, "Esc: back from a page");
    type(&k, "frobnicate");
    CHECK(strstr(screen_text(&k), "Bad command or file name") != NULL, "an unknown command");
    // Typing: Backspace, the cursor, history.
    for (char const* c = "dur"; *c; c++) desk_key(&k, DK_CHAR, *c);
    desk_key(&k, DK_LEFT, 0);
    desk_key(&k, DK_BACK, 0);
    desk_key(&k, DK_CHAR, 'i');
    CHECK(strcmp(k.in, "dir") == 0 && k.in_cur == 2, "line editing: \"%s\" at %d", k.in, k.in_cur);
    desk_key(&k, DK_ESC, 0);
    CHECK(k.in_len == 0, "Esc clears the line");
    desk_key(&k, DK_UP, 0);
    CHECK(strcmp(k.in, "frobnicate") == 0, "up: the last command (%s)", k.in);
    desk_key(&k, DK_ESC, 0);
    // Mail: reading HR's brings Henry's.
    type(&k, "mail");
    CHECK(k.view == DV_MAIL && strstr(screen_text(&k), "* HR") && !strstr(screen_text(&k), "Henry"), "inbox:\n%s",
          screen_text(&k));
    desk_key(&k, DK_ENTER, 0);
    CHECK(k.view == DV_PAGE && strstr(screen_text(&k), "Welcome to Aperture.") && (k.read & 1), "a mail read:\n%s",
          screen_text(&k));
    desk_key(&k, DK_ESC, 0);
    CHECK(k.view == DV_MAIL && strstr(screen_text(&k), "* Henry") && strstr(screen_text(&k), "  HR"),
          "back in the inbox, the reply in:\n%s", screen_text(&k));
    desk_key(&k, DK_ESC, 0);
    CHECK(strstr(screen_text(&k), "New mail from Henry") != NULL, "the reply told at the prompt");
    // The calendar, and the clock.
    type(&k, "cal");
    CHECK(strstr(screen_text(&k), "15:00  Henry's birthday cake") && strstr(screen_text(&k), "09:30  Stand-up") &&
              strstr(screen_text(&k), "09:00"),
          "cal:\n%s", screen_text(&k));
    // GLADOS: a round.
    CHECK(type(&k, "glados") == DESK_PLAY, "glados: play");
    CHECK(type(&k, "exit") == DESK_NONE && strstr(screen_text(&k), "not empty"), "no leaving yet");
    // Two rounds played, the first chamber approved.
    story_result_t r = {.round = 1};
    snprintf(r.headline, sizeof(r.headline), "Flaw a logged, +1");
    s.plays = 1;
    desk_after_round(&k, &r);
    CHECK(strstr(screen_text(&k), "[09:40] Round 1: Flaw a logged") && !strstr(screen_text(&k), "Overtime"),
          "after round 1:\n%s", screen_text(&k));
    s.plays = 2;
    s.at    = 1;
    r.round = 2;
    snprintf(r.headline, sizeof(r.headline), "Approved");
    desk_after_round(&k, &r);
    CHECK(strstr(screen_text(&k), "New mail from The boss: Overtime") != NULL, "round 2 brings the boss");
    type(&k, "cal");
    CHECK(strstr(screen_text(&k), "16:30  Henry's birthday cake  (moved, was 15:00)") != NULL, "the cake moves:\n%s",
          screen_text(&k));
    type(&k, "dir");
    CHECK(strstr(screen_text(&k), "SECRET   TXT") != NULL, "done 1: the secret file");
    s.plays = 3;
    type(&k, "cal");
    CHECK(strstr(screen_text(&k), "--:--  Henry's birthday cake  (cancelled)") != NULL, "and is cancelled:\n%s",
          screen_text(&k));
    type(&k, "time");
    CHECK(strstr(screen_text(&k), "Current time is 11:00") != NULL, "time");
    // A save, and back.
    char save[512];
    CHECK(desk_save_text(&k, save, sizeof(save)) > 0, "save");
    story_t  back;
    uint32_t read = 0;
    story_begin(&back, 0);
    CHECK(desk_load_save(save, &back, &d, &read) && memcmp(&back, &s, sizeof(s)) == 0 && read == k.read,
          "save read back: %s", save);
    CHECK(!desk_load_save("at: 1\n", &back, &d, &read), "a save cut short does not read");
    // The last approved: out of the door.
    s.at = 2;
    CHECK(type(&k, "exit") == DESK_LEAVE, "exit, the queue empty");
    CHECK(type(&k, "glados") == DESK_NONE && strstr(screen_text(&k), "nothing left"), "no more rounds");
    CHECK(desk_key(&k, DK_ESC, 0) == DESK_TITLE, "Esc at an empty prompt: the title");
}

static int selftest(void) {
    test_desk();
    test_story();
    test_parse_errors();
    test_sections();
    test_patch();
    test_draft();
    test_builtins();
    test_pressure();
    test_terms();
    if (s_fail) {
        printf("%d check(s) failed\n", s_fail);
        return 1;
    }
    printf("review tests: all passed\n");
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "selftest") == 0) return selftest();
    if (argc == 3 && strcmp(argv[1], "desk") == 0) {
        // A pack's desk/, read as the badge reads it.
        static desk_data_t d;
        char               err[160];
        if (!desk_load(&d, argv[2], err, sizeof(err))) {
            fprintf(stderr, "%s: %s\n", argv[2], err);
            return 1;
        }
        printf("desk: %d files, %d mails, %d events, %d of %d bytes\n", d.n_files, d.n_mails, d.n_events, (int)d.used,
               DESK_ARENA);
        return 0;
    }
    unsigned jitter      = 0;
    float    dt          = 1.0f / 50.0f;
    char     letters[32] = "";
    bool     repair      = false;
    int      a           = 1;
    for (; a < argc && argv[a][0] == '-'; a++) {
        if (strcmp(argv[a], "-jitter") == 0 && a + 1 < argc)
            jitter = (unsigned)atoi(argv[++a]) * 2654435761u + 1u;
        else if (strcmp(argv[a], "-dt") == 0 && a + 1 < argc && atof(argv[a + 1]) > 0.0)
            dt = (float)atof(argv[++a]);
        else if (strcmp(argv[a], "-patch") == 0 && a + 1 < argc)
            snprintf(letters, sizeof(letters), "%s", argv[++a]);
        else if (strcmp(argv[a], "-repair") == 0)
            repair = true;
        else
            break;
    }
    if (a >= argc || argv[a][0] == '-') {
        fprintf(stderr,
                "usage: host_review [-dt SECONDS | -jitter SEED] [-patch LETTERS] [-repair] FILE [SECTION...]\n"
                "       host_review selftest\n");
        return 2;
    }
    char const* path = argv[a++];
    char*       text = slurp(path);
    if (text == NULL) {
        fprintf(stderr, "%s: cannot read\n", path);
        return 1;
    }
    static review_t r;
    char            err[160];
    if (!review_parse(text, &r, err, sizeof(err))) {
        fprintf(stderr, "%s: %s\n", path, err);
        return 1;
    }
    int64_t const patch = flaws_of(&r, letters);
    if (patch < 0) {
        fprintf(stderr, "%s: no such flaw in \"%s\"\n", path, letters);
        return 1;
    }
    if (patch || repair) {
        static draft_t scratch;
        static char    out[CHAMBER_FILE_MAX];
        if (review_patch(text, &r, (uint32_t)patch, repair, &scratch, out, sizeof(out), err, sizeof(err)) < 0) {
            fprintf(stderr, "%s: %s\n", path, err);
            return 1;
        }
        free(text);
        text = strdup(out);
    }
    static level_t lv;
    if (!chamber_parse(text, &lv, NULL, NULL, err, sizeof(err))) {
        fprintf(stderr, "%s: %s\n", path, err);
        return 1;
    }

    char found[32][128];
    int  n_sec = 0;
    if (a < argc) {
        for (; a < argc && n_sec < 32; a++) snprintf(found[n_sec++], sizeof(found[0]), "%s", argv[a]);
    } else {
        n_sec = sections(text, found, 32);
    }
    int fails = 0;
    for (int s = 0; s < n_sec; s++) {
        static step_t steps[SCRIPT_MAX_STEPS];
        if (!route_of(text, found[s], steps, err, sizeof(err))) {
            fprintf(stderr, "%s: %s: %s\n", path, found[s], err);
            fails++;
            continue;
        }
        static game_t g;
        run_t const   run = play(&lv, steps, dt, jitter, &g);
        char          terms[512], verdict[64];
        review_describe(&lv, &g.track, terms, sizeof(terms));
        verdict_of(&r, &lv, &g.track, run, verdict, sizeof(verdict));
        if (run.exit >= 0.0f)
            printf("%s\t%.2f\t%s\t%s\n", found[s], (double)run.exit, verdict, terms);
        else
            printf("%s\tFAIL%s\t%s\t%s\n", found[s], run.died ? " (died)" : "", verdict, terms);
        if (run.exit < 0.0f) fails++;
    }
    free(text);
    return fails ? 1 : 0;
}
