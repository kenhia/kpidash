#ifndef KKSTATE_H
#define KKSTATE_H

/**
 * kkstate.h — the korg + klams card's model (WI #4613, sprint 038).
 *
 * Two feeds share one 1×1 cell, and each half lives on its own feed:
 *   kdash:korg:pulse    written every 5 min by korg's bin/korg-pulse on kai
 *   kdash:klams:status  written every 60 s by klams-monitor on kubs0
 * Both are CD-28 status feeds (kdashdata sprint 023): one JSON document under
 * one fixed key, ts-owned, cadence-declared. libkdash parses them
 * (kdash_parse_korg_pulse, kdash_parse_klams_status) and judges their age
 * (kdash_cadence_stale at KDASH_CADENCE_GRACE_S). This module adds only what
 * the card needs on top: the debounce, the scanner verdict, the sparkline's
 * points and the strings.
 *
 * Pure — no LVGL, no Redis, the clock passed in by the caller — so all of it
 * is reachable from the gate. The one environmental input is the process
 * timezone, and only for nothing: every date here is the writer's own.
 *
 * Anti-flicker (sprint 035's rule):
 *   - an absent key or a rejected record keeps the last record, ageing by its
 *     own stamp and going grey when that is stale;
 *   - a subsystem's dot turns red only after the same miss is seen on
 *     consecutive records (distinct `ts`), and klams's own verdict is held to
 *     the same rule: a single `degraded` or `down` record between two `ok`
 *     ones never reaches the panel. An unconfirmed `down` record is not
 *     adopted at all — its values are null, and blanking the half for one
 *     record is exactly the flash the rule forbids;
 *   - no record ever seen: the half draws its title and "no feed", so the
 *     other half does not move.
 *
 * Touched only from the LVGL thread, so there is no lock.
 */

#include <stdbool.h>
#include <stddef.h>

#include <kdash/kdash_payload.h>

#define KK_CONFIRM 2      /* consecutive records before a miss is shown */
#define KK_SPARK_DAYS 30  /* the line's span: today and the 29 days before */
#define KK_SCANNER_ROWS 2 /* rows the card has room for */

/* ---- korg ---- */

/* Feed one GET: NULL means the key is absent. Returns 1 when the record
 * changed, 0 when nothing did, -1 when the record was rejected (the caller
 * logs it; the previous record stays). */
int kk_observe_korg(const char *json);
const kdash_korg_pulse_t *kk_korg(void); /* NULL before the first record */
bool kk_korg_stale(double now);

/* One point of the 30-day line: x and y as fractions of the plot, 0..1, y up. */
typedef struct {
    double x, y;
} kk_point_t;

/* The line's points, by DATE rather than index (missing days are absent on
 * the wire, and a gap must not slide the days after it left): x is the day's
 * place in the 30 days ending at the newest entry, y the backlog over the
 * series' maximum (0 at the floor, as the mock-up's line). Entries older than
 * the span or with an unreadable date are dropped. Returns the count; fewer
 * than 2 draws no line. */
int kk_korg_spark(const kdash_korg_pulse_t *p, kk_point_t *out, int max);

/* ---- klams ---- */

int kk_observe_klams(const char *json);
const kdash_klams_status_t *kk_klams(void); /* NULL before the first record */
bool kk_klams_stale(double now);
/* klams's verdict after the debounce. */
kdash_klams_health_t kk_klams_health(void);
/* A subsystem is shown failing only once its miss is confirmed. */
bool kk_subsystem_down(int i);

typedef enum {
    KK_SCAN_OK = 0,  /* ran on schedule: the bar drains from last_ran_at   */
    KK_SCAN_STALLED, /* last_ran_at past its interval plus grace: red      */
    KK_SCAN_NEUTRAL, /* no last_ran_at (klams WI 4639): a write-age bar,
                        never red, because a quiet scanner writes nothing  */
} kk_scan_state_t;

typedef struct {
    kk_scan_state_t state;
    double frac;   /* bar fill, 0..1: what is left of the interval */
    char text[48]; /* "wrote 3d ago" / "ran 3h ago" / "never wrote" */
} kk_scan_view_t;

/* The comment-4672 rule. Healthy and neutral show the write age; only a stale
 * last_ran_at is an alarm, and its text says when it last ran. */
void kk_scan_view(const kdash_klams_scanner_t *s, double now, kk_scan_view_t *out);

/* ---- formatting ---- */

/* A duration, coarsest unit only: "40s", "22m", "3h", "2d". Negative is 0s. */
void kk_fmt_dur(double secs, char *buf, size_t n);
/* A count with thousands separators: "228,956". */
void kk_fmt_count(long long v, char *buf, size_t n);
/* The stale half's note: "no data 14m". */
void kk_fmt_nodata(double ts, double now, char *buf, size_t n);

void kk_reset(void); /* test hook */

#endif /* KKSTATE_H */
