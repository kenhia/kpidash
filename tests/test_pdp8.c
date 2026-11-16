/* test_pdp8.c — the PDP-8/I lamp panel's model (WI #4783, sprint 045).
 *
 * The expected layouts and lamp counts were computed by running the mock-up's
 * own layout() and emulator (sprints/planning/pdp8-mockups.html) under node
 * — tests/fixtures/pdp8/gen.js wrote expected.h — so this holds the port to
 * the specification rather than to itself. The instruction tests are by
 * hand, from the PDP-8 instruction set. */
#include "pdp8.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "expected.h"

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

static pdp8_t M;

/* Load `words` at 0200 over a reset machine. */
static void load(const uint16_t *words, int n) {
    pdp8_reset(&M, PDP8_PROG_WALK);
    memset(M.mem, 0, sizeof(M.mem));
    memcpy(M.mem + 0200, words, (size_t)n * sizeof(words[0]));
}

/* Run whole instructions: cycles until the machine is back in FETCH. */
static void step(int n) {
    for (int i = 0; i < n; i++) {
        do
            pdp8_cycle(&M);
        while (M.state != PDP8_FETCH);
    }
}

/* ---- Memory reference: the six instructions and their major states ---- */

static void test_memory_reference(void) {
    /* AND 0250 (page zero: 0050 is outside it, so use the current page). */
    static const uint16_t and_[] = {00250};
    load(and_, 1);
    M.mem[0250] = 07070;
    M.ac = 05555;
    pdp8_cycle(&M); /* FETCH */
    CHECK(M.state == PDP8_EXEC && M.ir == 0 && M.ma == 0200 && M.mb == 00250 && M.pc == 0201);
    pdp8_cycle(&M); /* EXECUTE */
    CHECK(M.state == PDP8_FETCH && M.ma == 0250 && M.mb == 07070 && M.ac == (05555 & 07070));

    /* TAD: add; a carry out complements the link. */
    static const uint16_t tad[] = {01250, 01250};
    load(tad, 2);
    M.mem[0250] = 04000;
    M.ac = 04000;
    step(1);
    CHECK(M.ac == 0 && M.l == 1);
    step(1);
    CHECK(M.ac == 04000 && M.l == 1);

    /* ISZ: increment, skip on zero. */
    static const uint16_t isz[] = {02250, 07000, 07000};
    load(isz, 3);
    M.mem[0250] = 07776;
    step(1);
    CHECK(M.mem[0250] == 07777 && M.pc == 0201);
    M.pc = 0200;
    step(1);
    CHECK(M.mem[0250] == 0 && M.pc == 0202);

    /* DCA: deposit and clear. */
    static const uint16_t dca[] = {03250};
    load(dca, 1);
    M.ac = 01234;
    step(1);
    CHECK(M.mem[0250] == 01234 && M.ac == 0 && M.mb == 01234);

    /* JMS: return address at the target, continue after it. */
    static const uint16_t jms[] = {04250};
    load(jms, 1);
    step(1);
    CHECK(M.mem[0250] == 0201 && M.pc == 0251);

    /* JMP direct: one cycle, no EXECUTE. */
    static const uint16_t jmp[] = {05210};
    load(jmp, 1);
    pdp8_cycle(&M);
    CHECK(M.state == PDP8_FETCH && M.pc == 0210 && M.ir == 5);

    /* Page zero vs current page: bit 0200 clear addresses page zero. */
    static const uint16_t pz[] = {01050};
    load(pz, 1);
    M.mem[0050] = 7;
    M.mem[0250] = 100;
    step(1);
    CHECK(M.ac == 7);
}

static void test_defer_and_autoindex(void) {
    /* TAD I 0250: FETCH, DEFER, EXECUTE. */
    static const uint16_t tadi[] = {01650};
    load(tadi, 1);
    M.mem[0250] = 0300;
    M.mem[0300] = 42;
    pdp8_cycle(&M);
    CHECK(M.state == PDP8_DEFER);
    pdp8_cycle(&M);
    CHECK(M.state == PDP8_EXEC && M.ma == 0250 && M.mb == 0300);
    pdp8_cycle(&M);
    CHECK(M.state == PDP8_FETCH && M.ac == 42 && M.ma == 0300);

    /* Each of those three cycles lit the state it was in, once each. */
    CHECK(M.on[PDP8_L_ST + PDP8_FETCH] == 1 && M.on[PDP8_L_ST + PDP8_DEFER] == 1 &&
          M.on[PDP8_L_ST + PDP8_EXEC] == 1);

    /* JMP I: FETCH, DEFER, and no EXECUTE. */
    static const uint16_t jmpi[] = {05650};
    load(jmpi, 1);
    M.mem[0250] = 0400;
    pdp8_cycle(&M);
    pdp8_cycle(&M);
    CHECK(M.state == PDP8_FETCH && M.pc == 0400);

    /* Auto-index: an indirect reference through 0010-0017 increments first. */
    static const uint16_t ai[] = {01410, 01410};
    load(ai, 2);
    M.mem[0010] = 0277;
    M.mem[0300] = 5;
    M.mem[0301] = 6;
    step(1);
    CHECK(M.mem[0010] == 0300 && M.ac == 5);
    step(1);
    CHECK(M.mem[0010] == 0301 && M.ac == 11);

    /* ...and 0020 is not an auto-index location. */
    static const uint16_t nai[] = {01420};
    load(nai, 1);
    M.mem[0020] = 0300;
    M.mem[0300] = 9;
    step(1);
    CHECK(M.mem[0020] == 0300 && M.ac == 9);
}

/* ---- Operate ---- */

static uint16_t op(uint16_t w, uint16_t ac, uint8_t l) {
    load(&w, 1);
    M.ac = ac;
    M.l = l;
    step(1);
    return M.ac;
}

static void test_group1(void) {
    CHECK(op(07200, 01234, 1) == 0 && M.l == 1);  /* CLA */
    CHECK(op(07100, 01234, 1) == 01234 && M.l == 0); /* CLL */
    CHECK(op(07040, 01234, 0) == 06543);          /* CMA */
    CHECK(op(07020, 0, 0) == 0 && M.l == 1);      /* CML */
    CHECK(op(07001, 0, 0) == 1);                  /* IAC */
    CHECK(op(07001, 07777, 0) == 0 && M.l == 1);  /* IAC carries into the link */
    CHECK(op(07004, 04000, 0) == 0 && M.l == 1);  /* RAL */
    CHECK(op(07004, 0, 1) == 1 && M.l == 0);      /* RAL: the link comes round */
    CHECK(op(07010, 1, 0) == 0 && M.l == 1);      /* RAR */
    CHECK(op(07006, 1, 0) == 4);                  /* RTL */
    CHECK(op(07012, 4, 0) == 1);                  /* RTR */
    CHECK(op(07041, 5, 0) == 07773);              /* CIA: CMA then IAC */
    CHECK(op(07300, 01234, 1) == 0 && M.l == 0);  /* CLA CLL */
    CHECK(op(07000, 01234, 1) == 01234 && M.l == 1); /* NOP */
    CHECK(M.ir == 7 && M.state == PDP8_FETCH);
}

static int skips(uint16_t w, uint16_t ac, uint8_t l) {
    op(w, ac, l);
    return M.pc == 0202;
}

static void test_group2(void) {
    CHECK(skips(07500, 04000, 0));   /* SMA */
    CHECK(!skips(07500, 03777, 0));
    CHECK(skips(07440, 0, 0));       /* SZA */
    CHECK(!skips(07440, 1, 0));
    CHECK(skips(07420, 0, 1));       /* SNL */
    CHECK(!skips(07420, 0, 0));
    CHECK(skips(07510, 03777, 0));   /* SPA */
    CHECK(!skips(07510, 04000, 0));
    CHECK(skips(07450, 1, 0));       /* SNA */
    CHECK(skips(07430, 0, 0));       /* SZL */
    CHECK(skips(07410, 0, 0));       /* SKP */
    CHECK(skips(07540, 0, 0));       /* SMA SZA: either */
    CHECK(op(07600, 01234, 0) == 0); /* CLA, group 2 */
    CHECK(skips(07640, 0, 0) && M.ac == 0); /* SZA CLA: the test sees AC first */
}

static void test_group3(void) {
    op(07421, 01234, 0); /* MQL */
    CHECK(M.mq == 01234 && M.ac == 0);
    load((const uint16_t[]){07501}, 1); /* MQA */
    M.ac = 00070;
    M.mq = 07000;
    step(1);
    CHECK(M.ac == 07070 && M.mq == 07000);
    load((const uint16_t[]){07701}, 1); /* CLA MQA: AC = MQ */
    M.ac = 1;
    M.mq = 02222;
    step(1);
    CHECK(M.ac == 02222);
    load((const uint16_t[]){07521}, 1); /* SWP: MQL and MQA, from the old MQ */
    M.ac = 1;
    M.mq = 2;
    step(1);
    CHECK(M.ac == 2 && M.mq == 1);
}

static void test_cdf(void) {
    static const uint16_t cdf[] = {06231, 06201};
    load(cdf, 2);
    step(1);
    CHECK(M.df == 3 && M.ir == 6 && M.ifl == 1);
    step(1);
    CHECK(M.df == 0);
}

/* ---- The programs, against the mock-up's own emulator ---- */

static void check_program(pdp8_prog_t prog, int cycles, const unsigned *on, const unsigned *regs) {
    pdp8_reset(&M, prog);
    for (int i = 0; i < cycles; i++)
        pdp8_cycle(&M);
    int same = 0;
    for (int i = 0; i < PDP8_LAMPS; i++)
        same += M.on[i] == on[i];
    CHECK(same == PDP8_LAMPS);
    CHECK(M.total == (uint32_t)cycles);
    CHECK(M.pc == regs[0] && M.ma == regs[1] && M.mb == regs[2] && M.ac == regs[3]);
    CHECK(M.mq == regs[4] && M.l == regs[5] && M.df == regs[6] && M.ifl == regs[7]);
    CHECK(M.ir == regs[8] && M.state == regs[9]);
}

static void test_programs_match_mockup(void) {
    check_program(PDP8_PROG_WALK, EXP_WALK_CYCLES, EXP_WALK_ON, EXP_WALK_REGS);
    check_program(PDP8_PROG_COUNT, EXP_COUNT_CYCLES, EXP_COUNT_ON, EXP_COUNT_REGS);
}

static void test_reset(void) {
    pdp8_reset(&M, PDP8_PROG_WALK);
    CHECK(M.pc == 0200 && M.ifl == 1 && M.df == 0 && M.state == PDP8_FETCH);
    CHECK(M.mem[0200] == 07300 && M.mem[0201] == 06221 && M.mem[0214] == 05203);
    step(2); /* CLA CLL; CDF 20 */
    CHECK(M.df == 2);
}

/* The bit moves one place per step through the 13 bits of link and AC, and
 * MQ counts the steps. */
static void test_walking_bit(void) {
    pdp8_reset(&M, PDP8_PROG_WALK);
    int moves = 0, bad = 0;
    uint16_t mq = 0;
    uint32_t prev = 0;
    for (int i = 0; i < 2000000 && moves < 40; i++) {
        pdp8_cycle(&M);
        if (M.state == PDP8_FETCH && M.ma == 0203) { /* just after RAL */
            uint32_t v = (uint32_t)M.l << 12 | M.ac;
            if (__builtin_popcount(v) != 1)
                bad++;
            if (prev && v != ((prev << 1 | prev >> 12) & 017777))
                bad++;
            if (prev && M.mq != (uint16_t)(mq + 1))
                bad++;
            prev = v;
            mq = M.mq;
            moves++;
        }
    }
    CHECK(moves == 40 && bad == 0);
    CHECK(mq == 39); /* the first RAL comes before the first count */
}

/* ---- Lamp time ---- */

static void test_fractions(void) {
    pdp8_reset(&M, PDP8_PROG_WALK);
    pdp8_run_ms(&M, 500);
    uint32_t total = M.total;
    CHECK(total == 12334); /* ceil(0.5 × 666667 × 0.037) */
    float f[PDP8_LAMPS];
    pdp8_fractions(&M, f);
    CHECK(f[PDP8_L_RUN] == 1.0f);
    CHECK(f[PDP8_L_PAUSE] == 0 && f[PDP8_L_ION] == 0);
    for (int i = 0; i < 5; i++)
        CHECK(f[PDP8_L_SC + i] == 0);
    float st = 0, ir = 0;
    for (int i = 0; i < 6; i++)
        st += f[PDP8_L_ST + i];
    for (int i = 0; i < 8; i++)
        ir += f[PDP8_L_IR + i];
    CHECK(fabsf(st - 1) < 1e-5f && fabsf(ir - 1) < 1e-5f);
    CHECK(f[PDP8_L_IF + 0] == 1.0f && f[PDP8_L_IF + 1] == 0);
    /* DF = 2 from the second cycle on: all but the first sample. */
    CHECK(fabsf(f[PDP8_L_DF + 1] - (float)(total - 1) / total) < 1e-6f);
    /* The delay loop dominates: ISZ (FETCH, EXEC) and JMP (FETCH) at 0204/0205.
     * Each cycle lights the state it was in, so FETCH is lit about two cycles
     * in three, EXEC one in three, and DEFER never (walk has no indirect). */
    CHECK(fabsf(f[PDP8_L_ST + PDP8_FETCH] - 2.0f / 3) < 0.01f);
    CHECK(fabsf(f[PDP8_L_ST + PDP8_EXEC] - 1.0f / 3) < 0.01f);
    CHECK(f[PDP8_L_ST + PDP8_DEFER] == 0);
    /* PC 0204-0206: bit 2 always, bit 7 always. */
    CHECK(f[PDP8_L_PC + 7] == 1.0f);
    /* Read and reset: nothing ran since, so all zero. */
    pdp8_fractions(&M, f);
    float sum = 0;
    for (int i = 0; i < PDP8_LAMPS; i++)
        sum += f[i];
    CHECK(sum == 0);
}

/* About 2 steps a second, and many short runs keep the clock. */
static void test_step_rate(void) {
    pdp8_reset(&M, PDP8_PROG_WALK);
    uint64_t cycles = 0;
    for (int i = 0; i < 120; i++) {
        pdp8_run_ms(&M, 500);
        cycles += M.total;
        M.total = 0;
    }
    double want = 60.0 * PDP8_CYCLES_PER_S * PDP8_SPEED;
    CHECK(fabs((double)cycles - want) <= 1.0);
    /* 3 × 4096 + 21 cycles a step: 24,667 cycles a second is 2.0 steps. */
    CHECK(M.mq >= 118 && M.mq <= 122);
}

/* ---- Colours ---- */

static void test_colours(void) {
    CHECK(pdp8_colour(PDP8_L_PC + 3) == PDP8_YELLOW);
    CHECK(pdp8_colour(PDP8_L_MA) == PDP8_GREEN && pdp8_colour(PDP8_L_MB + 11) == PDP8_GREEN);
    CHECK(pdp8_colour(PDP8_L_AC) == PDP8_RED && pdp8_colour(PDP8_L_LINK) == PDP8_RED);
    CHECK(pdp8_colour(PDP8_L_MQ + 5) == PDP8_BLUE && pdp8_colour(PDP8_L_SC + 4) == PDP8_BLUE);
    CHECK(pdp8_colour(PDP8_L_DF) == PDP8_YELLOW && pdp8_colour(PDP8_L_IF + 2) == PDP8_YELLOW);
    CHECK(pdp8_colour(PDP8_L_IR + 7) == PDP8_GREEN);
    CHECK(pdp8_colour(PDP8_L_ST + 5) == PDP8_YELLOW);
    CHECK(pdp8_colour(PDP8_L_RUN) == PDP8_GREEN);
    CHECK(pdp8_colour(PDP8_L_PAUSE) == PDP8_RED);
    CHECK(pdp8_colour(PDP8_L_ION) == PDP8_BLUE);
    CHECK(pdp8_colour_rgb(PDP8_RED) == 0xFF4A3D && pdp8_colour_rgb(PDP8_YELLOW) == 0xFFC533);
    CHECK(pdp8_colour_rgb(PDP8_GREEN) == 0x35D07F && pdp8_colour_rgb(PDP8_BLUE) == 0x4DA3FF);
}

/* ---- Layout, against the mock-up's layout() ---- */

static void test_layout(void) {
    static pdp8_layout_t L;
    for (size_t k = 0; k < sizeof(EXP_LAYOUTS) / sizeof(EXP_LAYOUTS[0]); k++) {
        const exp_layout_t *e = &EXP_LAYOUTS[k];
        pdp8_layout(e->free_w, &L);
        CHECK((int)L.mode == e->mode);
        if (e->mode == PDP8_MODE_NONE) {
            CHECK(L.nlamps == 0 && L.nlabels == 0);
            continue;
        }
        CHECK(L.lamp_w == e->lamp_w);
        CHECK(fabs(L.pitch - e->pitch) < 1e-6);
        CHECK(L.nlamps == e->nlamps && L.nlabels == e->nlabels);
        int lamps_ok = 0, labels_ok = 0;
        for (int i = 0; i < e->nlamps; i++) {
            const pdp8_lamp_t *l = &L.lamps[i];
            const exp_lamp_t *x = &e->lamps[i];
            lamps_ok += l->id == x->id && l->x == x->x && l->y == x->y && l->w == x->w &&
                        l->h == x->h;
        }
        for (int i = 0; i < e->nlabels; i++) {
            const pdp8_label_t *b = &L.labels[i];
            const exp_label_t *x = &e->labels[i];
            labels_ok += strcmp(b->text, x->t) == 0 && b->x == x->x && b->base == x->base &&
                         b->font == x->font;
        }
        if (lamps_ok != e->nlamps || labels_ok != e->nlabels)
            fprintf(stderr, "  layout at %d cards: %d/%d lamps, %d/%d labels\n", e->ncards,
                    lamps_ok, e->nlamps, labels_ok, e->nlabels);
        CHECK(lamps_ok == e->nlamps && labels_ok == e->nlabels);
    }

    /* The modes' lamp counts, as Ken approved them: 89, 72, 61. */
    pdp8_layout(1968, &L);
    CHECK(L.mode == PDP8_MODE_FULL && L.nlamps == 89);
    pdp8_layout(828, &L);
    CHECK(L.mode == PDP8_MODE_MEDIUM && L.nlamps == 72);
    pdp8_layout(372, &L);
    CHECK(L.mode == PDP8_MODE_COMPACT && L.nlamps == 61);

    /* Every lamp id at most once, every lamp inside the card, at every width. */
    int dup = 0, outside = 0, prev = PDP8_MODE_NONE, nonmonotone = 0;
    for (int w = 0; w <= 3792; w++) {
        pdp8_layout(w, &L);
        if ((int)L.mode < prev)
            nonmonotone++;
        prev = (int)L.mode;
        int seen[PDP8_LAMPS] = {0};
        for (int i = 0; i < L.nlamps; i++) {
            const pdp8_lamp_t *l = &L.lamps[i];
            dup += seen[l->id]++ > 0;
            outside += l->x < 0 || l->x + l->w > w || l->y < 0 || l->y + l->h > PDP8_CARD_H;
        }
        for (int i = 0; i < L.nlabels; i++)
            outside += L.labels[i].x < 0 || L.labels[i].base > PDP8_CARD_H;
    }
    CHECK(dup == 0 && outside == 0 && nonmonotone == 0);

    /* The thresholds at their edges. */
    pdp8_layout(239, &L);
    CHECK(L.mode == PDP8_MODE_NONE);
    /* compact needs (w - 44 - 56) / 15 >= 14: w >= 310. */
    pdp8_layout(309, &L);
    CHECK(L.mode == PDP8_MODE_NONE);
    pdp8_layout(310, &L);
    CHECK(L.mode == PDP8_MODE_COMPACT);
    /* medium needs (w - 100) / 20.1 >= 20: w >= 502. */
    pdp8_layout(501, &L);
    CHECK(L.mode == PDP8_MODE_COMPACT);
    pdp8_layout(502, &L);
    CHECK(L.mode == PDP8_MODE_MEDIUM);
    /* full needs (w - 486) / 20.1 >= 22: w >= 928.2. */
    pdp8_layout(928, &L);
    CHECK(L.mode == PDP8_MODE_MEDIUM);
    pdp8_layout(929, &L);
    CHECK(L.mode == PDP8_MODE_FULL);
}

int main(void) {
    test_memory_reference();
    test_defer_and_autoindex();
    test_group1();
    test_group2();
    test_group3();
    test_cdf();
    test_reset();
    test_programs_match_mockup();
    test_walking_bit();
    test_fractions();
    test_step_rate();
    test_colours();
    test_layout();
    printf("test_pdp8: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
