/* altair.c — the Altair 8800 lamp panel's model (WI #4777, sprint 044).
 * A port of sprints/planning/altair-mockups.html's script; see altair.h. */
#include "altair.h"

#include <math.h>
#include <string.h>

/* ---- Programs (the mock-up's PROGS) ---- */

/* Kill the Bit, 24 bytes. The bit is D, and LDAX D puts D on the high
 * address byte, so the bit walks the A8-A15 lamps.
 *
 *   0000  LXI H,0      ; HL: the delay counter
 *   0003  MVI D,80h    ; the bit
 *   0005  LXI B,0Eh    ; delay increment
 *   0008  LDAX D ×4    ; show D on the address lamps
 *   000C  DAD B        ; HL += 14
 *   000D  JNC 0008     ; until HL carries
 *   0010  IN FFh       ; the sense switches
 *   0012  XRA D        ; a hit clears the bit, a miss adds one
 *   0013  RRC
 *   0014  MOV D,A
 *   0015  JMP 0008
 */
static const uint8_t KTB[] = {0x21, 0x00, 0x00, 0x16, 0x80, 0x01, 0x0e, 0x00,
                              0x1a, 0x1a, 0x1a, 0x1a, 0x09, 0xd2, 0x08, 0x00,
                              0xdb, 0xff, 0xaa, 0x0f, 0x57, 0xc3, 0x08, 0x00};

/* The address counter: LXI H,0; INX H; MOV A,M; JMP 0003. */
static const uint8_t COUNT[] = {0x21, 0x00, 0x00, 0x23, 0x7e, 0xc3, 0x03, 0x00};

/* 2 MHz T-states per emulated second, as the mock-up's player counts them:
 * its press times are in "2e6 ×" units of emulated T-states. */
#define PLAYER_T_PER_S 2000000.0

void alt_reset(alt_machine_t *m, alt_prog_t prog, bool autoplay, uint32_t seed) {
    memset(m, 0, sizeof(*m));
    if (prog == ALT_PROG_COUNT)
        memcpy(m->mem, COUNT, sizeof(COUNT));
    else
        memcpy(m->mem, KTB, sizeof(KTB));
    m->autoplay = autoplay;
    m->rng = seed ? seed : 0x8800u;
    m->press_at = (uint64_t)(PLAYER_T_PER_S * 2);
}

/* xorshift32, as a [0, 1) double: the mock-up's Math.random, seeded. */
static double rnd(alt_machine_t *m) {
    uint32_t x = m->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    m->rng = x;
    return (double)(x >> 8) / 16777216.0;
}

/* One machine cycle: `t` T-states with these lines high. WO is active-low
 * (lit when the CPU is not writing), and nothing here writes, so it is lit
 * on every cycle. */
static void cyc(alt_machine_t *m, uint16_t addr, uint8_t data, bool m1, bool memr, bool inp,
                int t) {
    for (int i = 0; i < 16; i++)
        if (addr >> i & 1)
            m->on[ALT_L_A(i)] += (uint64_t)t;
    for (int i = 0; i < 8; i++)
        if (data >> i & 1)
            m->on[ALT_L_D(i)] += (uint64_t)t;
    if (m1)
        m->on[ALT_L_M1] += (uint64_t)t;
    if (memr)
        m->on[ALT_L_MEMR] += (uint64_t)t;
    if (inp)
        m->on[ALT_L_INP] += (uint64_t)t;
    m->on[ALT_L_WO] += (uint64_t)t;
    m->total += (uint64_t)t;
    m->t += (uint64_t)t;
}

static uint8_t imm(alt_machine_t *m) {
    uint8_t v = m->mem[m->pc];
    cyc(m, m->pc, v, false, true, false, 3);
    m->pc++;
    return v;
}

/* The mock-up's senseSwitches. A hit names a lit bit: the compare comes
 * before the rotate, so the lit position itself is the one to press. With
 * more than three bits lit the player always hits; otherwise half the time.
 * A miss presses any of the eight. */
static uint8_t sense(alt_machine_t *m) {
    if (!m->autoplay)
        return m->switches;
    if (m->t < m->press_at)
        return 0;
    m->press_at = m->t + (uint64_t)(PLAYER_T_PER_S * (1.5 + rnd(m) * 3));
    int lit[8], n = 0;
    for (int i = 0; i < 8; i++)
        if (m->d >> i & 1)
            lit[n++] = i;
    bool hit = n > 3 || (n > 0 && rnd(m) < 0.5);
    int bit = hit ? lit[(int)(rnd(m) * n)] : (int)(rnd(m) * 8);
    return (uint8_t)(1u << bit);
}

int alt_step(alt_machine_t *m) {
    uint64_t t0 = m->t;
    uint8_t op = m->mem[m->pc];
    /* MOV r,r and INX take 5 T-states in M1, everything else here 4. */
    cyc(m, m->pc, op, true, true, false, (op == 0x57 || op == 0x23) ? 5 : 4);
    m->pc++;
    switch (op) {
    case 0x21: /* LXI H */
        m->l = imm(m);
        m->h = imm(m);
        break;
    case 0x01: /* LXI B */
        m->c = imm(m);
        m->b = imm(m);
        break;
    case 0x16: /* MVI D */
        m->d = imm(m);
        break;
    case 0x1a: { /* LDAX D */
        uint16_t ad = (uint16_t)(m->d << 8 | m->e);
        m->a = m->mem[ad];
        cyc(m, ad, m->a, false, true, false, 3);
        break;
    }
    case 0x09: { /* DAD B: internal cycles, address bus holds PC */
        uint32_t s = (uint32_t)(m->h << 8 | m->l) + (uint32_t)(m->b << 8 | m->c);
        m->cy = s > 0xffff;
        m->h = (uint8_t)(s >> 8);
        m->l = (uint8_t)s;
        cyc(m, m->pc, 0, false, false, false, 6);
        break;
    }
    case 0xd2: { /* JNC */
        uint8_t lo = imm(m), hi = imm(m);
        if (!m->cy)
            m->pc = (uint16_t)(hi << 8 | lo);
        break;
    }
    case 0xc3: { /* JMP */
        uint8_t lo = imm(m), hi = imm(m);
        m->pc = (uint16_t)(hi << 8 | lo);
        break;
    }
    case 0xdb: { /* IN: the port number is on both address bytes */
        uint8_t p = imm(m);
        m->a = sense(m);
        cyc(m, (uint16_t)(p << 8 | p), m->a, false, false, true, 3);
        break;
    }
    case 0xaa: /* XRA D */
        m->a ^= m->d;
        m->cy = 0;
        break;
    case 0x0f: /* RRC */
        m->cy = m->a & 1;
        m->a = (uint8_t)(m->a >> 1 | m->cy << 7);
        break;
    case 0x57: /* MOV D,A */
        m->d = m->a;
        break;
    case 0x23: { /* INX H */
        uint16_t v = (uint16_t)((m->h << 8 | m->l) + 1);
        m->h = (uint8_t)(v >> 8);
        m->l = (uint8_t)v;
        break;
    }
    case 0x7e: { /* MOV A,M */
        uint16_t ad = (uint16_t)(m->h << 8 | m->l);
        m->a = m->mem[ad];
        cyc(m, ad, m->a, false, true, false, 3);
        break;
    }
    default: /* outside the subset: as the mock-up does, start again */
        m->pc = 0;
    }
    return (int)(m->t - t0);
}

void alt_run_ms(alt_machine_t *m, uint32_t ms) {
    int64_t budget = (int64_t)ms * ALT_CLOCK_HZ / 1000 + m->carry;
    int64_t ran = 0;
    while (ran < budget)
        ran += alt_step(m);
    m->carry = budget - ran;
}

void alt_fractions(alt_machine_t *m, float out[ALT_LAMPS]) {
    for (int i = 0; i < ALT_LAMPS; i++)
        out[i] = m->total ? (float)((double)m->on[i] / (double)m->total) : 0.0f;
    memset(m->on, 0, sizeof(m->on));
    m->total = 0;
}

int alt_level(float frac) {
    int lv = (int)floor((double)frac * (ALT_LEVELS - 1) + 0.5);
    if (lv < 0)
        return 0;
    return lv > ALT_LEVELS - 1 ? ALT_LEVELS - 1 : lv;
}

/* The mock-up's mix(): per channel, rounded half up. */
static uint32_t mix(uint32_t a, uint32_t b, double t) {
    uint32_t out = 0;
    for (int s = 16; s >= 0; s -= 8) {
        double ca = (double)(a >> s & 0xff), cb = (double)(b >> s & 0xff);
        out |= (uint32_t)floor(ca + (cb - ca) * t + 0.5) << s;
    }
    return out;
}

void alt_palette(uint32_t out[ALT_LEVELS]) {
    uint32_t off = mix(ALT_PANEL_RGB, ALT_LAMP_RGB, ALT_OFF_MIX / 100.0);
    uint32_t top = mix(off, ALT_LAMP_RGB, ALT_MAX_BRIGHTNESS / 100.0);
    for (int i = 0; i < ALT_LEVELS; i++)
        out[i] = mix(off, top, (double)i / (ALT_LEVELS - 1));
}

/* ---- Layout (the mock-up's layout()) ----
 *
 * Positions are in lamp pitches ("units"). The address lamps sit at
 * ADDR_U, grouped in octal (A15; A14-A12; A11-A9; ...), the data lamps
 * under the last eight of them, the status lamps at 0..9 and WAIT, HLDA at
 * 0 and 1 under them. Compact mode drops the status row and shifts
 * everything left three units. */
static const double ADDR_U[16] = {3,  4.5,  5.5,  6.5,  8,    9,    10,   11.5,
                                  12.5, 13.5, 15, 16, 17, 18.5, 19.5, 20.5}; /* A15..A0 */
static const char *const STATUS_NAMES[10] = {"INTE", "PROT", "MEMR",  "INP", "M1",
                                             "OUT",  "HLTA", "STACK", "WO",  "INT"};
static const char *const A_NAMES[16] = {"A0", "A1", "A2",  "A3",  "A4",  "A5",  "A6",  "A7",
                                        "A8", "A9", "A10", "A11", "A12", "A13", "A14", "A15"};
static const char *const D_NAMES[8] = {"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7"};

typedef struct {
    double left, p, shift;
    int lw;
} lx_t;

static int lx(const lx_t *g, double u) {
    return (int)floor(g->left + (u - g->shift) * g->p + (g->p - g->lw) / 2 + 0.5);
}

void alt_layout(int free_w, alt_layout_t *out) {
    memset(out, 0, sizeof(*out));
    out->w = free_w;
    double inner = free_w - ALT_PAD * 2, units = 21.5;
    out->mode = ALT_MODE_FULL;
    if (inner / units < ALT_FULL_PITCH) {
        out->mode = ALT_MODE_COMPACT;
        units = 18.5;
    }
    if (inner / units < ALT_MIN_PITCH || free_w < ALT_MIN_W) {
        out->mode = ALT_MODE_NONE;
        return;
    }
    double p = inner / units < ALT_MAX_PITCH ? inner / units : ALT_MAX_PITCH;
    lx_t g = {.p = p, .lw = (int)floor(p * 0.64 + 0.5)};
    g.left = ALT_PAD + (inner - p * units) / 2;
    g.shift = out->mode == ALT_MODE_FULL ? 0 : 3;
    out->pitch = p;
    out->lamp_w = g.lw;
    out->per_lamp = p >= ALT_LABEL_PITCH;

    int n = 0;
    for (int i = 0; i < 16; i++)
        out->lamps[n++] = (alt_lamp_t){ALT_L_A(15 - i), lx(&g, ADDR_U[i]), 1, A_NAMES[15 - i]};
    for (int i = 0; i < 8; i++)
        out->lamps[n++] = (alt_lamp_t){ALT_L_D(7 - i), lx(&g, ADDR_U[8 + i]), 0, D_NAMES[7 - i]};
    if (out->mode == ALT_MODE_FULL) {
        for (int i = 0; i < 10; i++)
            out->lamps[n++] = (alt_lamp_t){ALT_L_INTE + i, lx(&g, i), 0, STATUS_NAMES[i]};
        out->lamps[n++] = (alt_lamp_t){ALT_L_WAIT, lx(&g, 0), 1, "WAIT"};
        out->lamps[n++] = (alt_lamp_t){ALT_L_HLDA, lx(&g, 1), 1, "HLDA"};
    }
    out->nlamps = n;

    out->x_status = lx(&g, 0);
    out->x_address = lx(&g, 3);
    out->x_data = lx(&g, 12.5);
    out->x_name = (lx(&g, 10) + lx(&g, 12.5) + 1) / 2; /* the midpoint, rounded half up */
}
