#ifndef CALSTATE_H
#define CALSTATE_H

/**
 * calstate.h — the time and calendar card's model (WI #4665, sprint 040).
 *
 * Two inputs, and the card draws from the first alone:
 *   the panel's own local clock  the time, the date and the month grid
 *   kdash:calendar               the tagged days: Ken's days off, holidays
 *                                and special days (kdashdata CD-30, sprint
 *                                025), written hourly by korg on kai
 * libkdash parses the record (kdash_parse_calendar) and answers the window
 * (kdash_calendar_covers, kdash_calendar_day). This module adds the month
 * arithmetic, the cell flags, the coming-up list and the strings.
 *
 * Pure — no LVGL, no Redis, the clock passed in by the caller — so all of it
 * is reachable from the gate. The one environmental input is the process
 * timezone: "today" is the LOCAL date, which on rpi53 is America/Los_Angeles,
 * the zone every date in the feed is written in. Never UTC.
 *
 * Anti-flicker (sprint 035's rule, as CD-30 states it for this feed):
 *   - an absent key or a rejected record keeps the last good record; the
 *     tags are held, never blanked;
 *   - an old `ts` is a muted hint and nothing more;
 *   - the only stale condition is a window that no longer covers today, and
 *     then the grid and weekends draw untagged — the card never reflows.
 *
 * One kdash_calendar_t (about 5.8 KB) lives here as a static, with one static
 * scratch for parsing; nothing that size goes on the stack per frame.
 *
 * Touched only from the LVGL thread, so there is no lock.
 */

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <kdash/kdash_payload.h>

#define CAL_UPCOMING_MAX 4 /* rows the left column has room for */

/* One cell's encoding. They stack: the 27th can be OFF and SPECIAL. */
enum {
    CAL_WEEKEND = 1 << 0, /* Saturday or Sunday: grey background         */
    CAL_OFF = 1 << 1,     /* Ken's day off: blue-tinted background       */
    CAL_HOLIDAY = 1 << 2, /* rose number                                 */
    CAL_SPECIAL = 1 << 3, /* yellow ring                                 */
    CAL_TODAY = 1 << 4,   /* solid blue cell, dark number                */
    CAL_PAST = 1 << 5,    /* dimmed                                      */
};

typedef struct {
    int mday;       /* the date's day of its own month: the chip's number */
    uint8_t flags;  /* the chip draws like its cell                       */
    char label[KDASH_CALENDAR_NAME_MAX]; /* special, else holiday, else "day off" */
    char sub[32];   /* "Thu · in 13 days" / "Fri · today" / "Sat · tomorrow" */
} cal_upcoming_t;

typedef struct {
    /* Left column. */
    char time[6];     /* "18:42", 24-hour, zero-padded */
    char weekday[12]; /* "Friday"                       */
    char date[24];    /* "November 13"                  */
    int nup;
    cal_upcoming_t up[CAL_UPCOMING_MAX];
    char note[32];    /* shown when nup == 0: "nothing tagged" / "no calendar feed" */
    char hint[24];    /* the muted old-ts hint, "as of 3h"; "" when current */

    /* Right column. */
    char title[24];   /* "November 2026" */
    int first_dow;    /* weekday of the 1st, 0 = Sunday */
    int ndays;        /* 28..31 */
    int rows;         /* 4..6 */
    uint8_t cell[31]; /* flags for day 1..ndays at [0..ndays-1] */
} cal_view_t;

/* Feed one GET: NULL means the key is absent. Returns 1 when the record
 * changed, 0 when nothing did, -1 when it was rejected (the caller logs it;
 * the previous record stays). */
int cal_observe(const char *json);
const kdash_calendar_t *cal_record(void); /* NULL before the first record */

/* Everything the card draws at `now`, in the process's local timezone. */
void cal_view(time_t now, cal_view_t *out);

/* ---- date arithmetic, exposed for the tests ---- */

/* Days since 1970-01-01, proleptic Gregorian. No timezone. */
long cal_day_number(int y, int m, int d);
/* 0 = Sunday. */
int cal_weekday(int y, int m, int d);
int cal_days_in_month(int y, int m);
/* Rows a month needs in a Sunday-first grid: 4, 5 or 6. */
int cal_month_rows(int y, int m);

void cal_reset(void); /* test hook */

#endif /* CALSTATE_H */
