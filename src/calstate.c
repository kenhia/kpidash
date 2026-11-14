/**
 * calstate.c — the time and calendar card's model (WI #4665). See the header.
 */
#include "calstate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kdash/kdash_freshness.h>

static const char *const MONTHS[12] = {"January", "February", "March",     "April",
                                       "May",     "June",     "July",      "August",
                                       "September", "October", "November", "December"};
static const char *const WEEKDAYS[7] = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                                        "Thursday", "Friday", "Saturday"};

/* ---- the record ---- */

/* The one instance, and a scratch to parse into so a rejected record never
 * touches it. Both static: about 5.8 KB each, never on the stack. */
static kdash_calendar_t g_cal, g_scratch;
static bool g_have;
static char *g_raw; /* the last value seen, so an unchanged poll is a strcmp */

int cal_observe(const char *json) {
    if (!json)
        return 0;
    if (g_raw && strcmp(json, g_raw) == 0)
        return 0;
    free(g_raw);
    g_raw = strdup(json);
    if (!kdash_parse_calendar(json, strlen(json), &g_scratch))
        return -1;
    if (g_have && memcmp(&g_scratch, &g_cal, sizeof(g_cal)) == 0)
        return 0;
    g_cal = g_scratch;
    g_have = true;
    return 1;
}

const kdash_calendar_t *cal_record(void) {
    return g_have ? &g_cal : NULL;
}

void cal_reset(void) {
    free(g_raw);
    g_raw = NULL;
    memset(&g_cal, 0, sizeof(g_cal));
    g_have = false;
}

/* ---- date arithmetic ---- */

/* Howard Hinnant's days_from_civil. */
long cal_day_number(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int cal_weekday(int y, int m, int d) {
    long n = cal_day_number(y, m, d);
    int w = (int)((n + 4) % 7); /* 1970-01-01 was a Thursday */
    return w < 0 ? w + 7 : w;
}

int cal_days_in_month(int y, int m) {
    static const int DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
        return 29;
    return DAYS[m - 1];
}

int cal_month_rows(int y, int m) {
    return (cal_weekday(y, m, 1) + cal_days_in_month(y, m) + 6) / 7;
}

/* `YYYY-MM-DD`. The modulos bound every field, so -O3's -Wformat-truncation
 * can prove the 10 characters fit. */
static void fmt_date(char *buf, size_t n, int y, int m, int d) {
    snprintf(buf, n, "%04u-%02u-%02u", (unsigned)y % 10000u, (unsigned)m % 100u,
             (unsigned)d % 100u);
}

static bool parse_date(const char *s, int *y, int *m, int *d) {
    char tail;
    return sscanf(s, "%4d-%2d-%2d%c", y, m, d, &tail) == 3 && *m >= 1 && *m <= 12 && *d >= 1 &&
           *d <= 31;
}

/* ---- the view ---- */

/* The tag flags for one entry (NULL: none). */
static uint8_t tag_flags(const kdash_calendar_day_t *e) {
    if (!e)
        return 0;
    return (uint8_t)((e->off ? CAL_OFF : 0) | (e->holiday[0] ? CAL_HOLIDAY : 0) |
                     (e->special[0] ? CAL_SPECIAL : 0));
}

/* The old-ts hint, coarsest unit: "as of 40m", "as of 3h", "as of 2d". The
 * modulo bounds the number so -O3's -Wformat-truncation can prove it fits. */
static void fmt_hint(double secs, char *buf, size_t n) {
    unsigned long s = secs > 0 ? (unsigned long)secs : 0;
    unsigned long val = s < 3600 ? s / 60 : s < 2 * 86400 ? s / 3600 : s / 86400;
    char unit = s < 3600 ? 'm' : s < 2 * 86400 ? 'h' : 'd';
    snprintf(buf, n, "as of %u%c", (unsigned)(val % 1000u), unit);
}

void cal_view(time_t now, cal_view_t *v) {
    memset(v, 0, sizeof(*v));
    struct tm tm;
    if (!localtime_r(&now, &tm))
        return;
    const int y = tm.tm_year + 1900, m = tm.tm_mon + 1, d = tm.tm_mday;

    snprintf(v->time, sizeof(v->time), "%02d:%02d", tm.tm_hour, tm.tm_min);
    snprintf(v->weekday, sizeof(v->weekday), "%s", WEEKDAYS[tm.tm_wday]);
    snprintf(v->date, sizeof(v->date), "%s %d", MONTHS[m - 1], d);
    snprintf(v->title, sizeof(v->title), "%s %d", MONTHS[m - 1], y);
    v->first_dow = cal_weekday(y, m, 1);
    v->ndays = cal_days_in_month(y, m);
    v->rows = cal_month_rows(y, m);

    char today[KDASH_DATE_MAX];
    fmt_date(today, sizeof(today), y, m, d);
    const kdash_calendar_t *cal = cal_record();
    /* CD-30: tags only while the window covers today; otherwise the grid and
     * weekends draw untagged, and the old record is not believed. */
    const bool tagged = cal && kdash_calendar_covers(cal, today);

    for (int day = 1; day <= v->ndays; day++) {
        int dow = (v->first_dow + day - 1) % 7;
        uint8_t f = (dow == 0 || dow == 6) ? CAL_WEEKEND : 0;
        if (day < d)
            f |= CAL_PAST;
        if (day == d)
            f |= CAL_TODAY;
        if (tagged) {
            char date[KDASH_DATE_MAX];
            fmt_date(date, sizeof(date), y, m, day);
            f |= tag_flags(kdash_calendar_day(cal, date));
        }
        v->cell[day - 1] = f;
    }

    if (cal && kdash_cadence_stale(cal->ts, cal->expected_refresh_s, (long long)now,
                                   KDASH_CADENCE_GRACE_S)) {
        fmt_hint((double)now - cal->ts, v->hint, sizeof(v->hint));
    }

    if (!cal) {
        snprintf(v->note, sizeof(v->note), "no calendar feed");
        return;
    }
    if (!tagged) {
        snprintf(v->note, sizeof(v->note), "calendar out of date");
        return;
    }

    /* Coming up: the tagged days from today forward, past the end of the
     * month as far as the window runs. The entries are ascending by date. */
    const long today_n = cal_day_number(y, m, d);
    for (int i = 0; i < cal->days_count && v->nup < CAL_UPCOMING_MAX; i++) {
        const kdash_calendar_day_t *e = &cal->days[i];
        int ey, em, ed;
        if (strcmp(e->date, today) < 0 || !parse_date(e->date, &ey, &em, &ed))
            continue;
        cal_upcoming_t *u = &v->up[v->nup++];
        const int edow = cal_weekday(ey, em, ed);
        u->mday = ed;
        u->flags = (uint8_t)(tag_flags(e) | ((edow == 0 || edow == 6) ? CAL_WEEKEND : 0) |
                             (strcmp(e->date, today) == 0 ? CAL_TODAY : 0));
        snprintf(u->label, sizeof(u->label), "%s",
                 e->special[0] ? e->special : e->holiday[0] ? e->holiday : "day off");
        /* By date number, not seconds / 86400: a DST change makes a 23- or
         * 25-hour day, and "in 13 days" must not round across it. */
        const long n = cal_day_number(ey, em, ed) - today_n;
        char when[20];
        if (n == 0)
            snprintf(when, sizeof(when), "today");
        else if (n == 1)
            snprintf(when, sizeof(when), "tomorrow");
        else
            snprintf(when, sizeof(when), "in %u days", (unsigned)(n % 1000));
        snprintf(u->sub, sizeof(u->sub), "%.3s \xC2\xB7 %s", WEEKDAYS[edow], when);
    }
    if (v->nup == 0)
        snprintf(v->note, sizeof(v->note), "nothing tagged");
}
