/* test_wxstate.c — the weather filler's model (WI #4576, sprint 034).
 *
 * Held to the contract: it reads kdashdata's kdash-weather.schema.json at run
 * time and requires every example to parse and every x-counterexample to be
 * distrusted, so a contract change shows up here. The live record kweather
 * published on 2026-11-12 is the fixture for the rest. Times are checked in
 * America/Los_Angeles, rpi53's own zone. */
#include "wxstate.h"

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

static cJSON *schema(void) {
    char *text = slurp(KDASH_WEATHER_SCHEMA);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    free(text);
    return root;
}

static void test_schema_examples_parse(void) {
    cJSON *root = schema();
    CHECK(root != NULL);
    const cJSON *exs = cJSON_GetObjectItemCaseSensitive(root, "examples");
    CHECK(cJSON_GetArraySize(exs) == 3);
    const cJSON *ex;
    int i = 0;
    cJSON_ArrayForEach(ex, exs) {
        char *json = cJSON_PrintUnformatted(ex);
        wx_record_t r;
        int rc = wx_parse(json, &r);
        if (rc != 0) fprintf(stderr, "  example %d rejected\n", i);
        CHECK(rc == 0);
        free(json);
        i++;
    }

    wx_record_t r;
    char *json = cJSON_PrintUnformatted(cJSON_GetArrayItem(exs, 0));
    CHECK(wx_parse(json, &r) == 0);
    free(json);
    CHECK(r.ts == 1794528429.6 && STREQ(r.source, "open-meteo"));
    CHECK(r.now.has_temp && r.now.temp_f == 60.8 && r.now.humidity_pct == 63);
    CHECK(r.now.cond == WX_COND_CLOUDY && r.now.is_day);
    CHECK(r.ndays == 3 && r.days[1].cond == WX_COND_RAIN && r.days[1].pop_pct == 77);
    CHECK(r.days[2].cond == WX_COND_THUNDER && r.days[2].pop_pct == -1);

    /* NWS with no current reading: present-and-null everywhere it can be. */
    json = cJSON_PrintUnformatted(cJSON_GetArrayItem(exs, 1));
    CHECK(wx_parse(json, &r) == 0);
    free(json);
    CHECK(!r.now.has_temp && !r.now.has_feels && r.now.humidity_pct == -1);
    CHECK(r.now.cond == WX_COND_NONE);
    CHECK(!r.days[0].has_hi && r.days[0].has_lo && r.days[0].lo_f == 52);
    CHECK(r.days[1].has_hi && !r.days[1].has_lo);

    /* Polar night: every sun event null, negative temperatures. */
    json = cJSON_PrintUnformatted(cJSON_GetArrayItem(exs, 2));
    CHECK(wx_parse(json, &r) == 0);
    free(json);
    CHECK(r.sun.first_light == 0 && r.sun.sunrise == 0 && r.sun.sunset == 0 &&
          r.sun.last_light == 0);
    CHECK(r.now.temp_f == -4.0 && r.days[0].hi_f == -2 && r.days[0].lo_f == -11);
    cJSON_Delete(root);
}

static void test_counterexamples_distrusted(void) {
    cJSON *root = schema();
    const cJSON *cxs = cJSON_GetObjectItemCaseSensitive(root, "x-counterexamples");
    CHECK(cJSON_GetArraySize(cxs) == 5);
    const cJSON *cx;
    int i = 0;
    cJSON_ArrayForEach(cx, cxs) {
        char *json = cJSON_PrintUnformatted(cJSON_GetObjectItemCaseSensitive(cx, "record"));
        wx_record_t r;
        int rc = wx_parse(json, &r);
        if (rc == 0) fprintf(stderr, "  counterexample %d accepted\n", i);
        CHECK(rc == -1);
        free(json);
        i++;
    }
    cJSON_Delete(root);
}

static void test_rejects_other_breakage(void) {
    wx_record_t r;
    CHECK(wx_parse(NULL, &r) == -1);
    CHECK(wx_parse("not json", &r) == -1);
    CHECK(wx_parse("[]", &r) == -1);
    CHECK(wx_parse("{\"ts\":1}", &r) == -1); /* now, sun, days required */
    /* pop_pct out of range on a day, not only humidity on now. */
    const char *pop =
        "{\"ts\":1794528429.6,\"source\":\"nws\","
        "\"now\":{\"temp_f\":60.8,\"humidity_pct\":63,\"feels_f\":59.2,\"cond\":\"cloudy\","
        "\"is_day\":true},"
        "\"sun\":{\"first_light\":null,\"sunrise\":null,\"sunset\":null,\"last_light\":null},"
        "\"days\":[{\"date\":\"2026-11-12\",\"hi_f\":61,\"lo_f\":52,\"cond\":\"cloudy\","
        "\"pop_pct\":101}]}";
    CHECK(wx_parse(pop, &r) == -1);
    /* An extra field is fine (additionalProperties: true). */
    const char *extra =
        "{\"ts\":1794528429.6,\"source\":\"nws\",\"v\":1,"
        "\"now\":{\"temp_f\":null,\"humidity_pct\":null,\"feels_f\":null,\"cond\":null,"
        "\"is_day\":false,\"wind\":3},"
        "\"sun\":{\"first_light\":null,\"sunrise\":null,\"sunset\":null,\"last_light\":null},"
        "\"days\":[]}";
    CHECK(wx_parse(extra, &r) == 0 && r.ndays == 0);
}

static void test_live_record(void) {
    char *json = slurp(WX_LIVE_FIXTURE);
    CHECK(json != NULL);
    wx_record_t r;
    CHECK(wx_parse(json, &r) == 0);
    CHECK(STREQ(r.source, "nws") && r.ndays == 7);
    CHECK(r.now.cond == WX_COND_CLOUDY && !r.now.is_day);
    CHECK(r.days[2].pop_pct == 100);

    char buf[64];
    wx_fmt_time(r.sun.sunrise, buf, sizeof(buf));
    CHECK(STREQ(buf, "7:09 AM"));
    wx_fmt_time(r.sun.sunset, buf, sizeof(buf));
    CHECK(STREQ(buf, "4:36 PM"));
    wx_fmt_time(r.sun.first_light, buf, sizeof(buf));
    CHECK(STREQ(buf, "6:35 AM"));
    wx_fmt_time(r.sun.last_light, buf, sizeof(buf));
    CHECK(STREQ(buf, "5:10 PM"));
    wx_fmt_daylight(&r.sun, buf, sizeof(buf));
    CHECK(STREQ(buf, "9 h 28 m of daylight"));
    wx_fmt_feels(&r.now, buf, sizeof(buf));
    CHECK(STREQ(buf, "feels 56\xC2\xB0 \xC2\xB7 87% humidity"));
    CHECK(STREQ(wx_cond_name(r.now.cond), "Cloudy"));

    /* Weekdays: days[0] is always "Today"; 2026-11-13 was a Friday. */
    const char *want[] = {"Today", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};
    for (int i = 0; i < r.ndays; i++) {
        wx_fmt_weekday(&r.days[i], i, buf, sizeof(buf));
        CHECK(STREQ(buf, want[i]));
    }

    /* The shared scale at 3 columns (6 days): 38..64. */
    int lo, hi;
    CHECK(wx_temp_scale(&r, wx_days_shown(3, r.ndays), &lo, &hi));
    CHECK(lo == 38 && hi == 64);
    CHECK(wx_scale_y(64, lo, hi, 156, 330) == 156);
    CHECK(wx_scale_y(38, lo, hi, 156, 330) == 330);
    CHECK(wx_scale_y(51, lo, hi, 156, 330) == 243);
    /* 16:41 is 1001 minutes into the day. */
    CHECK(wx_day_frac(r.ts) == 1001 / 1440.0);
    free(json);
}

static void test_space(void) {
    /* Ken's rule: min(4, 10 - hosts), nothing at ten. */
    CHECK(wx_columns(7) == 3);
    CHECK(wx_columns(8) == 2);
    CHECK(wx_columns(9) == 1);
    CHECK(wx_columns(10) == 0);
    CHECK(wx_columns(6) == 4 && wx_columns(0) == 4);
    CHECK(wx_width(3) == 1111 && wx_width(4) == 1486 && wx_width(1) == WX_COL_W);
    CHECK(wx_width(0) == 0);
    /* Right after the hosts, then centred for six or fewer. */
    CHECK(wx_left(7) == 7 * 375);
    CHECK(wx_left(6) == 6 * 375);
    CHECK(wx_left(5) == 5 * 375 + 187);
    CHECK(wx_left(4) == 4 * 375 + 375);
    /* Zero hosts: the 4-column card sits in the middle of the row. */
    CHECK(wx_left(0) + wx_width(4) / 2 == (10 * 375 - 14) / 2);
    /* Days per width, never more than the record has. */
    CHECK(wx_days_shown(1, 8) == 0);
    CHECK(wx_days_shown(2, 8) == 3);
    CHECK(wx_days_shown(3, 8) == 6);
    CHECK(wx_days_shown(4, 8) == 8);
    CHECK(wx_days_shown(4, 7) == 7);
    CHECK(wx_days_shown(3, 1) == 1);
}

static void test_scale_with_nulls(void) {
    wx_record_t r;
    memset(&r, 0, sizeof(r));
    r.ndays = 2;
    r.days[0].has_lo = true;
    r.days[0].lo_f = 52;
    r.days[1].has_hi = true;
    r.days[1].hi_f = 59;
    int lo, hi;
    CHECK(wx_temp_scale(&r, 2, &lo, &hi) && lo == 52 && hi == 59);
    /* One value only: a flat scale draws at the bottom, never divides by 0. */
    CHECK(wx_temp_scale(&r, 1, &lo, &hi) && lo == 52 && hi == 52);
    CHECK(wx_scale_y(52, 52, 52, 156, 330) == 330);
    r.days[0].has_lo = false;
    CHECK(!wx_temp_scale(&r, 1, &lo, &hi));
}

static void test_formatting(void) {
    char buf[64];
    wx_fmt_deg(true, 60.8, buf, sizeof(buf));
    CHECK(STREQ(buf, "61\xC2\xB0"));
    wx_fmt_deg(true, -18.5, buf, sizeof(buf)); /* half up, as Math.round */
    CHECK(STREQ(buf, "-18\xC2\xB0"));
    wx_fmt_deg(true, -4.0, buf, sizeof(buf));
    CHECK(STREQ(buf, "-4\xC2\xB0"));
    wx_fmt_deg(false, 0, buf, sizeof(buf));
    CHECK(STREQ(buf, "\xE2\x80\x94"));
    wx_fmt_time(0, buf, sizeof(buf));
    CHECK(STREQ(buf, "\xE2\x80\x94"));

    wx_sun_t polar = {0};
    wx_fmt_daylight(&polar, buf, sizeof(buf));
    CHECK(STREQ(buf, "no sunrise today"));

    wx_now_t none = {.humidity_pct = -1};
    wx_fmt_feels(&none, buf, sizeof(buf));
    CHECK(STREQ(buf, "forecast only"));
    CHECK(STREQ(wx_cond_name(WX_COND_NONE), "No current reading"));
    CHECK(STREQ(wx_cond_name(WX_COND_THUNDER), "Thunderstorm"));

    wx_day_t bad = {.date = "soon"};
    wx_fmt_weekday(&bad, 2, buf, sizeof(buf));
    CHECK(STREQ(buf, "\xE2\x80\x94"));
}

static void test_freshness_and_age(void) {
    wx_record_t r = {.ts = 1794530513.4};
    char buf[64];
    wx_fmt_age(&r, r.ts + 20, buf, sizeof(buf));
    CHECK(STREQ(buf, "just now"));
    wx_fmt_age(&r, r.ts + 4 * 60 + 10, buf, sizeof(buf));
    CHECK(STREQ(buf, "4 m ago"));
    CHECK(!wx_is_stale(&r, r.ts + 3600));
    wx_fmt_age(&r, r.ts + 3600, buf, sizeof(buf));
    CHECK(STREQ(buf, "1 h ago"));
    CHECK(wx_is_stale(&r, r.ts + 3601));
    wx_fmt_age(&r, r.ts + 2 * 3600 + 5, buf, sizeof(buf));
    CHECK(STREQ(buf, "STALE \xC2\xB7 2 h"));
    wx_fmt_age(&r, r.ts + 3 * 86400, buf, sizeof(buf));
    CHECK(STREQ(buf, "STALE \xC2\xB7 3 d"));
    /* A writer clock a little ahead never reads negative. */
    wx_fmt_age(&r, r.ts - 30, buf, sizeof(buf));
    CHECK(STREQ(buf, "just now"));
}

static void test_observe(void) {
    wx_reset();
    CHECK(!wx_has_record());
    CHECK(wx_observe(NULL) == 0 && !wx_has_record()); /* never seen: not drawn */
    char *json = slurp(WX_LIVE_FIXTURE);
    CHECK(wx_observe(json) == 1 && wx_has_record());
    CHECK(wx_observe(json) == 0); /* same text: no reparse */
    CHECK(wx_observe(NULL) == 0 && wx_has_record()); /* absent keeps the last */
    /* A distrusted record is reported once and the last good one kept. */
    const char *bad = "{\"ts\":1794528429600}";
    CHECK(wx_observe(bad) == -1);
    CHECK(wx_observe(bad) == 0);
    CHECK(wx_has_record() && STREQ(wx_record()->source, "nws"));
    CHECK(wx_observe(json) == 1); /* and the good one is taken back */
    free(json);
    wx_reset();
}

int main(void) {
    setenv("TZ", "America/Los_Angeles", 1);
    tzset();
    test_schema_examples_parse();
    test_counterexamples_distrusted();
    test_rejects_other_breakage();
    test_live_record();
    test_space();
    test_scale_with_nulls();
    test_formatting();
    test_freshness_and_age();
    test_observe();
    fprintf(stderr, "test_wxstate: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
