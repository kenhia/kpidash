/* test_atstate.c — the apt-temps widget's model (WI #4848, sprint 047).
 *
 * Three fixed zones in a fixed order, each holding its slot with or without a
 * reading; the band colour and the 300 s freshness rule unchanged from the
 * cards it replaces; and the green-border signal: a fresh reading from any
 * other slug, and only a fresh one. */
#define KPIDASH_TEST_STUBS 1
#include "atstate.h"

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
#define NOW 100000.0

static apttemps_entry_t ent(const char *slug, float t, int h, double age) {
    apttemps_entry_t e;
    memset(&e, 0, sizeof(e));
    snprintf(e.slug, sizeof(e.slug), "%s", slug);
    snprintf(e.zone, sizeof(e.zone), "Some Zone");
    e.temp_f = t;
    e.humidity_pct = h;
    e.last_payload_ts = NOW - age;
    e.valid = true;
    return e;
}

static void test_zone_table(void) {
    CHECK(STREQ(at_zone_slug(0), "living") && STREQ(at_zone_name(0), "Living"));
    CHECK(STREQ(at_zone_slug(1), "kitchen") && STREQ(at_zone_name(1), "Kitchen"));
    CHECK(STREQ(at_zone_slug(2), "bedroom") && STREQ(at_zone_name(2), "Bedroom"));
    CHECK(STREQ(at_zone_slug(3), "") && STREQ(at_zone_name(-1), ""));
    CHECK(at_zone_index("living") == 0);
    CHECK(at_zone_index("kitchen") == 1);
    CHECK(at_zone_index("bedroom") == 2);
    CHECK(at_zone_index("Living") == -1); /* slugs are lowercase; no folding */
    CHECK(at_zone_index("office") == -1);
    CHECK(at_zone_index("") == -1);
    CHECK(at_zone_index(NULL) == -1);
}

/* Registry order is arrival order; the widget's is the fixed one. */
static void test_order_and_format(void) {
    apttemps_entry_t e[3] = {ent("bedroom", 63.25f, 41, 10), ent("living", 71.96f, 45, 10),
                             ent("kitchen", 76.0f, 38, 10)};
    at_view_t v;
    at_view(e, 3, NOW, &v);
    CHECK(v.zone[0].present && STREQ(v.zone[0].temp, "72.0\xC2\xB0") && STREQ(v.zone[0].hum, "45%"));
    CHECK(v.zone[0].color == APTTEMPS_COLOR_GREEN);
    CHECK(v.zone[1].present && STREQ(v.zone[1].temp, "76.0\xC2\xB0") && STREQ(v.zone[1].hum, "38%"));
    CHECK(v.zone[1].color == APTTEMPS_COLOR_ORANGE);
    CHECK(v.zone[2].present && STREQ(v.zone[2].temp, "63.2\xC2\xB0") && STREQ(v.zone[2].hum, "41%"));
    CHECK(v.zone[2].color == APTTEMPS_COLOR_BLUE);
    CHECK(!v.unknown);
}

/* No key at all: the slot stays, not present, gray. An entry that never
 * took a valid payload is the same thing. */
static void test_missing_zone(void) {
    apttemps_entry_t e[2] = {ent("living", 70.0f, 40, 10), ent("bedroom", 70.0f, 40, 10)};
    e[1].valid = false;
    at_view_t v;
    at_view(e, 2, NOW, &v);
    CHECK(v.zone[0].present);
    CHECK(!v.zone[1].present && v.zone[1].color == APTTEMPS_COLOR_GRAY && v.zone[1].temp[0] == 0);
    CHECK(!v.zone[2].present && v.zone[2].color == APTTEMPS_COLOR_GRAY);
    at_view(NULL, 0, NOW, &v);
    for (int i = 0; i < AT_ZONES; i++)
        CHECK(!v.zone[i].present);
    CHECK(!v.unknown);
}

/* 300 s old or more: gray, with the reading still shown. */
static void test_stale_zone(void) {
    apttemps_entry_t e[2] = {ent("living", 70.0f, 40, 299.9), ent("kitchen", 70.0f, 40, 300.0)};
    at_view_t v;
    at_view(e, 2, NOW, &v);
    CHECK(v.zone[0].color == APTTEMPS_COLOR_GREEN);
    CHECK(v.zone[1].present && v.zone[1].color == APTTEMPS_COLOR_GRAY);
    CHECK(STREQ(v.zone[1].temp, "70.0\xC2\xB0"));
}

/* The green-border signal: fresh unknown only. */
static void test_unknown(void) {
    apttemps_entry_t e[4] = {ent("living", 70.0f, 40, 10), ent("kitchen", 70.0f, 40, 10),
                             ent("bedroom", 70.0f, 40, 10), ent("sensor-a4c1", 70.0f, 40, 10)};
    at_view_t v;
    at_view(e, 4, NOW, &v);
    CHECK(v.unknown);
    /* ...and it is not drawn in any slot. */
    for (int i = 0; i < AT_ZONES; i++)
        CHECK(STREQ(v.zone[i].temp, "70.0\xC2\xB0"));

    e[3].last_payload_ts = NOW - 300.0; /* stale: does not count */
    at_view(e, 4, NOW, &v);
    CHECK(!v.unknown);
    e[3].last_payload_ts = NOW - 299.0;
    e[3].valid = false; /* never parsed: does not count */
    at_view(e, 4, NOW, &v);
    CHECK(!v.unknown);

    /* An unknown on its own, with no known zone publishing. */
    apttemps_entry_t one = ent("office", 70.0f, 40, 0);
    at_view(&one, 1, NOW, &v);
    CHECK(v.unknown && !v.zone[0].present && !v.zone[1].present && !v.zone[2].present);
}

/* Equal state, equal bytes: the widget repaints on memcmp. */
static void test_view_compares(void) {
    apttemps_entry_t e[2] = {ent("living", 70.0f, 40, 10), ent("x", 70.0f, 40, 10)};
    at_view_t a, b;
    memset(&b, 0xAB, sizeof(b));
    at_view(e, 2, NOW, &a);
    at_view(e, 2, NOW + 1, &b);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
}

/* The extremes stay inside the buffers. */
static void test_extremes(void) {
    apttemps_entry_t e[2] = {ent("living", -40.0f, 0, 10), ent("kitchen", 140.0f, 100, 10)};
    at_view_t v;
    at_view(e, 2, NOW, &v);
    CHECK(STREQ(v.zone[0].temp, "-40.0\xC2\xB0") && STREQ(v.zone[0].hum, "0%"));
    CHECK(STREQ(v.zone[1].temp, "140.0\xC2\xB0") && STREQ(v.zone[1].hum, "100%"));
}

int main(void) {
    test_zone_table();
    test_order_and_format();
    test_missing_zone();
    test_stale_zone();
    test_unknown();
    test_view_compares();
    test_extremes();
    printf("test_atstate: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
