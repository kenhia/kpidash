/* test_kkstate.c — the korg + klams card's model (WI #4613, sprint 038).
 *
 * Held to the contracts: it reads kdashdata's kdash-korg-pulse and
 * kdash-klams-status schemas at run time and requires every example to be
 * adopted and every x-counterexample to be either rejected or trimmed, so a
 * contract change shows up here. The two records live on rpi53 on 2026-11-14
 * are the other fixtures. Parsing and staleness are libkdash's; this holds
 * the card's own rules: the debounce, the scanner verdict (korg:4618 comment
 * 4672), the sparkline's points and the strings. */
#include "kkstate.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define NEAR(a, b) ((a) - (b) < 1e-9 && (b) - (a) < 1e-9)

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

static cJSON *load(const char *path) {
    char *text = slurp(path);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    free(text);
    return root;
}

/* ---- the contracts ---- */

static void test_korg_schema(void) {
    cJSON *root = load(KDASH_KORG_SCHEMA);
    CHECK(root != NULL);
    const cJSON *ex;
    int n = 0;
    cJSON_ArrayForEach(ex, cJSON_GetObjectItemCaseSensitive(root, "examples")) {
        char *s = cJSON_PrintUnformatted(ex);
        kk_reset();
        CHECK(kk_observe_korg(s) == 1);
        CHECK(kk_korg() != NULL);
        free(s);
        n++;
    }
    CHECK(n >= 3);
    n = 0;
    cJSON_ArrayForEach(ex, cJSON_GetObjectItemCaseSensitive(root, "x-counterexamples")) {
        char *s = cJSON_PrintUnformatted(cJSON_GetObjectItemCaseSensitive(ex, "record"));
        kk_reset();
        int r = kk_observe_korg(s);
        const kdash_korg_pulse_t *p = kk_korg();
        CHECK(r == -1 || (p && (p->by_project_skipped + p->history_skipped > 0 ||
                                p->by_project_truncated || p->history_truncated)));
        free(s);
        n++;
    }
    CHECK(n >= 1);
    cJSON_Delete(root);
}

static void test_klams_schema(void) {
    cJSON *root = load(KDASH_KLAMS_SCHEMA);
    CHECK(root != NULL);
    const cJSON *ex;
    int n = 0;
    cJSON_ArrayForEach(ex, cJSON_GetObjectItemCaseSensitive(root, "examples")) {
        char *s = cJSON_PrintUnformatted(ex);
        kk_reset();
        CHECK(kk_observe_klams(s) == 1);
        free(s);
        n++;
    }
    CHECK(n >= 3);
    n = 0;
    cJSON_ArrayForEach(ex, cJSON_GetObjectItemCaseSensitive(root, "x-counterexamples")) {
        char *s = cJSON_PrintUnformatted(cJSON_GetObjectItemCaseSensitive(ex, "record"));
        kk_reset();
        int r = kk_observe_klams(s);
        const kdash_klams_status_t *k = kk_klams();
        CHECK(r == -1 || (k && (k->scanners_skipped + k->subsystems_skipped +
                                        k->new_per_day_skipped > 0 ||
                                    k->scanners_truncated || k->subsystems_truncated ||
                                    k->new_per_day_truncated)));
        free(s);
        n++;
    }
    CHECK(n >= 1);
    cJSON_Delete(root);
}

/* ---- the live records ---- */

static void test_live_korg(void) {
    char *s = slurp(KORG_LIVE_FIXTURE);
    CHECK(s != NULL);
    kk_reset();
    CHECK(kk_korg() == NULL);
    CHECK(kk_korg_stale(1794618904.0)); /* no record: stale */
    CHECK(kk_observe_korg(s) == 1);
    CHECK(kk_observe_korg(s) == 0); /* the same poll again */
    const kdash_korg_pulse_t *p = kk_korg();
    CHECK(p->open == 31);
    CHECK(p->awaiting == 1);
    CHECK(p->active == 2);
    CHECK(p->by_project_count == 18);
    CHECK(STREQ(p->by_project[0].name, "kligd"));
    CHECK(p->by_project[0].open == 5);
    CHECK(p->other_open == 0);
    CHECK(p->history_count == 30);

    /* 300 s cadence plus 60 s grace. */
    CHECK(!kk_korg_stale(p->ts + 300));
    CHECK(!kk_korg_stale(p->ts + 360));
    CHECK(kk_korg_stale(p->ts + 362));

    /* The key going absent, or an off-contract value, keeps the record. */
    CHECK(kk_observe_korg(NULL) == 0);
    CHECK(kk_observe_korg("{\"ts\":1}") == -1);
    CHECK(kk_korg() != NULL && kk_korg()->open == 31);
    free(s);
}

static void test_live_klams(void) {
    char *s = slurp(KLAMS_LIVE_FIXTURE);
    CHECK(s != NULL);
    kk_reset();
    CHECK(kk_observe_klams(s) == 1);
    const kdash_klams_status_t *k = kk_klams();
    CHECK(k->health == KDASH_KLAMS_OK);
    CHECK(kk_klams_health() == KDASH_KLAMS_OK);
    CHECK(k->knowledge == 228956);
    CHECK(k->scanners_count == 2);
    CHECK(k->subsystems_count == 4);
    for (int i = 0; i < 4; i++)
        CHECK(!kk_subsystem_down(i));

    /* 60 s cadence plus 60 s grace. */
    CHECK(!kk_klams_stale(k->ts + 120));
    CHECK(kk_klams_stale(k->ts + 122));

    /* kai: no last_ran_at today (klams WI 4639) — neutral, a write age. */
    const double now = k->ts + 30;
    kk_scan_view_t v;
    kk_scan_view(&k->scanners[0], now, &v);
    CHECK(STREQ(k->scanners[0].name, "kai"));
    CHECK(v.state == KK_SCAN_NEUTRAL);
    CHECK(STREQ(v.text, "wrote 3h ago"));
    CHECK(v.frac == 0); /* 3.9 h since the write: the hour is spent */

    /* kubs0: ran 3 minutes before the record, wrote 3 days before it.
     * Healthy — the write age is secondary text, never the alarm. */
    kk_scan_view(&k->scanners[1], now, &v);
    CHECK(STREQ(k->scanners[1].name, "kubs0"));
    CHECK(v.state == KK_SCAN_OK);
    CHECK(STREQ(v.text, "wrote 3d ago"));
    CHECK(v.frac > 0.93 && v.frac < 0.94); /* 1 - 228.6/3600 */
    free(s);
}

/* ---- scanner rows ---- */

static void test_scan_view(void) {
    kdash_klams_scanner_t sc = {.last_wrote_at = 1000, .last_ran_at = 10000,
                                .expected_interval_s = 3600};
    kk_scan_view_t v;

    kk_scan_view(&sc, 10000 + 900, &v); /* a quarter of the hour gone */
    CHECK(v.state == KK_SCAN_OK);
    CHECK(NEAR(v.frac, 0.75));

    kk_scan_view(&sc, 10000 + 3660, &v); /* interval plus grace: still on time */
    CHECK(v.state == KK_SCAN_OK);
    CHECK(v.frac == 0);

    kk_scan_view(&sc, 10000 + 3661, &v); /* one second past: stalled */
    CHECK(v.state == KK_SCAN_STALLED);
    CHECK(STREQ(v.text, "ran 1h ago"));
    CHECK(v.frac == 0);

    sc.last_ran_at = 0; /* no source for it: neutral, by the write */
    kk_scan_view(&sc, 1000 + 1800, &v);
    CHECK(v.state == KK_SCAN_NEUTRAL);
    CHECK(NEAR(v.frac, 0.5));
    CHECK(STREQ(v.text, "wrote 30m ago"));

    kk_scan_view(&sc, 1000 + 86400 * 5, &v); /* never red, however old */
    CHECK(v.state == KK_SCAN_NEUTRAL);
    CHECK(STREQ(v.text, "wrote 5d ago"));

    sc.last_wrote_at = 0;
    kk_scan_view(&sc, 5000, &v);
    CHECK(v.state == KK_SCAN_NEUTRAL);
    CHECK(STREQ(v.text, "never wrote"));
    CHECK(v.frac == 0);

    sc.last_ran_at = 4000; /* ran, never wrote: healthy */
    kk_scan_view(&sc, 4060, &v);
    CHECK(v.state == KK_SCAN_OK);
    CHECK(STREQ(v.text, "never wrote"));
}

/* ---- the debounce ---- */

static char *klams_rec(double ts, const char *health, bool rerank_ok) {
    static char buf[1024];
    if (strcmp(health, "down") == 0)
        snprintf(buf, sizeof(buf),
                 "{\"ts\":%.1f,\"expected_refresh_s\":60,\"health\":\"down\",\"version\":null,"
                 "\"uptime_s\":null,\"knowledge\":null,\"facts\":null,\"events\":null,"
                 "\"scanners\":[],\"subsystems\":[],\"queue_depth\":null,"
                 "\"queue_capacity\":null,\"backup_at\":null,\"new_per_day\":[]}",
                 ts);
    else
        snprintf(buf, sizeof(buf),
                 "{\"ts\":%.1f,\"expected_refresh_s\":60,\"health\":\"%s\",\"version\":\"0.1.58\","
                 "\"uptime_s\":1,\"knowledge\":228934,\"facts\":40,\"events\":39,"
                 "\"scanners\":[],\"subsystems\":[{\"name\":\"postgres\",\"ok\":true},"
                 "{\"name\":\"rerank\",\"ok\":%s}],\"queue_depth\":0,"
                 "\"queue_capacity\":1024,\"backup_at\":null,\"new_per_day\":[]}",
                 ts, health, rerank_ok ? "true" : "false");
    return buf;
}

static void test_debounce(void) {
    kk_reset();
    CHECK(kk_observe_klams(klams_rec(100, "ok", true)) == 1);

    /* One degraded record between two ok ones never reaches the panel. */
    kk_observe_klams(klams_rec(160, "degraded", false));
    CHECK(kk_klams_health() == KDASH_KLAMS_OK);
    CHECK(!kk_subsystem_down(1));
    kk_observe_klams(klams_rec(220, "ok", true));
    CHECK(kk_klams_health() == KDASH_KLAMS_OK);
    CHECK(!kk_subsystem_down(1));

    /* The same miss on two consecutive records does. */
    kk_observe_klams(klams_rec(280, "degraded", false));
    CHECK(!kk_subsystem_down(1));
    CHECK(kk_klams_health() == KDASH_KLAMS_OK);
    /* The same record polled again is not a second record. */
    CHECK(kk_observe_klams(klams_rec(280, "degraded", false)) == 0);
    CHECK(!kk_subsystem_down(1));
    kk_observe_klams(klams_rec(340, "degraded", false));
    CHECK(kk_subsystem_down(1));
    CHECK(!kk_subsystem_down(0));
    CHECK(kk_klams_health() == KDASH_KLAMS_DEGRADED);

    /* Recovery shows at once. */
    kk_observe_klams(klams_rec(400, "ok", true));
    CHECK(!kk_subsystem_down(1));
    CHECK(kk_klams_health() == KDASH_KLAMS_OK);

    /* One down record is not adopted: the values stay. */
    CHECK(kk_observe_klams(klams_rec(460, "down", true)) == 0);
    CHECK(kk_klams()->knowledge == 228934);
    CHECK(kk_klams()->ts == 400);
    CHECK(kk_klams_health() == KDASH_KLAMS_OK);
    /* Two are: klams is down, its values blank (null). */
    CHECK(kk_observe_klams(klams_rec(520, "down", true)) == 1);
    CHECK(kk_klams_health() == KDASH_KLAMS_DOWN);
    CHECK(kk_klams()->knowledge == KDASH_COUNT_NULL);
    CHECK(kk_klams()->subsystems_count == 0);
    /* And it comes back at once. */
    CHECK(kk_observe_klams(klams_rec(580, "ok", true)) == 1);
    CHECK(kk_klams_health() == KDASH_KLAMS_OK);

    /* A first-ever record is believed as it stands, down included. */
    kk_reset();
    CHECK(kk_observe_klams(klams_rec(100, "down", true)) == 1);
    CHECK(kk_klams_health() == KDASH_KLAMS_DOWN);
}

/* ---- the 30-day line ---- */

static void test_spark(void) {
    kdash_korg_pulse_t p;
    memset(&p, 0, sizeof(p));
    kk_point_t pt[KDASH_KORG_HISTORY_MAX];

    CHECK(kk_korg_spark(&p, pt, 30) == 0); /* empty: no line */

    snprintf(p.history[0].date, sizeof(p.history[0].date), "2026-11-13");
    p.history[0].open = 25;
    p.history_count = 1;
    CHECK(kk_korg_spark(&p, pt, 30) == 1); /* one point: the card draws no line */
    CHECK(NEAR(pt[0].x, 1));
    CHECK(NEAR(pt[0].y, 1));

    /* Placed by date: a missing day leaves a gap rather than sliding. Across
     * a month end, too. */
    const char *dates[] = {"2026-10-15", "2026-10-31", "2026-11-01", "2026-11-13"};
    const int vals[] = {100, 50, 0, 25};
    for (int i = 0; i < 4; i++) {
        snprintf(p.history[i].date, sizeof(p.history[i].date), "%s", dates[i]);
        p.history[i].open = vals[i];
    }
    p.history_count = 4;
    CHECK(kk_korg_spark(&p, pt, 30) == 4);
    CHECK(NEAR(pt[0].x, 0));
    CHECK(NEAR(pt[1].x, 16.0 / 29));
    CHECK(NEAR(pt[2].x, 17.0 / 29));
    CHECK(NEAR(pt[3].x, 1));
    CHECK(NEAR(pt[0].y, 1));
    CHECK(NEAR(pt[1].y, 0.5));
    CHECK(NEAR(pt[2].y, 0));

    /* Older than the span, or unreadable: dropped. */
    snprintf(p.history[0].date, sizeof(p.history[0].date), "2026-10-14");
    snprintf(p.history[1].date, sizeof(p.history[1].date), "2026-1x-31");
    CHECK(kk_korg_spark(&p, pt, 30) == 2);
    CHECK(NEAR(pt[0].x, 17.0 / 29));
    CHECK(NEAR(pt[0].y, 0));
    CHECK(NEAR(pt[1].y, 1)); /* the max is of what is kept */

    /* All zero: a flat line on the floor, not a division by zero. */
    p.history[2].open = p.history[3].open = 0;
    CHECK(kk_korg_spark(&p, pt, 30) == 2);
    CHECK(pt[1].y == 0);

    /* The live series: 30 days, today last at the right edge. */
    char *s = slurp(KORG_LIVE_FIXTURE);
    kk_reset();
    kk_observe_korg(s);
    CHECK(kk_korg_spark(kk_korg(), pt, 30) == 30);
    CHECK(NEAR(pt[0].x, 0));
    CHECK(NEAR(pt[29].x, 1));
    CHECK(NEAR(pt[9].y, 1)); /* 2026-10-24's 165 is the max */
    CHECK(NEAR(pt[29].y, 31.0 / 165));
    free(s);
}

/* ---- strings ---- */

static void test_fmt(void) {
    char b[32];
    kk_fmt_count(228956, b, sizeof(b));
    CHECK(STREQ(b, "228,956"));
    kk_fmt_count(1234567, b, sizeof(b));
    CHECK(STREQ(b, "1,234,567"));
    kk_fmt_count(999, b, sizeof(b));
    CHECK(STREQ(b, "999"));
    kk_fmt_count(0, b, sizeof(b));
    CHECK(STREQ(b, "0"));
    kk_fmt_count(KDASH_COUNT_NULL, b, sizeof(b));
    CHECK(STREQ(b, ""));
    kk_fmt_count(1234567, b, 6); /* truncates, never overruns */
    CHECK(strlen(b) == 5);

    kk_fmt_dur(-5, b, sizeof(b));
    CHECK(STREQ(b, "0s"));
    kk_fmt_dur(59, b, sizeof(b));
    CHECK(STREQ(b, "59s"));
    kk_fmt_dur(14 * 60 + 5, b, sizeof(b));
    CHECK(STREQ(b, "14m"));
    kk_fmt_dur(3 * 86400 + 5, b, sizeof(b));
    CHECK(STREQ(b, "3d"));

    kk_fmt_nodata(1000, 1000 + 14 * 60, b, sizeof(b));
    CHECK(STREQ(b, "no data 14m"));
}

int main(void) {
    test_korg_schema();
    test_klams_schema();
    test_live_korg();
    test_live_klams();
    test_scan_view();
    test_debounce();
    test_spark();
    test_fmt();
    kk_reset();
    printf("test_kkstate: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
