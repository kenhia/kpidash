/* test_lampmach.c — the lamp panel's machine interface and rotation
 * (WI #4783, sprint 045).
 *
 * Three things: the Altair behind the interface is sprint 044's Altair (same
 * lamps, same labels, same palette — the generalisation must not change it);
 * the PDP-8 behind it carries the mock-up's colours per lamp; and the
 * rotation, driven by an injected clock. */
#include "lampmach.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "altair.h"
#include "expected.h" /* tests/fixtures/pdp8: EXP_PALETTE, from the mock-up */
#include "pdp8.h"

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

static lm_layout_t L;
static alt_layout_t A;

/* ---- The Altair, unchanged ---- */

static void test_altair_layout_unchanged(void) {
    const lm_machine_t *M = LM_MACHINES[LM_ALTAIR];
    static const int lamp_y[2] = {ALT_LAMP_Y0, ALT_LAMP_Y1};
    int bad = 0;
    for (int w = 0; w <= 3792; w++) {
        alt_layout(w, &A);
        M->layout(w, &L);
        if (L.drawable != (A.mode != ALT_MODE_NONE)) {
            bad++;
            continue;
        }
        if (!L.drawable)
            continue;
        bad += L.radius != ALT_LAMP_R || L.nlamps != A.nlamps;
        for (int i = 0; i < A.nlamps && i < L.nlamps; i++) {
            const alt_lamp_t *a = &A.lamps[i];
            const lm_lamp_t *l = &L.lamps[i];
            bad += l->x != a->x || l->y != lamp_y[a->row] || l->w != A.lamp_w ||
                   l->h != ALT_LAMP_H || l->id != a->id || l->pal != 0;
        }
        /* Sprint 044's labels: one per lamp plus the name in full per-lamp
         * mode; otherwise DATA and ADDRESS, then STATUS and 8800 (full) or
         * the name (compact). */
        bool full = A.mode == ALT_MODE_FULL;
        int want = A.per_lamp ? A.nlamps + full : full ? 4 : 3;
        bad += L.nlabels != want;
    }
    CHECK(bad == 0);

    /* Spot checks of the labels against sprint 044's build(). */
    alt_layout(1968, &A); /* 8 cards: full, per lamp */
    M->layout(1968, &L);
    CHECK(A.per_lamp && L.nlabels == 37);
    CHECK(strcmp(L.labels[0].text, A.lamps[0].name) == 0 && L.labels[0].centre &&
          L.labels[0].x == A.lamps[0].x + A.lamp_w / 2 && L.labels[0].base == ALT_LABEL_BASE1 &&
          L.labels[0].font == LM_FONT_20 && L.labels[0].rgb == ALT_LABEL_RGB);
    CHECK(strcmp(L.labels[36].text, "ALTAIR 8800") == 0 && L.labels[36].font == LM_FONT_24 &&
          L.labels[36].x == A.x_name && L.labels[36].base == ALT_NAME_BASE &&
          L.labels[36].centre && L.labels[36].rgb == ALT_NAME_RGB);

    alt_layout(1056, &A); /* 12 cards: full, per group */
    M->layout(1056, &L);
    CHECK(!A.per_lamp && L.nlabels == 4);
    CHECK(strcmp(L.labels[0].text, "DATA") == 0 && L.labels[0].x == A.x_data && !L.labels[0].centre);
    CHECK(strcmp(L.labels[1].text, "ADDRESS") == 0 && L.labels[1].x == A.x_address &&
          L.labels[1].base == ALT_LABEL_BASE1);
    CHECK(strcmp(L.labels[2].text, "STATUS") == 0 && L.labels[2].x == A.x_status);
    CHECK(strcmp(L.labels[3].text, "8800") == 0 && L.labels[3].rgb == ALT_NAME_RGB &&
          L.labels[3].base == ALT_LABEL_BASE1);

    alt_layout(600, &A); /* 14 cards: compact */
    M->layout(600, &L);
    CHECK(strcmp(L.labels[2].text, "ALTAIR 8800") == 0 && L.labels[2].font == LM_FONT_20 &&
          L.labels[2].x == A.x_address && L.labels[2].base == ALT_LABEL_BASE0);
}

static void test_palettes(void) {
    uint32_t want[ALT_LEVELS], got[LM_LEVELS];
    alt_palette(want);
    lm_palette(ALT_PANEL_RGB, ALT_LAMP_RGB, ALT_MAX_BRIGHTNESS, got);
    CHECK(LM_LEVELS == ALT_LEVELS && memcmp(want, got, sizeof(got)) == 0);
    const lm_machine_t *M = LM_MACHINES[LM_ALTAIR];
    CHECK(M->ncolours == 1 && M->colours[0] == ALT_LAMP_RGB && M->card_rgb == ALT_PANEL_RGB);

    const lm_machine_t *P = LM_MACHINES[LM_PDP8];
    CHECK(P->ncolours == 4 && P->max_brightness == 100 && P->card_rgb == 0x1E1E2E);
    int same = 0;
    for (int c = 0; c < 4; c++) {
        lm_palette(P->card_rgb, P->colours[c], P->max_brightness, got);
        for (int i = 0; i < LM_LEVELS; i++)
            same += got[i] == EXP_PALETTE[c][i];
    }
    CHECK(same == 4 * LM_LEVELS);

    int lv_same = 0;
    for (int i = -10; i <= 1010; i++)
        lv_same += lm_level((float)i / 1000) == alt_level((float)i / 1000);
    CHECK(lv_same == 1021);
}

/* ---- The PDP-8 behind the interface ---- */

static void test_pdp8_machine(void) {
    const lm_machine_t *M = LM_MACHINES[LM_PDP8];
    M->layout(1968, &L);
    CHECK(L.drawable && L.nlamps == 89 && L.radius == PDP8_LAMP_R);
    int pal_ok = 0;
    for (int i = 0; i < L.nlamps; i++)
        pal_ok += L.lamps[i].pal == pdp8_colour(L.lamps[i].id);
    CHECK(pal_ok == 89);
    int name = -1;
    for (int i = 0; i < L.nlabels; i++)
        if (strcmp(L.labels[i].text, "PDP-8/I") == 0)
            name = i;
    CHECK(name >= 0 && L.labels[name].font == LM_FONT_24 && !L.labels[name].centre);
    M->layout(144, &L);
    CHECK(!L.drawable && L.nlamps == 0);

    float f[LM_MAX_LAMPS];
    M->reset();
    M->advance(500, f);
    CHECK(f[PDP8_L_RUN] == 1.0f && f[PDP8_L_IF] == 1.0f);
    M->advance(0, f);
    CHECK(f[PDP8_L_RUN] == 0);
}

/* ---- Rotation, on an injected clock ---- */

static const bool BOTH[LM_NMACHINES] = {true, true};

static void test_rotation_default(void) {
    lm_rotation_t r;
    lm_rotation_init(&r);
    CHECK(r.cur == -1);
    CHECK(lm_rotation_tick(&r, 1000, LM_PERIOD_DEFAULT_S, BOTH) && r.cur == LM_ALTAIR);
    CHECK(!lm_rotation_tick(&r, 1000 + 899999, LM_PERIOD_DEFAULT_S, BOTH) && r.cur == LM_ALTAIR);
    CHECK(lm_rotation_tick(&r, 1000 + 900000, LM_PERIOD_DEFAULT_S, BOTH) && r.cur == LM_PDP8);
    CHECK(!lm_rotation_tick(&r, 1000 + 1799999, LM_PERIOD_DEFAULT_S, BOTH));
    CHECK(lm_rotation_tick(&r, 1000 + 1800000, LM_PERIOD_DEFAULT_S, BOTH) && r.cur == LM_ALTAIR);
}

static void test_rotation_changed_period(void) {
    lm_rotation_t r;
    lm_rotation_init(&r);
    lm_rotation_tick(&r, 0, 900, BOTH);
    /* 100 s in, Ken sets 60: the machine has had more than that already, so
     * the next tick switches. */
    CHECK(!lm_rotation_tick(&r, 100000, 900, BOTH));
    CHECK(lm_rotation_tick(&r, 100500, 60, BOTH) && r.cur == LM_PDP8);
    CHECK(!lm_rotation_tick(&r, 160499, 60, BOTH));
    CHECK(lm_rotation_tick(&r, 160500, 60, BOTH) && r.cur == LM_ALTAIR);
    /* Lengthened mid-showing: the new period counts from when it came on. */
    CHECK(!lm_rotation_tick(&r, 200000, 120, BOTH));
    CHECK(lm_rotation_tick(&r, 280500, 120, BOTH) && r.cur == LM_PDP8);
}

static void test_rotation_clamp(void) {
    lm_rotation_t r;
    lm_rotation_init(&r);
    lm_rotation_tick(&r, 0, 1, BOTH);
    CHECK(!lm_rotation_tick(&r, 9999, 1, BOTH));
    CHECK(lm_rotation_tick(&r, 10000, 1, BOTH) && r.cur == LM_PDP8);
    CHECK(!lm_rotation_tick(&r, 15000, 0, BOTH));
    CHECK(!lm_rotation_tick(&r, 19999, -5, BOTH));
    CHECK(lm_rotation_tick(&r, 20000, -5, BOTH) && r.cur == LM_ALTAIR);
}

static void test_rotation_skips(void) {
    const bool alt_only[LM_NMACHINES] = {true, false};
    const bool pdp_only[LM_NMACHINES] = {false, true};
    const bool none[LM_NMACHINES] = {false, false};
    lm_rotation_t r;

    /* A machine that cannot draw is never shown, and the one that can stays
     * on (running, not restarted) past its period. */
    lm_rotation_init(&r);
    CHECK(lm_rotation_tick(&r, 0, 60, pdp_only) && r.cur == LM_PDP8);
    CHECK(!lm_rotation_tick(&r, 60000, 60, pdp_only) && r.cur == LM_PDP8);
    CHECK(!lm_rotation_tick(&r, 120000, 60, pdp_only) && r.cur == LM_PDP8);

    /* The width changes under it: replaced at once, not at the period. */
    CHECK(lm_rotation_tick(&r, 125000, 60, alt_only) && r.cur == LM_ALTAIR);
    /* Both fit again: Altair stays until its period, then the PDP-8. */
    CHECK(!lm_rotation_tick(&r, 184999, 60, BOTH));
    CHECK(lm_rotation_tick(&r, 185000, 60, BOTH) && r.cur == LM_PDP8);

    /* Nothing fits: none, reported once; then the first that fits. */
    CHECK(lm_rotation_tick(&r, 190000, 60, none) && r.cur == -1);
    CHECK(!lm_rotation_tick(&r, 190500, 60, none) && r.cur == -1);
    CHECK(lm_rotation_tick(&r, 191000, 60, BOTH) && r.cur == LM_ALTAIR);

    lm_rotation_init(&r);
    CHECK(!lm_rotation_tick(&r, 0, 60, none) && r.cur == -1);
}

static void test_rotation_wraps(void) {
    lm_rotation_t r;
    lm_rotation_init(&r);
    uint32_t t0 = 0xFFFFFFFFu - 5000; /* lv_tick wraps after 49.7 days */
    lm_rotation_tick(&r, t0, 10, BOTH);
    CHECK(!lm_rotation_tick(&r, t0 + 9999, 10, BOTH));
    CHECK(lm_rotation_tick(&r, t0 + 10000, 10, BOTH) && r.cur == LM_PDP8);
}

int main(void) {
    test_altair_layout_unchanged();
    test_palettes();
    test_pdp8_machine();
    test_rotation_default();
    test_rotation_changed_period();
    test_rotation_clamp();
    test_rotation_skips();
    test_rotation_wraps();
    printf("test_lampmach: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
