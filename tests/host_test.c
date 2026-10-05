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

#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
            s_fail++;                                   \
        }                                               \
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
    vec3_t const veye  = portal_map_point(&a, &b, eye);
    vec3_t const look  = v3_sub(b.center, veye);
    vec3_t const far   = v3_mad(b.center, look, 1.0f);
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
    s->events                               = 0;
    s->t                                    = 0;
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
    int const ev  = player_update(&s->pl, &s->lv, s->portals, in, DT);
    s->events    |= ev;
    s->t         += DT;
    return ev;
}

// Walk at (x, z) until within reach, an event in `stop` fires, or time is up.
static int walk_at(sim_t* s, float x, float z, float speed, int stop, float max_t) {
    int ev = 0;
    for (float t = 0; t < max_t; t += DT) {
        float const dx = x - s->pl.pos.x, dz = z - s->pl.pos.z;
        if (dx * dx + dz * dz < 0.2f * 0.2f && s->pl.on_ground) break;
        s->pl.yaw                = atan2f(dx, dz);
        player_input_t const in  = {.fwd = fminf(speed, sqrtf(dx * dx + dz * dz) * 2.0f)};
        ev                      |= step(s, &in);
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
        player_input_t const in  = {0};
        ev                      |= step(s, &in);
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
    ev       = 0;
    for (int i = 0; i < 200 && !(ev & PL_EV_TELEPORT); i++) {
        player_input_t const in  = {.fwd = 1.0f};
        ev                      |= step(&s, &in);
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
        s.pl.yaw                 = 0.0f;
        player_input_t const in  = {.fwd = 0.5f};
        ev                      |= step(&s, &in);
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
        CHECK(demo_has_solution(demo_find(chamber_builtins[i].id)), "chamber %s has a solution",
              chamber_builtins[i].id);
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
    static game_t      g;
    game_input_t const idle = {0};

    game_load(&g, 0);
    g.lv.cubes[0]   = v3(2.5f, 1.0f, 2.5f);
    g.n_cubes       = 1;
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

    static step_t sa[SCRIPT_MAX_STEPS], sb[SCRIPT_MAX_STEPS];
    for (int i = 0; i < chamber_builtin_count; i++) {
        CHECK(draft_from_text(&d, chamber_id(i), chamber_text(i), err, sizeof(err)), "%s loads into a draft: %s",
              chamber_id(i), err);
        CHECK(draft_level(&d, &lv, err, sizeof(err)), "%s: the draft parses: %s", chamber_id(i), err);
        int na = 0, nb = -1;
        chamber_build(i, &ref, sa, &na);
        CHECK(level_same(&lv, &ref), "%s: through a draft, the same level", chamber_id(i));
        // The text the editor would save: the same solution, step by step.
        CHECK(draft_text(&d, a, sizeof(a)) > 0 && chamber_parse(a, &lv, sb, &nb, err, sizeof(err)),
              "%s: the draft's text parses: %s", chamber_id(i), err);
        CHECK(na > 0 && na == nb, "%s: keeps its %d steps (%d)", chamber_id(i), na, nb);
        for (int k = 0; k < na && k < nb; k++)
            CHECK(memcmp(&sa[k], &sb[k], sizeof(sa[k])) == 0, "%s: step %d the same", chamber_id(i), k);
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
    CHECK(!draft_level(&d, &lv, err, sizeof(err)) && strstr(err, "door 'a'") != NULL, "a broken door is reported: %s",
          err);
    draft_paint(&d, 4, 1, 3, 'a');
    CHECK(draft_level(&d, &lv, err, sizeof(err)) && lv.n_doors == 1, "a door three wide: %s", err);

    draft_paint(&d, 6, 1, 6, 'C');
    draft_resize(&d, 5, 5, 5);
    draft_resize(&d, 8, 5, 8);
    CHECK(draft_get(&d, 6, 1, 6) == '#', "cells cut off by a resize come back as metal");
    draft_text(&d, a, sizeof(a));
    CHECK(draft_from_text(&d, "x", a, err, sizeof(err)), "the draft's own text loads: %s", err);
    draft_text(&d, b, sizeof(b));
    CHECK(strcmp(a, b) == 0, "text -> draft -> text is the same text");
}

// What the editor keeps of a file it did not write: all of its solution or
// nothing (it refuses), an indented `solution` line, a facing in degrees.
static void test_draft_keeps(void) {
    static draft_t d;
    static level_t lv;
    static step_t  steps[SCRIPT_MAX_STEPS];
    static char    text[16 * 1024], out[24 * 1024];
    char           err[96];
    // A solution of 3 KB, comments mostly; the last step is the one to lose.
    int            n = snprintf(text, sizeof(text),
                                "size: 5 3 5\nfacing: 45.5\nlayer 1\n#####\n#...#\n#.S.#\n#...#\n#####\n"
                                "  solution\n");
    for (int i = 0; i < 40; i++)
        n += snprintf(text + n, sizeof(text) - n, "// a note that takes up some room %02d...\n", i);
    n += snprintf(text + n, sizeof(text) - n, "wait 1\nwalk_to 1.5 3.5 0.5\n");
    CHECK(draft_from_text(&d, "long", text, err, sizeof(err)), "a 3 KB solution loads: %s", err);
    int ns = 0;
    CHECK(draft_text(&d, out, sizeof(out)) > 0 && chamber_parse(out, &lv, steps, &ns, err, sizeof(err)),
          "and is written out whole: %s", err);
    CHECK(ns == 2 && steps[1].op == OP_WALK_TO && fabsf(steps[1].b - 0.5f) < 1e-6f, "both steps kept (%d)", ns);
    CHECK(fabsf(lv.spawn_yaw - 45.5f * 3.14159265f / 180.0f) < 1e-4f, "facing 45.5 kept (%.4f)", lv.spawn_yaw);
    // Too long to keep: refused, not cut.
    while (n < (int)sizeof(d.solution) + 100) n += snprintf(text + n, sizeof(text) - n, "// more and more\n");
    snprintf(text + n, sizeof(text) - n, "wait 2\n");
    CHECK(chamber_parse(text, &lv, NULL, NULL, err, sizeof(err)), "the long one is a chamber: %s", err);
    CHECK(!draft_from_text(&d, "longer", text, err, sizeof(err)) && strstr(err, "solution") != NULL,
          "a solution longer than the draft keeps is refused: %s", err);
}

static bool write_file(char const* dir, char const* name, char const* text);

// Saving from the editor: a new file gets an id free in the list and on
// the card, and what is written must read back.
static void test_draft_save(void) {
    static draft_t d;
    static level_t lv;
    static char    out[24 * 1024];
    char           err[96], id[CHAMBER_ID_N], want[CHAMBER_ID_N + 8], dir[200], path[300];
    char const*    build = getenv("BUILD");
    snprintf(dir, sizeof(dir), "%s/test_save", build != NULL && build[0] ? build : "build");
    mkdir(dir, 0755);
    snprintf(want, sizeof(want), "my-%s", chamber_id(0));
    draft_fresh_id(dir, chamber_id(0), id, sizeof(id));
    CHECK(strcmp(id, want) == 0, "a copy of %s is called %s: %s", chamber_id(0), want, id);
    // Files on the card take their names, even ones that are not chambers
    // (and so are not in the list).
    snprintf(path, sizeof(path), "%s.txt", want);
    write_file(dir, path, "not a chamber");
    write_file(dir, "my-01.txt", "nor this");
    draft_fresh_id(dir, chamber_id(0), id, sizeof(id));
    CHECK(strcmp(id, "my-02") == 0, "with my-%s and my-01 on the card, my-02: %s", chamber_id(0), id);
    draft_fresh_id(dir, NULL, id, sizeof(id));
    CHECK(strcmp(id, "my-02") == 0, "a new one, my-02: %s", id);
    snprintf(path, sizeof(path), "%s/%s.txt", dir, want);
    remove(path);
    snprintf(path, sizeof(path), "%s/my-01.txt", dir);
    remove(path);

    draft_new(&d, "my-03", 8, 5, 8);
    CHECK(draft_save_text(&d, out, sizeof(out), err, sizeof(err)) > 0, "a new chamber saves: %s", err);
    // A solution that does not read: the map is fine, the file would not be.
    snprintf(d.solution, sizeof(d.solution), "solution\nfly 3\n");
    CHECK(draft_level(&d, &lv, err, sizeof(err)), "the map alone is a chamber: %s", err);
    CHECK(draft_save_text(&d, out, sizeof(out), err, sizeof(err)) < 0 && strstr(err, "unknown step") != NULL,
          "a file that would not read back is not saved: %s", err);
}

// Pedestal buttons, droppers, the story line and the timer (chamber 11).
static void test_pedestal_dropper(void) {
    static game_t      g;
    game_input_t const idle = {0};
    int const          c    = demo_chamber(demo_find("11-against-the-clock"));
    CHECK(c >= 0, "chamber 11 is there");
    if (c < 0) return;
    game_load(&g, c);
    CHECK(g.lv.n_buttons == 2 && !g.lv.buttons[0].pedestal && g.lv.buttons[1].pedestal,
          "a floor button and a pedestal");
    CHECK(g.lv.buttons[0].cube_only && !g.lv.buttons[1].cube_only, "the floor button is for a cube only");
    // The player standing on a cube button: nothing. The cube on it: down.
    button_t const* fb = &g.lv.buttons[0];
    g.pl.pos           = v3((float)fb->x + 0.5f, 1.0f, (float)fb->z + 0.5f);
    for (int i = 0; i < 10; i++) game_step(&g, &idle, 0.02f);
    CHECK(!g.lv.buttons[0].pressed, "the player does not press a cube button");
    game_load(&g, c);
    g.cubes[0].body.pos = v3((float)fb->x + 0.5f, 1.0f, (float)fb->z + 0.5f);
    for (int i = 0; i < 10; i++) game_step(&g, &idle, 0.02f);
    CHECK(g.lv.buttons[0].pressed, "a cube does");
    game_load(&g, c);
    CHECK(fabsf(g.lv.timer - 3.0f) < 1e-6f && g.lv.story[0] != '\0', "timer 3 and a story");
    CHECK(g.n_cubes == 1 && g.lv.cube_drop[0] && g.cubes[0].body.pos.y > 3.0f, "the cube starts in the dropper");
    for (int i = 0; i < 100; i++) game_step(&g, &idle, 0.02f);
    CHECK(g.cubes[0].body.on_ground && fabsf(g.cubes[0].body.pos.y - 1.0f) < 0.01f, "and drops to the floor (y %.2f)",
          g.cubes[0].body.pos.y);
    // Lost, it comes out of the dropper again.
    g.cubes[0].body.pos = v3(5.5f, -5.0f, 5.5f);
    int ev              = game_step(&g, &idle, 0.02f);
    CHECK((ev & GAME_EV_DROPPER) && g.cubes[0].body.pos.y > 3.0f, "a lost cube drops out of the dropper again");

    // The pedestal is a slim post: it stops you, about where it is drawn.
    game_load(&g, c);
    button_t const* pp = &g.lv.buttons[1];
    g.pl.pos           = v3((float)pp->x + 2.5f, 1.0f, (float)pp->z + 0.5f);
    g.pl.yaw           = -1.5707963f;  // west, straight at it
    for (int i = 0; i < 100; i++) game_step(&g, &(game_input_t){.fwd = 1.0f}, 0.02f);
    CHECK(fabsf(g.pl.pos.x - ((float)pp->x + 0.75f + PL_HALF_W)) < 0.05f,
          "walking into a pedestal stops at its post (x %.2f)", g.pl.pos.x);
    game_load(&g, c);
    // A cube resting on the pedestal's pad does not press it; Use does.
    button_t const* pb  = &g.lv.buttons[1];
    g.cubes[0].body.pos = v3((float)pb->x + 0.5f, (float)pb->y + 1.0f, (float)pb->z + 0.5f);
    g.cubes[0].body.vel = v3(0, 0, 0);
    for (int i = 0; i < 20; i++) game_step(&g, &idle, 0.02f);
    CHECK(!g.lv.buttons[1].pressed, "a cube on a pedestal button does not press it");
    game_load(&g, c);
    g.pl.pos         = v3(2.4f, 1.0f, 4.5f);
    g.pl.yaw         = -1.5707963f;  // west
    vec3_t const eye = player_eye(&g.pl), to = v3(1.5f, 2.1f, 4.5f);
    g.pl.pitch = atan2f(eye.y - to.y, eye.x - to.x);
    int down = 0, up = 0, ticks = 0;
    ev = game_step(&g, &(game_input_t){.use = true}, 0.02f);
    CHECK(ev & GAME_EV_PRESS, "Use presses the pedestal button");
    float held = 0.0f;
    for (int i = 0; i < 250; i++) {
        down  += (ev & GAME_EV_BUTTON_DOWN) != 0;
        up    += (ev & GAME_EV_BUTTON_UP) != 0;
        ticks += (ev & GAME_EV_TICK) != 0;
        if (g.lv.buttons[1].pressed) held += 0.02f;
        ev = game_step(&g, &idle, 0.02f);
    }
    CHECK(down == 1 && up == 1 && ticks == 2 && fabsf(held - 3.0f) < 0.05f,
          "down for 3 s (%.2f), ticking each second (%d), down %d up %d", held, ticks, down, up);

    // The puzzle: with the cube on the floor button, walking from the
    // pedestal to the door is too slow; the door is shut on arrival.
    game_load(&g, c);
    g.cubes[0].body.pos = v3((float)g.lv.buttons[0].x + 0.5f, 1.0f, (float)g.lv.buttons[0].z + 0.5f);
    g.pl.pos            = v3(2.4f, 1.0f, 4.5f);
    g.pl.yaw            = -1.5707963f;
    g.pl.pitch          = atan2f(eye.y - to.y, eye.x - to.x);
    game_step(&g, &(game_input_t){.use = true}, 0.02f);
    g.pl.yaw     = atan2f(22.5f - 2.4f, 5.4f - 4.5f);  // straight for the door
    g.pl.pitch   = 0.0f;
    float best_z = 0.0f;
    for (int i = 0; i < 400; i++) {
        if (g.pl.pos.x > 22.0f) g.pl.yaw = 0.0f;  // there: turn north, into it
        game_step(&g, &(game_input_t){.fwd = 1.0f}, 0.02f);
        best_z = fmaxf(best_z, g.pl.pos.z);
    }
    CHECK(best_z < 6.0f, "on foot, the door is shut before you get there (got to z %.2f)", best_z);
}

// Lasers: emitters, catchers, reflection cubes, burns, and portals.
static void test_lasers(void) {
    static game_t      g;
    static level_t     lv;
    game_input_t const idle = {0};
    char               err[96];
    int const          c = demo_chamber(demo_find("12-redirection"));
    CHECK(c >= 0, "chamber 12 is there");
    if (c < 0) return;
    game_load(&g, c);
    CHECK(g.lv.n_lasers == 1 && g.lv.lasers[0].dir == DIR_NX && g.lv.buttons[0].laser && g.lv.cube_reflect[0],
          "one emitter firing west, a catcher button, a reflection cube");
    game_step(&g, &idle, 0.02f);
    CHECK(g.beam_n[0] == 1 && fabsf(g.beam[0][0].b.x - 1.0f) < 0.01f && !g.lv.buttons[0].pressed,
          "the beam runs to the west wall (%d pieces, to x %.2f)", g.beam_n[0], g.beam[0][0].b.x);
    // Glass in the way: the beam goes through.
    level_set(&g.lv, 10, 1, 2, MAT_GLASS);
    game_step(&g, &idle, 0.02f);
    CHECK(g.beam_n[0] == 1 && fabsf(g.beam[0][0].b.x - 1.0f) < 0.01f, "through glass (to x %.2f)", g.beam[0][0].b.x);
    level_set(&g.lv, 10, 1, 2, MAT_AIR);
    // The reflection cube in the beam, facing north: the catcher lights,
    // and the door opens.
    g.cubes[0].body.pos = v3(3.5f, 1.0f, 2.5f);
    g.cubes[0].yaw      = 0.0f;
    for (int i = 0; i < 50; i++) game_step(&g, &idle, 0.02f);
    CHECK(g.beam_n[0] == 2 && g.lv.buttons[0].pressed && g.lv.doors[0].open == 1.0f,
          "turned north by the cube, it lights the catcher (%d pieces) and the door opens (%.2f)", g.beam_n[0],
          g.lv.doors[0].open);
    // A plain cube just stops it.
    g.lv.cube_reflect[0] = false;
    game_step(&g, &idle, 0.02f);
    CHECK(g.beam_n[0] == 1 && !g.lv.buttons[0].pressed, "a plain cube stops the beam");
    // Standing in it: a burn, and soon death.
    game_load(&g, c);
    g.pl.pos  = v3(8.5f, 1.0f, 2.5f);
    int ev    = 0;
    int steps = 0;
    while (!(ev & PL_EV_DIED) && steps < 100) ev |= game_step(&g, &idle, 0.02f), steps++;
    CHECK((ev & GAME_EV_BURN) && (ev & PL_EV_DIED) && steps * 0.02f > BEAM_BURN - 0.05f &&
              steps * 0.02f < BEAM_BURN + 0.1f,
          "in the beam: burnt, dead after %.2f s", steps * 0.02f);

    // Through a portal pair: in at the west wall, out of the north wall,
    // south onto a catcher.
    char const* text =
        "size: 9 4 10\n"
        "layer 0\n#########\n#WWWWWWW#\n#WWWWWWW#\n#WWWWWWW#\n#WWWWWWW#\n#WWWWWWW#\n#WWWWWWW#\n"
        "#WWWWWWW#\n#WWWWWWW#\n#########\n"
        "layer 1\n####W#a##\n#.......#\n#.......#\n#.......#\n#.S.....#\n#.......#\n#.......#\n"
        "W.......L\n#...O...#\n#########\n"
        "layer 2\n####W#a##\n#.......#\n#.......#\n#.......#\n#.......#\n#.......#\n#.......#\n"
        "W.......#\n#...1...#\n#########\n";
    CHECK(chamber_parse(text, &lv, NULL, NULL, err, sizeof(err)), "the portal room: %s", err);
    game_load_level(&g, &lv);
    CHECK(portal_place_at(&g.lv, 0, 1, 2, DIR_PX, v3(0, 1, 0), NULL, &g.portals[0]) &&
              portal_place_at(&g.lv, 4, 1, 9, DIR_NZ, v3(0, 1, 0), &g.portals[0], &g.portals[1]),
          "portals on the west and north walls");
    game_step(&g, &idle, 0.02f);
    CHECK(g.beam_n[0] == 2 && g.lv.buttons[0].pressed,
          "through the portals onto the catcher (%d pieces, to %.2f %.2f %.2f)", g.beam_n[0],
          g.beam[0][g.beam_n[0] - 1].b.x, g.beam[0][g.beam_n[0] - 1].b.y, g.beam[0][g.beam_n[0] - 1].b.z);

    // An emitter fires out of exactly one open side.
    CHECK(!chamber_parse("size: 5 3 3\nlayer 1\n#####\n#SL.#\n#####\n", &lv, NULL, NULL, err, sizeof(err)) &&
              strstr(err, "one open side") != NULL,
          "an emitter open on two sides is refused: %s", err);
}

// Light bridges (chamber 13): laid out from the emitter, through portals,
// solid to stand on.
static void test_bridges(void) {
    static game_t      g;
    static level_t     lv;
    game_input_t const idle = {0};
    char               err[96];
    int const          c = demo_chamber(demo_find("13-hard-light"));
    CHECK(c >= 0, "chamber 13 is there");
    if (c < 0) return;
    // Without the bridge the pit is the end of you.
    game_load(&g, c);
    g.pl.pos = v3(7.5f, 2.0f, 3.2f);
    g.pl.yaw = 0.0f;
    int ev   = 0;
    for (int i = 0; i < 150 && !(ev & PL_EV_DIED); i++) ev |= game_step(&g, &(game_input_t){.fwd = 1.0f}, 0.02f);
    CHECK(ev & PL_EV_DIED, "walking north without the bridge: into the goo");
    game_load(&g, c);
    CHECK(g.lv.n_bridges == 1 && g.lv.bridges[0].dir == DIR_PX && g.bridge_n[0] == 1 &&
              fabsf(g.bridge[0][0].b.x - 13.0f) < 0.01f && fabsf(g.bridge[0][0].a.y - 2.0f) < 1e-4f,
          "one bridge, east along the floor to the wall (to x %.2f)", g.bridge[0][0].b.x);
    // Portals on the east and south walls: on north across the pit.
    CHECK(portal_place_at(&g.lv, 13, 2, 2, DIR_NX, v3(0, 1, 0), NULL, &g.portals[0]) &&
              portal_place_at(&g.lv, 7, 2, 0, DIR_PZ, v3(0, 1, 0), &g.portals[0], &g.portals[1]),
          "the two portals");
    game_step(&g, &idle, 0.02f);
    beam_seg_t const* s = &g.bridge[0][1];
    CHECK(g.bridge_n[0] == 2 && fabsf(s->a.x - 7.5f) < 0.01f && fabsf(s->a.y - 2.0f) < 0.01f && s->b.z > 10.9f,
          "through them, north at x %.2f, height %.2f, to z %.2f", s->a.x, s->a.y, s->b.z);
    // Standing on it over the pit, and a cube resting on it.
    g.pl.pos   = v3(7.5f, 2.0f, 5.5f);
    g.pl.vel   = v3(0, 0, 0);
    g.n_cubes  = 1;
    g.cubes[0] = (cube_t){.body = {v3(7.5f, 2.5f, 6.8f), v3(0, 0, 0), CUBE_HALF, 2.0f * CUBE_HALF, CUBE_HALF, false}};
    ev         = 0;
    for (int i = 0; i < 100; i++) ev |= game_step(&g, &idle, 0.02f);
    CHECK(!(ev & PL_EV_DIED) && fabsf(g.pl.pos.y - 2.0f) < 0.01f && g.pl.on_ground, "the player stands on it (y %.2f)",
          g.pl.pos.y);
    CHECK(fabsf(g.cubes[0].body.pos.y - 2.0f) < 0.01f && g.cubes[0].body.on_ground, "and so does a cube (y %.2f)",
          g.cubes[0].body.pos.y);
    // A floor portal would stand it on end: it stops at the wall instead.
    game_load(&g, c);
    level_set(&g.lv, 7, 1, 2, MAT_WHITE);
    level_set(&g.lv, 7, 1, 3, MAT_WHITE);
    CHECK(portal_place_at(&g.lv, 13, 2, 2, DIR_NX, v3(0, 1, 0), NULL, &g.portals[0]) &&
              portal_place_at(&g.lv, 7, 1, 2, DIR_PY, v3(0, 0, 1), &g.portals[0], &g.portals[1]),
          "a wall portal and a floor portal");
    game_step(&g, &idle, 0.02f);
    CHECK(g.bridge_n[0] == 1, "out of a floor portal no bridge stands up (%d pieces)", g.bridge_n[0]);

    CHECK(!chamber_parse("size: 5 4 3\nlayer 1\n#####\n#SH.#\n#####\nlayer 2\n#####\n###.#\n#####\n", &lv, NULL, NULL,
                         err, sizeof(err)) &&
              strstr(err, "one open side") != NULL,
          "a bridge emitter open two ways is refused: %s", err);
    CHECK(!chamber_parse("size: 5 4 3\nlayer 1\n#####\n#S#H#\n#####\nlayer 2\n#####\n#...#\n#####\n", &lv, NULL, NULL,
                         err, sizeof(err)) &&
              strstr(err, "sideways") != NULL,
          "a bridge emitter open only upwards is refused: %s", err);
}

// Gel (chamber 14): dispensers drip, blobs paint, paint does things.
static void test_gel(void) {
    static game_t      g;
    static level_t     lv;
    game_input_t const idle = {0};
    char               err[96];
    int const          c = demo_chamber(demo_find("14-repulsion"));
    CHECK(c >= 0, "chamber 14 is there");
    if (c < 0) return;
    game_load(&g, c);
    CHECK(g.lv.n_gels == 1 && g.lv.gels[0].gel == GEL_BLUE, "one blue dispenser");
    // Left alone, it paints the floor under itself.
    gel_src_t const* src = &g.lv.gels[0];
    int              ev  = 0;
    for (int i = 0; i < 150; i++) ev |= game_step(&g, &idle, 0.02f);
    CHECK((ev & GAME_EV_PAINT) && level_paint(&g.lv, src->x, 0, src->z) == GEL_BLUE &&
              level_paint(&g.lv, src->x + 1, 0, src->z + 1) == GEL_BLUE,
          "it paints the floor under itself, 3 x 3");
    // Through a floor portal under it and a ceiling portal by the ledge:
    // the floor there.
    game_load(&g, c);
    CHECK(portal_place_at(&g.lv, 2, 0, 2, DIR_PY, v3(0, 0, 1), NULL, &g.portals[0]) &&
              portal_place_at(&g.lv, 6, 7, 5, DIR_NY, v3(0, 0, 1), &g.portals[0], &g.portals[1]),
          "floor and ceiling portals");
    for (int i = 0; i < 150; i++) game_step(&g, &idle, 0.02f);
    CHECK(level_paint(&g.lv, 6, 0, 6) == GEL_BLUE && level_paint(&g.lv, src->x, 0, src->z) == GEL_NONE,
          "through the portals it paints the floor under the other one, and not its own");

    // Blue: a fall comes back up; a jump goes high.
    g.pl.pos       = v3(6.5f, 4.0f, 6.0f);
    g.pl.vel       = v3(0, 0, 0);
    g.pl.on_ground = false;
    ev             = 0;
    float vy_max   = 0.0f;
    for (int i = 0; i < 60; i++) {
        ev |= game_step(&g, &idle, 0.02f);
        if (ev & PL_EV_BOUNCE) vy_max = fmaxf(vy_max, g.pl.vel.y);
    }
    CHECK((ev & PL_EV_BOUNCE) && vy_max > 7.0f, "a 3 m fall onto blue bounces back up (%.1f m/s)", vy_max);
    game_load(&g, c);
    level_set_paint(&g.lv, 6, 0, 3, GEL_BLUE);
    g.pl.pos = v3(6.5f, 1.0f, 3.5f);
    game_step(&g, &idle, 0.02f);
    game_step(&g, &(game_input_t){.jump = true}, 0.02f);
    float top = 0.0f;
    for (int i = 0; i < 80; i++) game_step(&g, &idle, 0.02f), top = fmaxf(top, g.pl.pos.y);
    CHECK(top > 4.3f, "a jump from blue clears 3 m (feet to %.2f)", top);
    // A cube bounces too.
    game_load(&g, c);
    level_set_paint(&g.lv, 6, 0, 3, GEL_BLUE);
    g.n_cubes  = 1;
    g.cubes[0] = (cube_t){.body = {v3(6.5f, 4.0f, 3.5f), v3(0, 0, 0), CUBE_HALF, 2.0f * CUBE_HALF, CUBE_HALF, false}};
    float cube_up = 0.0f;
    for (int i = 0; i < 60; i++) game_step(&g, &idle, 0.02f), cube_up = fmaxf(cube_up, g.cubes[0].body.vel.y);
    CHECK(cube_up > 5.0f, "a cube dropped on blue bounces (%.1f m/s)", cube_up);

    // Orange: a run goes well past walking pace.
    game_load(&g, c);
    for (int z = 1; z <= 6; z++)
        for (int x = 4; x <= 10; x++) level_set_paint(&g.lv, x, 0, z, GEL_ORANGE);
    g.pl.pos  = v3(4.5f, 1.0f, 3.5f);
    g.pl.yaw  = 1.5707963f;  // east
    float run = 0.0f;
    for (int i = 0; i < 40; i++) {
        game_step(&g, &(game_input_t){.fwd = 1.0f}, 0.02f);
        run = fmaxf(run, sqrtf(g.pl.vel.x * g.pl.vel.x + g.pl.vel.z * g.pl.vel.z));
    }
    CHECK(run > 7.0f, "on orange the player runs at %.1f m/s", run);
    // White: metal that takes a portal.
    game_load(&g, c);
    portal_t pt;
    CHECK(!portal_place_at(&g.lv, 11, 1, 3, DIR_NX, v3(0, 1, 0), NULL, &pt), "bare metal takes no portal");
    level_set_paint(&g.lv, 11, 1, 3, GEL_WHITE);
    level_set_paint(&g.lv, 11, 2, 3, GEL_WHITE);
    CHECK(portal_place_at(&g.lv, 11, 1, 3, DIR_NX, v3(0, 1, 0), NULL, &pt), "painted white, it does");
    CHECK(!level_set_paint(&g.lv, 9, 3, 8, GEL_BLUE), "the exit takes no paint");

    CHECK(!chamber_parse("size: 5 3 3\nlayer 1\n#####\n#SU.#\n#####\n", &lv, NULL, NULL, err, sizeof(err)) &&
              strstr(err, "air under it") != NULL,
          "a dispenser with no air under it is refused: %s", err);
}

// Energy pellets (chamber 15): fired, bounced, carried through portals,
// caught.
static void test_pellets(void) {
    static game_t      g;
    static level_t     lv;
    game_input_t const idle = {0};
    char               err[96];
    int const          c = demo_chamber(demo_find("15-catch"));
    CHECK(c >= 0, "chamber 15 is there");
    if (c < 0) return;
    game_load(&g, c);
    CHECK(g.lv.n_launchers == 1 && g.lv.launchers[0].dir == DIR_NX && g.lv.buttons[0].receiver,
          "a launcher firing west, a receiver button");
    g.pl.pos = v3(3.5f, 1.0f, 6.5f);  // out of its way
    // Without portals: off the west wall and back, gone after its life,
    // and fired again.
    int ev   = 0;
    for (int i = 0; i < 100; i++) ev |= game_step(&g, &idle, 0.02f);
    CHECK(g.pellets[0].live && g.pellets[0].vel.x > 0.0f, "it comes back off the west wall (vx %.1f)",
          g.pellets[0].vel.x);
    for (int i = 0; i < (int)((PELLET_LIFE - 2.0f + 0.1f) / 0.02f); i++) game_step(&g, &idle, 0.02f);
    CHECK(!g.pellets[0].live, "and fizzles out after %.0f s", PELLET_LIFE);
    for (int i = 0; i < (int)((PELLET_WAIT + 0.1f) / 0.02f); i++) game_step(&g, &idle, 0.02f);
    CHECK(g.pellets[0].live && !g.lv.buttons[0].pressed, "then the launcher fires another");
    // Through the portals into the receiver: the button latches, the door
    // opens, the launcher rests.
    game_load(&g, c);
    g.pl.pos = v3(3.5f, 1.0f, 6.5f);
    CHECK(portal_place_at(&g.lv, 0, 1, 2, DIR_PX, v3(0, 1, 0), NULL, &g.portals[0]) &&
              portal_place_at(&g.lv, 6, 1, 0, DIR_PZ, v3(0, 1, 0), &g.portals[0], &g.portals[1]),
          "portals on the west and south walls");
    ev = 0;
    for (int i = 0; i < 200; i++) ev |= game_step(&g, &idle, 0.02f);
    CHECK((ev & GAME_EV_CAUGHT) && g.lv.buttons[0].pressed && g.lv.doors[0].open == 1.0f && g.pellets[0].done,
          "caught: the door opens");
    g.portals[0].open = g.portals[1].open = false;
    for (int i = 0; i < 300; i++) game_step(&g, &idle, 0.02f);
    CHECK(g.lv.buttons[0].pressed && !g.pellets[0].live, "and stays open; the launcher fires no more");
    // In its way: dead.
    game_load(&g, c);
    g.pl.pos = v3(6.5f, 1.0f, 2.5f);
    ev       = 0;
    for (int i = 0; i < 100 && !(ev & PL_EV_DIED); i++) ev |= game_step(&g, &idle, 0.02f);
    CHECK(ev & PL_EV_DIED, "a pellet kills the player it hits");
    // A cube in its way sends it back.
    game_load(&g, c);
    g.pl.pos   = v3(3.5f, 1.0f, 6.5f);
    g.n_cubes  = 1;
    g.cubes[0] = (cube_t){.body = {v3(6.5f, 1.0f, 2.5f), v3(0, 0, 0), CUBE_HALF, 2.0f * CUBE_HALF, CUBE_HALF, false}};
    for (int i = 0; i < 60; i++) game_step(&g, &idle, 0.02f);
    CHECK(g.pellets[0].live && g.pellets[0].vel.x > 0.0f && g.pellets[0].pos.x > 6.8f,
          "a cube bounces it back (at x %.2f)", g.pellets[0].pos.x);

    CHECK(!chamber_parse("size: 5 3 3\nlayer 1\n#####\n#SP.#\n#####\n", &lv, NULL, NULL, err, sizeof(err)) &&
              strstr(err, "one open side") != NULL,
          "a launcher open on two sides is refused: %s", err);
}

// The parser's rules for what it cannot make sense of.
static void test_parse_rules(void) {
    static level_t    lv;
    static step_t     steps[SCRIPT_MAX_STEPS];
    char              err[96], text[512];
    char const*       room     = "layer 1\n#####\n#.S.#\n#####\n";
    // Numbers that are not: the start's position would become NaN.
    char const* const facing[] = {"nan", "inf", "-inf", "1e39"};
    for (size_t i = 0; i < sizeof(facing) / sizeof(facing[0]); i++) {
        snprintf(text, sizeof(text), "size: 5 3 3\nfacing: %s\n%s", facing[i], room);
        CHECK(!chamber_parse(text, &lv, NULL, NULL, err, sizeof(err)) && strstr(err, "facing") != NULL,
              "facing: %s is refused: %s", facing[i], err);
    }
    snprintf(text, sizeof(text), "size: 5 3 3\nfacing: 450\n%s", room);
    CHECK(chamber_parse(text, &lv, NULL, NULL, err, sizeof(err)) && fabsf(lv.spawn_yaw - 1.5707963f) < 1e-4f,
          "facing: 450 is east (%.4f)", lv.spawn_yaw);
    char const* const step[][2] = {
        {"wait nan", "not a number"},      {"walk_to 1 inf", "not a number"}, {"walk_to 1 2 nan", "not a number"},
        {"look 1 2 1e39", "not a number"}, {"face nan 0", "not a number"},    {"walk_to 1 2 3 4", "too much"},
    };
    for (size_t i = 0; i < sizeof(step) / sizeof(step[0]); i++) {
        snprintf(text, sizeof(text), "size: 5 3 3\n%ssolution\n%s\n", room, step[i][0]);
        CHECK(!chamber_parse(text, &lv, steps, NULL, err, sizeof(err)) && strstr(err, step[i][1]) != NULL,
              "\"%s\" is refused (%s): %s", step[i][0], step[i][1], err);
    }
    snprintf(text, sizeof(text), "size: 5 3 3\n%ssolution\nwalk_to 1 2 0.5\n", room);
    CHECK(chamber_parse(text, &lv, steps, NULL, err, sizeof(err)) && fabsf(steps[0].b - 0.5f) < 1e-6f,
          "walk_to with a pace: %s", err);

    char const* const bad[][2] = {
        // The same layer twice: its cubes and buttons would add up.
        {"size: 5 3 3\nlayer 1\n#####\n#SC.#\n#####\nlayer 1\n#####\n#S#.#\n#####\n", "given twice"},
        // One door's box holding another's cell.
        {"size: 7 4 3\nlayer 1\n#######\n#S.aba#\n#######\nlayer 2\n#######\n#..aba#\n#######\n", "overlap"},
        {"size: 6 4 3\nlayer 1\n######\n#S.ab#\n######\nlayer 2\n######\n#..ba#\n######\n", "overlap"},
        // A dropper's cube needs room under it.
        {"size: 5 4 3\nlayer 1\n#####\n#S#.#\n#####\nlayer 2\n#####\n#.V.#\n#####\n", "needs air under it"},
        {"size: 5 3 3\ntimer: 0\nlayer 1\n#####\n#.S.#\n#####\n", "timer"},
        {"size: 5 3 3\ntimer: 99\nlayer 1\n#####\n#.S.#\n#####\n", "timer"},
        // Nothing can rest on a button over a fizzler.
        {"size: 5 4 3\nlayer 1\n#####\n#SFa#\n#####\nlayer 2\n#####\n#.1.#\n#####\n", "nothing under it"},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        CHECK(!chamber_parse(bad[i][0], &lv, NULL, NULL, err, sizeof(err)) && strstr(err, bad[i][1]) != NULL,
              "refused (%s): %s", bad[i][1], err);

    // A byte-order mark, as Windows editors write one.
    snprintf(text, sizeof(text), "\xEF\xBB\xBFname: Bom\nsize: 5 3 3\n%s", room);
    CHECK(chamber_parse(text, &lv, NULL, NULL, err, sizeof(err)) && strcmp(lv.name, "Bom") == 0,
          "a file with a byte-order mark reads: %s", err);

    // Faith plates take their targets in reading order -- layer by layer
    // upwards -- whatever order the file gives the layers in.
    char const* const plates =
        "size: 5 5 3\n"
        "layer 3\n#####\n#T..#\n#####\n"
        "layer 2\n#####\n#..T#\n#####\n"
        "layer 1\n#####\n#JSJ#\n#####\n";
    static level_t again;
    static char    out[4096];
    CHECK(chamber_parse(plates, &lv, NULL, NULL, err, sizeof(err)) && lv.n_jumps == 2, "two plates: %s", err);
    CHECK(
        lv.jumps[0].x == 1 && fabsf(lv.jumps[0].target.x - 3.5f) < 1e-6f && fabsf(lv.jumps[0].target.y - 2.0f) < 1e-6f,
        "the first plate throws to the lower target (%.1f %.1f)", lv.jumps[0].target.x, lv.jumps[0].target.y);
    CHECK(chamber_write(&lv, NULL, 0, out, sizeof(out)) > 0 &&
              chamber_parse(out, &again, NULL, NULL, err, sizeof(err)) && level_same(&lv, &again),
          "and saved, the same pairs: %s", err);
}

static int gcd(int a, int b) {
    return b == 0 ? a : gcd(b, a % b);
}

// SD chambers: the id is the whole file name; with more files than the
// list holds, the first by name are the ones kept.
static void test_dir_limits(void) {
    char const* build = getenv("BUILD");
    char        dir[200], name[300];
    snprintf(dir, sizeof(dir), "%s/test_many", build != NULL && build[0] ? build : "build");
    mkdir(dir, 0755);
    char const* const body     = "size: 3 3 3\nlayer 1\n###\n#S#\n###\n";
    char const* const longname = "zz-the-long-and-winding-corridor-of-doom-and-more.txt";
    int const         files    = CHAMBER_MAX + 5;
    // Made in a scrambled order (a step that shares no factor with the
    // count visits every one): whether a directory lists names in the order
    // they were made (btrfs), the other way round (tmpfs) or by hash
    // (ext4), the list does not come out in name order by itself.
    int               step     = 17;
    while (gcd(step, files) != 1) step++;
    for (int k = 0; k < files; k++) {
        int const i = k * step % files;
        snprintf(name, sizeof(name), "m%02d.txt", i);
        write_file(dir, i == 0 ? "a-first.txt" : i == files - 1 ? longname : name, body);
    }
    int const loaded = chamber_reload_dir(dir);
    int const room   = CHAMBER_MAX - chamber_builtin_n();
    CHECK(loaded == room && chamber_count() == CHAMBER_MAX, "%d loaded, room for %d", loaded, room);
    CHECK(strcmp(chamber_id(chamber_builtin_n()), "a-first") == 0, "the first by name first: %s",
          chamber_id(chamber_builtin_n()));
    snprintf(name, sizeof(name), "m%02d", room - 1);
    CHECK(strcmp(chamber_id(CHAMBER_MAX - 1), name) == 0, "and the rest in name order: %s, wanted %s",
          chamber_id(CHAMBER_MAX - 1), name);
    // Fewer files: the long name gets a place, under its whole name.
    for (int i = 1; i < files - 1; i++) {
        snprintf(name, sizeof(name), "%s/m%02d.txt", dir, i);
        if (i > 3) remove(name);
    }
    chamber_reload_dir(dir);
    CHECK(chamber_find("zz-the-long-and-winding-corridor-of-doom-and-more") >= 0, "a long file name is its id");
    for (int i = 1; i <= 3; i++) {
        snprintf(name, sizeof(name), "%s/m%02d.txt", dir, i);
        remove(name);
    }
    snprintf(name, sizeof(name), "%s/a-first.txt", dir);
    remove(name);
    snprintf(name, sizeof(name), "%s/%s", dir, longname);
    remove(name);
    chamber_reload_dir(dir);  // empty now: the built-in ones only
    CHECK(chamber_count() == chamber_builtin_n(), "the list is back to the built-in chambers");
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
    CHECK(chamber_find("b-second") == before + 1 && chamber_find(chamber_id(0)) == 0 && chamber_find("no-such") == -1,
          "found by id: %d %d %d", chamber_find("b-second"), chamber_find(chamber_id(0)), chamber_find("no-such"));
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
    char const* const made[] = {"a-first.txt", "b-second.txt", "c-broken.txt"};
    for (size_t i = 0; i < sizeof(made) / sizeof(made[0]); i++) {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", dir, made[i]);
        remove(path);
    }
}

// More than the renderer's buffers hold is refused when a chamber is
// read, not drawn on the badge with walls missing.
static void test_caps(void) {
    static level_t lv;
    static char    text[8192];
    char           err[96];
    // `fill` on layer 1 (and on layer 2 if two), every other cell, over a
    // metal floor; the start in a corner.
    for (int k = 0; k < 2; k++) {
        int const  size = k == 0 ? 20 : 16;
        char const fill = k == 0 ? '#' : 'G';
        int        n    = snprintf(text, sizeof(text), "size: %d 4 %d\nlayer 0\n", size, size);
        for (int z = 0; z < size; z++)
            n += snprintf(text + n, sizeof(text) - n, "%.*s\n", size, "########################");
        for (int y = 1; y <= (k == 0 ? 1 : 2); y++) {
            n += snprintf(text + n, sizeof(text) - n, "layer %d\n", y);
            for (int z = 0; z < size; z++) {
                for (int x = 0; x < size; x++)
                    text[n++] = y == 1 && x == 0 && z == 0 ? 'S' : (x + z + y) % 2 ? fill : '.';
                text[n++] = '\n';
            }
        }
        text[n]       = 0;
        bool const ok = chamber_parse(text, &lv, NULL, NULL, err, sizeof(err));
        CHECK(!ok && strstr(err, k == 0 ? "too detailed" : "too much glass") != NULL, "a %s chamber: \"%s\"",
              k == 0 ? "pillared" : "glass-filled", ok ? "read" : err);
    }
    // Every chamber that comes with the game fits.
    for (int i = 0; i < chamber_count(); i++) {
        if (!level_load(&lv, i)) continue;
        CHECK(level_mesh(&lv, NULL, 0, NULL, 0) <= LV_MAX_QUADS && level_clear_faces(&lv) <= LV_MAX_CLEAR,
              "%s: %d faces, %d clear", chamber_id(i), level_mesh(&lv, NULL, 0, NULL, 0), level_clear_faces(&lv));
    }
}

// Glass: you see through it, but neither you nor a shot gets through.
static void test_glass(void) {
    static game_t g;
    int const     d = demo_find("08-through-the-glass");
    CHECK(d >= 0, "chamber 08 is there");
    if (d < 0) return;
    game_load(&g, demo_chamber(d));
    g.pl.pos          = v3(5.5f, 1.0f, 3.0f);
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
    // Each editor key picks one brush, and none is one the editor uses itself.
    for (int i = 0; i < chamber_legend_n; i++) {
        char const k = chamber_legend[i].key;
        if (k == 0) continue;
        CHECK(strchr(CHAMBER_EDITOR_KEYS, k) == NULL, "'%c' is on key %c, which the editor uses", chamber_legend[i].ch,
              k);
        for (int j = i + 1; j < chamber_legend_n; j++)
            CHECK(chamber_legend[j].key != k, "key %c picks both '%c' and '%c'", k, chamber_legend[i].ch,
                  chamber_legend[j].ch);
    }
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
        CHECK(known ? !unknown : unknown, "'%c' %s: %s", c,
              known ? "is in the legend but does not read" : "reads but is not in the legend", err);
    }
    // Eight doors, each with its button, all in one chamber.
    snprintf(text, sizeof(text), "size: 10 4 4\nlayer 1\n##########\n#abcdefgh#\n#12345678#\n####S#####\n");
    CHECK(chamber_parse(text, &lv, NULL, NULL, err, sizeof(err)) && lv.n_doors == 8 && lv.n_buttons == 8,
          "eight doors and eight buttons: %s", err);
    CHECK(!chamber_parse("size: 4 3 4\nlayer 1\n####\n#2.#\n#S.#\n####\n", &lv, NULL, NULL, err, sizeof(err)) &&
              strstr(err, "no door 'b'") != NULL,
          "a button without its door is reported: %s", err);
    // The old letters still read.
    CHECK(chamber_parse("size: 5 3 4\nlayer 1\n#####\n#aB.#\n#S..#\n#####\n", &lv, NULL, NULL, err, sizeof(err)) ==
                  false &&
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
    button_t*          b0   = &g.lv.buttons[0];
    button_t*          b1   = &g.lv.buttons[1];
    // The player on one button: not enough.
    g.pl.pos                = v3((float)b0->x + 0.5f, (float)b0->y + 1.0f, (float)b0->z + 0.5f);
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
                  chamber_builtins[c].id, 1.0f / FPS_DT[k],
                  (st.events & PL_EV_DIED) ? "died" : "never reached the exit", st.g.pl.pos.x, st.g.pl.pos.y,
                  st.g.pl.pos.z);
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
        float const  yaws[] = {0.0f, 3.1415927f, 1.5707963f, -1.5707963f};
        vec3_t const from[] = {{5.5f, 1.0f, 3.8f}, {5.5f, 1.0f, 8.0f}, {3.8f, 1.0f, 6.0f}, {7.2f, 1.0f, 6.0f}};
        for (int w = 0; w < 4; w++) {
            grill_portals(&g);
            g.pl.pos              = from[w];
            g.pl.yaw              = yaws[w];
            game_input_t const go = {.fwd = 1.0f};
            for (int i = 0; i < (int)(1.0f / dt); i++) game_step(&g, &go, dt);
            for (int i = 0; i < (int)(2.0f / dt); i++) game_step(&g, &idle, dt);
            CHECK(!in_solid(&g.lv, player_box(&g.pl)),
                  "%.0f fps: walked over the floor portal (way %d), ends inside a solid", 1.0f / dt, w);
        }
    }
}

// A portal's tunnel is open from the front only: through a thin wall
// with a portal on its far face, you cannot walk in from behind.
static void test_portal_back(void) {
    static game_t g;
    if (!load_text(&g,
                   "size: 7 4 9\nfacing: south\n"
                   "layer 1\n#######\n#.....#\n#..S..#\n#.....#\n#WWWWW#\n#.....#\n#.....#\n#.....#\n#######\n"
                   "layer 2\n#######\n#.....#\n#.....#\n#.....#\n#WWWWW#\n#.....#\n#.....#\n#.....#\n#######\n"))
        return;
    // The wall is z = 4; its south face looks into the south room.
    CHECK(portal_place_at(&g.lv, 3, 1, 4, DIR_NZ, v3(0, 1, 0), NULL, &g.portals[0]),
          "back: portal on the wall's far face");
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
    CHECK(g.held < 0 && v3_len(v3_sub(g.cubes[0].body.pos, home)) < 0.05f,
          "fizzler: a carried cube goes back to its start");
    // Backing into it with the cube held out the other way: the cube goes
    // too, though it never touches the grill itself.
    grill_portals(&g);
    g.pl.pos   = v3(8.5f, 1.0f, 2.2f);
    g.pl.yaw   = 0.0f;
    g.pl.pitch = atan2f(PL_EYE - CUBE_HALF, 1.3f);
    CHECK(game_use(&g) == GAME_EV_PICKUP, "fizzler: picked up the cube to back in with");
    g.pl.pos = v3(8.5f, 1.0f, 4.0f);
    g.pl.yaw = 3.14159265f;  // facing away from the grill, the cube out in front
    for (int i = 0; i < 50; i++) game_step(&g, &(game_input_t){0}, 0.02f);
    int ev = 0;
    for (int i = 0; i < 200 && !(ev & GAME_EV_FIZZLE); i++) ev |= game_step(&g, &(game_input_t){.fwd = -1.0f}, 0.02f);
    CHECK((ev & GAME_EV_FIZZLE) && g.held < 0 && v3_len(v3_sub(g.cubes[0].body.pos, home)) < 0.05f,
          "fizzler: backing in, the carried cube goes back to its start (held %d)", g.held);
    // A free cube thrown into it, fast, at 10 fps.
    grill_portals(&g);
    g.cubes[0].body.pos = v3(4.5f, 1.4f, 4.0f);
    g.cubes[0].body.vel = v3(0.0f, 0.0f, 20.0f);
    int fz              = 0;
    for (int i = 0; i < 5; i++) fz |= game_step(&g, &(game_input_t){0}, 0.1f) & GAME_EV_FIZZLE;
    CHECK(fz, "fizzler: a cube at 20 m/s and 10 fps does not jump it");
    // The player, as fast.
    grill_portals(&g);
    g.pl.pos       = v3(4.5f, 1.0f, 4.0f);
    g.pl.vel       = v3(0.0f, 2.0f, 20.0f);
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
        jump_t const j    = g.lv.jumps[0];
        g.pl.pos          = v3((float)j.x + 0.5f, (float)j.y + 1.0f, (float)j.z + 0.5f);
        // Where it comes down: the first step back on the ground once thrown.
        bool        flew  = false;
        vec3_t      land  = g.pl.pos;
        float const start = g.pl.pos.y;
        float       peak  = start;
        for (int i = 0; i < (int)(4.0f / dt); i++) {
            game_step(&g, &idle, dt);
            if (!g.pl.on_ground) flew = true;
            peak = fmaxf(peak, g.pl.pos.y);
            if (flew && g.pl.on_ground) {
                land = g.pl.pos;
                break;
            }
        }
        float const miss = v3_len(v3_sub(v3(land.x, 0, land.z), v3(j.target.x, 0, j.target.z)));
        CHECK(flew && miss < 0.5f && fabsf(land.y - j.target.y) < 0.05f,
              "faith plate at %.0f fps: came down %.2f m off target", 1.0f / dt, miss);
        // The arc tops out 2.5 m (JUMP_APEX, game.c) over the higher end:
        // high enough to clear what a chamber puts in the way.
        float const rise = peak - fmaxf(start, j.target.y);
        CHECK(fabsf(rise - 2.5f) < 0.3f, "faith plate at %.0f fps: the arc rose %.2f m, not 2.5", 1.0f / dt, rise);
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
    button_t const* bt  = &g.lv.buttons[0];
    g.held              = 0;
    g.cubes[0].body.pos = v3((float)bt->x + 0.5f, (float)bt->y + 1.05f, (float)bt->z + 0.5f);
    game_step(&g, &idle, 0.02f);
    CHECK(!g.lv.buttons[0].pressed, "a carried cube does not press a button");

    // No floor portal under a button: its pad covers that floor.
    button_t const* b0 = &g.lv.buttons[0];
    portal_t        under;
    for (int dz = -2; dz <= 2; dz++) level_set(&g.lv, b0->x, b0->y, b0->z + dz, MAT_WHITE);
    CHECK(portal_place_at(&g.lv, b0->x, b0->y, b0->z + 1, DIR_PY, v3(0, 0, 1), NULL, &under),
          "white floor beside it takes a portal");
    CHECK(level_portalable(&g.lv, b0->x, b0->y, b0->z) &&
              !portal_place_at(&g.lv, b0->x, b0->y, b0->z, DIR_PY, v3(0, 0, 1), NULL, &under) &&
              !portal_place_at(&g.lv, b0->x, b0->y, b0->z - 1, DIR_PY, v3(0, 0, 1), NULL, &under),
          "no portal on the floor under a button");

    // The old button letter A is button 1.
    CHECK(chamber_parse("size: 5 3 4\nlayer 1\n#####\n#.a.#\n#S.A#\n#####\n", &a, NULL, NULL, err, sizeof(err)),
          "old A: %s", err);
    CHECK(chamber_parse("size: 5 3 4\nlayer 1\n#####\n#.a.#\n#S.1#\n#####\n", &b, NULL, NULL, err, sizeof(err)),
          "new 1: %s", err);
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

    // A faith plate throws a cube as it throws the player.
    static level_t lv;
    char const*    plate_room =
        "size: 12 7 5\n"
        "layer 0\n############\n#WWWWWWWWWW#\n#WJWWWWWWWW#\n#WWWWWWWWWW#\n############\n"
        "layer 1\n############\n#..........#\n#.C......T.#\n#S.........#\n############\n"
        "layer 2\n############\n#..........#\n#..........#\n#..........#\n############\n"
        "layer 3\n############\n#..........#\n#..........#\n#..........#\n############\n"
        "layer 4\n############\n#..........#\n#..........#\n#..........#\n############\n"
        "layer 5\n############\n#..........#\n#..........#\n#..........#\n############\n";
    CHECK(chamber_parse(plate_room, &lv, NULL, NULL, err, sizeof(err)), "the plate room: %s", err);
    game_load_level(&g, &lv);
    bool   thrown = false;
    vec3_t cube   = g.cubes[0].body.pos;
    for (int i = 0; i < 150; i++) {
        game_step(&g, &idle, 0.02f);
        if (!g.cubes[0].body.on_ground) thrown = true;
        if (thrown && g.cubes[0].body.on_ground) {
            cube = g.cubes[0].body.pos;  // where it comes down (it slides on a little)
            break;
        }
    }
    vec3_t const target = g.lv.jumps[0].target;
    CHECK(thrown && v3_len(v3_sub(v3(cube.x, 0, cube.z), v3(target.x, 0, target.z))) < 0.5f,
          "a cube on a faith plate comes down on its target (at %.2f %.2f %.2f)", cube.x, cube.y, cube.z);

    // The platform waits for a player standing in its way, rather than
    // moving into them.
    char const* lane =
        "size: 12 4 3\n"
        "layer 0\n############\n#WWWWWWWWWW#\n############\n"
        "layer 1\n############\n#MM...S.N..#\n############\n"
        "layer 2\n############\n#..........#\n############\n";
    CHECK(chamber_parse(lane, &lv, NULL, NULL, err, sizeof(err)), "the platform lane: %s", err);
    game_load_level(&g, &lv);
    bool into = false;
    for (int i = 0; i < 400; i++) {
        game_step(&g, &idle, 0.02f);
        aabb_t const pa = player_box(&g.pl), pf = platform_aabb(&g);
        into = into || (pa.lo.x < pf.hi.x && pf.lo.x < pa.hi.x && pa.lo.y < pf.hi.y && pf.lo.y < pa.hi.y);
    }
    CHECK(!into, "the platform does not move into the player");
    CHECK(platform_aabb(&g).hi.x > 4.5f, "it came up to them, though (to %.2f)", platform_aabb(&g).hi.x);

    // The moving platform stops a portal shot.
    game_load(&g, demo_chamber(demo_find("09-the-ferry")));
    g.pl.pos   = v3(5.0f, 2.0f, 3.0f);  // on the platform
    g.pl.pitch = 1.45f;                 // looking straight down at it
    CHECK(!game_fire(&g, 0), "a shot at the moving platform places nothing");
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);  // every FAIL line out, even if a later test crashes
    test_solutions_fps();
    test_floor_portal();
    test_portal_back();
    test_platform_ceiling();
    test_fizzlers();
    test_faith_plate_fps();
    test_more_things();
    test_legend();
    test_two_buttons();
    test_caps();
    test_draft_keeps();
    test_parse_rules();
    test_pedestal_dropper();
    test_lasers();
    test_bridges();
    test_gel();
    test_pellets();
    test_draft_save();
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
    test_dir_limits();
    if (s_fail) {
        printf("%d check(s) failed\n", s_fail);
        return 1;
    }
    printf("host tests: all passed\n");
    return 0;
}
