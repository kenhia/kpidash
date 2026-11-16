/* pdp8.c — the PDP-8/I lamp panel's model (WI #4783, sprint 045).
 * A port of sprints/planning/pdp8-mockups.html's script; see pdp8.h. */
#include "pdp8.h"

#include <math.h>
#include <string.h>

/* ---- Programs (the mock-up's PROGS), loaded at 0200 ----
 *
 * walk:   0200 CLA CLL      0205 JMP .-1      0212 MQL
 *         0201 CDF 20       0206 ISZ CNT      0213 TAD SAV
 *         0202 IAC          0207 NOP          0214 JMP LOOP
 *   LOOP: 0203 RAL          0210 DCA SAV
 *         0204 ISZ DLY      0211 TAD CNT      DLY 0220, CNT 0221, SAV 0222
 *
 * The bit rotates once through the 13 bits of link and AC per pass, the
 * delay is ISZ DLY's 4096 passes, and MQ shows CNT, the passes so far.
 *
 * count:  0200 CLA CLL      0204 ISZ CNT      0210 CMA
 *         0201 CDF 20       0205 NOP          0211 MQL
 *   LOOP: 0202 ISZ DLY      0206 CLA          0212 TAD CNT
 *         0203 JMP .-1      0207 TAD CNT      0213 JMP LOOP
 */
static const uint16_t WALK[] = {07300, 06221, 07001, 07004, 02220, 05204, 02221,
                                07000, 03222, 01221, 07421, 01222, 05203};
static const uint16_t COUNT[] = {07300, 06221, 02220, 05202, 02221, 07000,
                                 07200, 01221, 07040, 07421, 01221, 05202};
#define ORIGIN 0200

void pdp8_reset(pdp8_t *m, pdp8_prog_t prog) {
    memset(m, 0, sizeof(*m));
    if (prog == PDP8_PROG_COUNT)
        memcpy(m->mem + ORIGIN, COUNT, sizeof(COUNT));
    else
        memcpy(m->mem + ORIGIN, WALK, sizeof(WALK));
    m->pc = ORIGIN;
    m->ifl = 1;
}

/* One machine cycle's worth of lamp time. The step counter, PAUSE and ION
 * are never lit: nothing here uses them, as in the mock-up. */
static void sample(pdp8_t *m) {
    for (int i = 0; i < 12; i++) {
        m->on[PDP8_L_PC + i] += m->pc >> i & 1;
        m->on[PDP8_L_MA + i] += m->ma >> i & 1;
        m->on[PDP8_L_MB + i] += m->mb >> i & 1;
        m->on[PDP8_L_AC + i] += m->ac >> i & 1;
        m->on[PDP8_L_MQ + i] += m->mq >> i & 1;
    }
    for (int i = 0; i < 3; i++) {
        m->on[PDP8_L_DF + i] += m->df >> i & 1;
        m->on[PDP8_L_IF + i] += m->ifl >> i & 1;
    }
    m->on[PDP8_L_LINK] += m->l;
    m->on[PDP8_L_IR + m->ir]++;
    m->on[PDP8_L_ST + m->state]++;
    m->on[PDP8_L_RUN]++;
    m->total++;
}

/* Rotate the 13 bits of link and AC one place. */
static void rot(pdp8_t *m, bool left) {
    uint32_t v = (uint32_t)m->l << 12 | m->ac;
    uint32_t r = left ? (v << 1 | v >> 12) & 017777 : (v >> 1 | (v & 1) << 12);
    m->l = (uint8_t)(r >> 12);
    m->ac = (uint16_t)(r & 07777);
}

static void operate(pdp8_t *m, uint16_t w) {
    if (!(w & 0400)) { /* group 1 */
        if (w & 0200)
            m->ac = 0;
        if (w & 0100)
            m->l = 0;
        if (w & 0040)
            m->ac ^= 07777;
        if (w & 0020)
            m->l ^= 1;
        if (w & 0001) {
            m->ac++;
            if (m->ac > 07777) {
                m->ac = 0;
                m->l ^= 1;
            }
        }
        int twice = w & 0002 ? 2 : 1;
        for (int i = 0; i < twice; i++) {
            if (w & 0010)
                rot(m, false);
            if (w & 0004)
                rot(m, true);
        }
    } else if (!(w & 0001)) { /* group 2: the skips and CLA */
        bool skip = ((w & 0100) && (m->ac & 04000)) || ((w & 0040) && m->ac == 0) ||
                    ((w & 0020) && m->l);
        if (w & 0010)
            skip = !skip;
        if (skip)
            m->pc = (m->pc + 1) & 07777;
        if (w & 0200)
            m->ac = 0;
    } else { /* group 3: MQ */
        if (w & 0200)
            m->ac = 0;
        uint16_t mq = m->mq;
        if (w & 0020) {
            m->mq = m->ac;
            m->ac = 0;
        }
        if (w & 0100)
            m->ac |= mq;
    }
}

void pdp8_cycle(pdp8_t *m) {
    if (m->state == PDP8_FETCH) {
        m->ma = m->pc;
        m->mb = m->mem[m->ma];
        m->ir = (uint8_t)(m->mb >> 9);
        m->pc = (m->pc + 1) & 07777;
        if (m->ir == 7) {
            operate(m, m->mb);
        } else if (m->ir == 6) {
            if ((m->mb & 07707) == 06201) /* CDF */
                m->df = m->mb >> 3 & 7;
        } else {
            m->ea = (uint16_t)((m->mb & 0200 ? m->ma & 07600 : 0) | (m->mb & 0177));
            if (m->mb & 0400)
                m->state = PDP8_DEFER;
            else if (m->ir == 5)
                m->pc = m->ea;
            else
                m->state = PDP8_EXEC;
        }
        /* Sampled after the decode, so FETCH's own cycle lights the state
         * it leads to, as the mock-up's does. */
        sample(m);
        return;
    }
    if (m->state == PDP8_DEFER) {
        m->ma = m->ea;
        m->mb = m->mem[m->ma];
        if ((m->ma & 07770) == 0010) { /* auto-index, 0010-0017 */
            m->mb = (m->mb + 1) & 07777;
            m->mem[m->ma] = m->mb;
        }
        m->ea = m->mb;
        if (m->ir == 5) {
            m->pc = m->ea;
            m->state = PDP8_FETCH;
        } else {
            m->state = PDP8_EXEC;
        }
        sample(m);
        return;
    }
    m->ma = m->ea; /* EXECUTE */
    switch (m->ir) {
    case 0: /* AND */
        m->mb = m->mem[m->ma];
        m->ac &= m->mb;
        break;
    case 1: { /* TAD */
        m->mb = m->mem[m->ma];
        uint32_t s = (uint32_t)m->ac + m->mb;
        if (s > 07777)
            m->l ^= 1;
        m->ac = (uint16_t)(s & 07777);
        break;
    }
    case 2: /* ISZ */
        m->mb = (m->mem[m->ma] + 1) & 07777;
        m->mem[m->ma] = m->mb;
        if (!m->mb)
            m->pc = (m->pc + 1) & 07777;
        break;
    case 3: /* DCA */
        m->mb = m->ac;
        m->mem[m->ma] = m->ac;
        m->ac = 0;
        break;
    case 4: /* JMS */
        m->mb = m->pc;
        m->mem[m->ma] = m->pc;
        m->pc = (m->ma + 1) & 07777;
        break;
    }
    sample(m);
    m->state = PDP8_FETCH;
}

void pdp8_run_ms(pdp8_t *m, uint32_t ms) {
    /* The mock-up's budget, multiplied in its order. */
    double budget = (double)ms / 1000.0 * PDP8_CYCLES_PER_S * PDP8_SPEED + m->carry;
    uint32_t ran = 0;
    while (ran < budget) {
        pdp8_cycle(m);
        ran++;
    }
    m->carry = budget - ran;
}

void pdp8_fractions(pdp8_t *m, float out[PDP8_LAMPS]) {
    for (int i = 0; i < PDP8_LAMPS; i++)
        out[i] = m->total ? (float)((double)m->on[i] / (double)m->total) : 0.0f;
    memset(m->on, 0, sizeof(m->on));
    m->total = 0;
}

int pdp8_colour(int lamp) {
    if (lamp < PDP8_L_MA)
        return PDP8_YELLOW; /* PC */
    if (lamp < PDP8_L_AC)
        return PDP8_GREEN; /* MA, MB */
    if (lamp < PDP8_L_MQ)
        return PDP8_RED; /* AC */
    if (lamp < PDP8_L_LINK)
        return PDP8_BLUE; /* MQ */
    if (lamp == PDP8_L_LINK)
        return PDP8_RED;
    if (lamp < PDP8_L_SC)
        return PDP8_YELLOW; /* data and instruction fields */
    if (lamp < PDP8_L_IR)
        return PDP8_BLUE; /* step counter */
    if (lamp < PDP8_L_ST)
        return PDP8_GREEN; /* instruction lamps */
    if (lamp < PDP8_L_RUN)
        return PDP8_YELLOW; /* major states */
    if (lamp == PDP8_L_RUN)
        return PDP8_GREEN;
    if (lamp == PDP8_L_PAUSE)
        return PDP8_RED;
    return PDP8_BLUE; /* ION */
}

uint32_t pdp8_colour_rgb(int colour) {
    static const uint32_t RGB[PDP8_NCOLOURS] = {PDP8_RED_RGB, PDP8_YELLOW_RGB, PDP8_GREEN_RGB,
                                                PDP8_BLUE_RGB};
    return colour >= 0 && colour < PDP8_NCOLOURS ? RGB[colour] : PDP8_RED_RGB;
}

/* ---- Layout (the mock-up's layout()) ----
 *
 * Positions are in lamp pitches ("units") from the end of the row-label
 * column. The register rows sit from unit 6.6 (1.5 in compact), grouped in
 * octal; the fields, link and step counter use units 0-5 of the rows they
 * share; the right-hand block (instruction lamps, major states, RUN, PAUSE,
 * ION) sits after SEP, in full mode only. */
static const char *const ROWS[5] = {"PC", "MA", "MB", "AC", "MQ"};
static const int REG[5] = {PDP8_L_PC, PDP8_L_MA, PDP8_L_MB, PDP8_L_AC, PDP8_L_MQ};
static const char *const IR_NAMES[8] = {"AND", "TAD", "ISZ", "DCA", "JMS", "JMP", "IOT", "OPR"};
static const char *const ST_NAMES[6] = {"FETCH", "EXEC", "DEFER", "WC", "CA", "BREAK"};
static const char *const RUN_NAMES[3] = {"RUN", "PAUSE", "ION"};
static const int RUN_IDS[3] = {PDP8_L_RUN, PDP8_L_PAUSE, PDP8_L_ION};

static int jround(double v) { /* JavaScript's Math.round */
    return (int)floor(v + 0.5);
}

typedef struct {
    double left, p;
    int lw;
    pdp8_layout_t *out;
} lx_t;

static int ux(const lx_t *g, double u) {
    return jround(g->left + PDP8_LBL + u * g->p + (g->p - g->lw) / 2);
}

static void add(lx_t *g, int id, int x, int y, int w, int h) {
    g->out->lamps[g->out->nlamps++] = (pdp8_lamp_t){id, x, y, w, h};
}

static void text(lx_t *g, const char *t, int x, int base, int font, uint32_t rgb) {
    g->out->labels[g->out->nlabels++] = (pdp8_label_t){t, x, base, font, rgb};
}

void pdp8_layout(int free_w, pdp8_layout_t *out) {
    memset(out, 0, sizeof(*out));
    out->w = free_w;
    double inner = free_w - PDP8_PAD * 2, units, p;
    if (free_w < PDP8_MIN_W)
        return;
    if ((p = (inner - PDP8_LBL - PDP8_SEP - PDP8_RP) / 20.1) >= PDP8_FULL_PITCH) {
        out->mode = PDP8_MODE_FULL;
        units = 20.1;
    } else if ((p = (inner - PDP8_LBL) / 20.1) >= PDP8_MEDIUM_PITCH) {
        out->mode = PDP8_MODE_MEDIUM;
        units = 20.1;
    } else if ((p = (inner - PDP8_LBL) / 15) >= PDP8_MIN_PITCH) {
        out->mode = PDP8_MODE_COMPACT;
        units = 15;
    } else {
        return;
    }
    if (p > PDP8_MAX_PITCH)
        p = PDP8_MAX_PITCH;
    bool full = out->mode == PDP8_MODE_FULL, compact = out->mode == PDP8_MODE_COMPACT;
    double content = PDP8_LBL + units * p + (full ? PDP8_SEP + PDP8_RP : 0);
    lx_t g = {.p = p, .lw = jround(p * 0.66), .out = out};
    g.left = PDP8_PAD + (inner - content) / 2;
    out->pitch = p;
    out->lamp_w = g.lw;

    for (int r = 0; r < 5; r++) {
        int y = PDP8_ROW0 + r * PDP8_ROW_PITCH;
        for (int i = 0; i < 12; i++) {
            double u = (compact ? 1.5 : 6.6) + i + 0.5 * (i / 3);
            add(&g, REG[r] + 11 - i, ux(&g, u), y, g.lw, PDP8_LAMP_H);
        }
        text(&g, ROWS[r], jround(g.left), y + 22, PDP8_F_ROW, PDP8_LABEL_RGB);
    }
    if (compact) {
        add(&g, PDP8_L_LINK, ux(&g, 0), PDP8_ROW0 + 3 * PDP8_ROW_PITCH, g.lw, PDP8_LAMP_H);
    } else {
        for (int i = 0; i < 3; i++) {
            add(&g, PDP8_L_DF + 2 - i, ux(&g, i), PDP8_ROW0, g.lw, PDP8_LAMP_H);
            add(&g, PDP8_L_IF + 2 - i, ux(&g, 3 + i), PDP8_ROW0, g.lw, PDP8_LAMP_H);
        }
        add(&g, PDP8_L_LINK, ux(&g, 5), PDP8_ROW0 + 3 * PDP8_ROW_PITCH, g.lw, PDP8_LAMP_H);
        for (int i = 0; i < 5; i++)
            add(&g, PDP8_L_SC + 4 - i, ux(&g, 1 + i), PDP8_ROW0 + 4 * PDP8_ROW_PITCH, g.lw,
                PDP8_LAMP_H);
        if (p * 6 >= PDP8_NAME_SPAN)
            text(&g, "PDP-8/I", ux(&g, 0), PDP8_ROW0 + PDP8_ROW_PITCH + 23, PDP8_F_NAME,
                 PDP8_NAME_RGB);
    }
    if (full) {
        int rx = jround(g.left + PDP8_LBL + units * p + PDP8_SEP);
        int span = 4 * PDP8_ROW_PITCH + PDP8_LAMP_H;
        for (int k = 0; k < 8; k++) {
            int y = PDP8_ROW0 + jround(k * (span - 16) / 7.0);
            add(&g, PDP8_L_IR + k, rx, y, 30, 16);
            text(&g, IR_NAMES[k], rx + 38, y + 15, PDP8_F_SIDE, PDP8_LABEL_RGB);
        }
        for (int k = 0; k < 6; k++) {
            int y = PDP8_ROW0 + jround(k * (span - 20) / 5.0);
            add(&g, PDP8_L_ST + k, rx + 108, y, 30, 20);
            text(&g, ST_NAMES[k], rx + 146, y + 17, PDP8_F_SIDE, PDP8_LABEL_RGB);
        }
        for (int k = 0; k < 3; k++) {
            int y = PDP8_ROW0 + jround(k * (span - 20) / 5.0);
            add(&g, RUN_IDS[k], rx + 243, y, 30, 20);
            text(&g, RUN_NAMES[k], rx + 281, y + 17, PDP8_F_SIDE, PDP8_LABEL_RGB);
        }
    }
}
