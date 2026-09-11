/* test_service_card.c — Service helpers and JSON parse tests (T017/T018).
 *
 * Covers:
 *  - service_parse_state for each enum + NULL/empty/bogus
 *  - service_color truth table per data-model.md §2
 *  - redis_parse_service_payload for valid + 4 malformed cases (FR-022a)
 */
#define KPIDASH_TEST_STUBS 1
#include "registry.h"
#include "redis.h"

#include <stdio.h>
#include <string.h>

#ifdef KPIDASH_TEST_STUBS
void ui_show_redis_error(const char *msg) { (void)msg; }
void ui_hide_redis_error(void) {}
void status_redis_check_ack(void *ctx) { (void)ctx; }
void fortune_on_pushed(const char *json) { (void)json; }
bool screenshot_save(const char *path) { (void)path; return false; }
#endif

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

static void test_parse_state(void) {
    CHECK(service_parse_state(NULL) == SERVICE_STATE_UNKNOWN);
    CHECK(service_parse_state("") == SERVICE_STATE_UNKNOWN);
    CHECK(service_parse_state("bogus") == SERVICE_STATE_UNKNOWN);
    CHECK(service_parse_state("ok") == SERVICE_STATE_OK);
    CHECK(service_parse_state("OK") == SERVICE_STATE_OK);
    CHECK(service_parse_state("unhealthy") == SERVICE_STATE_UNHEALTHY);
    CHECK(service_parse_state("maintenance") == SERVICE_STATE_MAINTENANCE);
    CHECK(service_parse_state("down") == SERVICE_STATE_DOWN);
}

static void test_color_truth_table(void) {
    service_entry_t e = {0};
    double now = 100.0;
    e.last_payload_ts = 99.0; /* fresh: now - ts = 1.0 < SERVICE_FRESH_SECONDS */

    e.last_valid_state = SERVICE_STATE_OK;
    CHECK(service_color(&e, now) == SERVICE_COLOR_GREEN);

    e.last_valid_state = SERVICE_STATE_UNHEALTHY;
    CHECK(service_color(&e, now) == SERVICE_COLOR_YELLOW);

    e.last_valid_state = SERVICE_STATE_MAINTENANCE;
    CHECK(service_color(&e, now) == SERVICE_COLOR_BLUE);

    e.last_valid_state = SERVICE_STATE_DOWN;
    CHECK(service_color(&e, now) == SERVICE_COLOR_GRAY);

    e.last_valid_state = SERVICE_STATE_UNKNOWN;
    CHECK(service_color(&e, now) == SERVICE_COLOR_GRAY);

    /* Stale: now - ts >= SERVICE_FRESH_SECONDS → RED for non-DOWN, GRAY for DOWN. */
    e.last_payload_ts = 0.0;
    e.last_valid_state = SERVICE_STATE_OK;
    CHECK(service_color(&e, now) == SERVICE_COLOR_RED);

    e.last_valid_state = SERVICE_STATE_UNHEALTHY;
    CHECK(service_color(&e, now) == SERVICE_COLOR_RED);

    e.last_valid_state = SERVICE_STATE_MAINTENANCE;
    CHECK(service_color(&e, now) == SERVICE_COLOR_RED);

    e.last_valid_state = SERVICE_STATE_DOWN;
    CHECK(service_color(&e, now) == SERVICE_COLOR_GRAY); /* DOWN sticky */

    /* Freshness boundary at SERVICE_FRESH_SECONDS (75.0):
     *   age just under  → still fresh (coloured),
     *   age == window   → aged out (RED). */
    e.last_valid_state = SERVICE_STATE_UNHEALTHY;
    e.last_payload_ts = 100.0 - (SERVICE_FRESH_SECONDS - 0.1); /* age 74.9 → fresh */
    CHECK(service_color(&e, 100.0) == SERVICE_COLOR_YELLOW);
    e.last_payload_ts = 100.0 - SERVICE_FRESH_SECONDS;         /* age 75.0 → stale */
    CHECK(service_color(&e, 100.0) == SERVICE_COLOR_RED);

    /* NULL → GRAY. */
    CHECK(service_color(NULL, now) == SERVICE_COLOR_GRAY);
}

static void test_parse_payload_valid(void) {
    service_entry_t out;
    const char *json = "{\"ts\":1700000000.5,\"state\":\"ok\",\"text\":\"all good\","
                       "\"host\":\"kai\",\"icon\":3}";
    int r = redis_parse_service_payload(json, &out);
    CHECK(r == 0);
    CHECK(out.last_valid_state == SERVICE_STATE_OK);
    CHECK(out.last_payload_ts > 1.69e9);
    CHECK(strcmp(out.text, "all good") == 0);
    CHECK(strcmp(out.host, "kai") == 0);
    CHECK(out.icon_index == 3);
}

static void test_parse_payload_missing_state(void) {
    service_entry_t out;
    const char *json = "{\"ts\":1700000000,\"text\":\"x\"}";
    CHECK(redis_parse_service_payload(json, &out) == -1);
}

static void test_parse_payload_bad_state(void) {
    service_entry_t out;
    const char *json = "{\"ts\":1700000000,\"state\":\"frobnicated\",\"text\":\"x\"}";
    CHECK(redis_parse_service_payload(json, &out) == -1);
}

static void test_parse_payload_missing_ts(void) {
    service_entry_t out;
    const char *json = "{\"state\":\"ok\",\"text\":\"x\"}";
    CHECK(redis_parse_service_payload(json, &out) == -1);
}

static void test_parse_payload_missing_text(void) {
    service_entry_t out;
    const char *json = "{\"ts\":1700000000,\"state\":\"ok\"}";
    CHECK(redis_parse_service_payload(json, &out) == -1);
}

static void test_parse_payload_optional_fields_absent(void) {
    service_entry_t out;
    const char *json = "{\"ts\":1700000000,\"state\":\"maintenance\",\"text\":\"draining\"}";
    int r = redis_parse_service_payload(json, &out);
    CHECK(r == 0);
    CHECK(out.last_valid_state == SERVICE_STATE_MAINTENANCE);
    CHECK(out.host[0] == '\0');
    CHECK(out.icon_index == -1);
}

/* Sprint 007 / T003: composite (name, host) registry identity. */
static void test_registry_composite_key(void) {
    service_entry_t *a = service_registry_find_or_create("svc", "alpha");
    service_entry_t *b = service_registry_find_or_create("svc", "beta");
    service_entry_t *a2 = service_registry_find_or_create("svc", "alpha");
    CHECK(a != NULL);
    CHECK(b != NULL);
    CHECK(a != b);
    CHECK(a == a2);
    CHECK(strcmp(a->name, "svc") == 0);
    CHECK(strcmp(a->host, "alpha") == 0);
    CHECK(strcmp(b->host, "beta") == 0);

    /* Sentinel host "_" is just another value at the registry layer. */
    service_entry_t *c = service_registry_find_or_create("svc", "_");
    CHECK(c != NULL);
    CHECK(c != a);
    CHECK(c != b);

    /* NULL / empty host rejected. */
    CHECK(service_registry_find_or_create("svc", NULL) == NULL);
    CHECK(service_registry_find_or_create("svc", "") == NULL);
}

/* Sprint 007 / T005: apply_payload must NOT clobber identity (name, host). */
static void test_registry_apply_preserves_identity(void) {
    service_entry_t *e = service_registry_find_or_create("preserve", "h1");
    CHECK(e != NULL);
    service_entry_t parsed = {0};
    parsed.last_valid_state = SERVICE_STATE_OK;
    parsed.last_payload_ts = 12345.0;
    strncpy(parsed.text, "hello", sizeof(parsed.text) - 1);
    strncpy(parsed.host, "WRONG", sizeof(parsed.host) - 1);
    strncpy(parsed.name, "WRONG", sizeof(parsed.name) - 1);
    parsed.icon_index = 7;
    service_registry_apply_payload(e, &parsed);
    CHECK(strcmp(e->name, "preserve") == 0);
    CHECK(strcmp(e->host, "h1") == 0);
    CHECK(strcmp(e->text, "hello") == 0);
    CHECK(e->icon_index == 7);
    CHECK(e->last_valid_state == SERVICE_STATE_OK);
}

/* ---- WI #902: per-service freshness windows -----------------------------
 *
 * A daily feed under the default ~75s window is RED from about a minute after
 * every run, so "kmon is healthy" and "kmon has been dead for a week" render
 * identically. That is the exact failure the card was commissioned to catch,
 * so the window is the whole feature and these tests pin both of its edges.
 */
static void test_fresh_window_lookup(void) {
    CHECK(service_fresh_window("kmon") == SERVICE_DAILY_FRESH_SECONDS);

    /* Anything not known to be a daily feed keeps the default — including the
     * empty name every {0}-initialised entry in this file carries. */
    CHECK(service_fresh_window("api") == SERVICE_FRESH_SECONDS);
    CHECK(service_fresh_window("") == SERVICE_FRESH_SECONDS);
    CHECK(service_fresh_window(NULL) == SERVICE_FRESH_SECONDS);

    /* Match is exact, not a prefix: a different service whose name merely
     * starts with "kmon" must not inherit the 26-hour tolerance. */
    CHECK(service_fresh_window("kmon-agent") == SERVICE_FRESH_SECONDS);
}

static void test_daily_feed_survives_a_day_and_dies_on_the_second(void) {
    service_entry_t e = {0};
    snprintf(e.name, sizeof(e.name), "kmon");
    snprintf(e.host, sizeof(e.host), "kai");
    e.last_valid_state = SERVICE_STATE_OK;

    const double now = 1000000.0;
    const double HOUR = 3600.0;

    /* A normal daily cadence: yesterday's run is still authoritative. */
    e.last_payload_ts = now - 24.0 * HOUR;
    CHECK(service_color(&e, now) == SERVICE_COLOR_GREEN);

    /* Late, but inside the DST + jitter allowance. */
    e.last_payload_ts = now - 25.9 * HOUR;
    CHECK(service_color(&e, now) == SERVICE_COLOR_GREEN);

    /* The window is exclusive at its own edge, matching SERVICE_FRESH_SECONDS. */
    e.last_payload_ts = now - SERVICE_DAILY_FRESH_SECONDS;
    CHECK(service_color(&e, now) == SERVICE_COLOR_RED);

    /* One skipped day must be visible — this is the 10-day silent outage. */
    e.last_payload_ts = now - 27.0 * HOUR;
    CHECK(service_color(&e, now) == SERVICE_COLOR_RED);
    e.last_payload_ts = now - 10.0 * 24.0 * HOUR;
    CHECK(service_color(&e, now) == SERVICE_COLOR_RED);

    /* A published RED is RED whatever its age: the state is not overridden by
     * being fresh, only by being stale. */
    e.last_valid_state = SERVICE_STATE_UNHEALTHY;
    e.last_payload_ts = now - 1.0 * HOUR;
    CHECK(service_color(&e, now) == SERVICE_COLOR_YELLOW);

    /* DOWN stays sticky-GRAY for a daily feed exactly as for a 60s one. */
    e.last_valid_state = SERVICE_STATE_DOWN;
    e.last_payload_ts = now - 10.0 * 24.0 * HOUR;
    CHECK(service_color(&e, now) == SERVICE_COLOR_GRAY);
}

static void test_default_window_is_unchanged_for_other_services(void) {
    /* The regression that would matter most: widening kmon's window must not
     * widen anybody else's. A 60s-cadence service is still RED at 80s. */
    service_entry_t e = {0};
    snprintf(e.name, sizeof(e.name), "api");
    e.last_valid_state = SERVICE_STATE_OK;

    const double now = 1000.0;
    e.last_payload_ts = now - 70.0;
    CHECK(service_color(&e, now) == SERVICE_COLOR_GREEN);
    e.last_payload_ts = now - 80.0;
    CHECK(service_color(&e, now) == SERVICE_COLOR_RED);
}

int main(void) {
    test_parse_state();
    test_color_truth_table();
    test_parse_payload_valid();
    test_parse_payload_missing_state();
    test_parse_payload_bad_state();
    test_parse_payload_missing_ts();
    test_parse_payload_missing_text();
    test_parse_payload_optional_fields_absent();
    test_registry_composite_key();
    test_registry_apply_preserves_identity();
    test_fresh_window_lookup();
    test_daily_feed_survives_a_day_and_dies_on_the_second();
    test_default_window_is_unchanged_for_other_services();

    fprintf(stderr, "test_service_card: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
