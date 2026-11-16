#ifndef LAMPMACH_H
#define LAMPMACH_H

/**
 * lampmach.h — the lamp panel's machines, and the rotation between them
 * (WI #4783, sprint 045).
 *
 * Sprint 044 built the footer's lamp panel for one machine, the Altair
 * (src/altair.h), and left it open for more. With the PDP-8/I
 * (src/pdp8.h) there are two, and the shape a machine needs is known:
 *
 *   - a layout: free width in; lamps out, each with a position, a size and
 *     a palette index, plus labels; or "nothing" when it does not fit;
 *   - a step-and-sample function: run some milliseconds, return each lamp's
 *     on-fraction since the last call;
 *   - a reset, because a machine starts again each time it comes on screen;
 *   - its lamp colours, each mixed into LM_LEVELS opaque fills once.
 *
 * Kept that small on purpose. The widget (src/widgets/lamp_panel.c) knows
 * nothing about either machine; everything machine-shaped is here or behind
 * these pointers.
 *
 * The rotation shows one machine at a time, each for a period Ken sets
 * through Redis (KPIDASH_KEY_CMD_LAMP_PERIOD, parsed in redis.c).
 *
 * Pure — no LVGL, no clock — so all of it is reachable from the gate.
 */

#include <stdbool.h>
#include <stdint.h>

#define LM_LEVELS 16        /* brightness levels, off included: both machines' */
#define LM_OFF_MIX 10       /* off lamp: the card mixed this percent toward the lamp */
#define LM_MAX_COLOURS 4
#define LM_MAX_LAMPS 89     /* the PDP-8 */
#define LM_MAX_LABELS 40    /* the Altair per lamp: 36 and its name */
#define LM_REDRAW_MS 500    /* both machines redraw twice a second */
#define LM_CATCHUP_MS 600   /* a stalled loop owes the emulator at most this */

/* Label fonts; the widget maps them to the repo's generated fonts. */
enum { LM_FONT_20, LM_FONT_24 };

typedef struct {
    int16_t x, y, w, h;  /* in the card's frame */
    uint8_t id;          /* index into the machine's fractions */
    uint8_t pal;         /* index into the machine's colours */
} lm_lamp_t;

typedef struct {
    const char *text;    /* static storage */
    int16_t x, base;     /* left edge (or centre) and baseline */
    uint8_t font;        /* LM_FONT_* */
    bool centre;         /* x is the centre, not the left edge */
    uint32_t rgb;
} lm_label_t;

typedef struct {
    bool drawable;       /* false: too narrow, nothing is drawn */
    int radius;          /* every lamp's corner radius */
    int nlamps;
    lm_lamp_t lamps[LM_MAX_LAMPS];
    int nlabels;
    lm_label_t labels[LM_MAX_LABELS];
} lm_layout_t;

typedef struct {
    const char *name;
    int nlamps;                          /* the fractions' length */
    int ncolours;
    uint32_t colours[LM_MAX_COLOURS];    /* full-brightness lamp colours */
    int max_brightness;                  /* percent */
    uint32_t card_rgb;
    void (*reset)(void);
    void (*advance)(uint32_t ms, float *frac); /* run, then sample and reset */
    void (*layout)(int free_w, lm_layout_t *out);
} lm_machine_t;

enum { LM_ALTAIR, LM_PDP8, LM_NMACHINES };
extern const lm_machine_t *const LM_MACHINES[LM_NMACHINES];

/* `colour`'s LM_LEVELS fills as 0xRRGGBB, dark to bright, mixed as both
 * mock-ups do: off is the card LM_OFF_MIX percent toward the lamp, and the
 * levels run from off to off mixed `max_brightness` percent to the lamp. */
void lm_palette(uint32_t card, uint32_t colour, int max_brightness, uint32_t out[LM_LEVELS]);

/* A fraction as a level, 0 .. LM_LEVELS - 1. */
int lm_level(float frac);

/* ---- Rotation ---- */

#define LM_PERIOD_DEFAULT_S 900 /* each machine 15 minutes */
#define LM_PERIOD_MIN_S 10      /* a typo cannot make it strobe */
#define LM_PERIOD_MAX_S 86400   /* a day; also keeps period × 1000 inside 32 bits */

typedef struct {
    int cur;        /* the machine on screen, -1 for none */
    uint32_t since; /* ms timestamp it came on */
} lm_rotation_t;

void lm_rotation_init(lm_rotation_t *r);

/* Decide which machine is on screen at `now` (ms, any epoch; wraps safely).
 * `drawable[i]` says whether machine i fits the current width. The machine
 * on screen stays until it has had `period_s`, then the next one that fits
 * takes over; one that stops fitting is replaced at once. The period is read
 * on every call, so a change applies at the next call. Returns true when the
 * machine on screen changed (to another, to none, or from none): the caller
 * resets the new machine and redraws. */
bool lm_rotation_tick(lm_rotation_t *r, uint32_t now, int period_s,
                      const bool drawable[LM_NMACHINES]);

#endif /* LAMPMACH_H */
