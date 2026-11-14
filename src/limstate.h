#ifndef LIMSTATE_H
#define LIMSTATE_H

/**
 * limstate.h — the Claude limits card's model (WI #4609, sprint 037).
 *
 * Reads claude:limits, the shared HASH several hosts write (kdashdata registry,
 * schema claude-limits), through libkdash's own parser and staleness rules
 * (kdash_parse_claude_limits, kdash_claude_limits_stale,
 * kdash_claude_limits_scoped_stale), and derives everything the card draws:
 * the colour band of each gauge, the pace tick, the reset and age strings.
 * Pure — no LVGL, no Redis, the clock passed in by the caller — so all of it
 * is reachable from the gate. The one environmental input is the process
 * timezone (localtime_r): "resets Mon 09:00" is the DASHBOARD's local time.
 *
 * Anti-flicker (sprint 035's rule): nothing here goes away on a brief gap. An
 * absent key or a rejected record keeps the last record, ageing by its own
 * stamps; a record that arrives without a scoped set keeps the last scoped set,
 * which then greys by ITS stamp rather than vanishing and reflowing the card.
 * No record ever seen: the card is not drawn.
 *
 * Layout mode (WI #4631, sprint 039): a record that says there is no five-hour
 * window (CD-29's five_hour_none) draws the 7-day and scoped windows as the two
 * rings, or the 7-day ring alone with no scoped set. The mode shown follows the
 * record but is debounced the way sprint 035 debounced NO PATH: it changes only
 * after LIM_MODE_STREAK consecutive new, fresh records give the same different
 * answer, so two hosts publishing different shapes cannot flap the card. A
 * stale record, an absent key and a rejected record never move it.
 *
 * Touched only from the LVGL thread, so there is no lock.
 */

#include <stdbool.h>
#include <stddef.h>

#include <kdash/kdash_payload.h>

#define LIM_FIVE_HOUR_S (5.0 * 3600)
#define LIM_SEVEN_DAY_S (7.0 * 86400)
#define LIM_WARN_PCT 75 /* Ken's ruling, 2026-11-13: yellow at 75 and above… */
#define LIM_CRIT_PCT 90 /* …red at 90 and above, peach below */
#define LIM_HOST_MAX 64 /* the schema's host pattern is 1..63 characters */

typedef enum {
    LIM_BAND_OK = 0, /* peach */
    LIM_BAND_WARN,   /* yellow */
    LIM_BAND_CRIT,   /* red */
} lim_band_t;

typedef enum {
    LIM_MODE_FIVE = 0, /* 5 HOUR and 7 DAY rings, the scoped bar (sprint 037) */
    LIM_MODE_SCOPED,   /* no five-hour window: 7 DAY and the scoped window as rings */
    LIM_MODE_SEVEN,    /* no five-hour window and no scoped set: one 7 DAY ring */
} lim_mode_t;

#define LIM_MODE_STREAK 3 /* sprint 035's NET_WAN_MISS_STREAK, the same rule */

typedef struct {
    kdash_claude_limits_t l;
    char host[LIM_HOST_MAX]; /* "" when absent or off-pattern; display only */
} lim_record_t;

/* Parse one HGETALL field/value list. False when libkdash rejects it (either
 * required percentage missing or out of range); `out` is zeroed first either
 * way, so two parses of the same list compare equal with memcmp. */
bool lim_parse(const char *const *fields, const char *const *values, int n, lim_record_t *out);

/* Feed one poll read at `now`: n <= 0 (or NULL lists) means the key is
 * absent. Returns 1 when the record changed, 0 when nothing changed (the same
 * record again, or absent), -1 when this record was rejected (logged by the
 * caller; the previous record stays). `now` is for the mode debounce only: a
 * record already stale when read does not count toward a layout change. */
int lim_observe(const char *const *fields, const char *const *values, int n, double now);
bool lim_has_record(void);
const lim_record_t *lim_record(void);
void lim_reset(void); /* test hook */

/* The layout one record calls for, on its own. Only a valid record that says
 * five_hour_none is "no window"; a record without the flag (an older writer)
 * has one. Run after the scoped carry, so a carried set counts as present. */
lim_mode_t lim_mode_of(const lim_record_t *r);
/* The layout drawn: the first record's answer, then debounced. FIVE before
 * any record. */
lim_mode_t lim_mode(void);

/* Per-gauge staleness, libkdash's rule at libkdash's default grace and legacy
 * window. The two rings share the headline stamp; the bar has its own. */
bool lim_headline_stale(const lim_record_t *r, double now);
bool lim_scoped_stale(const lim_record_t *r, double now);

/* ---- Derivations ---- */

/* The percentage as drawn: rounded half up, clamped to 0..100. */
int lim_pct(double pct);
/* Colour band of a drawn percentage (so 74.6, drawn "75", is yellow). */
lim_band_t lim_band(int pct);
/* Fraction of the window already elapsed at `now`, 0..1, for the pace tick.
 * -1 = no tick: the reset time is unknown (0), or further away than the window
 * is long, which no consistent record can say. A reset already passed (the
 * record not yet refreshed) is 1. */
double lim_pace(double resets_at, double window_s, double now);

/* ---- Formatting ---- */

/* "resets in 2h 14m" / "resets in 9m" under a day away; "resets Mon 09:00"
 * (local) beyond; "resets now" once passed; "reset unknown" for 0. Minutes
 * round up, so the text never says "0m" while there is time left. */
void lim_fmt_reset(double resets_at, double now, char *buf, size_t n);
/* A duration, coarsest unit only: "40s", "22m", "3h", "2d". Negative is 0s. */
void lim_fmt_dur(double secs, char *buf, size_t n);
/* The header's right-hand text. Fresh: "kai · 40s ago" ("40s ago" with no
 * host). Headline stale: "no data 22m". */
void lim_fmt_header(const lim_record_t *r, double now, bool stale, char *buf, size_t n);
/* The bar's label: "Fable, 7 day". */
void lim_fmt_scoped_label(const lim_record_t *r, char *buf, size_t n);
/* The scoped window's RING label, in the rings' capitals: "FABLE · 7 DAY", or
 * with `window` false the model alone, "FABLE". ASCII is upper-cased; other
 * bytes pass through. The card picks the longest that fits under the ring. */
void lim_fmt_scoped_ring_label(const lim_record_t *r, bool window, char *buf, size_t n);

#endif /* LIMSTATE_H */
