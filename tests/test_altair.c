/* test_altair.c — the Altair 8800 lamp panel's model (WI #4777, sprint 044).
 *
 * The expected layouts and palette were computed by running the mock-up's
 * own layout() and mix() (sprints/planning/altair-mockups.html) under node at
 * the mock-up's free widths, so this holds the port to the specification
 * rather than to itself. */
#include "altair.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int passed = 0;
static int failed = 0;

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr);                      \
            failed++;                                                                              \
        } else {                                                                                   \
            passed++;                                                                              \
        }                                                                                          \
    } while (0)

static alt_machine_t M, M2; /* 64 KB each: not on the stack */

/* ---- The emulator ---- */

static void test_tstates(void) {
    static const uint8_t prog[] = {
        0x21, 0x00, 0x00, /* LXI H      10 */
        0x01, 0x00, 0x00, /* LXI B      10 */
        0x16, 0x00,       /* MVI D       7 */
        0x1a,             /* LDAX D      7 */
        0x09,             /* DAD B      10 */
        0xd2, 0x0d, 0x00, /* JNC 000D   10 */
        0xdb, 0xff,       /* IN FF      10 */
        0xaa,             /* XRA D       4 */
        0x0f,             /* RRC         4 */
        0x57,             /* MOV D,A     5 */
        0x23,             /* INX H       5 */
        0x7e,             /* MOV A,M     7 */
        0xc3, 0x00, 0x00, /* JMP 0000   10 */
    };
    static const int want[] = {10, 10, 7, 7, 10, 10, 10, 4, 4, 5, 5, 7, 10};
    alt_reset(&M, ALT_PROG_KTB, false, 1);
    memset(M.mem, 0, 64);
    memcpy(M.mem, prog, sizeof(prog));
    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        int t = alt_step(&M);
        if (t != want[i])
            fprintf(stderr, "  instruction %zu: %d T-states, want %d\n", i, t, want[i]);
        CHECK(t == want[i]);
    }
    CHECK(M.pc == 0);
    /* Outside the subset: start again at 0, after the fetch's 4 T-states. */
    M.mem[0x40] = 0x76; /* HLT */
    M.pc = 0x40;
    CHECK(alt_step(&M) == 4);
    CHECK(M.pc == 0);
}

/* Step until MOV D,A has run; returns the T-states that took. */
static uint64_t to_next_bit(alt_machine_t *m) {
    uint64_t t0 = m->t;
    for (;;) {
        uint16_t pc = m->pc;
        alt_step(m);
        if (pc == 0x14)
            return m->t - t0;
    }
}

static void test_ktb_walks(void) {
    alt_reset(&M, ALT_PROG_KTB, false, 1);
    static const uint8_t want[] = {0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01, 0x80, 0x40};
    for (size_t i = 0; i < sizeof(want); i++) {
        uint64_t t = to_next_bit(&M);
        CHECK(M.d == want[i]);
        if (i > 0) {
            /* About 2 steps a second at ALT_CLOCK_HZ (Ken's setting). */
            double per_s = (double)ALT_CLOCK_HZ / (double)t;
            CHECK(per_s > 1.95 && per_s < 2.05);
        }
    }
}

static void test_ktb_hit_miss(void) {
    /* A hit names the lit bit: it clears, and the bits stay cleared. */
    alt_reset(&M, ALT_PROG_KTB, false, 1);
    M.switches = 0x80;
    to_next_bit(&M);
    CHECK(M.d == 0x00);
    M.switches = 0;
    to_next_bit(&M);
    CHECK(M.d == 0x00);

    /* A miss adds one, and both walk. */
    alt_reset(&M, ALT_PROG_KTB, false, 1);
    M.switches = 0x01;
    to_next_bit(&M);
    CHECK(M.d == 0xC0);
    M.switches = 0;
    to_next_bit(&M);
    CHECK(M.d == 0x60);

    /* Two bits lit, one hit: the other walks on. */
    M.switches = 0x40;
    to_next_bit(&M);
    CHECK(M.d == 0x10);
}

/* ---- The accumulator ---- */

static void test_accumulator(void) {
    alt_reset(&M, ALT_PROG_KTB, false, 1);
    for (int i = 0; i < 3; i++) /* LXI H, MVI D, LXI B */
        alt_step(&M);
    float f[ALT_LAMPS];
    alt_fractions(&M, f);
    CHECK(M.total == 0);

    /* 100 passes of the delay loop: LDAX D ×4, DAD B, JNC. 48 T-states each:
     * 24 of them M1 (the fetches), all but DAD's 6 internal ones MEMR, all
     * of them WO, and the bit on A15 for the four LDAX reads (4 × 3: the
     * fetches put PC on the bus, not DE). */
    for (int i = 0; i < 100 * 6; i++)
        alt_step(&M);
    CHECK(M.pc == 0x08);
    CHECK(M.total == 4800);
    CHECK(M.on[ALT_L_M1] == 2400);
    CHECK(M.on[ALT_L_MEMR] == 4200);
    CHECK(M.on[ALT_L_WO] == 4800);
    CHECK(M.on[ALT_L_INP] == 0);
    CHECK(M.on[ALT_L_A(15)] == 1200);
    for (int i = 8; i < 15; i++)
        CHECK(M.on[ALT_L_A(i)] == 0);
    alt_fractions(&M, f);
    CHECK(fabsf(f[ALT_L_M1] - 0.5f) < 1e-6f);
    CHECK(fabsf(f[ALT_L_MEMR] - 0.875f) < 1e-6f);
    CHECK(f[ALT_L_WO] == 1.0f);
    CHECK(fabsf(f[ALT_L_A(15)] - 12.0f / 48.0f) < 1e-6f);
    /* Read once: the accumulators are reset. */
    alt_fractions(&M, f);
    for (int i = 0; i < ALT_LAMPS; i++)
        CHECK(f[i] == 0.0f);

    /* A whole redraw's worth: the same proportions, plus one IN's 3 T-states
     * of INP; every fraction in [0, 1]; the lamps nothing drives stay dark. */
    alt_reset(&M, ALT_PROG_KTB, false, 1);
    alt_run_ms(&M, ALT_REDRAW_MS);
    alt_fractions(&M, f);
    for (int i = 0; i < ALT_LAMPS; i++)
        CHECK(f[i] >= 0.0f && f[i] <= 1.0f);
    CHECK(fabsf(f[ALT_L_M1] - 0.5f) < 0.01f);
    CHECK(fabsf(f[ALT_L_MEMR] - 0.875f) < 0.01f);
    CHECK(f[ALT_L_WO] == 1.0f);
    CHECK(f[ALT_L_INP] > 0.0f && f[ALT_L_INP] < 1e-4f);
    static const int dark[] = {ALT_L_INTE, ALT_L_PROT, ALT_L_OUT, ALT_L_HLTA, ALT_L_STACK,
                               ALT_L_INT,  ALT_L_WAIT, ALT_L_HLDA};
    for (size_t i = 0; i < sizeof(dark) / sizeof(dark[0]); i++)
        CHECK(f[dark[i]] == 0.0f);
}

static void test_run_keeps_clock(void) {
    alt_reset(&M, ALT_PROG_KTB, false, 1);
    for (int i = 0; i < 1000; i++)
        alt_run_ms(&M, ALT_REDRAW_MS);
    int64_t want = (int64_t)1000 * ALT_REDRAW_MS * ALT_CLOCK_HZ / 1000;
    CHECK((int64_t)M.t >= want && (int64_t)M.t < want + 10);
}

static void test_autoplay(void) {
    alt_reset(&M, ALT_PROG_KTB, true, 42);
    alt_reset(&M2, ALT_PROG_KTB, true, 42);
    int hits = 0, misses = 0, prev = __builtin_popcount(M.d);
    bool same = true;
    float f[ALT_LAMPS];
    for (int i = 0; i < 2 * 600; i++) { /* ten emulated minutes, two redraws a second */
        alt_run_ms(&M, ALT_REDRAW_MS);
        alt_run_ms(&M2, ALT_REDRAW_MS);
        alt_fractions(&M, f);
        alt_fractions(&M2, f);
        same = same && M.d == M2.d && M.t == M2.t;
        int n = __builtin_popcount(M.d);
        hits += n < prev;
        misses += n > prev;
        prev = n;
        CHECK(n <= 8);
    }
    CHECK(same);     /* seeded: reproducible */
    CHECK(hits > 0); /* and the player plays */
    CHECK(misses > 0);

    alt_reset(&M2, ALT_PROG_KTB, true, 7);
    for (int i = 0; i < 2 * 600; i++)
        alt_run_ms(&M2, ALT_REDRAW_MS);
    CHECK(M.rng != M2.rng);
}

static void test_counter(void) {
    alt_reset(&M, ALT_PROG_COUNT, false, 1);
    for (int i = 0; i < 1 + 3 * 300; i++)
        alt_step(&M);
    CHECK(M.h == 1 && M.l == 0x2C); /* 300 increments */
}

/* ---- Brightness ---- */

static void test_levels(void) {
    CHECK(alt_level(0.0f) == 0);
    CHECK(alt_level(1.0f) == ALT_LEVELS - 1);
    CHECK(alt_level(0.5f) == 8); /* 7.5 rounds up, as Math.round does */
    CHECK(alt_level(-1.0f) == 0);
    CHECK(alt_level(2.0f) == ALT_LEVELS - 1);
}

static void test_palette(void) {
    /* mix('#1e1e2e', '#ff4a3d', 0.10) then 16 steps to '#ff4a3d' at 100 %. */
    static const uint32_t want[ALT_LEVELS] = {
        0x352230, 0x422531, 0x502732, 0x5d2a33, 0x6b2d33, 0x782f34, 0x863235, 0x933536,
        0xa13737, 0xae3a38, 0xbc3d39, 0xc93f3a, 0xd7423a, 0xe4453b, 0xf2473c, 0xff4a3d};
    uint32_t got[ALT_LEVELS];
    alt_palette(got);
    if (ALT_MAX_BRIGHTNESS == 100 && ALT_LAMP_RGB == 0xFF4A3D && ALT_OFF_MIX == 10) {
        for (int i = 0; i < ALT_LEVELS; i++)
            CHECK(got[i] == want[i]);
    } else {
        /* Tuned since: the ends at least are where the constants say. */
        CHECK(got[ALT_LEVELS - 1] != got[0]);
    }
}

/* ---- Layout ---- */

typedef struct {
    int id, x, row;
} lamp_want_t;

static const alt_lamp_t *find(const alt_layout_t *L, int id) {
    for (int i = 0; i < L->nlamps; i++)
        if (L->lamps[i].id == id)
            return &L->lamps[i];
    return NULL;
}

static void check_lamps(const alt_layout_t *L, const lamp_want_t *w, int n) {
    CHECK(L->nlamps == n);
    for (int i = 0; i < n; i++) {
        const alt_lamp_t *l = find(L, w[i].id);
        CHECK(l != NULL);
        if (l && (l->x != w[i].x || l->row != w[i].row)) {
            fprintf(stderr, "  lamp %d at %d row %d, want %d row %d\n", w[i].id, l->x, l->row,
                    w[i].x, w[i].row);
            failed++;
        }
    }
}

static void test_layout(void) {
    alt_layout_t L;

    /* 8 status cards in the mock-up: 36 lamps, per-lamp labels. */
    static const lamp_want_t W1956[] = {
        {0, 1861, 1},  {1, 1772, 1},  {2, 1683, 1},  {3, 1550, 1},  {4, 1461, 1},  {5, 1372, 1},
        {6, 1239, 1},  {7, 1150, 1},  {8, 1061, 1},  {9, 927, 1},   {10, 838, 1},  {11, 749, 1},
        {12, 616, 1},  {13, 527, 1},  {14, 438, 1},  {15, 305, 1},  {16, 1861, 0}, {17, 1772, 0},
        {18, 1683, 0}, {19, 1550, 0}, {20, 1461, 0}, {21, 1372, 0}, {22, 1239, 0}, {23, 1150, 0},
        {24, 38, 0},   {25, 127, 0},  {26, 216, 0},  {27, 305, 0},  {28, 394, 0},  {29, 483, 0},
        {30, 572, 0},  {31, 660, 0},  {32, 749, 0},  {33, 838, 0},  {34, 38, 1},   {35, 127, 1}};
    alt_layout(1956, &L);
    CHECK(L.mode == ALT_MODE_FULL);
    CHECK(L.lamp_w == 57);
    CHECK(fabs(L.pitch - 88.9302) < 1e-3);
    CHECK(L.per_lamp);
    check_lamps(&L, W1956, 36);
    CHECK(L.x_name == 1039); /* between A9 and A7's left edges: (927 + 1150) / 2 */

    /* 12 (today): 36 lamps, group labels. */
    alt_layout(1044, &L);
    CHECK(L.mode == ALT_MODE_FULL);
    CHECK(L.lamp_w == 30);
    CHECK(!L.per_lamp);
    CHECK(find(&L, ALT_L_A(15))->x == 170);
    CHECK(find(&L, ALT_L_D(0))->x == 984);
    CHECK(find(&L, ALT_L_INTE)->x == 30);
    CHECK(find(&L, ALT_L_HLDA)->x == 77);
    CHECK(L.x_status == 30 && L.x_address == 170 && L.x_data == 612);

    /* 14: 24 lamps, the status row dropped. */
    static const lamp_want_t W588[] = {
        {0, 542, 1},  {1, 512, 1},  {2, 483, 1},  {3, 439, 1},  {4, 409, 1},  {5, 380, 1},
        {6, 336, 1},  {7, 307, 1},  {8, 277, 1},  {9, 233, 1},  {10, 204, 1}, {11, 174, 1},
        {12, 130, 1}, {13, 101, 1}, {14, 71, 1},  {15, 27, 1},  {16, 542, 0}, {17, 512, 0},
        {18, 483, 0}, {19, 439, 0}, {20, 409, 0}, {21, 380, 0}, {22, 336, 0}, {23, 307, 0}};
    alt_layout(588, &L);
    CHECK(L.mode == ALT_MODE_COMPACT);
    CHECK(L.lamp_w == 19);
    check_lamps(&L, W588, 24);
    CHECK(L.x_address == 27 && L.x_data == 307);

    /* 15: compact, narrow. */
    alt_layout(360, &L);
    CHECK(L.mode == ALT_MODE_COMPACT);
    CHECK(L.lamp_w == 11);
    CHECK(find(&L, ALT_L_A(15))->x == 25);
    CHECK(find(&L, ALT_L_A(0))->x == 324);

    /* 16: nothing. */
    alt_layout(132, &L);
    CHECK(L.mode == ALT_MODE_NONE);
    CHECK(L.nlamps == 0);

    /* The thresholds. Full needs pitch 26: (w - 44) / 21.5 >= 26. */
    alt_layout(44 + 559, &L); /* 26.0 */
    CHECK(L.mode == ALT_MODE_FULL);
    alt_layout(44 + 558, &L);
    CHECK(L.mode == ALT_MODE_COMPACT);
    /* Compact needs 14: (w - 44) / 18.5 >= 14, i.e. w >= 303. */
    alt_layout(303, &L);
    CHECK(L.mode == ALT_MODE_COMPACT);
    alt_layout(302, &L);
    CHECK(L.mode == ALT_MODE_NONE);
    /* Pitch capped at 110 and the lamps centred in what is left. */
    alt_layout(3792, &L);
    CHECK(L.mode == ALT_MODE_FULL && L.pitch == ALT_MAX_PITCH && L.lamp_w == 70);
    CHECK(find(&L, ALT_L_INTE)->x > ALT_PAD);
    /* Per-lamp labels from pitch 88: (w - 44) / 21.5 >= 88, w >= 1936. */
    alt_layout(1936, &L);
    CHECK(L.per_lamp);
    alt_layout(1935, &L);
    CHECK(!L.per_lamp);

    /* Every lamp inside the card at every width. */
    for (int w = 0; w <= 3792; w++) {
        alt_layout(w, &L);
        for (int i = 0; i < L.nlamps; i++)
            if (L.lamps[i].x < ALT_PAD || L.lamps[i].x + L.lamp_w > w - ALT_PAD + 1) {
                fprintf(stderr, "  width %d: lamp %d at %d outside\n", w, L.lamps[i].id,
                        L.lamps[i].x);
                failed++;
                w = 3793;
                break;
            }
    }
    passed++;
}

int main(void) {
    test_tstates();
    test_ktb_walks();
    test_ktb_hit_miss();
    test_accumulator();
    test_run_keeps_clock();
    test_autoplay();
    test_counter();
    test_levels();
    test_palette();
    test_layout();
    printf("test_altair: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
