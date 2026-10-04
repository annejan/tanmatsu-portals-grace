// Host tests: the portal maths, placement, and a scripted player solving
// every chamber with real shots and real physics. `make check`.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
    CHECK(cs.n == 5, "five planes, got %d", cs.n);
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

int main(void) {
    test_basis();
    test_map();
    test_clip();
    test_mesh();
    test_chamber_1();
    test_chamber_2();
    test_chamber_3();
    test_loop();
    if (s_fail) {
        printf("%d check(s) failed\n", s_fail);
        return 1;
    }
    printf("host tests: all passed\n");
    return 0;
}
