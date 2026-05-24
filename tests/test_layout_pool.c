/* test_layout_pool.c — Unit tests for layout_pool_place() (T011).
 * FR-003 (priority order), FR-004 (drop-and-continue). */
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

/* (a) Canonical request set fits; all placed in priority order. */
static void test_canonical_set_fits(void) {
    widget_request_t reqs[] = {
        {WIDGET_FORTUNE, WIDGET_CELLS_FORTUNE, NULL},
        {WIDGET_REPO_STATUS, WIDGET_CELLS_REPO_STATUS, NULL},
        {WIDGET_ACTIVITIES, WIDGET_CELLS_ACTIVITIES, NULL},
        {WIDGET_GRAPH, WIDGET_CELLS_GRAPH, NULL},
    };
    widget_request_t out[8] = {0};
    int n = layout_pool_place(reqs, 4, out, 8);
    CHECK(n == 4);
    CHECK(out[0].kind == WIDGET_GRAPH);
    CHECK(out[1].kind == WIDGET_ACTIVITIES);
    CHECK(out[2].kind == WIDGET_REPO_STATUS);
    CHECK(out[3].kind == WIDGET_FORTUNE);
    int total = 0;
    for (int i = 0; i < n; i++) total += out[i].cells;
    CHECK(total == 8);
}

/* (b) Oversized synthetic Graph(>12) drops itself; the rest still fit. */
static void test_oversized_drops_self(void) {
    widget_request_t reqs[] = {
        {WIDGET_GRAPH, 13, NULL},
        {WIDGET_ACTIVITIES, WIDGET_CELLS_ACTIVITIES, NULL},
        {WIDGET_REPO_STATUS, WIDGET_CELLS_REPO_STATUS, NULL},
        {WIDGET_FORTUNE, WIDGET_CELLS_FORTUNE, NULL},
    };
    widget_request_t out[8] = {0};
    int n = layout_pool_place(reqs, 4, out, 8);
    CHECK(n == 3);
    CHECK(out[0].kind == WIDGET_ACTIVITIES);
    CHECK(out[1].kind == WIDGET_REPO_STATUS);
    CHECK(out[2].kind == WIDGET_FORTUNE);
}

/* (c) Drop-and-continue: Graph(10) eats 10/12; Activities(4) won't fit;
 *     Fortune(2) still fits in remaining 2 cells. */
static void test_drop_and_continue(void) {
    widget_request_t reqs[] = {
        {WIDGET_GRAPH, 10, NULL},
        {WIDGET_ACTIVITIES, 4, NULL},
        {WIDGET_FORTUNE, 2, NULL},
    };
    widget_request_t out[8] = {0};
    int n = layout_pool_place(reqs, 3, out, 8);
    CHECK(n == 2);
    CHECK(out[0].kind == WIDGET_GRAPH);
    CHECK(out[1].kind == WIDGET_FORTUNE);
}

/* (d) Multi-instance Graph: input alphabetical order preserved by stable sort. */
static void test_multi_graph_stable_order(void) {
    static const char *host_a = "alpha";
    static const char *host_b = "bravo";
    static const char *host_c = "charlie";
    widget_request_t reqs[] = {
        {WIDGET_GRAPH, WIDGET_CELLS_GRAPH, host_a},
        {WIDGET_GRAPH, WIDGET_CELLS_GRAPH, host_b},
        {WIDGET_GRAPH, WIDGET_CELLS_GRAPH, host_c},
        {WIDGET_ACTIVITIES, WIDGET_CELLS_ACTIVITIES, NULL},
        {WIDGET_REPO_STATUS, WIDGET_CELLS_REPO_STATUS, NULL},
        {WIDGET_FORTUNE, WIDGET_CELLS_FORTUNE, NULL},
    };
    widget_request_t out[8] = {0};
    int n = layout_pool_place(reqs, 6, out, 8);
    CHECK(n == 6);
    CHECK(out[0].kind == WIDGET_GRAPH && out[0].payload == host_a);
    CHECK(out[1].kind == WIDGET_GRAPH && out[1].payload == host_b);
    CHECK(out[2].kind == WIDGET_GRAPH && out[2].payload == host_c);
    CHECK(out[3].kind == WIDGET_ACTIVITIES);
    CHECK(out[4].kind == WIDGET_REPO_STATUS);
    CHECK(out[5].kind == WIDGET_FORTUNE);
}

/* (e) Empty input. */
static void test_empty(void) {
    widget_request_t out[4] = {0};
    int n = layout_pool_place(NULL, 0, out, 4);
    CHECK(n == 0);
}

/* (f) Capacity exactly hit; nothing else fits. */
static void test_exact_capacity(void) {
    widget_request_t reqs[] = {
        {WIDGET_GRAPH, 12, NULL},
        {WIDGET_FORTUNE, 2, NULL},
    };
    widget_request_t out[4] = {0};
    int n = layout_pool_place(reqs, 2, out, 4);
    CHECK(n == 1);
    CHECK(out[0].kind == WIDGET_GRAPH);
}

int main(void) {
    test_canonical_set_fits();
    test_oversized_drops_self();
    test_drop_and_continue();
    test_multi_graph_stable_order();
    test_empty();
    test_exact_capacity();

    fprintf(stderr, "test_layout_pool: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
