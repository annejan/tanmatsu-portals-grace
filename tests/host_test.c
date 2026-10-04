// Host tests: the portal maths, placement, and a scripted player solving
// every chamber with real shots and real physics. `make check`.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "chamber.h"
#include "demo.h"
#include "draft.h"
#include "game.h"
#include "level.h"
#include "player.h"
#include "portal.h"

static int s_fail;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);     \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
            s_fail++;                                       \
        }                                                   \
    } while (0)

// Two levels the same in every field. chamber_parse clears a level before
// it fills it, padding included, so memcmp is exact.
static bool level_same(level_t const* a, level_t const* b) {
    return memcmp(a, b, sizeof(*a)) == 0;
}

static bool near3(vec3_t a, vec3_t b, float eps) {
    return fabsf(a.x - b.x) < eps && fabsf(a.y - b.y) < eps && fabsf(a.z - b.z) < eps;
}

static void test_basis(void) {
    float const angles[][3] = {{0, 0, 0}, {1.0f, 0.3f, 0}, {-2.5f, -1.2f, 0.4f}, {3.0f, 1.4f, -1.0f}};
    for (size_t i = 0; i < sizeof(angles) / sizeof(angles[0]); i++) {
        basis_t const b = basis_from_angles(angles[i][0], angles[i][1], angles[i][2]);
        float         y, p, r;
        basis_to_angles(&b, &y, &p, &r);
        basis_t const c = basis_from_angles(y, p, r);
        CHECK(near3(b.right, c.right, 1e-4f) && near3(b.up, c.up, 1e-4f) && near3(b.fwd, c.fwd, 1e-4f),
              "basis round trip %zu", i);
    }
    // Straight down, where yaw and roll fold together.
    basis_t const d = basis_from_angles(0.7f, 1.5707963f, 0.0f);
    float         y, p, r;
    basis_to_angles(&d, &y, &p, &r);
    basis_t const e = basis_from_angles(y, p, r);
    CHECK(near3(d.right, e.right, 1e-3f) && near3(d.fwd, e.fwd, 1e-3f), "basis straight down");
}

static void test_map(void) {
    level_t lv;
    level_load(&lv, 0);
    portal_t a, b;
    CHECK(portal_place_at(&lv, 0, 1, 3, DIR_PX, v3(0, 1, 0), NULL, &a), "place a");
    CHECK(portal_place_at(&lv, 4, 0, 2, DIR_PY, v3(0, 0, 1), &a, &b), "place b on the floor");
    CHECK(near3(a.center, v3(1.0f, 2.0f, 3.5f), 1e-5f), "a centre %f %f %f", a.center.x, a.center.y, a.center.z);
    CHECK(near3(b.center, v3(4.5f, 1.0f, 3.0f), 1e-5f), "b centre %f %f %f", b.center.x, b.center.y, b.center.z);

    vec3_t const pts[] = {{1.5f, 2.2f, 3.1f}, {0.2f, 1.0f, 4.0f}, {3, 4, 5}};
    for (size_t i = 0; i < 3; i++) {
        vec3_t const there = portal_map_point(&a, &b, pts[i]);
        vec3_t const back  = portal_map_point(&b, &a, there);
        CHECK(near3(pts[i], back, 1e-4f), "a->b->a %zu", i);
    }
    // In through a's front, out of b's front.
    vec3_t const into = v3_scale(a.n, -1.0f);
    CHECK(near3(portal_map_dir(&a, &b, into), b.n, 1e-5f), "going in comes out");
    CHECK(near3(portal_map_point(&a, &b, a.center), b.center, 1e-5f), "centre to centre");
    // Up stays up.
    CHECK(near3(portal_map_dir(&a, &b, a.up), b.up, 1e-5f), "up to up");
    // No mirroring: the image of a right-handed basis is right-handed.
    basis_t const m  = basis_from_angles(0.4f, 0.2f, 0.0f);
    basis_t const mm = portal_map_basis(&a, &b, &m);
    CHECK(v3_dot(v3_cross(mm.right, mm.up), mm.fwd) > 0.99f, "handedness");

    // No overlapping the other portal.
    portal_t c;
    CHECK(!portal_place_at(&lv, 0, 2, 3, DIR_PX, v3(0, 1, 0), &a, &c), "overlap refused");
    // Not on metal (the ceiling).
    CHECK(!portal_place_at(&lv, 4, 6, 4, DIR_NY, v3(0, 0, 1), NULL, &c), "metal refused");
}

static void test_clip(void) {
    level_t lv;
    level_load(&lv, 0);
    portal_t a, b;
    portal_place_at(&lv, 0, 1, 3, DIR_PX, v3(0, 1, 0), NULL, &a);
    portal_place_at(&lv, 0, 1, 12, DIR_PX, v3(0, 1, 0), &a, &b);
    vec3_t const eye = v3(5.0f, 2.6f, 3.0f);
    clipset_t    cs;
    portal_clip_through(&a, &b, eye, NULL, &cs);
    CHECK(cs.n == PORTAL_OVAL_N + 1, "a plane per oval edge and the exit's wall, got %d", cs.n);
    // The far wall seen straight through: kept. The wall b hangs on: gone.
    vec3_t const veye = portal_map_point(&a, &b, eye);
    vec3_t const look = v3_sub(b.center, veye);
    vec3_t const far  = v3_mad(b.center, look, 1.0f);
    cvert_t      in[3] = {{far, 0, 0}, {v3_add(far, v3(0, 0.01f, 0)), 0, 0}, {v3_add(far, v3(0, 0, 0.01f)), 0, 0}};
    cvert_t      out[CLIP_MAX_VERTS];
    CHECK(clip_polygon(&cs, in, 3, out) == 3, "straight through is kept");
    cvert_t wall[4] = {{v3(1, 1, 9), 0, 0}, {v3(1, 6, 9), 0, 0}, {v3(1, 6, 15), 0, 0}, {v3(1, 1, 15), 0, 0}};
    CHECK(clip_polygon(&cs, wall, 4, out) == 0, "the exit's own wall is cut");
    // Something far off to the side of the hole: cut.
    cvert_t side[3] = {{v3(8, 5, 2), 0, 0}, {v3(8, 5.1f, 2), 0, 0}, {v3(8, 5, 2.1f), 0, 0}};
    CHECK(clip_polygon(&cs, side, 3, out) == 0, "outside the hole is cut");
}

static void test_mesh(void) {
    level_t lv;
    level_load(&lv, 0);
    static mquad_t q[LV_MAX_QUADS];
    int const      n = level_mesh(&lv, NULL, 0, q, LV_MAX_QUADS);
    CHECK(n > 6 && n < 64, "chamber 1 meshes to %d quads", n);
    float area = 0;
    for (int i = 0; i < n; i++) area += q[i].su * q[i].sv;
    // 8 x 14 floor + ceiling, two 14 x 5 and two 8 x 5 walls.
    CHECK(fabsf(area - (2 * 8 * 14 + 2 * 14 * 5 + 2 * 8 * 5)) < 0.01f, "inner area %f", area);
    hole_t const h[2] = {{0, 1, 3, DIR_PX}, {0, 2, 3, DIR_PX}};
    int const    m    = level_mesh(&lv, h, 2, q, LV_MAX_QUADS);
    float        a2   = 0;
    for (int i = 0; i < m; i++) a2 += q[i].su * q[i].sv;
    CHECK(fabsf(area - a2 - 2.0f) < 0.01f, "a portal takes two faces out");
}

// --- Scripted play ------------------------------------------------------

#define DT 0.02f

typedef struct {
    level_t  lv;
    player_t pl;
    portal_t portals[2];
    int      events;
    float    t;
} sim_t;

static void sim_init(sim_t* s, int chamber) {
    level_load(&s->lv, chamber);
    player_spawn(&s->pl, &s->lv);
    s->portals[0].open = s->portals[1].open = false;
    s->events                                = 0;
    s->t                                     = 0;
}

static bool shoot(sim_t* s, int which, vec3_t target) {
    vec3_t const eye  = player_eye(&s->pl);
    vec3_t const look = v3_norm(v3_sub(target, eye));
    portal_t     p;
    if (!portal_place(&s->lv, eye, look, &s->portals[which ^ 1], &p)) return false;
    s->portals[which] = p;
    return true;
}

static int step(sim_t* s, player_input_t const* in) {
    int const ev = player_update(&s->pl, &s->lv, s->portals, in, DT);
    s->events |= ev;
    s->t += DT;
    return ev;
}

// Walk at (x, z) until within reach, an event in `stop` fires, or time is up.
static int walk_at(sim_t* s, float x, float z, float speed, int stop, float max_t) {
    int ev = 0;
    for (float t = 0; t < max_t; t += DT) {
        float const dx = x - s->pl.pos.x, dz = z - s->pl.pos.z;
        if (dx * dx + dz * dz < 0.2f * 0.2f && s->pl.on_ground) break;
        s->pl.yaw              = atan2f(dx, dz);
        player_input_t const in = {.fwd = fminf(speed, sqrtf(dx * dx + dz * dz) * 2.0f)};
        ev |= step(s, &in);
        if (ev & stop) break;
    }
    return ev;
}

static int walk_to(sim_t* s, float x, float z, int stop, float max_t) {
    return walk_at(s, x, z, 1.0f, stop, max_t);
}

static int wait(sim_t* s, int stop, float max_t) {
    int ev = 0;
    for (float t = 0; t < max_t; t += DT) {
        player_input_t const in = {0};
        ev |= step(s, &in);
        if (ev & stop) break;
    }
    return ev;
}

static void test_chamber_1(void) {
    sim_t s;
    sim_init(&s, 0);
    CHECK(shoot(&s, PORTAL_BLUE, v3(1.0f, 1.9f, 3.5f)), "c1 blue");
    CHECK(shoot(&s, PORTAL_ORANGE, v3(1.0f, 1.9f, 12.5f)), "c1 orange");
    int ev = walk_to(&s, -1.0f, 3.5f, PL_EV_TELEPORT | PL_EV_DIED, 5);
    CHECK(ev & PL_EV_TELEPORT, "c1 went through (at %f %f %f)", s.pl.pos.x, s.pl.pos.y, s.pl.pos.z);
    CHECK(s.pl.pos.z > 11.0f, "c1 came out at the far end, z %f", s.pl.pos.z);
    ev = walk_to(&s, 5.0f, 13.0f, PL_EV_EXIT | PL_EV_DIED, 5);
    CHECK(ev & PL_EV_EXIT, "c1 reached the exit (ev %d)", ev);
    CHECK(!(s.events & PL_EV_DIED), "c1 nobody died");

    // Shot straight ahead at eye height, as a player does: the portal
    // stands on the floor and walking straight in goes through.
    sim_init(&s, 0);
    s.pl.yaw = -1.5707963f;
    CHECK(portal_place(&s.lv, player_eye(&s.pl), player_view(&s.pl).fwd, NULL, &s.portals[0]), "c1 eye-level blue");
    CHECK(fabsf(s.portals[0].center.y - 2.0f) < 1e-4f, "c1 eye-level portal stands on the floor, centre y %f",
          s.portals[0].center.y);
    s.pl.yaw = -1.2f;
    CHECK(portal_place(&s.lv, player_eye(&s.pl), player_view(&s.pl).fwd, &s.portals[0], &s.portals[1]),
          "c1 eye-level orange");
    s.pl.yaw = -1.5707963f;
    ev = 0;
    for (int i = 0; i < 200 && !(ev & PL_EV_TELEPORT); i++) {
        player_input_t const in = {.fwd = 1.0f};
        ev |= step(&s, &in);
    }
    CHECK(ev & PL_EV_TELEPORT, "c1 walked straight into an eye-level shot");

    // Without portals the goo is the only way, and it kills.
    sim_init(&s, 0);
    ev = walk_to(&s, 5.0f, 13.0f, PL_EV_EXIT | PL_EV_DIED, 8);
    CHECK((ev & PL_EV_DIED) && !(ev & PL_EV_EXIT), "c1 the goo kills (ev %d)", ev);
}

static void test_chamber_2(void) {
    sim_t s;
    sim_init(&s, 1);
    CHECK(!shoot(&s, PORTAL_BLUE, v3(8.9f, 2.0f, 5.0f)), "c2 the right wall is metal");
    CHECK(shoot(&s, PORTAL_BLUE, v3(1.0f, 1.9f, 4.0f)), "c2 blue");
    CHECK(shoot(&s, PORTAL_ORANGE, v3(5.5f, 6.9f, 13.0f)), "c2 orange");
    int ev = walk_to(&s, -1.0f, s.portals[0].center.z, PL_EV_TELEPORT, 5);
    CHECK(ev & PL_EV_TELEPORT, "c2 went through");
    ev = wait(&s, PL_EV_LANDED, 1.0f);
    CHECK(s.pl.on_ground && s.pl.pos.y > 4.9f, "c2 stood on the ledge, y %f", s.pl.pos.y);
    ev = walk_to(&s, 5.0f, 11.5f, PL_EV_EXIT | PL_EV_DIED, 5);
    CHECK(ev & PL_EV_EXIT, "c2 reached the exit (ev %d)", ev);
}

static void test_chamber_3(void) {
    sim_t s;
    sim_init(&s, 2);
    walk_to(&s, 6.5f, 2.85f, PL_EV_DIED, 2);  // to the balcony's edge, to see down
    CHECK(shoot(&s, PORTAL_BLUE, v3(6.5f, 1.0f, 6.2f)), "c3 blue on the pit floor");
    CHECK(s.portals[0].face == DIR_PY, "c3 blue is on a floor");
    CHECK(shoot(&s, PORTAL_ORANGE, v3(1.0f, 8.9f, 5.5f)), "c3 orange high on the west wall");
    // Step off at half pace and let go: the fall carries onto the portal.
    int ev = 0;
    for (float t = 0; t < 3 && s.pl.on_ground; t += DT) {
        s.pl.yaw                = 0.0f;
        player_input_t const in = {.fwd = 0.5f};
        ev |= step(&s, &in);
    }
    ev |= wait(&s, PL_EV_TELEPORT | PL_EV_DIED | PL_EV_LANDED, 3);
    CHECK(ev & PL_EV_TELEPORT, "c3 fell in (at %f %f %f)", s.pl.pos.x, s.pl.pos.y, s.pl.pos.z);
    CHECK(s.pl.vel.x > 10.0f, "c3 flung east at %f m/s", s.pl.vel.x);
    ev = wait(&s, PL_EV_LANDED | PL_EV_DIED, 4);
    CHECK(!(ev & PL_EV_DIED) && s.pl.pos.x > 15.0f && s.pl.pos.y > 2.9f, "c3 landed on the far platform at %f %f",
          s.pl.pos.x, s.pl.pos.y);
    ev = walk_to(&s, 20.0f, 4.5f, PL_EV_EXIT | PL_EV_DIED, 5);
    CHECK(ev & PL_EV_EXIT, "c3 reached the exit (ev %d)", ev);
}

// A floor portal under a ceiling portal: falling forever, never faster
// than terminal velocity, never escaping the column.
static void test_loop(void) {
    sim_t s;
    sim_init(&s, 0);
    level_set(&s.lv, 2, 6, 2, MAT_WHITE);
    level_set(&s.lv, 2, 6, 3, MAT_WHITE);
    CHECK(portal_place_at(&s.lv, 2, 0, 2, DIR_PY, v3(0, 0, 1), NULL, &s.portals[0]), "loop floor");
    CHECK(portal_place_at(&s.lv, 2, 6, 2, DIR_NY, v3(0, 0, 1), &s.portals[0], &s.portals[1]), "loop ceiling");
    s.pl.pos = v3(2.5f, 1.0f, 3.0f);
    int tp   = 0;
    for (int i = 0; i < 500; i++) {
        player_input_t const in = {0};
        if (step(&s, &in) & PL_EV_TELEPORT) tp++;
    }
    CHECK(tp > 5, "loop went round %d times", tp);
    CHECK(s.pl.pos.y > -1.0f && s.pl.pos.y < 6.0f, "loop stayed in the column, y %f", s.pl.pos.y);
    CHECK(fabsf(s.pl.pos.x - 2.5f) < 0.3f && fabsf(s.pl.pos.z - 3.0f) < 0.8f, "loop no drift %f %f", s.pl.pos.x,
          s.pl.pos.z);
}

// Every scripted demo does what it is there to show.
static void test_demos(void) {
    demo_state_t st;
    for (int i = 0; i < demo_count(); i++) {
        char const* n = demo_name(i);
        if (!demo_has_solution(i)) continue;
        demo_eval(i, demo_duration(i), &st);
        CHECK(!(st.events & PL_EV_DIED), "demo %s: nobody dies", n);
        bool const fixed = strcmp(n, "c1walk") == 0 || strcmp(n, "c1loop") == 0;
        if (fixed) {
            CHECK(st.events & PL_EV_TELEPORT, "demo %s: goes through a portal", n);
        } else {
            CHECK(st.events & PL_EV_EXIT, "chamber %s: its solution reaches the exit (stopped at %.2f %.2f %.2f)", n,
                  st.g.pl.pos.x, st.g.pl.pos.y, st.g.pl.pos.z);
        }
    }
    // Every built-in chamber has a solution, so make check proves it can be solved.
    for (int i = 0; i < chamber_builtin_count; i++)
        CHECK(demo_has_solution(demo_find(chamber_builtins[i].id)), "chamber %s has a solution", chamber_builtins[i].id);
    // A pure function of time: the same instant twice is the same state.
    demo_state_t a, b;
    demo_eval(demo_find("c1walk"), 2.37f, &a);
    demo_eval(demo_find("c1walk"), 2.37f, &b);
    CHECK(memcmp(&a.g.pl, &b.g.pl, sizeof(a.g.pl)) == 0, "demo replay is deterministic");
    // c1loop goes round and round: count the teleports, each a jump of
    // more than 2 m between two steps.
    int loops = 0;
    for (float t = 0.04f; t < demo_duration(demo_find("c1loop")); t += 0.02f) {
        demo_eval(demo_find("c1loop"), t, &a);
        demo_eval(demo_find("c1loop"), t - 0.02f, &b);
        if (v3_len(v3_sub(a.g.pl.pos, b.g.pl.pos)) > 2.0f) loops++;
    }
    CHECK(loops >= 2, "c1loop goes round more than once (%d)", loops);
}

// Walking into an eye-level portal pair at badge frame rates: one clean
// teleport, feet on the floor throughout. At 15-25 fps the box used to
// stop "fitting" mid-move, the wall behind the portal turned solid round
// it, and it was thrown up the wall or out of the world.
static void test_frame_rates(void) {
    float const dts[] = {0.016f, 0.02f, 0.033f, 0.04f, 0.05f, 0.06f, 0.08f, 0.1f};
    for (size_t k = 0; k < sizeof(dts) / sizeof(dts[0]); k++) {
        float const dt = dts[k];
        for (int side = 0; side < 2; side++) {
            level_t lv;
            level_load(&lv, 0);
            player_t p;
            player_spawn(&p, &lv);
            portal_t pt[2] = {0};
            p.yaw          = -1.5707963f;
            portal_place(&lv, player_eye(&p), player_view(&p).fwd, NULL, &pt[0]);
            p.yaw = side ? 1.5707963f : -1.2f;  // beside it, or across the room
            portal_place(&lv, player_eye(&p), player_view(&p).fwd, &pt[0], &pt[1]);
            p.yaw    = -1.5707963f;
            int   tp = 0;
            float lo = 99, hi = -99;
            for (float t = 0; t < 1.6f; t += dt) {
                player_input_t const in = {.fwd = 1.0f};
                if (player_update(&p, &lv, pt, &in, dt) & PL_EV_TELEPORT) tp++;
                lo = fminf(lo, p.pos.y);
                hi = fmaxf(hi, p.pos.y);
            }
            CHECK(tp == 1, "dt %.3f side %d: one teleport, got %d", dt, side, tp);
            CHECK(lo > 0.99f && hi < 1.02f, "dt %.3f side %d: feet stay on the floor, y %.2f..%.2f", dt, side, lo, hi);
        }
    }
}

// Cubes lost in the goo come back where they started; a door does not
// shut on the player standing in it; a cube dropped through a floor
// portal comes out of the other one.
static void test_things(void) {
    static game_t g;
    game_input_t const idle = {0};

    game_load(&g, 0);
    g.lv.cubes[0] = v3(2.5f, 1.0f, 2.5f);
    g.n_cubes     = 1;
    g.cubes[0].body = (body_t){v3(5.0f, 2.0f, 7.0f), v3(0, 0, 0), CUBE_HALF, 2 * CUBE_HALF, CUBE_HALF, false};
    for (int i = 0; i < 100; i++) game_step(&g, &idle, 0.02f);
    CHECK(fabsf(g.cubes[0].body.pos.x - 2.5f) < 0.01f && fabsf(g.cubes[0].body.pos.z - 2.5f) < 0.01f,
          "a cube in the goo comes back at its spawn, at %.2f %.2f", g.cubes[0].body.pos.x, g.cubes[0].body.pos.z);

    game_load(&g, 3);
    g.lv.doors[0].open = 1.0f;
    g.pl.pos           = v3(6.0f, 1.0f, 8.5f);  // in the doorway, button up
    for (int i = 0; i < 50; i++) game_step(&g, &idle, 0.02f);
    CHECK(g.lv.doors[0].open > 0.99f, "a door stays open on a player in it (%.2f)", g.lv.doors[0].open);
    g.pl.pos = v3(6.0f, 1.0f, 5.0f);
    for (int i = 0; i < 50; i++) game_step(&g, &idle, 0.02f);
    CHECK(g.lv.doors[0].open < 0.01f, "and shuts once they step out (%.2f)", g.lv.doors[0].open);
    CHECK(level_solid(&g.lv, 5, 1, 8), "a shut door is solid");

    game_load(&g, 0);
    CHECK(portal_place_at(&g.lv, 2, 0, 2, DIR_PY, v3(0, 0, 1), NULL, &g.portals[0]), "floor portal");
    CHECK(portal_place_at(&g.lv, 0, 1, 12, DIR_PX, v3(0, 1, 0), &g.portals[0], &g.portals[1]), "wall portal");
    g.n_cubes       = 1;
    g.lv.cubes[0]   = v3(7.0f, 1.0f, 2.0f);
    g.cubes[0].body = (body_t){v3(2.5f, 3.0f, 3.0f), v3(0, 0, 0), CUBE_HALF, 2 * CUBE_HALF, CUBE_HALF, false};
    for (int i = 0; i < 100; i++) game_step(&g, &idle, 0.02f);
    CHECK(g.cubes[0].body.pos.z > 10.0f && g.cubes[0].body.pos.x > 1.0f,
          "a cube dropped into a floor portal comes out of the wall one, at %.2f %.2f %.2f", g.cubes[0].body.pos.x,
          g.cubes[0].body.pos.y, g.cubes[0].body.pos.z);
}

// Every built-in chamber, written out and read back, is the same chamber
// with the same solution: what the editor saves is what it loaded.
static void test_chamber_write(void) {
    static level_t a, b;
    static step_t  sa[SCRIPT_MAX_STEPS], sb[SCRIPT_MAX_STEPS];
    static char    text[32 * 1024];
    char           err[96];
    for (int i = 0; i < chamber_builtin_count; i++) {
        int na = 0, nb = 0;
        CHECK(chamber_build(i, &a, sa, &na), "%s builds", chamber_id(i));
        int const len = chamber_write(&a, sa, na, text, sizeof(text));
        CHECK(len > 0, "%s writes", chamber_id(i));
        bool const ok = chamber_parse(text, &b, sb, &nb, err, sizeof(err));
        CHECK(ok, "%s reads back: %s", chamber_id(i), err);
        if (!ok) continue;
        // Everything: cells, header, doors, buttons, cubes, plates and
        // their targets, the platform and where it goes, the start.
        CHECK(level_same(&a, &b), "%s: written and read back, the same level", chamber_id(i));
        CHECK(na == nb, "%s: same number of steps (%d, %d)", chamber_id(i), na, nb);
        for (int k = 0; k < na && k < nb; k++)
            CHECK(sa[k].op == sb[k].op && sa[k].which == sb[k].which && fabsf(sa[k].a - sb[k].a) < 1e-3f &&
                      fabsf(sa[k].b - sb[k].b) < 1e-3f && fabsf(sa[k].c - sb[k].c) < 1e-3f,
                  "%s: step %d the same", chamber_id(i), k);
    }
    CHECK(chamber_write(&a, NULL, 0, text, 16) == -1, "a short buffer is refused");
}

// The editor's draft: a new one is a valid chamber; every built-in one
// goes through it unchanged; painting and resizing do what they say.
static void test_draft(void) {
    static draft_t d;
    static level_t lv, ref;
    static char    a[24 * 1024], b[24 * 1024];
    char           err[96];
    draft_new(&d, "my-01", 10, 6, 12);
    CHECK(draft_level(&d, &lv, err, sizeof(err)), "a new draft is a chamber: %s", err);
    CHECK(lv.w == 10 && lv.h == 6 && lv.d == 12 && level_get(&lv, 0, 2, 5) == MAT_WHITE &&
              level_get(&lv, 4, 2, 5) == MAT_AIR,
          "with white walls and air inside");

    for (int i = 0; i < chamber_builtin_count; i++) {
        CHECK(draft_from_text(&d, chamber_id(i), chamber_text(i)), "%s loads into a draft", chamber_id(i));
        CHECK(draft_level(&d, &lv, err, sizeof(err)), "%s: the draft parses: %s", chamber_id(i), err);
        chamber_build(i, &ref, NULL, NULL);
        CHECK(level_same(&lv, &ref), "%s: through a draft, the same level", chamber_id(i));
        CHECK(draft_text(&d, a, sizeof(a)) > 0 && strstr(a, "\nsolution\n") != NULL, "%s: keeps its solution",
              chamber_id(i));
    }

    draft_new(&d, "my-02", 8, 5, 8);
    draft_paint(&d, 2, 1, 2, 'S');
    int s_count = 0;
    for (int y = 0; y < d.h; y++)
        for (int z = 0; z < d.d; z++)
            for (int x = 0; x < d.w; x++) s_count += draft_get(&d, x, y, z) == 'S';
    CHECK(s_count == 1, "painting S moves the start (%d)", s_count);
    draft_paint(&d, 3, 1, 3, 'a');
    draft_paint(&d, 5, 1, 3, 'a');
    CHECK(!draft_level(&d, &lv, err, sizeof(err)) && strstr(err, "door 'a'") != NULL, "a broken door is reported: %s", err);
    draft_paint(&d, 4, 1, 3, 'a');
    CHECK(draft_level(&d, &lv, err, sizeof(err)) && lv.n_doors == 1, "a door three wide: %s", err);

    draft_paint(&d, 6, 1, 6, 'C');
    draft_resize(&d, 5, 5, 5);
    draft_resize(&d, 8, 5, 8);
    CHECK(draft_get(&d, 6, 1, 6) == '#', "cells cut off by a resize come back as metal");
    draft_text(&d, a, sizeof(a));
    draft_from_text(&d, "x", a);
    draft_text(&d, b, sizeof(b));
    CHECK(strcmp(a, b) == 0, "text -> draft -> text is the same text");
}

// Chambers from a directory come after the built-in ones, in name order;
// a file that does not parse is skipped. Run last: it adds to the list.
// Write `text` to `dir`/`name`; false (and a failed check) if it cannot.
static bool write_file(char const* dir, char const* name, char const* text) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* f = fopen(path, "w");
    CHECK(f != NULL, "cannot write %s", path);
    if (f == NULL) return false;
    fputs(text, f);
    fclose(f);
    return true;
}

static void test_chamber_dir(void) {
    // Under the build directory the Makefile uses ($BUILD, else build/).
    char const* build = getenv("BUILD");
    char        dir[200];
    snprintf(dir, sizeof(dir), "%s/test_chambers", build != NULL && build[0] ? build : "build");
    mkdir(dir, 0755);
    if (!write_file(dir, "b-second.txt", "name: Second\nsize: 3 3 3\nlayer 1\n###\n#S#\n###\n") ||
        !write_file(dir, "a-first.txt", "name: First\nsize: 3 3 3\nfacing: east\nlayer 1\n###\n#S#\n###\n") ||
        !write_file(dir, "c-broken.txt", "name: Broken\nsize: 3 3 3\nlayer 1\n#X#\n"))
        return;
    int const before = chamber_count();
    CHECK(chamber_load_dir(dir) == 2, "two of the three files load");
    CHECK(chamber_count() == before + 2, "they join the list");
    CHECK(strcmp(chamber_id(before), "a-first") == 0 && strcmp(chamber_id(before + 1), "b-second") == 0,
          "in name order, after the built-in ones");
    level_t lv;
    CHECK(level_load(&lv, before) && strcmp(lv.name, "First") == 0 && fabsf(lv.spawn_yaw - 1.5707963f) < 1e-4f,
          "and play: name and facing read");

    // What the parser says about mistakes.
    char        err[96];
    char const* bad[][2] = {
        {"size: 3 3 3\nlayer 1\n###\n#S#\n", "has 2 of its 3 rows"},
        {"size: 3 3 3\nlayer 1\n###\n#?#\n###\n", "unknown cell"},
        {"size: 3 3 3\nlayer 1\n###\n#.#\n###\n", "exactly one S"},
        {"size: 3 3 3\nwobble: 1\n", "unknown key"},
        {"size: 3 3 3\nlayer 1\n###\n#S#\n###\nsolution\nfly 3\n", "unknown step"},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        bool const ok = chamber_parse(bad[i][0], &lv, NULL, NULL, err, sizeof(err));
        CHECK(!ok && strstr(err, bad[i][1]) != NULL, "parse error %zu says \"%s\": got \"%s\"", i, bad[i][1], err);
    }
}

// Glass: you see through it, but neither you nor a shot gets through.
static void test_glass(void) {
    static game_t g;
    int const     d = demo_find("08-through-the-glass");
    CHECK(d >= 0, "chamber 08 is there");
    if (d < 0) return;
    game_load(&g, demo_chamber(d));
    g.pl.pos = v3(5.5f, 1.0f, 3.0f);
    vec3_t const eye  = player_eye(&g.pl);
    vec3_t const look = v3_norm(v3_sub(v3(5.5f, 2.5f, 11.0f), eye));
    portal_t     p;
    CHECK(!portal_place(&g.lv, eye, look, NULL, &p), "a shot through the glass takes no portal");
    game_input_t const fwd = {.fwd = 1.0f};
    g.pl.yaw               = 0.0f;
    for (int k = 0; k < 150; k++) game_step(&g, &fwd, 0.02f);
    CHECK(g.pl.pos.z < 5.71f, "walking into the glass stops at it (z %.2f)", g.pl.pos.z);
}

// The map's characters: one meaning each; every one of them reads, and
// nothing else does.
static void test_legend(void) {
    for (int i = 0; i < chamber_legend_n; i++)
        for (int j = i + 1; j < chamber_legend_n; j++)
            CHECK(chamber_legend[i].ch != chamber_legend[j].ch, "'%c' means both \"%s\" and \"%s\"",
                  chamber_legend[i].ch, chamber_legend[i].what, chamber_legend[j].what);
    static level_t lv;
    char           err[96], text[256];
    for (int c = 33; c < 127; c++) {
        bool known = false;
        for (int i = 0; i < chamber_legend_n; i++) known = known || chamber_legend[i].ch == (char)c;
        // A 4 x 3 x 4 box with the character in the middle of layer 1,
        // over a metal floor; the start beside it.
        snprintf(text, sizeof(text), "size: 4 3 4\nlayer 1\n####\n#%c.#\n#S.#\n####\n", c);
        bool const ok      = chamber_parse(text, &lv, NULL, NULL, err, sizeof(err));
        bool const unknown = !ok && strstr(err, "unknown cell") != NULL;
        CHECK(known ? !unknown : unknown, "'%c' %s: %s", c, known ? "is in the legend but does not read" : "reads but is not in the legend", err);
    }
    // Eight doors, each with its button, all in one chamber.
    snprintf(text, sizeof(text), "size: 10 4 4\nlayer 1\n##########\n#abcdefgh#\n#12345678#\n####S#####\n");
    CHECK(chamber_parse(text, &lv, NULL, NULL, err, sizeof(err)) && lv.n_doors == 8 && lv.n_buttons == 8,
          "eight doors and eight buttons: %s", err);
    CHECK(!chamber_parse("size: 4 3 4\nlayer 1\n####\n#2.#\n#S.#\n####\n", &lv, NULL, NULL, err, sizeof(err)) &&
              strstr(err, "no door 'b'") != NULL,
          "a button without its door is reported: %s", err);
    // The old letters still read.
    CHECK(chamber_parse("size: 5 3 4\nlayer 1\n#####\n#aB.#\n#S..#\n#####\n", &lv, NULL, NULL, err, sizeof(err)) == false &&
              strstr(err, "no door 'b'") != NULL,
          "old button B is button 2: %s", err);
}

// A door with two buttons opens only while both are down.
static void test_two_buttons(void) {
    static game_t g;
    int const     i = demo_chamber(demo_find("10-two-buttons"));
    game_load(&g, i);
    CHECK(g.lv.n_buttons == 2 && g.lv.buttons[0].link == g.lv.buttons[1].link, "chamber 10: two buttons for one door");
    game_input_t const idle = {0};
    button_t*          b0 = &g.lv.buttons[0];
    button_t*          b1 = &g.lv.buttons[1];
    // The player on one button: not enough.
    g.pl.pos = v3((float)b0->x + 0.5f, (float)b0->y + 1.0f, (float)b0->z + 0.5f);
    for (int k = 0; k < 60; k++) game_step(&g, &idle, 0.02f);
    CHECK(b0->pressed && !b1->pressed && g.lv.doors[0].open == 0.0f, "one button of two: the door stays shut");
    // A cube on the other as well: open.
    g.cubes[0].body.pos = v3((float)b1->x + 0.5f, (float)b1->y + 1.0f, (float)b1->z + 0.5f);
    for (int k = 0; k < 60; k++) game_step(&g, &idle, 0.02f);
    CHECK(b0->pressed && b1->pressed && g.lv.doors[0].open > 0.99f, "both: it opens (%.2f)", g.lv.doors[0].open);
}

// --- Badge frame rates -------------------------------------------------------
//
// The badge runs at 15 to 25 fps. Every test above that steps at 50 fps
// alone missed bugs that only showed at the badge's own rate, so these
// step at each of them.

static float const FPS_DT[] = {1.0f / 50, 1.0f / 30, 1.0f / 25, 1.0f / 20, 1.0f / 15, 1.0f / 10};
#define N_FPS ((int)(sizeof(FPS_DT) / sizeof(FPS_DT[0])))

// A chamber from text, into a game.
static bool load_text(game_t* g, char const* text) {
    static level_t lv;
    char           err[96];
    bool const     ok = chamber_parse(text, &lv, NULL, NULL, err, sizeof(err));
    CHECK(ok, "test chamber parses: %s", err);
    if (ok) game_load_level(g, &lv);
    return ok;
}

// Whether a box overlaps anything solid in the grid.
static bool in_solid(level_t const* lv, aabb_t a) {
    for (int y = (int)floorf(a.lo.y + 0.002f); y <= (int)floorf(a.hi.y - 0.002f); y++)
        for (int z = (int)floorf(a.lo.z + 0.002f); z <= (int)floorf(a.hi.z - 0.002f); z++)
            for (int x = (int)floorf(a.lo.x + 0.002f); x <= (int)floorf(a.hi.x - 0.002f); x++)
                if (level_solid(lv, x, y, z)) return true;
    return false;
}

static aabb_t player_box(player_t const* p) {
    return (aabb_t){v3(p->pos.x - PL_HALF_W, p->pos.y, p->pos.z - PL_HALF_W),
                    v3(p->pos.x + PL_HALF_W, p->pos.y + PL_HEIGHT, p->pos.z + PL_HALF_W)};
}

// Every built-in chamber's solution reaches the exit at every frame rate.
static void test_solutions_fps(void) {
    for (int c = 0; c < chamber_builtin_count; c++) {
        int const d = demo_find(chamber_builtins[c].id);
        for (int k = 0; k < N_FPS; k++) {
            static demo_state_t st;
            demo_eval_dt(d, demo_duration(d), FPS_DT[k], &st);
            CHECK((st.events & PL_EV_EXIT) && !(st.events & PL_EV_DIED), "%s at %.0f fps: %s (at %.2f %.2f %.2f)",
                  chamber_builtins[c].id, 1.0f / FPS_DT[k], (st.events & PL_EV_DIED) ? "died" : "never reached the exit",
                  st.g.pl.pos.x, st.g.pl.pos.y, st.g.pl.pos.z);
        }
    }
}

// Chamber 07 with its solution's portals: blue in the room-1 floor,
// orange on the far west wall, standing on the floor.
static void grill_portals(game_t* g) {
    game_load(g, demo_chamber(demo_find("07-the-grill")));
    CHECK(portal_place_at(&g->lv, 5, 0, 5, DIR_PY, v3(0, 0, 1), NULL, &g->portals[0]), "07: floor portal");
    CHECK(portal_place_at(&g->lv, 0, 1, 8, DIR_PX, v3(0, 1, 0), &g->portals[0], &g->portals[1]), "07: wall portal");
}

// A floor portal: standing beside it does not pull you in; dropping or
// walking into it anywhere along it never leaves you inside the floor.
static void test_floor_portal(void) {
    static game_t      g;
    game_input_t const idle = {0};
    for (int k = 0; k < N_FPS; k++) {
        float const  dt      = FPS_DT[k];
        vec3_t const spots[] = {{5.62f, 1.0f, 3.82f}, {6.9f, 1.0f, 6.0f}, {4.1f, 1.0f, 6.0f}, {3.0f, 1.0f, 3.0f}};
        for (size_t s = 0; s < sizeof(spots) / sizeof(spots[0]); s++) {
            grill_portals(&g);
            g.pl.pos  = spots[s];
            int moved = 0;
            for (int i = 0; i < (int)(3.0f / dt); i++) moved |= game_step(&g, &idle, dt) & PL_EV_TELEPORT;
            CHECK(!moved && v3_len(v3_sub(g.pl.pos, spots[s])) < 0.05f,
                  "%.0f fps: standing at %.2f %.2f beside a floor portal stays put (now %.2f %.2f %.2f)", 1.0f / dt,
                  spots[s].x, spots[s].z, g.pl.pos.x, g.pl.pos.y, g.pl.pos.z);
        }
        // Dropping in, from a little above, all along its length.
        for (float z = 5.05f; z < 7.0f; z += 0.1f) {
            grill_portals(&g);
            g.pl.pos = v3(5.5f, 1.6f, z);
            for (int i = 0; i < (int)(3.0f / dt); i++) game_step(&g, &idle, dt);
            CHECK(g.pl.pos.y > 0.99f && !in_solid(&g.lv, player_box(&g.pl)),
                  "%.0f fps: dropped in at z %.2f, ends inside a solid (%.2f %.2f %.2f)", 1.0f / dt, z, g.pl.pos.x,
                  g.pl.pos.y, g.pl.pos.z);
        }
        // Walking over it, four ways.
        float const yaws[] = {0.0f, 3.1415927f, 1.5707963f, -1.5707963f};
        vec3_t const from[] = {{5.5f, 1.0f, 3.8f}, {5.5f, 1.0f, 8.0f}, {3.8f, 1.0f, 6.0f}, {7.2f, 1.0f, 6.0f}};
        for (int w = 0; w < 4; w++) {
            grill_portals(&g);
            g.pl.pos              = from[w];
            g.pl.yaw              = yaws[w];
            game_input_t const go = {.fwd = 1.0f};
            for (int i = 0; i < (int)(1.0f / dt); i++) game_step(&g, &go, dt);
            for (int i = 0; i < (int)(2.0f / dt); i++) game_step(&g, &idle, dt);
            CHECK(!in_solid(&g.lv, player_box(&g.pl)), "%.0f fps: walked over the floor portal (way %d), ends inside a solid", 1.0f / dt, w);
        }
    }
}

// A portal's tunnel is open from the front only: through a thin wall
// with a portal on its far face, you cannot walk in from behind.
static void test_portal_back(void) {
    static game_t g;
    if (!load_text(&g, "size: 7 4 9\nfacing: south\n"
                       "layer 1\n#######\n#.....#\n#..S..#\n#.....#\n#WWWWW#\n#.....#\n#.....#\n#.....#\n#######\n"
                       "layer 2\n#######\n#.....#\n#.....#\n#.....#\n#WWWWW#\n#.....#\n#.....#\n#.....#\n#######\n"))
        return;
    // The wall is z = 4; its south face looks into the south room.
    CHECK(portal_place_at(&g.lv, 3, 1, 4, DIR_NZ, v3(0, 1, 0), NULL, &g.portals[0]), "back: portal on the wall's far face");
    CHECK(portal_place_at(&g.lv, 1, 1, 4, DIR_NZ, v3(0, 1, 0), &g.portals[0], &g.portals[1]), "back: second portal");
    g.pl.pos              = v3(3.5f, 1.0f, 6.5f);
    g.pl.yaw              = 3.1415927f;  // south, at the wall's back
    game_input_t const go = {.fwd = 1.0f};
    int                tp = 0;
    for (int i = 0; i < 150; i++) tp |= game_step(&g, &go, 0.02f) & PL_EV_TELEPORT;
    CHECK(!tp && g.pl.pos.z > 5.25f, "back: walking at the wall from behind stops at it (z %.2f)", g.pl.pos.z);
}

// A platform that would carry its rider into the ceiling waits instead.
static void test_platform_ceiling(void) {
    static game_t  g;
    static level_t lift;
    char           err[96];
    // An elevator two cells high under a ceiling at y = 5 (layer 5 is metal).
    if (!chamber_parse("size: 6 6 6\n"
                       "layer 0\n######\n#WWWW#\n#WWWW#\n#WWWW#\n#WWWW#\n######\n"
                       "layer 1\n######\n#....#\n#.MM.#\n#.MM.#\n#....#\n######\n"
                       "layer 2\n######\n#....#\n#.S..#\n#....#\n#....#\n######\n"
                       "layer 3\n######\n#....#\n#....#\n#.N..#\n#....#\n######\n"
                       "layer 4\n######\n#....#\n#....#\n#....#\n#....#\n######\n",
                       &lift, NULL, NULL, err, sizeof(err))) {
        CHECK(false, "platform: test chamber: %s", err);
        return;
    }
    game_input_t const idle = {0};
    bool               bad  = false;
    for (int k = 0; k < N_FPS; k++) {
        game_load_level(&g, &lift);
        for (int i = 0; i < (int)(8.0f / FPS_DT[k]); i++) {
            game_step(&g, &idle, FPS_DT[k]);
            bad = bad || in_solid(&g.lv, player_box(&g.pl));
        }
    }
    CHECK(!bad, "platform: the rider is never carried into the ceiling");
}

// Fizzlers: they close the portals, take a carried cube, and take a cube
// that touches them -- at any speed.
static void test_fizzlers(void) {
    static game_t      g;
    game_input_t const go = {.fwd = 1.0f};
    // Walking into the grill closes both portals.
    grill_portals(&g);
    g.pl.pos = v3(7.5f, 1.0f, 4.5f);
    g.pl.yaw = 0.0f;
    for (int i = 0; i < 50; i++) game_step(&g, &go, 0.02f);
    CHECK(!g.portals[0].open && !g.portals[1].open, "fizzler: walking through it closes the portals");
    // A cube carried in goes back where it started.
    grill_portals(&g);
    g.pl.pos   = v3(8.5f, 1.0f, 2.2f);
    g.pl.yaw   = 0.0f;
    g.pl.pitch = atan2f(PL_EYE - CUBE_HALF, 1.3f);  // down at the cube's middle, 1.3 m ahead
    CHECK(game_use(&g) == GAME_EV_PICKUP, "fizzler: picked up the cube");
    for (int i = 0; i < 100; i++) game_step(&g, &go, 0.02f);
    vec3_t const home = g.lv.cubes[0];
    CHECK(g.held < 0 && v3_len(v3_sub(g.cubes[0].body.pos, home)) < 0.05f, "fizzler: a carried cube goes back to its start");
    // A free cube thrown into it, fast, at 10 fps.
    grill_portals(&g);
    g.cubes[0].body.pos = v3(4.5f, 1.4f, 4.0f);
    g.cubes[0].body.vel = v3(0.0f, 0.0f, 20.0f);
    int fz              = 0;
    for (int i = 0; i < 5; i++) fz |= game_step(&g, &(game_input_t){0}, 0.1f) & GAME_EV_FIZZLE;
    CHECK(fz, "fizzler: a cube at 20 m/s and 10 fps does not jump it");
    // The player, as fast.
    grill_portals(&g);
    g.pl.pos = v3(4.5f, 1.0f, 4.0f);
    g.pl.vel = v3(0.0f, 2.0f, 20.0f);
    g.pl.on_ground = false;
    for (int i = 0; i < 3; i++) game_step(&g, &(game_input_t){0}, 0.1f);
    CHECK(!g.portals[0].open, "fizzler: a player at 20 m/s and 10 fps does not jump it");
}

// A faith plate lands what it throws on its target, at any frame rate.
static void test_faith_plate_fps(void) {
    static game_t      g;
    game_input_t const idle = {0};
    int const          c    = demo_chamber(demo_find("06-faith-plate"));
    for (int k = 0; k < N_FPS; k++) {
        float const dt = FPS_DT[k];
        game_load(&g, c);
        jump_t const j = g.lv.jumps[0];
        g.pl.pos       = v3((float)j.x + 0.5f, (float)j.y + 1.0f, (float)j.z + 0.5f);
        // Where it comes down: the first step back on the ground once thrown.
        bool   flew = false;
        vec3_t land = g.pl.pos;
        for (int i = 0; i < (int)(4.0f / dt); i++) {
            game_step(&g, &idle, dt);
            if (!g.pl.on_ground) flew = true;
            if (flew && g.pl.on_ground) {
                land = g.pl.pos;
                break;
            }
        }
        float const miss = v3_len(v3_sub(v3(land.x, 0, land.z), v3(j.target.x, 0, j.target.z)));
        CHECK(flew && miss < 0.5f && fabsf(land.y - j.target.y) < 0.05f, "faith plate at %.0f fps: came down %.2f m off target",
              1.0f / dt, miss);
    }
}

// Odds and ends the fixes are about.
static void test_more_things(void) {
    static game_t      g;
    game_input_t const idle = {0};
    char               err[96];
    static level_t     a, b;

    // A cube being carried does not press a button, even held right on it.
    game_load(&g, demo_chamber(demo_find("04-button")));
    button_t const* bt = &g.lv.buttons[0];
    g.held             = 0;
    g.cubes[0].body.pos = v3((float)bt->x + 0.5f, (float)bt->y + 1.05f, (float)bt->z + 0.5f);
    game_step(&g, &idle, 0.02f);
    CHECK(!g.lv.buttons[0].pressed, "a carried cube does not press a button");

    // The old button letter A is button 1.
    CHECK(chamber_parse("size: 5 3 4\nlayer 1\n#####\n#.a.#\n#S.A#\n#####\n", &a, NULL, NULL, err, sizeof(err)), "old A: %s", err);
    CHECK(chamber_parse("size: 5 3 4\nlayer 1\n#####\n#.a.#\n#S.1#\n#####\n", &b, NULL, NULL, err, sizeof(err)), "new 1: %s", err);
    CHECK(level_same(&a, &b), "old button A reads as button 1");

    // A floor portal moved while a cube is halfway into it: the cube is
    // not left buried in the floor.
    grill_portals(&g);
    g.cubes[0].body.pos = v3(5.5f, 0.6f, 6.0f);
    g.cubes[0].body.vel = v3(0, 0, 0);
    CHECK(portal_place_at(&g.lv, 2, 0, 2, DIR_PY, v3(0, 0, 1), &g.portals[1], &g.portals[0]), "moved the floor portal");
    for (int i = 0; i < 50; i++) game_step(&g, &idle, 0.02f);
    CHECK(g.cubes[0].body.pos.y > 0.99f && !in_solid(&g.lv, cube_aabb(&g.cubes[0])),
          "a cube half in a floor portal that moves comes back out (y %.2f)", g.cubes[0].body.pos.y);

    // The moving platform stops a portal shot.
    game_load(&g, demo_chamber(demo_find("09-the-ferry")));
    g.pl.pos   = v3(5.0f, 2.0f, 3.0f);  // on the platform
    g.pl.pitch = 1.45f;                   // looking straight down at it
    CHECK(!game_fire(&g, 0), "a shot at the moving platform places nothing");
}

int main(void) {
    test_solutions_fps();
    test_floor_portal();
    test_portal_back();
    test_platform_ceiling();
    test_fizzlers();
    test_faith_plate_fps();
    test_more_things();
    test_legend();
    test_two_buttons();
    test_glass();
    test_things();
    test_frame_rates();
    test_demos();
    test_basis();
    test_map();
    test_clip();
    test_mesh();
    test_chamber_1();
    test_chamber_2();
    test_chamber_3();
    test_loop();
    test_chamber_write();
    test_draft();
    test_chamber_dir();
    if (s_fail) {
        printf("%d check(s) failed\n", s_fail);
        return 1;
    }
    printf("host tests: all passed\n");
    return 0;
}
