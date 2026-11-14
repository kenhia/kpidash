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

/* A read time at which both fixtures above are fresh. */
#define T_READ 1794617010.0

/* HGETALL claude:limits on rpi53, 2026-11-14 02:48:52Z: kai's oauth poll once
 * the plan had no five-hour window (CD-29's five_hour_none, sprint 039). */
static const char *none_f[] = {
    "five_hour_pct", "seven_day_pct", "updated_at", "host", "source", "expected_refresh_s", "ts",
    "five_hour_resets_at", "seven_day_resets_at", "scoped_model", "scoped_pct", "scoped_active",
    "scoped_count", "scoped_updated_at", "scoped_expected_refresh_s", "scoped_resets_at",
    "five_hour_none",
};
static const char *none_v[] = {
    "0", "11", "1794621843", "kai", "oauth", "300", "1794621844",
    "0", "1795089599", "Fable", "13", "1",
    "1", "1794621843", "300", "1795089599",
    "1",
};
#define NONE_UPDATED 1794621843.0

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
    CHECK(lim_observe(NULL, NULL, 0, T_READ) == 0); /* absent, nothing seen: not drawn */
    CHECK(!lim_has_record());

    CHECK(lim_observe(live_f, live_v, NF(live_f), T_READ) == 1);
    CHECK(lim_has_record());
    CHECK(lim_observe(live_f, live_v, NF(live_f), T_READ) == 0); /* the same again */

    /* A brief gap keeps the record. */
    CHECK(lim_observe(NULL, NULL, 0, T_READ) == 0);
    CHECK(lim_has_record());
    CHECK(lim_record()->l.five_hour_pct == 8);

    /* A rejected record keeps the previous one. */
    const char *f[] = {"five_hour_pct"};
    const char *v[] = {"x"};
    CHECK(lim_observe(f, v, NF(f), T_READ) == -1);
    CHECK(lim_record()->l.five_hour_pct == 8);
    CHECK(STREQ(lim_record()->host, "kai"));
}

/* A statusline write carries no scoped set. The bar must not vanish and
 * reflow the card: it keeps its last numbers and greys by its own stamp. */
static void test_scoped_carry(void) {
    lim_reset();
    CHECK(lim_observe(live_f, live_v, NF(live_f), T_READ) == 1);
    CHECK(lim_observe(bare_f, bare_v, NF(bare_f), T_READ) == 1);
    const lim_record_t *r = lim_record();
    CHECK(r->l.five_hour_pct == 42 && r->l.seven_day_pct == 61);
    CHECK(STREQ(r->host, "cleo"));
    CHECK(r->l.scoped_valid);
    CHECK(STREQ(r->l.scoped_model, "Fable"));
    CHECK(r->l.scoped_pct == 11);
    CHECK(r->l.scoped_updated_at == LIVE_UPDATED); /* its OWN stamp, not the headline's */

    /* Repeating the bare write is still "nothing changed". */
    CHECK(lim_observe(bare_f, bare_v, NF(bare_f), T_READ) == 0);

    /* With no scoped set ever seen there is nothing to carry. */
    lim_reset();
    CHECK(lim_observe(bare_f, bare_v, NF(bare_f), T_READ) == 1);
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
    lim_fmt_scoped_ring_label(&r, true, b, sizeof(b));
    CHECK(STREQ(b, "FABLE \xC2\xB7 7 DAY"));
    lim_fmt_scoped_ring_label(&r, false, b, sizeof(b));
    CHECK(STREQ(b, "FABLE"));
    memcpy(r.l.scoped_model, "claude-opus-5", 14); /* the schema's own example */
    lim_fmt_scoped_ring_label(&r, true, b, sizeof(b));
    CHECK(STREQ(b, "CLAUDE-OPUS-5 \xC2\xB7 7 DAY"));
}

/* ---- Layout mode (WI #4631, sprint 039) ---- */

/* A record stamped `upd`: none = -1 writes no five_hour_none field (a writer
 * that predates CD-29), 0 or 1 writes it; `scoped` adds a scoped set. */
static const char *mf[16], *mv[16];
static char mstore[16][24];
static int mrec(double upd, int none, bool scoped) {
    int n = 0;
#define PUT(field, fmt, val)                                                                       \
    do {                                                                                           \
        snprintf(mstore[n], sizeof(mstore[n]), fmt, val);                                          \
        mf[n] = field;                                                                             \
        mv[n] = mstore[n];                                                                         \
        n++;                                                                                       \
    } while (0)
    PUT("five_hour_pct", "%d", none == 1 ? 0 : 9);
    PUT("seven_day_pct", "%d", 11);
    PUT("updated_at", "%.0f", upd);
    PUT("expected_refresh_s", "%d", 300);
    if (none >= 0)
        PUT("five_hour_none", "%d", none);
    if (scoped) {
        PUT("scoped_model", "%s", "Fable");
        PUT("scoped_pct", "%d", 13);
        PUT("scoped_updated_at", "%.0f", upd);
        PUT("scoped_expected_refresh_s", "%d", 300);
    }
#undef PUT
    return n;
}

/* Feed one record read 10 s after its stamp, so it is fresh. */
static int feed(double upd, int none, bool scoped) {
    int n = mrec(upd, none, scoped);
    return lim_observe(mf, mv, n, upd + 10);
}

static void test_mode_of(void) {
    lim_record_t r;
    CHECK(lim_parse(none_f, none_v, NF(none_f), &r));
    CHECK(r.l.five_hour_none && r.l.five_hour_pct == 0);
    CHECK(lim_mode_of(&r) == LIM_MODE_SCOPED);

    CHECK(lim_parse(live_f, live_v, NF(live_f), &r)); /* no flag: a window */
    CHECK(lim_mode_of(&r) == LIM_MODE_FIVE);

    int n = mrec(1000, 1, false);
    CHECK(lim_parse(mf, mv, n, &r));
    CHECK(lim_mode_of(&r) == LIM_MODE_SEVEN);
    n = mrec(1000, 0, true); /* stated: a window */
    CHECK(lim_parse(mf, mv, n, &r));
    CHECK(lim_mode_of(&r) == LIM_MODE_FIVE);
    n = mrec(1000, 0, false);
    CHECK(lim_parse(mf, mv, n, &r));
    CHECK(lim_mode_of(&r) == LIM_MODE_FIVE);

    /* Unknown or invalid never reads as "no window". */
    memset(&r, 0, sizeof(r));
    r.l.five_hour_none = true;
    CHECK(lim_mode_of(&r) == LIM_MODE_FIVE);
}

static void test_mode_first_record(void) {
    lim_reset();
    CHECK(lim_mode() == LIM_MODE_FIVE);
    CHECK(lim_observe(none_f, none_v, NF(none_f), NONE_UPDATED + 47) == 1);
    CHECK(lim_mode() == LIM_MODE_SCOPED); /* nothing drawn yet, so nothing to flap */

    /* Even a stale first record: the card has to be drawn some way. */
    lim_reset();
    CHECK(lim_observe(none_f, none_v, NF(none_f), NONE_UPDATED + 3600) == 1);
    CHECK(lim_mode() == LIM_MODE_SCOPED);
}

static void test_mode_debounce(void) {
    lim_reset();
    CHECK(feed(1000, -1, true) == 1);
    CHECK(lim_mode() == LIM_MODE_FIVE);

    /* One record read many times by the 1 s poll is one vote. */
    for (int i = 0; i < 5; i++)
        feed(1100, 1, true);
    CHECK(lim_mode() == LIM_MODE_FIVE);
    CHECK(feed(1200, 1, true) == 1);
    CHECK(lim_mode() == LIM_MODE_FIVE); /* two records */
    CHECK(feed(1300, 1, true) == 1);
    CHECK(lim_mode() == LIM_MODE_SCOPED); /* the third */

    /* Two writers disagreeing record by record never move it (kai with the
     * flag, komarchy's older publisher without). */
    for (int i = 0; i < 6; i++) {
        feed(1400 + 200 * i, -1, true);
        feed(1500 + 200 * i, 1, true);
        CHECK(lim_mode() == LIM_MODE_SCOPED);
    }

    /* Two in a row, then one agreeing record: the count starts over. */
    feed(3000, -1, true);
    feed(3100, -1, true);
    feed(3200, 1, true);
    feed(3300, -1, true);
    feed(3400, -1, true);
    CHECK(lim_mode() == LIM_MODE_SCOPED);
    feed(3500, -1, true);
    CHECK(lim_mode() == LIM_MODE_FIVE);
}

static void test_mode_not_on_stale_or_missing(void) {
    lim_reset();
    CHECK(feed(1000, -1, true) == 1);

    /* Each record already stale when first read: no vote. */
    for (int i = 1; i <= 4; i++) {
        int n = mrec(1000 + 100 * i, 1, true);
        CHECK(lim_observe(mf, mv, n, 1000 + 100 * i + 361) == 1);
    }
    CHECK(lim_mode() == LIM_MODE_FIVE);

    /* Absent and rejected: no vote either, and the streak survives a gap. */
    feed(2000, 1, true);
    feed(2100, 1, true);
    CHECK(lim_observe(NULL, NULL, 0, 2111) == 0);
    const char *f[] = {"five_hour_pct"};
    const char *v[] = {"x"};
    CHECK(lim_observe(f, v, NF(f), 2111) == -1);
    CHECK(lim_mode() == LIM_MODE_FIVE);
    feed(2200, 1, true);
    CHECK(lim_mode() == LIM_MODE_SCOPED);
}

static void test_mode_scoped_and_seven(void) {
    /* A statusline write with no scoped set carries the last one, so it
     * still votes SCOPED: the right ring stays and greys by its own stamp. */
    lim_reset();
    feed(1000, 1, true);
    CHECK(lim_mode() == LIM_MODE_SCOPED);
    for (int i = 1; i <= 4; i++)
        feed(1000 + 100 * i, 1, false);
    CHECK(lim_mode() == LIM_MODE_SCOPED);
    CHECK(lim_record()->l.scoped_valid);
    CHECK(lim_record()->l.scoped_updated_at == 1000);

    /* No scoped set ever seen: one ring, until a scoped set has held for
     * three records. */
    lim_reset();
    feed(1000, 1, false);
    CHECK(lim_mode() == LIM_MODE_SEVEN);
    feed(1100, 1, true);
    CHECK(lim_mode() == LIM_MODE_SEVEN);
    feed(1200, 1, false); /* carried: still a SCOPED vote */
    CHECK(lim_mode() == LIM_MODE_SEVEN);
    feed(1300, 1, false);
    CHECK(lim_mode() == LIM_MODE_SCOPED);
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
    test_mode_of();
    test_mode_first_record();
    test_mode_debounce();
    test_mode_not_on_stale_or_missing();
    test_mode_scoped_and_seven();
    printf("test_limstate: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
