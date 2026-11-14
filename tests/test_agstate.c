/* test_agstate.c — the Agents card's model (WI #4695, sprint 041).
 *
 * Held to the contract: it reads kdashdata's kdash-agents schema at run time
 * and requires every example to be adopted and every x-counterexample to be
 * rejected, skipped or unlinked, so a contract change shows up here. The
 * clock is injected and the zone pinned to America/Los_Angeles (rpi53's), so
 * the window, the minute step and the hour grid are exercised without
 * waiting for them. */
#include "agstate.h"

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

static time_t local(int y, int m, int d, int hh, int mm, int ss) {
    struct tm tm = {0};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_hour = hh;
    tm.tm_min = mm;
    tm.tm_sec = ss;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

/* ---- a record builder: bars in hours on the 13th's clock, as the mock-up ---- */

typedef struct {
    const char *id, *host, *name;
    double a, b; /* hours from local midnight on the 13th; negative is the 12th */
    const char *state;
    int shipped, overseer;
    const char *parent;
} spec_t;

static time_t g_midnight;

static char *build(double ts, const spec_t *s, int n) {
    size_t cap = 256 + (size_t)n * 400;
    char *buf = malloc(cap);
    size_t k = (size_t)snprintf(buf, cap,
                                "{\"ts\":%.1f,\"expected_refresh_s\":60,\"window_s\":86400,\"bars\":[",
                                ts);
    for (int i = 0; i < n; i++) {
        char parent[64] = "";
        if (s[i].parent)
            snprintf(parent, sizeof(parent), ",\"parent\":\"%s\"", s[i].parent);
        k += (size_t)snprintf(buf + k, cap - k,
                              "%s{\"id\":\"%s\",\"host\":\"%s\",\"name\":\"%s\",\"start\":%.1f,"
                              "\"end\":%.1f,\"state\":\"%s\",\"shipped\":%s,\"overseer\":%s%s}",
                              i ? "," : "", s[i].id, s[i].host, s[i].name,
                              (double)g_midnight + s[i].a * 3600, (double)g_midnight + s[i].b * 3600,
                              s[i].state, s[i].shipped ? "true" : "false",
                              s[i].overseer ? "true" : "false", parent);
    }
    snprintf(buf + k, cap - k, "]}");
    return buf;
}

/* The mock-up's day (agents-mockups.html, `S`), now = 18:42 on the 13th.
 * Running bars end at the publish time, which is ts. */
#define NOW_H 18.7
static const spec_t MOCK[] = {
    {"o1", "cleo", "kpidash-refresh", -5.0, 1.7, "done", 0, 1, NULL},
    {"b1", "kubs0", "klams", -3.6, -2.3, "done", 1, 0, "o1"},
    {"b2", "kai", "kdashdata", -2.2, -0.9, "done", 1, 0, "o1"},
    {"b3", "kai", "korg", -0.7, 0.4, "done", 1, 0, "o1"},
    {"b4", "kubs0", "k-homelab", -0.4, 1.1, "done", 1, 0, "o1"},
    {"b5", "kai", "kpidash", 0.5, 1.6, "done", 1, 0, "o1"},
    {"b6", "kimac", "kloom", -3.9, -1.8, "done", 0, 0, NULL},
    {"b7", "kai", "korg", 6.0, 7.4, "done", 0, 0, NULL},
    {"b8", "thrall", "kpong", 7.2, 8.6, "done", 1, 0, NULL},
    {"o2", "cleo", "low-hanging-fruit", 9.0, 14.5, "done", 0, 1, NULL},
    {"c1", "kai", "kmon", 9.3, 10.4, "done", 1, 0, "o2"},
    {"c2", "kubs0", "agent-skills", 9.9, 11.9, "done", 1, 0, "o2"},
    {"c3", "kai", "kfo", 10.6, 11.5, "done", 1, 0, "o2"},
    {"c4", "kai", "kweather", 11.8, 13.3, "done", 1, 0, "o2"},
    {"c5", "kubs0", "k-homelab", 12.4, 14.0, "done", 1, 0, "o2"},
    {"b9", "cleo", "kctrldeck", 14.8, 16.4, "done", 1, 0, NULL},
    {"ba", "cleo", "kvimh", 15.3, NOW_H, "working", 0, 0, NULL},
    {"bb", "kai", "kloc", 16.6, 17.6, "done", 0, 0, NULL},
    {"bc", "cleo", "khow", 17.1, NOW_H, "waiting", 0, 0, NULL},
};
#define NMOCK ((int)(sizeof(MOCK) / sizeof(MOCK[0])))

static const ag_bar_t *find(const ag_view_t *v, const char *name, int nth) {
    for (int i = 0; i < v->nbars; i++)
        if (STREQ(v->bars[i].name, name) && nth-- == 0)
            return &v->bars[i];
    return NULL;
}

static bool overlaps(const ag_view_t *v) {
    for (int i = 0; i < v->nbars; i++)
        for (int j = i + 1; j < v->nbars; j++) {
            const ag_bar_t *a = &v->bars[i], *b = &v->bars[j];
            if (a->row == b->row && a->x0 < b->x1 + AG_PACK_GAP_PX && b->x0 < a->x1 + AG_PACK_GAP_PX)
                return true;
        }
    return false;
}

static ag_view_t g_v; /* ~20 KB: not on the stack */

/* ---- the contract ---- */

static void test_schema(void) {
    char *text = slurp(KDASH_AGENTS_SCHEMA);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    free(text);
    CHECK(root != NULL);
    const cJSON *ex;
    int n = 0;
    cJSON_ArrayForEach(ex, cJSON_GetObjectItemCaseSensitive(root, "examples")) {
        char *s = cJSON_PrintUnformatted(ex);
        ag_reset();
        CHECK(ag_observe(s) == 1);
        CHECK(ag_record() != NULL);
        /* Every example draws, at its own ts: no NOFEED, nothing hidden. */
        ag_view((time_t)ag_record()->ts, &g_v);
        CHECK(g_v.status != AG_NOFEED);
        CHECK(g_v.hidden == 0);
        free(s);
        n++;
    }
    CHECK(n >= 3);
    n = 0;
    cJSON_ArrayForEach(ex, cJSON_GetObjectItemCaseSensitive(root, "x-counterexamples")) {
        char *s = cJSON_PrintUnformatted(cJSON_GetObjectItemCaseSensitive(ex, "record"));
        ag_reset();
        int r = ag_observe(s);
        const kdash_agents_t *a = ag_record();
        CHECK(r == -1 || (a && ((a->bars_skipped > 0 && a->bars_count == 0) ||
                                a->parents_dropped > 0)));
        free(s);
        n++;
    }
    CHECK(n >= 1);
    cJSON_Delete(root);
}

/* ---- the mock-up's day ---- */

static void test_mockup(void) {
    const time_t now = local(2026, 11, 13, 18, 42, 0);
    ag_reset();
    char *s = build((double)now, MOCK, NMOCK);
    CHECK(ag_observe(s) == 1);
    CHECK(ag_observe(s) == 0); /* unchanged value: a strcmp, no new generation */
    free(s);
    ag_view(now, &g_v);
    CHECK(g_v.status == AG_BARS);
    CHECK(!g_v.stale);
    CHECK(g_v.hint[0] == '\0');
    CHECK(g_v.nbars == NMOCK);
    CHECK(g_v.hidden == 0);
    CHECK(g_v.rows == 5); /* the target render: kloom alone on the fifth row */
    CHECK(g_v.row_h == AG_ROW_H_MAX);
    CHECK(!overlaps(&g_v));

    const ag_bar_t *o1 = find(&g_v, "kpidash-refresh", 0), *o2 = find(&g_v, "low-hanging-fruit", 0);
    CHECK(o1 && o1->row == 0 && o1->overseer && o1->host == AG_HOST_CLEO);
    CHECK(o2 && o2->row == 0);
    /* The legs, as the target draws them. */
    CHECK(find(&g_v, "klams", 0)->row == 1);
    CHECK(find(&g_v, "kdashdata", 0)->row == 2);
    CHECK(find(&g_v, "korg", 0)->row == 1);
    CHECK(find(&g_v, "k-homelab", 0)->row == 2);
    CHECK(find(&g_v, "kpidash", 0)->row == 3);
    CHECK(find(&g_v, "kloom", 0)->row == 4);
    CHECK(find(&g_v, "kmon", 0)->row == 1);
    CHECK(find(&g_v, "agent-skills", 0)->row == 2);
    CHECK(find(&g_v, "kfo", 0)->row == 1);
    CHECK(find(&g_v, "kweather", 0)->row == 1);
    CHECK(find(&g_v, "k-homelab", 1)->row == 2);
    /* Unparented bars go in the highest free row. */
    CHECK(find(&g_v, "korg", 1)->row == 0);
    CHECK(find(&g_v, "kpong", 0)->row == 1);
    CHECK(find(&g_v, "kctrldeck", 0)->row == 0);
    CHECK(find(&g_v, "kloc", 0)->row == 0);
    CHECK(find(&g_v, "kvimh", 0)->row == 1);
    CHECK(find(&g_v, "khow", 0)->row == 2);

    /* Every leg points at its overseer, and sits below it. */
    for (int i = 0; i < g_v.nbars; i++) {
        const ag_bar_t *b = &g_v.bars[i];
        if (b->parent < 0)
            continue;
        const ag_bar_t *p = &g_v.bars[b->parent];
        CHECK(p->overseer);
        CHECK(b->row > p->row);
        CHECK(p == o1 || p == o2);
    }
    CHECK(find(&g_v, "kloom", 0)->parent == -1);

    /* Hosts: kimac and thrall are "other". */
    CHECK(find(&g_v, "kloom", 0)->host == AG_HOST_OTHER);
    CHECK(find(&g_v, "kpong", 0)->host == AG_HOST_OTHER);
    CHECK(find(&g_v, "klams", 0)->host == AG_HOST_KUBS0);
    CHECK(find(&g_v, "korg", 0)->host == AG_HOST_KAI);

    /* Running bars reach the now line, square; finished ones do not. */
    const ag_bar_t *kv = find(&g_v, "kvimh", 0), *kh = find(&g_v, "khow", 0);
    CHECK(kv->running && kv->x1 == AG_PLOT_X1 && kv->state == KDASH_AGENT_WORKING);
    CHECK(kh->running && kh->x1 == AG_PLOT_X1 && kh->state == KDASH_AGENT_WAITING);
    CHECK(!find(&g_v, "kloc", 0)->running);
    CHECK(find(&g_v, "kloc", 0)->x1 < AG_PLOT_X1);
    CHECK(find(&g_v, "kctrldeck", 0)->shipped);

    /* The axis: 24 h over 2456 px. kpidash-refresh started at 19:00 on the
     * 12th, 18 min after the window opened at 18:42: x 36 + 0.3/24 * 2456. */
    CHECK(o1->x0 == 36 + 31);
    /* Rows from y 104, 76 px with 10 px between. */
    CHECK(o1->y == 104 && o1->h == 76);
    CHECK(find(&g_v, "kloom", 0)->y == 104 + 4 * 86);

    /* The hour grid: every 2 hours, 20:00 to 18:00, midnight heavier. */
    CHECK(g_v.nhours == 12);
    CHECK(g_v.hours[0].hour == 20);
    CHECK(g_v.hours[2].hour == 0 && g_v.hours[2].midnight);
    CHECK(g_v.hours[11].hour == 18);
    CHECK(!g_v.hours[0].midnight);
    /* 20:00 is 1h18m into the window. */
    CHECK(g_v.hours[0].x == 36 + 133);
}

/* ---- the now edge moves once a minute, nothing else ---- */

static void test_minute(void) {
    const time_t now = local(2026, 11, 13, 18, 42, 0);
    ag_reset();
    char *s = build((double)now, MOCK, NMOCK);
    ag_observe(s);
    free(s);
    static ag_view_t a, b;
    ag_view(now, &a);
    ag_view(now + 59, &b);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    CHECK(b.now == now);
    ag_view(now + 60, &b);
    CHECK(memcmp(&a, &b, sizeof(a)) != 0);
    CHECK(b.now == now + 60);
}

/* ---- window clipping ---- */

static void test_clipping(void) {
    const time_t now = local(2026, 11, 13, 18, 42, 0);
    g_midnight = local(2026, 11, 13, 0, 0, 0);
    /* Started 30 h ago, still running; one started 26 h ago and finished
     * 23 h ago (in the window by an hour); one wholly before the window is
     * the parser's to drop. */
    const spec_t S[] = {
        {"a", "kai", "long-runner", NOW_H - 30, NOW_H, "working", 0, 0, NULL},
        {"b", "kubs0", "early", NOW_H - 26, NOW_H - 23, "done", 0, 0, NULL},
    };
    ag_reset();
    char *s = build((double)now, S, 2);
    CHECK(ag_observe(s) == 1);
    free(s);
    ag_view(now, &g_v);
    CHECK(g_v.nbars == 2);
    const ag_bar_t *a = find(&g_v, "long-runner", 0), *b = find(&g_v, "early", 0);
    CHECK(a->x0 == AG_PLOT_X0 && a->x1 == AG_PLOT_X1);
    CHECK(b->x0 == AG_PLOT_X0);
    CHECK(b->x1 == AG_PLOT_X0 + 102); /* 1 h = 102.3 px */
    CHECK(b->row == 1);               /* the long runner holds row 0 */

    /* Two hours later "early" has left the window, and the long runner is
     * now 2 h past its last publish: stale, so it stops at its own end. */
    ag_view(now + 2 * 3600, &g_v);
    CHECK(g_v.nbars == 1);
    CHECK(g_v.stale);
    CHECK(STREQ(g_v.hint, "as of 2h"));
    a = find(&g_v, "long-runner", 0);
    CHECK(!a->running);
    CHECK(a->x1 == AG_PLOT_X0 + (int32_t)(22.0 / 24 * 2456 + 0.5));

    /* A sliver of a bar is still wide enough for its border; one at the now
     * line keeps inside the plot. */
    const spec_t T[] = {
        {"c", "kai", "blip", 10.0, 10.01, "done", 1, 0, NULL},
        {"d", "kai", "fresh", NOW_H - 0.01, NOW_H, "working", 0, 0, NULL},
    };
    ag_reset();
    s = build((double)now, T, 2);
    ag_observe(s);
    free(s);
    ag_view(now, &g_v);
    CHECK(find(&g_v, "blip", 0)->x1 - find(&g_v, "blip", 0)->x0 == AG_BAR_MIN_W);
    CHECK(find(&g_v, "fresh", 0)->x1 == AG_PLOT_X1);
    CHECK(find(&g_v, "fresh", 0)->x1 - find(&g_v, "fresh", 0)->x0 == AG_BAR_MIN_W);
}

/* ---- no feed, empty day, hold through a gap ---- */

static void test_states(void) {
    const time_t now = local(2026, 11, 13, 18, 42, 0);
    g_midnight = local(2026, 11, 13, 0, 0, 0);
    ag_reset();
    ag_view(now, &g_v);
    CHECK(g_v.status == AG_NOFEED);
    CHECK(g_v.nbars == 0);
    CHECK(g_v.nhours == 12); /* the grid still draws */
    CHECK(ag_observe(NULL) == 0);
    CHECK(ag_record() == NULL);

    /* Empty: a record with no bars, and one whose bars all left the window. */
    char *s = build((double)now, NULL, 0);
    CHECK(ag_observe(s) == 1);
    free(s);
    ag_view(now, &g_v);
    CHECK(g_v.status == AG_EMPTY);
    const spec_t OLD[] = {{"a", "kai", "gone", 0.0, 1.0, "done", 0, 0, NULL}};
    s = build((double)local(2026, 11, 13, 2, 0, 0), OLD, 1);
    CHECK(ag_observe(s) == 1);
    free(s);
    ag_view(local(2026, 11, 14, 3, 0, 0), &g_v);
    CHECK(g_v.status == AG_EMPTY);
    CHECK(g_v.stale);

    /* Hold: an absent key and a rejected record keep the last good one. */
    s = build((double)now, MOCK, NMOCK);
    CHECK(ag_observe(s) == 1);
    free(s);
    unsigned gen = ag_generation();
    CHECK(ag_observe(NULL) == 0);
    CHECK(ag_observe("{\"ts\":1}") == -1);
    CHECK(ag_observe("not json") == -1);
    CHECK(ag_generation() == gen);
    ag_view(now, &g_v);
    CHECK(g_v.nbars == NMOCK);

    /* Fresh within cadence + grace (60 + 60 s); stale past it. */
    ag_view(now + 119, &g_v);
    CHECK(!g_v.stale);
    CHECK(find(&g_v, "kvimh", 0)->running);
    ag_view(now + 180, &g_v);
    CHECK(g_v.stale);
    CHECK(STREQ(g_v.hint, "as of 3m"));
    CHECK(!find(&g_v, "kvimh", 0)->running);
    CHECK(find(&g_v, "kvimh", 0)->x1 < AG_PLOT_X1);
}

/* ---- a crowded day: rows shrink, then the oldest are left out ---- */

static void test_crowded(void) {
    const time_t now = local(2026, 11, 13, 18, 42, 0);
    g_midnight = local(2026, 11, 13, 0, 0, 0);
    static spec_t S[40];
    static char ids[40][8], names[40][16];
    /* 40 bars that all overlap 12:00-13:00: each needs its own row. */
    for (int i = 0; i < 40; i++) {
        snprintf(ids[i], sizeof(ids[i]), "x%d", i);
        snprintf(names[i], sizeof(names[i]), "bar%02d", i);
        S[i] = (spec_t){ids[i], "kai", names[i], 11.0 + i * 0.01, 13.0 + i * 0.1,
                        "done", 0, 0, NULL};
    }
    for (int n = 1; n <= 40; n++) {
        ag_reset();
        char *s = build((double)now, S, n);
        CHECK(ag_observe(s) == 1);
        free(s);
        ag_view(now, &g_v);
        CHECK(!overlaps(&g_v));
        if (n <= AG_ROWS_MAX) {
            CHECK(g_v.rows == n && g_v.hidden == 0 && g_v.nbars == n);
        } else {
            CHECK(g_v.rows == AG_ROWS_MAX);
            CHECK(g_v.hidden == n - AG_ROWS_MAX);
            CHECK(g_v.nbars == AG_ROWS_MAX);
            /* The left-out ones are the earliest to end: the newest stay. */
            CHECK(find(&g_v, names[n - 1], 0) != NULL);
            CHECK(find(&g_v, names[0], 0) == NULL);
        }
        CHECK(g_v.row_h >= AG_ROW_H_MIN && g_v.row_h <= AG_ROW_H_MAX);
        /* The last row ends inside the plot. */
        CHECK(AG_ROW_TOP + g_v.rows * g_v.row_h + (g_v.rows - 1) * AG_ROW_GAP <= AG_ROW_BOT);
    }
    CHECK(AG_ROWS_MAX == 10);

    /* An overseer left out takes its legs with it. */
    const spec_t P[] = {
        {"o", "cleo", "old-overseer", 1.0, 2.0, "done", 0, 1, NULL},
        /* The legs end AFTER the bars below, so only the take-the-legs rule
         * removes them: on their own they would be the last to go. */
        {"l1", "kai", "leg1", 1.2, 17.0, "done", 0, 0, "o"},
        {"l2", "kai", "leg2", 1.3, 17.0, "done", 0, 0, "o"},
    };
    static spec_t Q[3 + AG_ROWS_MAX];
    memcpy(Q, P, sizeof(P));
    for (int i = 0; i < AG_ROWS_MAX; i++)
        Q[3 + i] = (spec_t){ids[i], "kubs0", names[i], 1.5 + i * 0.01, 16.0, "done", 0, 0, NULL};
    ag_reset();
    char *s = build((double)now, Q, 3 + AG_ROWS_MAX);
    CHECK(ag_observe(s) == 1);
    free(s);
    ag_view(now, &g_v);
    CHECK(find(&g_v, "old-overseer", 0) == NULL);
    CHECK(find(&g_v, "leg1", 0) == NULL && find(&g_v, "leg2", 0) == NULL);
    CHECK(g_v.hidden == 3);
    CHECK(g_v.rows == AG_ROWS_MAX);
}

/* ---- a working overseer with two running legs ---- */

static void test_working_overseer(void) {
    const time_t now = local(2026, 11, 13, 18, 42, 0);
    g_midnight = local(2026, 11, 13, 0, 0, 0);
    /* Listed with a leg BEFORE its overseer (an earlier start): the link and
     * the placement must not depend on the order. */
    const spec_t S[] = {
        {"l0", "kai", "early-leg", 15.0, 16.0, "done", 1, 0, "o"},
        {"o", "cleo", "agents-card", 15.5, NOW_H, "working", 0, 1, NULL},
        {"l1", "kai", "kpidash", 16.0, NOW_H, "working", 0, 0, "o"},
        {"l2", "kubs0", "ksitrep", 16.2, NOW_H, "waiting", 0, 0, "o"},
    };
    ag_reset();
    char *s = build((double)now, S, 4);
    CHECK(ag_observe(s) == 1);
    free(s);
    ag_view(now, &g_v);
    const ag_bar_t *o = find(&g_v, "agents-card", 0);
    CHECK(o && o->row == 0 && o->running && o->overseer);
    const ag_bar_t *l1 = find(&g_v, "kpidash", 0), *l2 = find(&g_v, "ksitrep", 0),
                   *l0 = find(&g_v, "early-leg", 0);
    /* Below the overseer: kpidash starts as early-leg ends (too close to
     * share its row), ksitrep 12 minutes later (room to). */
    CHECK(l1->row == 2 && l2->row == 1);
    CHECK(l0->row == 1);
    CHECK(&g_v.bars[l1->parent] == o && &g_v.bars[l2->parent] == o && &g_v.bars[l0->parent] == o);
    CHECK(l1->running && l2->running);
    CHECK(!overlaps(&g_v));
}

/* A leg goes BELOW its overseer even when a higher row has come free. */
static void test_leg_floor(void) {
    const time_t now = local(2026, 11, 13, 18, 42, 0);
    g_midnight = local(2026, 11, 13, 0, 0, 0);
    const spec_t S[] = {
        {"a", "kai", "early", 0.0, 6.0, "done", 0, 0, NULL},       /* row 0 */
        {"o", "cleo", "overseer", 3.0, 12.0, "done", 0, 1, NULL},  /* row 1: row 0 busy */
        {"l", "kubs0", "leg", 8.0, 10.0, "done", 1, 0, "o"},       /* row 0 is free by 08:00 */
    };
    ag_reset();
    char *s = build((double)now, S, 3);
    CHECK(ag_observe(s) == 1);
    free(s);
    ag_view(now, &g_v);
    CHECK(find(&g_v, "early", 0)->row == 0);
    CHECK(find(&g_v, "overseer", 0)->row == 1);
    CHECK(find(&g_v, "leg", 0)->row == 2);
}

/* ---- the label ---- */

/* 12 px a character, counting UTF-8 lead bytes. */
static int32_t measure12(const char *s, void *ctx) {
    (void)ctx;
    int32_t n = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80)
            n++;
    return n * 12;
}

static void test_label(void) {
    char out[KDASH_AGENTS_NAME_MAX + 4];
    CHECK(ag_fit_label("kdashdata", 108, measure12, NULL, out, sizeof(out)));
    CHECK(STREQ(out, "kdashdata"));
    /* One character short: cut, ellipsis counted as one. */
    CHECK(ag_fit_label("kdashdata", 107, measure12, NULL, out, sizeof(out)));
    CHECK(STREQ(out, "kdashda\xE2\x80\xA6"));
    /* Down to four characters and the ellipsis... */
    CHECK(ag_fit_label("kdashdata", 60, measure12, NULL, out, sizeof(out)));
    CHECK(STREQ(out, "kdas\xE2\x80\xA6"));
    /* ...and no further: no name at all. */
    CHECK(!ag_fit_label("kdashdata", 59, measure12, NULL, out, sizeof(out)));
    CHECK(out[0] == '\0');
    CHECK(!ag_fit_label("kdashdata", 0, measure12, NULL, out, sizeof(out)));
    /* Never mid-character, and no space left before the ellipsis. */
    CHECK(ag_fit_label("D\xC3\xAD" "a de \xC3\x81" "nimo", 72, measure12, NULL, out, sizeof(out)));
    CHECK(STREQ(out, "D\xC3\xAD" "a d\xE2\x80\xA6"));
    CHECK(ag_fit_label("D\xC3\xAD" "a de \xC3\x81" "nimo", 84, measure12, NULL, out, sizeof(out)));
    CHECK(STREQ(out, "D\xC3\xAD" "a de\xE2\x80\xA6")); /* "Día de …" loses its space */
    CHECK(ag_fit_label("D\xC3\xAD" "a de \xC3\x81" "nimo", 108, measure12, NULL, out, sizeof(out)));
    CHECK(STREQ(out, "D\xC3\xAD" "a de \xC3\x81\xE2\x80\xA6"));
    /* A 31-character name, the contract's bound. */
    const char *w31 = "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";
    CHECK(ag_fit_label(w31, 31 * 12, measure12, NULL, out, sizeof(out)));
    CHECK(STREQ(out, w31));
    CHECK(ag_fit_label(w31, 30 * 12, measure12, NULL, out, sizeof(out)));
    CHECK(strlen(out) == 29 + 3);
}

/* ---- the hour grid across a DST change ---- */

static void test_dst(void) {
    /* 2026-11-01 is the fall-back day: 01:00-02:00 happens twice. At 12:00
     * PST the window opened 24 real hours earlier, 13:00 PDT on the 31st, so
     * it holds 25 wall hours. */
    const time_t now = local(2026, 11, 1, 12, 0, 0);
    ag_reset();
    ag_view(now, &g_v);
    /* Even hours after 13:00 on the 31st and before 12:00: 14..22 (5), 00,
     * 02 (once: the repeated hour is 01, which is odd), 04..10 (4). */
    CHECK(g_v.nhours == 11);
    for (int i = 1; i < g_v.nhours; i++)
        CHECK(g_v.hours[i].x > g_v.hours[i - 1].x);
    /* Midnight is followed by 02:00 three real hours later. */
    for (int i = 0; i + 1 < g_v.nhours; i++)
        if (g_v.hours[i].midnight)
            CHECK(g_v.hours[i + 1].hour == 2 && g_v.hours[i + 1].x - g_v.hours[i].x > 300);
}

int main(void) {
    setenv("TZ", "America/Los_Angeles", 1);
    tzset();
    g_midnight = local(2026, 11, 13, 0, 0, 0);
    test_schema();
    test_mockup();
    test_minute();
    test_clipping();
    test_states();
    test_crowded();
    test_working_overseer();
    test_leg_floor();
    test_label();
    test_dst();
    ag_reset();
    printf("test_agstate: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
