#ifndef PDP8_H
#define PDP8_H

/**
 * pdp8.h — the PDP-8/I lamp panel's model (WI #4783, sprint 045).
 *
 * The second machine for the footer's lamp panel, after the Altair
 * (src/altair.h): the front-panel lamps of a PDP-8/I in four LED colours,
 * driven by a small PDP-8 emulator. Each lamp's brightness is the share of
 * machine cycles its bit was set since the last redraw.
 *
 * The specification is the mock-up Ken approved,
 * sprints/planning/pdp8-mockups.html: its script holds the emulator, the two
 * programs, the lamp sampling, the layout arithmetic and the colour table,
 * and this is a port of it. Where the port has to say something the script
 * does not, the comment at that spot says so.
 *
 * The PDP-8 here is the mock-up's: the eight basic instructions with FETCH,
 * DEFER and EXECUTE major states, operate groups 1 and 3, the group 2 skips
 * and CLA, auto-index, and CDF. One 4K field of memory; the field registers
 * are lamps, not addressing. No third-party code and no DEC software: the
 * two programs were written for the mock-up.
 *
 * Pure — no LVGL, no clock, no allocation — so all of it is reachable from
 * the gate. The widget is src/widgets/lamp_panel.c, through src/lampmach.h.
 */

#include <stdbool.h>
#include <stdint.h>

/* ---- Tuning: Ken's settings (WI #4783), all in one place ---- */
#define PDP8_CYCLES_PER_S 666667.0 /* real speed: 1.5 us a machine cycle */
#define PDP8_SPEED 0.037           /* of real: about 2 walking-bit steps a second */
#define PDP8_REDRAW_MS 500         /* 2 redraws a second */
#define PDP8_LEVELS 16             /* brightness levels, off included */
#define PDP8_MAX_BRIGHTNESS 100    /* percent */
#define PDP8_RED_RGB 0xFF4A3D      /* the LED palette */
#define PDP8_YELLOW_RGB 0xFFC533
#define PDP8_GREEN_RGB 0x35D07F
#define PDP8_BLUE_RGB 0x4DA3FF
#define PDP8_PANEL_RGB 0x1E1E2E    /* the ordinary Mocha card (Base) */
#define PDP8_LABEL_RGB 0xA6ADC8    /* Subtext0 */
#define PDP8_NAME_RGB 0x6C7086     /* Overlay0 */

/* ---- Lamps: 89, by register; bit i of a register is lamp BASE + i ---- */
#define PDP8_L_PC 0
#define PDP8_L_MA 12
#define PDP8_L_MB 24
#define PDP8_L_AC 36
#define PDP8_L_MQ 48
#define PDP8_L_LINK 60
#define PDP8_L_DF 61    /* data field, 3 */
#define PDP8_L_IF 64    /* instruction field, 3 */
#define PDP8_L_SC 67    /* step counter, 5 */
#define PDP8_L_IR 72    /* instruction lamps AND .. OPR, 8 */
#define PDP8_L_ST 80    /* major states FETCH, EXEC, DEFER, WC, CA, BREAK */
#define PDP8_L_RUN 86
#define PDP8_L_PAUSE 87
#define PDP8_L_ION 88
#define PDP8_LAMPS 89

/* Major states, as the mock-up numbers them (and as the state lamps sit). */
enum { PDP8_FETCH = 0, PDP8_EXEC = 1, PDP8_DEFER = 2 };

/* The lamp colours, as palette indices: the mock-up's "by register" scheme. */
enum { PDP8_RED = 0, PDP8_YELLOW, PDP8_GREEN, PDP8_BLUE, PDP8_NCOLOURS };

/* ---- Programs ---- */
typedef enum {
    PDP8_PROG_WALK,  /* a bit walking through link and AC, MQ counting the steps */
    PDP8_PROG_COUNT, /* a counter in AC, its complement in MQ */
} pdp8_prog_t;
#define PDP8_PROG PDP8_PROG_WALK /* the one the panel runs */

/* ---- The machine ---- */
typedef struct {
    uint16_t mem[4096];
    uint16_t pc, ma, mb, ac, mq, ea;
    uint8_t l, df, ifl, sc, ir, state;

    double carry;               /* pdp8_run_ms's overshoot, in cycles */
    uint32_t on[PDP8_LAMPS];    /* cycles each lamp was lit since the last read */
    uint32_t total;             /* cycles since the last pdp8_fractions */
} pdp8_t;

/* Load `prog` at 0200 and reset the registers and the accumulators. The
 * machine starts with IF = 1, and both programs set DF = 2 with a CDF, so the
 * field lamps show something; that is the mock-up's, and deliberate. */
void pdp8_reset(pdp8_t *m, pdp8_prog_t prog);

/* One machine cycle (FETCH, DEFER or EXECUTE), then sample the lamps. */
void pdp8_cycle(pdp8_t *m);

/* Run `ms` milliseconds at PDP8_CYCLES_PER_S × PDP8_SPEED. The fractional
 * cycle owed is carried to the next run, so many runs keep the clock. */
void pdp8_run_ms(pdp8_t *m, uint32_t ms);

/* Each lamp's on-fraction since the last call, then reset the accumulators.
 * All zero when nothing ran. */
void pdp8_fractions(pdp8_t *m, float out[PDP8_LAMPS]);

/* A lamp's colour (PDP8_RED .. PDP8_BLUE), and a colour as 0xRRGGBB. */
int pdp8_colour(int lamp);
uint32_t pdp8_colour_rgb(int colour);

/* ---- Layout: free width in, lamps out ---- */
typedef enum {
    PDP8_MODE_NONE,    /* too narrow: nothing is drawn */
    PDP8_MODE_COMPACT, /* 61 lamps: the five registers and the link */
    PDP8_MODE_MEDIUM,  /* 72: register rows, fields and step counter, no right-hand block */
    PDP8_MODE_FULL,    /* all 89 */
} pdp8_mode_t;

#define PDP8_PAD 22          /* the card's inner side padding */
#define PDP8_MIN_W 240       /* below this free width, nothing */
#define PDP8_LBL 56          /* the row-label column */
#define PDP8_RP 356          /* the right-hand block */
#define PDP8_SEP 30          /* between the registers and the right-hand block */
#define PDP8_FULL_PITCH 22   /* full needs this pitch beside the right-hand block */
#define PDP8_MEDIUM_PITCH 20
#define PDP8_MIN_PITCH 14    /* compact needs this */
#define PDP8_MAX_PITCH 72
#define PDP8_NAME_SPAN 130   /* "PDP-8/I" from 6 pitches of this */

/* Vertical, in the card's frame (the status cards' 240 px height). */
#define PDP8_CARD_H 240
#define PDP8_ROW0 26
#define PDP8_ROW_PITCH 40
#define PDP8_LAMP_H 28
#define PDP8_LAMP_R 4

/* Label fonts, as the mock-up names them; the widget maps them. */
enum { PDP8_F_ROW, PDP8_F_NAME, PDP8_F_SIDE };

typedef struct {
    int id;          /* PDP8_L_* + bit */
    int x, y, w, h;  /* in the card's frame */
} pdp8_lamp_t;

typedef struct {
    const char *text;
    int x, base;     /* left edge and baseline, in the card's frame */
    int font;        /* PDP8_F_* */
    uint32_t rgb;
} pdp8_label_t;

#define PDP8_MAX_LABELS 24

typedef struct {
    pdp8_mode_t mode;
    int w;           /* the card's width: the free width */
    double pitch;
    int lamp_w;
    int nlamps;
    pdp8_lamp_t lamps[PDP8_LAMPS];
    int nlabels;
    pdp8_label_t labels[PDP8_MAX_LABELS];
} pdp8_layout_t;

void pdp8_layout(int free_w, pdp8_layout_t *out);

#endif /* PDP8_H */
