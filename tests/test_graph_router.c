/* test_graph_router.c — graph host-series helper tests (T028 + T033).
 *
 * Since sprint 032 the series carries only the NAS host's dev telemetry, for
 * the NAS card's activity chart. Covers:
 *  - graph_host_find_or_create returns a stable pointer per host name
 *  - graph_host_touch updates last_sample_ts
 *  - sentinel host "(legacy)" is accepted
 *  - multi-host distinct routing (kdash vs kai)
 *  - NULL and empty host names are refused
 */
#define KPIDASH_TEST_STUBS 1
#include "registry.h"

#include <stdio.h>
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

static void test_stable_pointer(void) {
    graph_host_series_t *a = graph_host_find_or_create("hostA");
    graph_host_series_t *b = graph_host_find_or_create("hostA");
    CHECK(a != NULL);
    CHECK(a == b);
    CHECK(strcmp(a->host, "hostA") == 0);
}

static void test_touch_updates_ts(void) {
    graph_host_series_t *s = graph_host_find_or_create("touch_host");
    CHECK(s != NULL);
    graph_host_touch("touch_host", 1234.5);
    CHECK(s->last_sample_ts == 1234.5);
    graph_host_touch("touch_host", 9999.0);
    CHECK(s->last_sample_ts == 9999.0);
}

static void test_sentinel_legacy(void) {
    graph_host_series_t *s = graph_host_find_or_create("(legacy)");
    CHECK(s != NULL);
    CHECK(strcmp(s->host, "(legacy)") == 0);
}

static void test_multi_host_distinct(void) {
    graph_host_series_t *kdash = graph_host_find_or_create("kdash");
    graph_host_series_t *kai = graph_host_find_or_create("kai");
    CHECK(kdash != NULL && kai != NULL);
    CHECK(kdash != kai);
    graph_host_touch("kdash", 100.0);
    graph_host_touch("kai", 200.0);
    CHECK(kdash->last_sample_ts == 100.0);
    CHECK(kai->last_sample_ts == 200.0);
}

static void test_find_or_create_rejects_null_and_empty(void) {
    CHECK(graph_host_find_or_create(NULL) == NULL);
    CHECK(graph_host_find_or_create("") == NULL);
}

int main(void) {
    test_stable_pointer();
    test_touch_updates_ts();
    test_sentinel_legacy();
    test_multi_host_distinct();
    test_find_or_create_rejects_null_and_empty();

    fprintf(stderr, "test_graph_router: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
