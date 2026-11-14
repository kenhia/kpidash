/* test_calstate.c — the time and calendar card's model (WI #4665, sprint 040).
 *
 * Held to the contract: it reads kdashdata's kdash-calendar schema at run
 * time and requires every example to be adopted and every x-counterexample to
 * be rejected or skipped, so a contract change shows up here. The clock is
 * injected and the zone pinned to America/Los_Angeles — rpi53's zone and the
 * feed's — so midnight, a month end and both DST changes are exercised
 * without waiting for them. */
#include "calstate.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int passed = 0;
static int failed = 0;

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr);                      \
            failed++;                                                                              \
        } else {                                                                                   \
            passed++;                                                                              \
        }                                                                                          \
    } while (0)

#define STREQ(a, b) (strcmp((a), (b)) == 0)
#define TAGS (CAL_OFF | CAL_HOLIDAY | CAL_SPECIAL)

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    if (buf) buf[n] = '\0';
    fclose(f);
    return buf;
}

/* A local wall-clock time in the pinned zone. isdst -1 lets mktime decide,
 * except in the repeated hour, where the caller says which one it means. */
static time_t local(int y, int m, int d, int hh, int mm, int ss, int isdst) {
    struct tm tm = {0};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_hour = hh;
    tm.tm_min = mm;
    tm.tm_sec = ss;
    tm.tm_isdst = isdst;
    return mktime(&tm);
}

/* The handoff's example (korg:4671), with Ken's birthday on the 27th. */
static const char *NOV =
    "{\"ts\":1794643200.5,\"expected_refresh_s\":3600,\"from\":\"2026-11-01\","
    "\"to\":\"2027-02-14\",\"days\":["
    "{\"date\":\"2026-11-11\",\"off\":false,\"holiday\":\"Veterans Day\",\"short\":\"Veterans\"},"
    "{\"date\":\"2026-11-25\",\"off\":true},"
    "{\"date\":\"2026-11-26\",\"off\":true,\"holiday\":\"Thanksgiving\",\"short\":\"Thanksgiving\"},"
    "{\"date\":\"2026-11-27\",\"off\":true,\"special\":\"Ken's birthday\",\"short\":\"Bday\"},"
    "{\"date\":\"2026-12-25\",\"off\":true,\"holiday\":\"Christmas Day\",\"short\":\"Christmas\"},"
    "{\"date\":\"2027-01-01\",\"off\":true,\"holiday\":\"New Year's Day\",\"short\":\"New Year\"},"
    "{\"date\":\"2027-01-18\",\"off\":false,\"holiday\":\"Martin Luther King Jr. Day\","
    "\"short\":\"MLK Day\"},"
    "{\"date\":\"2027-02-14\",\"off\":false,\"special\":\"Valentine's Day\"}]}";

/* ---- arithmetic ---- */

static void test_arithmetic(void) {
    CHECK(cal_day_number(1970, 1, 1) == 0);
    CHECK(cal_weekday(1970, 1, 1) == 4);
    CHECK(cal_weekday(2026, 11, 1) == 0); /* the mock-up's November: a Sunday */
    CHECK(cal_weekday(2026, 11, 13) == 5);
    CHECK(cal_weekday(2026, 8, 1) == 6);
    CHECK(cal_days_in_month(2026, 2) == 28);
    CHECK(cal_days_in_month(2028, 2) == 29);
    CHECK(cal_days_in_month(2100, 2) == 28);
    CHECK(cal_days_in_month(2000, 2) == 29);
    CHECK(cal_days_in_month(2026, 12) == 31);
    CHECK(cal_month_rows(2026, 11) == 5);
    CHECK(cal_month_rows(2026, 8) == 6); /* the mock-up's six-week month */
    CHECK(cal_month_rows(2026, 2) == 4); /* Feb 2026 starts on a Sunday */
    CHECK(cal_month_rows(2027, 1) == 6); /* starts Friday, 31 days */
}

/* ---- the contract ---- */

static void test_schema(void) {
    char *text = slurp(KDASH_CALENDAR_SCHEMA);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    free(text);
    CHECK(root != NULL);
    const cJSON *ex;
    int n = 0;
    cJSON_ArrayForEach(ex, cJSON_GetObjectItemCaseSensitive(root, "examples")) {
        char *s = cJSON_PrintUnformatted(ex);
        cal_reset();
        CHECK(cal_observe(s) == 1);
        CHECK(cal_record() != NULL);
        free(s);
        n++;
    }
    CHECK(n >= 1);
    n = 0;
    cJSON_ArrayForEach(ex, cJSON_GetObjectItemCaseSensitive(root, "x-counterexamples")) {
        char *s = cJSON_PrintUnformatted(cJSON_GetObjectItemCaseSensitive(ex, "record"));
        cal_reset();
        int r = cal_observe(s);
        const kdash_calendar_t *c = cal_record();
        CHECK(r == -1 || (c && c->days_skipped > 0 && c->days_count == 0));
        free(s);
        n++;
    }
    CHECK(n >= 1);
    cJSON_Delete(root);
}

/* ---- the mock-up's two states ---- */

static void test_the_13th(void) {
    cal_reset();
    CHECK(cal_observe(NOV) == 1);
    cal_view_t v;
    cal_view(local(2026, 11, 13, 18, 42, 7, -1), &v);
    CHECK(STREQ(v.time, "18:42"));
    CHECK(STREQ(v.weekday, "Friday"));
    CHECK(STREQ(v.date, "November 13"));
    CHECK(STREQ(v.title, "November 2026"));
    CHECK(v.first_dow == 0 && v.ndays == 30 && v.rows == 5);
    CHECK(v.cell[0] == (CAL_WEEKEND | CAL_PAST)); /* the 1st, a Sunday */
    CHECK(v.cell[6] == (CAL_WEEKEND | CAL_PAST));
    CHECK(v.cell[9] == CAL_PAST);
    CHECK(v.cell[10] == (CAL_HOLIDAY | CAL_PAST)); /* Veterans Day, worked */
    CHECK(v.cell[12] == CAL_TODAY);
    CHECK(v.cell[13] == CAL_WEEKEND);
    CHECK(v.cell[24] == CAL_OFF);
    CHECK(v.cell[25] == (CAL_OFF | CAL_HOLIDAY));
    CHECK(v.cell[26] == (CAL_OFF | CAL_SPECIAL));
    CHECK(v.cell[27] == CAL_WEEKEND);
    CHECK(v.nup == 4);
    CHECK(v.up[0].mday == 25 && STREQ(v.up[0].label, "day off"));
    CHECK(STREQ(v.up[0].sub, "Wed \xC2\xB7 in 12 days"));
    CHECK(v.up[0].flags == CAL_OFF);
    CHECK(STREQ(v.up[1].label, "Thanksgiving") && STREQ(v.up[1].sub, "Thu \xC2\xB7 in 13 days"));
    CHECK(STREQ(v.up[2].label, "Ken's birthday") && STREQ(v.up[2].sub, "Fri \xC2\xB7 in 14 days"));
    CHECK(v.up[2].flags == (CAL_OFF | CAL_SPECIAL));
    /* The list runs past the month end, as far as the window goes. */
    CHECK(v.up[3].mday == 25 && STREQ(v.up[3].label, "Christmas Day"));
    CHECK(STREQ(v.up[3].sub, "Fri \xC2\xB7 in 42 days"));
    CHECK(v.hint[0] == '\0'); /* ts is current */
}

static void test_the_27th(void) {
    cal_reset();
    CHECK(cal_observe(NOV) == 1);
    cal_view_t v;
    cal_view(local(2026, 11, 27, 9, 15, 0, -1), &v);
    CHECK(STREQ(v.time, "09:15")); /* 24-hour, zero-padded */
    CHECK(v.cell[26] == (CAL_OFF | CAL_SPECIAL | CAL_TODAY));
    CHECK(v.cell[25] == (CAL_OFF | CAL_HOLIDAY | CAL_PAST));
    CHECK(v.nup == 4);
    CHECK(STREQ(v.up[0].label, "Ken's birthday") && STREQ(v.up[0].sub, "Fri \xC2\xB7 today"));
    CHECK(v.up[0].flags == (CAL_OFF | CAL_SPECIAL | CAL_TODAY));
    CHECK(STREQ(v.up[1].label, "Christmas Day"));
    CHECK(STREQ(v.up[2].label, "New Year's Day"));
    /* A holiday Ken works: off false, so no tint, and the full name. */
    CHECK(STREQ(v.up[3].label, "Martin Luther King Jr. Day"));
    CHECK(v.up[3].flags == CAL_HOLIDAY);
    CHECK(STREQ(v.up[3].sub, "Mon \xC2\xB7 in 52 days"));

    cal_view(local(2026, 11, 26, 20, 0, 0, -1), &v);
    CHECK(STREQ(v.up[0].label, "Thanksgiving") && STREQ(v.up[0].sub, "Thu \xC2\xB7 today"));
    CHECK(STREQ(v.up[1].sub, "Fri \xC2\xB7 tomorrow"));
}

/* ---- rollover ---- */

static void test_midnight(void) {
    cal_reset();
    cal_observe(NOV);
    cal_view_t v;
    time_t t = local(2026, 11, 13, 23, 59, 59, -1);
    cal_view(t, &v);
    CHECK(STREQ(v.time, "23:59") && STREQ(v.date, "November 13"));
    CHECK(v.cell[12] == CAL_TODAY);
    cal_view(t + 1, &v);
    CHECK(STREQ(v.time, "00:00") && STREQ(v.date, "November 14"));
    CHECK(STREQ(v.weekday, "Saturday"));
    CHECK(v.cell[12] == CAL_PAST && v.cell[13] == (CAL_WEEKEND | CAL_TODAY));
    CHECK(STREQ(v.up[0].sub, "Wed \xC2\xB7 in 11 days"));
}

static void test_month_end(void) {
    cal_reset();
    cal_observe(NOV);
    cal_view_t v;
    time_t t = local(2026, 11, 30, 23, 59, 0, -1);
    cal_view(t, &v);
    CHECK(STREQ(v.title, "November 2026") && v.cell[29] == CAL_TODAY);
    cal_view(t + 60, &v);
    CHECK(STREQ(v.title, "December 2026") && STREQ(v.date, "December 1"));
    CHECK(v.first_dow == 2 && v.ndays == 31 && v.rows == 5);
    CHECK(v.cell[0] == CAL_TODAY);
    CHECK(v.cell[24] == (CAL_OFF | CAL_HOLIDAY));
    CHECK(STREQ(v.up[0].label, "Christmas Day") && STREQ(v.up[0].sub, "Fri \xC2\xB7 in 24 days"));

    /* The year turns too. */
    cal_view(local(2026, 12, 31, 23, 59, 59, -1) + 1, &v);
    CHECK(STREQ(v.title, "January 2027") && v.rows == 6);
    CHECK(v.cell[0] == (CAL_OFF | CAL_HOLIDAY | CAL_TODAY));
}

static void test_dst(void) {
    /* Fall back, 2026-11-01: 01:00-01:59 happens twice and the day is 25 h. */
    const char *rec = "{\"ts\":1793000000,\"expected_refresh_s\":3600,\"from\":\"2026-10-01\","
                      "\"to\":\"2027-03-31\",\"days\":["
                      "{\"date\":\"2026-11-02\",\"off\":true},"
                      "{\"date\":\"2027-03-15\",\"off\":true}]}";
    cal_reset();
    CHECK(cal_observe(rec) == 1);
    cal_view_t v;
    time_t pdt = local(2026, 11, 1, 1, 30, 0, 1);
    time_t pst = local(2026, 11, 1, 1, 30, 0, 0);
    CHECK(pst - pdt == 3600);
    cal_view(pdt, &v);
    CHECK(STREQ(v.time, "01:30") && STREQ(v.date, "November 1"));
    cal_view(pst, &v);
    CHECK(STREQ(v.time, "01:30") && STREQ(v.date, "November 1"));
    /* 23:30 on the 25-hour day is still the 1st, and the 2nd is tomorrow. */
    cal_view(local(2026, 11, 1, 23, 30, 0, -1), &v);
    CHECK(STREQ(v.date, "November 1") && STREQ(v.up[0].sub, "Mon \xC2\xB7 tomorrow"));
    /* Across the change by date, not by seconds: 10-31 noon PDT to 11-02. */
    cal_view(local(2026, 10, 31, 12, 0, 0, -1), &v);
    CHECK(STREQ(v.up[0].sub, "Mon \xC2\xB7 in 2 days"));
    CHECK(v.cell[30] == (CAL_WEEKEND | CAL_TODAY));

    /* Spring forward, 2027-03-14: 01:59 PST is followed by 03:00 PDT. */
    time_t t = local(2027, 3, 14, 1, 59, 0, -1);
    cal_view(t, &v);
    CHECK(STREQ(v.time, "01:59"));
    cal_view(t + 60, &v);
    CHECK(STREQ(v.time, "03:00") && STREQ(v.date, "March 14"));
    CHECK(STREQ(v.up[0].sub, "Mon \xC2\xB7 tomorrow"));
    /* The 23-hour day: 23:30 on the 14th is still the 14th. */
    cal_view(local(2027, 3, 14, 23, 30, 0, -1), &v);
    CHECK(STREQ(v.date, "March 14"));
}

/* ---- the feed's absence and age ---- */

static void test_no_feed(void) {
    cal_reset();
    CHECK(cal_observe(NULL) == 0);
    CHECK(cal_record() == NULL);
    cal_view_t v;
    cal_view(local(2026, 11, 13, 18, 42, 0, -1), &v);
    /* The clock and grid draw anyway; weekends are still grey. */
    CHECK(STREQ(v.time, "18:42") && v.ndays == 30);
    CHECK(v.cell[13] == CAL_WEEKEND && v.cell[24] == 0);
    CHECK(v.nup == 0 && STREQ(v.note, "no calendar feed"));
    CHECK(v.hint[0] == '\0');
}

static void test_hold(void) {
    cal_reset();
    CHECK(cal_observe(NOV) == 1);
    CHECK(cal_observe(NOV) == 0);  /* the same poll again */
    CHECK(cal_observe(NULL) == 0); /* the key vanished: held */
    CHECK(cal_record() != NULL && cal_record()->days_count == 8);
    CHECK(cal_observe("{\"ts\":1}") == -1); /* off-contract: held */
    CHECK(cal_observe("not json") == -1);
    CHECK(cal_record() != NULL && cal_record()->days_count == 8);
    cal_view_t v;
    cal_view(local(2026, 11, 13, 18, 42, 0, -1), &v);
    CHECK(v.cell[25] == (CAL_OFF | CAL_HOLIDAY) && v.nup == 4);
    /* A record that differs only in ts is a change worth adopting. */
    CHECK(cal_observe("{\"ts\":1794646800,\"expected_refresh_s\":3600,\"from\":\"2026-11-01\","
                      "\"to\":\"2027-02-14\",\"days\":[]}") == 1);
    cal_view(local(2026, 11, 13, 18, 42, 0, -1), &v);
    CHECK(v.cell[25] == 0 && v.nup == 0 && STREQ(v.note, "nothing tagged"));
}

static void test_old_ts(void) {
    cal_reset();
    cal_observe(NOV);
    const time_t ts = 1794643200;
    cal_view_t v;
    cal_view(ts + 3000, &v);
    CHECK(v.hint[0] == '\0');
    cal_view(ts + 3660, &v); /* exactly interval + grace: not yet */
    CHECK(v.hint[0] == '\0');
    cal_view(ts + 7300, &v);
    CHECK(STREQ(v.hint, "as of 2h"));
    CHECK(v.nup == 4); /* an old ts never blanks the tags */
    cal_view(ts + 3 * 86400 + 5, &v);
    CHECK(STREQ(v.hint, "as of 3d"));
    CHECK(v.nup > 0);
}

static void test_window_passed(void) {
    cal_reset();
    cal_observe(NOV); /* window 2026-11-01 .. 2027-02-14 */
    cal_view_t v;
    cal_view(local(2027, 2, 14, 12, 0, 0, -1), &v);
    CHECK(v.cell[13] == (CAL_SPECIAL | CAL_TODAY | CAL_WEEKEND)); /* the last covered day */
    cal_view(local(2027, 2, 15, 0, 0, 0, -1), &v);
    /* Out of the window: the grid and weekends draw, untagged. */
    for (int i = 0; i < v.ndays; i++)
        CHECK((v.cell[i] & TAGS) == 0);
    CHECK(v.cell[13] == (CAL_WEEKEND | CAL_PAST));
    CHECK(v.nup == 0 && STREQ(v.note, "calendar out of date"));
    CHECK(v.hint[0] != '\0');
    /* And before it: November's record does not tag October. */
    cal_view(local(2026, 10, 31, 12, 0, 0, -1), &v);
    CHECK(v.nup == 0 && STREQ(v.note, "calendar out of date"));
}

/* ---- a full-cap record: CD-30's 9.4 KB case ---- */

static size_t full_cap(char *buf, size_t n, int count) {
    size_t o = (size_t)snprintf(buf, n,
                                "{\"ts\":1794643200.5,\"expected_refresh_s\":3600,"
                                "\"from\":\"2026-11-01\",\"to\":\"2027-02-14\",\"days\":[");
    for (int i = 0; i < count; i++) {
        int y, m, d;
        long dn = cal_day_number(2026, 11, 1) + i;
        /* civil_from_days, briefly: walk forward from the 1st. */
        y = 2026, m = 11, d = 1;
        for (long k = cal_day_number(2026, 11, 1); k < dn; k++)
            if (++d > cal_days_in_month(y, m)) {
                d = 1;
                if (++m > 12) m = 1, y++;
            }
        o += (size_t)snprintf(buf + o, n - o,
                              "%s{\"date\":\"%04d-%02d-%02d\",\"off\":true,"
                              "\"holiday\":\"Holiday name at thirty-one ch%02d\","
                              "\"special\":\"Special name at thirty-one ch%02d\","
                              "\"short\":\"Twelve ch %02d\"}",
                              i ? "," : "", y, m, d, i % 100, i % 100, i % 100);
    }
    o += (size_t)snprintf(buf + o, n - o, "]}");
    return o;
}

static void test_full_cap(void) {
    static char buf[16384];
    size_t len = full_cap(buf, sizeof(buf), 64);
    CHECK(len > 9000 && len < sizeof(buf));
    cal_reset();
    CHECK(cal_observe(buf) == 1);
    const kdash_calendar_t *c = cal_record();
    CHECK(c && c->days_count == 64 && !c->days_truncated && c->days_skipped == 0);
    CHECK(c && strlen(c->days[63].holiday) == 31 && strlen(c->days[63].short_label) == 12);
    cal_view_t v;
    cal_view(local(2026, 11, 13, 12, 0, 0, -1), &v);
    CHECK(v.nup == 4 && v.up[0].mday == 13);
    /* The special name wins the label, and the model passes all 31
     * characters through: cutting to the column is the card's job. */
    CHECK(STREQ(v.up[0].label, "Special name at thirty-one ch12"));
    CHECK(strlen(v.up[0].label) == 31);

    /* The observed-holiday spelling CD-30 fixes, whole. */
    cal_reset();
    CHECK(cal_observe("{\"ts\":1794643200,\"expected_refresh_s\":3600,\"from\":\"2026-11-01\","
                      "\"to\":\"2027-02-14\",\"days\":[{\"date\":\"2026-11-16\",\"off\":true,"
                      "\"holiday\":\"Independence Day (observed)\"}]}") == 1);
    cal_view(local(2026, 11, 13, 12, 0, 0, -1), &v);
    CHECK(v.nup == 1 && STREQ(v.up[0].label, "Independence Day (observed)"));

    /* Past 64, the earliest are kept. */
    len = full_cap(buf, sizeof(buf), 66);
    CHECK(len < sizeof(buf));
    cal_reset();
    CHECK(cal_observe(buf) == 1);
    c = cal_record();
    CHECK(c && c->days_count == 64 && c->days_truncated);
    CHECK(c && STREQ(c->days[0].date, "2026-11-01"));
}

int main(void) {
    setenv("TZ", "America/Los_Angeles", 1);
    tzset();
    test_arithmetic();
    test_schema();
    test_the_13th();
    test_the_27th();
    test_midnight();
    test_month_end();
    test_dst();
    test_no_feed();
    test_hold();
    test_old_ts();
    test_window_passed();
    test_full_cap();
    cal_reset();
    printf("test_calstate: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
