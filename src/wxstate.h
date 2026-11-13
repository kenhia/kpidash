#ifndef WXSTATE_H
#define WXSTATE_H

/**
 * wxstate.h — the weather filler's model (WI #4576, sprint 034).
 *
 * Reads kdash:weather:home v1 (kdashdata sprint 021, schema kdash-weather),
 * written about every 10 minutes by kweather, and derives everything the
 * filler draws: how many host-width columns it gets, where it sits, the
 * shared temperature scale of the forecast bars, the day fractions of the sun
 * bar and every formatted string. Pure — no LVGL, no Redis, no clock passed in
 * by anything but the caller — so all of it is reachable from the gate. The
 * one environmental input is the process timezone (localtime_r): sun times
 * and the day bar are the DASHBOARD's local day, as the contract asks.
 *
 * Freshness is reader-owned (the key has no TTL): a record whose `ts` is more
 * than WX_FRESH_S old is stale and drawn grey. An absent key keeps the last
 * record, ageing by its own `ts`. A record the contract would reject (an
 * unknown `cond`, a millisecond stamp, a percent outside 0..100, a required
 * field absent rather than null — the schema's x-counterexamples) is
 * distrusted and the previous one kept. No record ever seen: not drawn.
 *
 * Touched only from the LVGL thread, so there is no lock.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WX_DAYS_MAX 8
#define WX_FRESH_S 3600.0
#define WX_TS_MAX 1e11 /* the schema's bound: a millisecond stamp fails */

/* The host row's geometry (sprint 030 / hostcols), which the filler shares:
 * ten host-width columns of 361 px on a 375 px pitch, 596 px tall. */
#define WX_PITCH 375
#define WX_COL_W 361
#define WX_H 596
#define WX_ROW_COLS 10
#define WX_MAX_COLS 4

typedef enum {
    WX_COND_NONE = 0, /* null: no classification, no icon */
    WX_COND_CLEAR,
    WX_COND_PARTLY,
    WX_COND_CLOUDY,
    WX_COND_FOG,
    WX_COND_DRIZZLE,
    WX_COND_RAIN,
    WX_COND_SLEET,
    WX_COND_SNOW,
    WX_COND_THUNDER,
} wx_cond_t;

typedef struct {
    bool has_temp, has_feels;
    double temp_f, feels_f;
    int humidity_pct; /* -1 = null */
    wx_cond_t cond;
    bool is_day;
} wx_now_t;

/* Unix seconds; 0 = null (the event does not happen today). */
typedef struct {
    int64_t first_light, sunrise, sunset, last_light;
} wx_sun_t;

typedef struct {
    char date[11]; /* YYYY-MM-DD */
    bool has_hi, has_lo;
    int hi_f, lo_f;
    wx_cond_t cond;
    int pop_pct; /* -1 = null */
} wx_day_t;

typedef struct {
    double ts;
    char source[17];
    wx_now_t now;
    wx_sun_t sun;
    wx_day_t days[WX_DAYS_MAX];
    int ndays;
} wx_record_t;

/* Parse one payload. 0 on success; -1 when the contract would reject it —
 * see the header comment. `out` is zeroed first either way, so two parses of
 * the same text compare equal with memcmp. */
int wx_parse(const char *json, wx_record_t *out);

/* Feed one poll: `json` is the GET result, NULL when the key is absent.
 * Returns 1 when a new record was accepted, 0 when nothing changed (the same
 * text again, or absent), -1 when this text was distrusted (logged by the
 * caller; the previous record stays). */
int wx_observe(const char *json);
bool wx_has_record(void);
const wx_record_t *wx_record(void);
void wx_reset(void); /* test hook */

bool wx_is_stale(const wx_record_t *r, double now);

/* ---- Space (Ken's rule, WI #4576) ---- */

/* Columns the filler gets when the hosts use `host_cols` of the ten:
 * min(4, 10 - host_cols), never negative. 0 = not drawn. */
int wx_columns(int host_cols);
/* Pixel width at n columns: 375 n - 14. */
int wx_width(int n);
/* Offset of the filler's left edge from the first host column's left edge.
 * It starts right after the hosts, and with six hosts or fewer the 4-column
 * card is centred in what is left: + 0.5 * 375 * (6 - host_cols). */
int wx_left(int host_cols);
/* Forecast days drawn at n columns: 3 at 2, 6 at 3, 8 at 4, none at 1 —
 * never more than the record has. */
int wx_days_shown(int n, int ndays);

/* ---- Derivations ---- */

/* The shared temperature scale over days[0..k): min and max of every non-null
 * hi and lo. False when there is none. */
bool wx_temp_scale(const wx_record_t *r, int k, int *tmin, int *tmax);
/* y of temperature v between y_top (tmax) and y_bot (tmin). A flat scale
 * (tmin == tmax) puts everything at y_bot, as the mock-up does. */
int wx_scale_y(int v, int tmin, int tmax, int y_top, int y_bot);
/* Fraction of the local day (midnight to midnight) at unix time ts, in
 * minutes, 0..1. */
double wx_day_frac(double ts);

/* ---- Formatting ---- */

/* "61°", rounded half up as the mock-up's Math.round; "—" for null. */
void wx_fmt_deg(bool has, double v, char *buf, size_t n);
/* "4:37 PM" in local time; "—" for 0. */
void wx_fmt_time(int64_t ts, char *buf, size_t n);
/* "Today" for i == 0, else the date's short weekday ("Fri"); "—" if the date
 * does not parse. */
void wx_fmt_weekday(const wx_day_t *d, int i, char *buf, size_t n);
/* "9 h 28 m of daylight"; "no sunrise today" when there is no sunrise. */
void wx_fmt_daylight(const wx_sun_t *s, char *buf, size_t n);
/* "Cloudy", "Partly cloudy"…; "No current reading" for null. */
const char *wx_cond_name(wx_cond_t c);
/* "feels 59° · 63% humidity"; "forecast only" when there is no temperature. */
void wx_fmt_feels(const wx_now_t *now, char *buf, size_t n);
/* "just now", "4 m ago", "2 h ago"; stale: "STALE · 2 h". Minute resolution,
 * so the text changes at most once a minute. */
void wx_fmt_age(const wx_record_t *r, double now, char *buf, size_t n);

#endif /* WXSTATE_H */
