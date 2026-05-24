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
    e.last_payload_ts = 99.0; /* fresh: now - ts = 1.0 < 60.0 */

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

    /* Stale: now - ts >= 60.0 → RED for non-DOWN, GRAY for DOWN. */
    e.last_payload_ts = 0.0;
    e.last_valid_state = SERVICE_STATE_OK;
    CHECK(service_color(&e, now) == SERVICE_COLOR_RED);

    e.last_valid_state = SERVICE_STATE_UNHEALTHY;
    CHECK(service_color(&e, now) == SERVICE_COLOR_RED);

    e.last_valid_state = SERVICE_STATE_MAINTENANCE;
    CHECK(service_color(&e, now) == SERVICE_COLOR_RED);

    e.last_valid_state = SERVICE_STATE_DOWN;
    CHECK(service_color(&e, now) == SERVICE_COLOR_GRAY); /* DOWN sticky */

    /* Just-aged-out boundary: now - ts = 60.0 → stale → RED. */
    e.last_payload_ts = 40.0;
    e.last_valid_state = SERVICE_STATE_UNHEALTHY;
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

int main(void) {
    test_parse_state();
    test_color_truth_table();
    test_parse_payload_valid();
    test_parse_payload_missing_state();
    test_parse_payload_bad_state();
    test_parse_payload_missing_ts();
    test_parse_payload_missing_text();
    test_parse_payload_optional_fields_absent();

    fprintf(stderr, "test_service_card: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
