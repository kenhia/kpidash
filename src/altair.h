#ifndef ALTAIR_H
#define ALTAIR_H

/**
 * altair.h — the Altair 8800 lamp panel's model (WI #4777, sprint 044).
 *
 * Decoration, like the weather filler: the front-panel lamps of an Altair
 * 8800 in whatever width the footer's status cards leave, driven by a small
 * 8080 emulator running Kill the Bit with a simulated player. Each lamp's
 * brightness is how long its line was high since the last redraw.
 *
 * The specification is the mock-up Ken approved,
 * sprints/planning/altair-mockups.html: its script holds the emulator, the
 * cycle model, the layout arithmetic and the colours, and this is a port of
 * it. Where the port has to say something the script does not, the comment
 * at that spot says so.
 *
 * The 8080 here is the mock-up's subset: only the opcodes Kill the Bit and
 * the address counter use (LXI H/B, MVI D, LDAX D, DAD B, JNC, JMP, IN,
 * XRA D, RRC, MOV D,A, INX H, MOV A,M). Any other opcode jumps to 0, as the
 * mock-up's does. No third-party code and no ROMs.
 *
 * Structure, kept open for other machines later: a panel is a table of lamp
 * groups (the layout below) plus a machine that fills lamp on-times (the
 * emulator). Only the Altair is built.
 *
 * Pure — no LVGL, no clock, no allocation — so all of it is reachable from
 * the gate. The widget is src/widgets/lamp_panel.c.
 */

#include <stdbool.h>
#include <stdint.h>

/* ---- Tuning: Ken's settings (WI #4777), all in one place ---- */
#define ALT_CLOCK_HZ 450000     /* 0.225 × 2 MHz: about 2 Kill the Bit steps a second */
#define ALT_REDRAW_MS 500       /* 2 redraws a second */
#define ALT_LEVELS 16           /* brightness levels, off included */
#define ALT_MAX_BRIGHTNESS 100  /* percent; Ken expects to lower it on the real panel */
#define ALT_LAMP_RGB 0xFF4A3D   /* LED red */
#define ALT_PANEL_RGB 0x1E1E2E  /* the ordinary Mocha card (Base) */
#define ALT_OFF_MIX 10          /* off lamp: the card mixed this percent toward the lamp */
#define ALT_LABEL_RGB 0xA6ADC8  /* Subtext0 */
#define ALT_NAME_RGB 0x6C7086   /* Overlay0 */

/* ---- Lamps: 0-15 address, 16-23 data, 24-33 status, 34 WAIT, 35 HLDA ---- */
#define ALT_LAMPS 36
#define ALT_L_A(i) (i)
#define ALT_L_D(i) (16 + (i))
enum {
    ALT_L_INTE = 24,
    ALT_L_PROT,
    ALT_L_MEMR,
    ALT_L_INP,
    ALT_L_M1,
    ALT_L_OUT,
    ALT_L_HLTA,
    ALT_L_STACK,
    ALT_L_WO,
    ALT_L_INT,
    ALT_L_WAIT,
    ALT_L_HLDA,
};

/* ---- Programs ---- */
typedef enum {
    ALT_PROG_KTB,   /* Kill the Bit; the sense switches are the player */
    ALT_PROG_COUNT, /* the mock-up's address counter */
} alt_prog_t;

/* ---- The machine ---- */
typedef struct {
    uint8_t mem[65536];
    uint16_t pc;
    uint8_t a, b, c, d, e, h, l, cy;

    /* What IN reads: the front panel's sense switches. With `autoplay` set
     * the simulated player presses them instead (the mock-up's
     * senseSwitches), on a seeded generator so a run is reproducible. */
    uint8_t switches;
    bool autoplay;
    uint32_t rng;
    uint64_t press_at; /* emulated T-state of the player's next press */

    uint64_t t;        /* emulated T-states since alt_reset */
    int64_t carry;     /* alt_run's overshoot, owed back on the next run */
    uint64_t on[ALT_LAMPS];
    uint64_t total;    /* T-states accumulated since the last alt_fractions */
} alt_machine_t;

/* Load `prog` at 0 and reset the CPU and the accumulators. `autoplay` turns
 * the simulated player on; `seed` seeds it (0 is replaced by a fixed seed). */
void alt_reset(alt_machine_t *m, alt_prog_t prog, bool autoplay, uint32_t seed);

/* Execute one instruction; returns its T-states. */
int alt_step(alt_machine_t *m);

/* Run `ms` milliseconds of emulated time at ALT_CLOCK_HZ. The last
 * instruction overshoots; the overshoot is taken off the next run, so many
 * runs keep the clock. */
void alt_run_ms(alt_machine_t *m, uint32_t ms);

/* Each lamp's on-fraction since the last call, then reset the accumulators.
 * All zero when nothing ran. */
void alt_fractions(alt_machine_t *m, float out[ALT_LAMPS]);

/* A fraction as a brightness level, 0 .. ALT_LEVELS - 1. */
int alt_level(float frac);

/* The ALT_LEVELS lamp colours as 0xRRGGBB, dark to bright: computed once and
 * drawn opaque (no opacity styles on the Pi). */
void alt_palette(uint32_t out[ALT_LEVELS]);

/* ---- Layout: free width in, lamps out ---- */
typedef enum {
    ALT_MODE_NONE,    /* too narrow: nothing is drawn */
    ALT_MODE_COMPACT, /* 24 lamps: address and data, status row dropped */
    ALT_MODE_FULL,    /* all 36 */
} alt_mode_t;

#define ALT_PAD 22          /* the card's inner side padding */
#define ALT_MIN_W 240       /* below this free width, nothing */
#define ALT_FULL_PITCH 26   /* full mode needs at least this pitch */
#define ALT_MIN_PITCH 14    /* compact needs at least this */
#define ALT_MAX_PITCH 110
#define ALT_LABEL_PITCH 88  /* per-lamp labels from this pitch */

/* Vertical, in the card's frame (the status cards' 240 px height). */
#define ALT_CARD_H 240
#define ALT_LAMP_H 62
#define ALT_LAMP_R 5
#define ALT_CARD_R 12
#define ALT_LAMP_Y0 46        /* top row: status and data */
#define ALT_LAMP_Y1 154       /* bottom row: WAIT, HLDA and address */
#define ALT_LABEL_BASE0 32    /* label baselines, top and bottom rows */
#define ALT_LABEL_BASE1 140
#define ALT_NAME_BASE 84      /* "ALTAIR 8800" between the rows, full + per-lamp */

typedef struct {
    int id;   /* lamp index (ALT_L_*) */
    int x;    /* left edge, in the card's frame */
    int row;  /* 0 top, 1 bottom */
    const char *name;
} alt_lamp_t;

typedef struct {
    alt_mode_t mode;
    int w;          /* the card's width: the free width */
    double pitch;
    int lamp_w;
    bool per_lamp;  /* per-lamp labels, else per-group */
    int nlamps;
    alt_lamp_t lamps[ALT_LAMPS];

    /* Group labels' left edges (per-group mode) and the name's centre
     * (full, per-lamp). The lamp-unit positions the mock-up labels at. */
    int x_status, x_address, x_data, x_name;
} alt_layout_t;

void alt_layout(int free_w, alt_layout_t *out);

#endif /* ALTAIR_H */
