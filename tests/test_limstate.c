/* test_limstate.c — the Claude limits card's model (WI #4609, sprint 037).
 *
 * Parsing and staleness are libkdash's, so these tests hold the card to the
 * rules it adds on top: the colour bands, the pace tick, the reset and age
 * strings, and the anti-flicker carry. The fixture is the live claude:limits
 * record kai published at 2026-11-14 00:42:03Z, read off rpi53. Times are
 * checked in America/Los_Angeles, rpi53's own zone. */
#include "limstate.h"

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
#define NF(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* HGETALL claude:limits on rpi53, 2026-11-14 00:42:50Z. */
static const char *live_f[] = {
    "five_hour_pct", "seven_day_pct", "updated_at", "host", "source", "expected_refresh_s", "ts",
    "five_hour_resets_at", "seven_day_resets_at", "scoped_model", "scoped_pct", "scoped_active",
    "scoped_count", "scoped_updated_at", "scoped_expected_refresh_s", "scoped_resets_at",
};
static const char *live_v[] = {
    "8", "8", "1794616923", "kai", "oauth", "300", "1794616924",
    "1795089599", "1795089599", "Fable", "11", "1",
    "1", "1794616923", "300", "1795089599",
};
#define LIVE_UPDATED 1794616923.0

/* The same headline, as a statusline writer publishes it: no scoped set. */
static const char *bare_f[] = {"five_hour_pct", "seven_day_pct", "updated_at",
                               "expected_refresh_s", "host"};
static const char *bare_v[] = {"42", "61", "1794617000", "60", "cleo"};

static time_t local_time(int y, int mo, int d, int h, int mi) {
    struct tm t = {0};
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_isdst = -1;
    return mktime(&t);
}

static void test_parse_live(void) {
    lim_record_t r;
    CHECK(lim_parse(live_f, live_v, NF(live_f), &r));
    CHECK(r.l.valid);
    CHECK(STREQ(r.host, "kai"));
    CHECK(r.l.five_hour_pct == 8 && r.l.seven_day_pct == 8);
    CHECK(r.l.scoped_valid);
    CHECK(STREQ(r.l.scoped_model, "Fable"));
    CHECK(r.l.scoped_pct == 11);
    CHECK(r.l.updated_at == LIVE_UPDATED);

    /* Two parses of the same list compare equal byte for byte. */
    lim_record_t r2;
    CHECK(lim_parse(live_f, live_v, NF(live_f), &r2));
    CHECK(memcmp(&r, &r2, sizeof(r)) == 0);
}

static void test_parse_rejects(void) {
    lim_record_t r;
    const char *f[] = {"five_hour_pct", "host"};
    const char *v[] = {"42", "kai"};
    CHECK(!lim_parse(f, v, NF(f), &r));
    CHECK(!r.l.valid && r.host[0] == '\0');

    const char *f2[] = {"five_hour_pct", "seven_day_pct"};
    const char *v2[] = {"lots", "5"};
    CHECK(!lim_parse(f2, v2, NF(f2), &r));
    /* Out of range is libkdash's clamp, not a rejection. */
    const char *v3[] = {"101", "5"};
    CHECK(lim_parse(f2, v3, NF(f2), &r));
    CHECK(lim_pct(r.l.five_hour_pct) == 100);
    CHECK(!lim_parse(NULL, NULL, 0, &r));
}

static void test_host_display_only(void) {
    lim_record_t r;
    const char *f[] = {"five_hour_pct", "seven_day_pct", "host"};
    const char *bad[] = {"1", "2", "kai; rm -rf"};
    CHECK(lim_parse(f, bad, NF(f), &r));
    CHECK(r.host[0] == '\0'); /* off the schema's pattern: not drawn */
    const char *none[] = {"1", "2", ""};
    CHECK(lim_parse(f, none, NF(f), &r));
    CHECK(r.host[0] == '\0');
    const char *ok[] = {"1", "2", "kubs0.lan"};
    CHECK(lim_parse(f, ok, NF(f), &r));
    CHECK(STREQ(r.host, "kubs0.lan"));
}

static void test_observe(void) {
    lim_reset();
    CHECK(!lim_has_record());
    CHECK(lim_observe(NULL, NULL, 0) == 0); /* absent, nothing seen: not drawn */
    CHECK(!lim_has_record());

    CHECK(lim_observe(live_f, live_v, NF(live_f)) == 1);
    CHECK(lim_has_record());
    CHECK(lim_observe(live_f, live_v, NF(live_f)) == 0); /* the same again */

    /* A brief gap keeps the record. */
    CHECK(lim_observe(NULL, NULL, 0) == 0);
    CHECK(lim_has_record());
    CHECK(lim_record()->l.five_hour_pct == 8);

    /* A rejected record keeps the previous one. */
    const char *f[] = {"five_hour_pct"};
    const char *v[] = {"x"};
    CHECK(lim_observe(f, v, NF(f)) == -1);
    CHECK(lim_record()->l.five_hour_pct == 8);
    CHECK(STREQ(lim_record()->host, "kai"));
}

/* A statusline write carries no scoped set. The bar must not vanish and
 * reflow the card: it keeps its last numbers and greys by its own stamp. */
static void test_scoped_carry(void) {
    lim_reset();
    CHECK(lim_observe(live_f, live_v, NF(live_f)) == 1);
    CHECK(lim_observe(bare_f, bare_v, NF(bare_f)) == 1);
    const lim_record_t *r = lim_record();
    CHECK(r->l.five_hour_pct == 42 && r->l.seven_day_pct == 61);
    CHECK(STREQ(r->host, "cleo"));
    CHECK(r->l.scoped_valid);
    CHECK(STREQ(r->l.scoped_model, "Fable"));
    CHECK(r->l.scoped_pct == 11);
    CHECK(r->l.scoped_updated_at == LIVE_UPDATED); /* its OWN stamp, not the headline's */

    /* Repeating the bare write is still "nothing changed". */
    CHECK(lim_observe(bare_f, bare_v, NF(bare_f)) == 0);

    /* With no scoped set ever seen there is nothing to carry. */
    lim_reset();
    CHECK(lim_observe(bare_f, bare_v, NF(bare_f)) == 1);
    CHECK(!lim_record()->l.scoped_valid);
}

static void test_stale(void) {
    lim_record_t r;
    CHECK(lim_parse(live_f, live_v, NF(live_f), &r));
    /* cadence 300 + libkdash's 60 s grace */
    CHECK(!lim_headline_stale(&r, LIVE_UPDATED + 360));
    CHECK(lim_headline_stale(&r, LIVE_UPDATED + 361));
    CHECK(!lim_scoped_stale(&r, LIVE_UPDATED + 360));
    CHECK(lim_scoped_stale(&r, LIVE_UPDATED + 361));
    /* Skew (a stamp in the future) is never stale. */
    CHECK(!lim_headline_stale(&r, LIVE_UPDATED - 600));

    /* A stampless scoped set is always stale, even beside a fresh headline. */
    r.l.scoped_updated_at = 0;
    CHECK(!lim_headline_stale(&r, LIVE_UPDATED + 10));
    CHECK(lim_scoped_stale(&r, LIVE_UPDATED + 10));

    /* No cadence published: the legacy hour. */
    r.l.expected_refresh_s = 0;
    CHECK(!lim_headline_stale(&r, LIVE_UPDATED + 3600));
    CHECK(lim_headline_stale(&r, LIVE_UPDATED + 3601));
}

static void test_pct_and_band(void) {
    CHECK(lim_pct(74.4) == 74);
    CHECK(lim_pct(74.5) == 75);
    CHECK(lim_pct(0) == 0);
    CHECK(lim_pct(100) == 100);
    CHECK(lim_pct(-3) == 0);
    CHECK(lim_pct(140) == 100);
    CHECK(lim_band(0) == LIM_BAND_OK);
    CHECK(lim_band(74) == LIM_BAND_OK);
    CHECK(lim_band(75) == LIM_BAND_WARN);
    CHECK(lim_band(89) == LIM_BAND_WARN);
    CHECK(lim_band(90) == LIM_BAND_CRIT);
    CHECK(lim_band(100) == LIM_BAND_CRIT);
    CHECK(lim_band(lim_pct(74.6)) == LIM_BAND_WARN); /* drawn "75", so yellow */
}

static void test_pace(void) {
    const double now = 1794600000;
    /* 2 h left of 5: three fifths through. */
    double p = lim_pace(now + 2 * 3600, LIM_FIVE_HOUR_S, now);
    CHECK(p > 0.5999 && p < 0.6001);
    /* 7-day window, 1 day left. */
    p = lim_pace(now + 86400, LIM_SEVEN_DAY_S, now);
    CHECK(p > 6.0 / 7 - 1e-9 && p < 6.0 / 7 + 1e-9);
    /* At the start and the end. */
    CHECK(lim_pace(now + LIM_FIVE_HOUR_S, LIM_FIVE_HOUR_S, now) == 0.0);
    CHECK(lim_pace(now, LIM_FIVE_HOUR_S, now) == 1.0);
    /* Passed, not yet refreshed. */
    CHECK(lim_pace(now - 30, LIM_FIVE_HOUR_S, now) == 1.0);
    /* Unknown: no tick. */
    CHECK(lim_pace(0, LIM_FIVE_HOUR_S, now) < 0);
    /* Further away than the window is long: no tick. This is the live record,
     * whose five-hour reset sat five days out. */
    CHECK(lim_pace(1795089599, LIM_FIVE_HOUR_S, LIVE_UPDATED) < 0);
    CHECK(lim_pace(1795089599, LIM_SEVEN_DAY_S, LIVE_UPDATED) >= 0);
}

static void test_fmt_reset(void) {
    char b[48];
    const double now = 1794600000;
    lim_fmt_reset(now + 2 * 3600 + 13 * 60 + 30, now, b, sizeof(b));
    CHECK(STREQ(b, "resets in 2h 14m"));
    lim_fmt_reset(now + 9 * 60, now, b, sizeof(b));
    CHECK(STREQ(b, "resets in 9m"));
    lim_fmt_reset(now + 10, now, b, sizeof(b));
    CHECK(STREQ(b, "resets in 1m")); /* never "0m" while time is left */
    lim_fmt_reset(now + 3600, now, b, sizeof(b));
    CHECK(STREQ(b, "resets in 1h"));
    lim_fmt_reset(now + 23 * 3600 + 59 * 60, now, b, sizeof(b));
    CHECK(STREQ(b, "resets in 23h 59m"));
    lim_fmt_reset(now - 5, now, b, sizeof(b));
    CHECK(STREQ(b, "resets now"));
    lim_fmt_reset(0, now, b, sizeof(b));
    CHECK(STREQ(b, "reset unknown"));

    /* A day or more away: the local weekday and time. */
    time_t mon = local_time(2026, 11, 16, 9, 0); /* Monday */
    lim_fmt_reset((double)mon, (double)mon - 3 * 86400, b, sizeof(b));
    CHECK(STREQ(b, "resets Mon 09:00"));
    lim_fmt_reset(1795089599, LIVE_UPDATED, b, sizeof(b));
    CHECK(STREQ(b, "resets Thu 03:59"));
}

static void test_fmt_dur_and_header(void) {
    char b[64];
    lim_fmt_dur(40, b, sizeof(b));
    CHECK(STREQ(b, "40s"));
    lim_fmt_dur(59.9, b, sizeof(b));
    CHECK(STREQ(b, "59s"));
    lim_fmt_dur(60, b, sizeof(b));
    CHECK(STREQ(b, "1m"));
    lim_fmt_dur(22 * 60 + 30, b, sizeof(b));
    CHECK(STREQ(b, "22m"));
    lim_fmt_dur(3 * 3600 + 1, b, sizeof(b));
    CHECK(STREQ(b, "3h"));
    lim_fmt_dur(2 * 86400 + 7, b, sizeof(b));
    CHECK(STREQ(b, "2d"));
    lim_fmt_dur(-5, b, sizeof(b));
    CHECK(STREQ(b, "0s"));

    lim_record_t r;
    CHECK(lim_parse(live_f, live_v, NF(live_f), &r));
    lim_fmt_header(&r, LIVE_UPDATED + 47, false, b, sizeof(b));
    CHECK(STREQ(b, "kai \xC2\xB7 47s ago"));
    lim_fmt_header(&r, LIVE_UPDATED + 22 * 60, true, b, sizeof(b));
    CHECK(STREQ(b, "no data 22m"));
    r.host[0] = '\0';
    lim_fmt_header(&r, LIVE_UPDATED + 120, false, b, sizeof(b));
    CHECK(STREQ(b, "2m ago"));

    lim_fmt_scoped_label(&r, b, sizeof(b));
    CHECK(STREQ(b, "Fable, 7 day"));
}

int main(void) {
    setenv("TZ", "America/Los_Angeles", 1);
    tzset();
    test_parse_live();
    test_parse_rejects();
    test_host_display_only();
    test_observe();
    test_scoped_carry();
    test_stale();
    test_pct_and_band();
    test_pace();
    test_fmt_reset();
    test_fmt_dur_and_header();
    printf("test_limstate: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
