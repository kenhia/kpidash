/**
 * test_layout.c — Unit arithmetic tests for layout.h macros (SC-007)
 *
 * Verifies all layout constants, macros, and accounting identities.
 */
#include <stdio.h>
#include <stdlib.h>

#include "layout.h"

static int passed = 0;
static int failed = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
            failed++; \
        } else { \
            passed++; \
        } \
    } while (0)

static void test_unit_w_n(void) {
    CHECK(UNIT_W_N(1) == 632);
    CHECK(UNIT_W_N(2) == 1264);
    CHECK(UNIT_W_N(3) == 1896);
    CHECK(UNIT_W_N(6) == 3792);
}

static void test_unit_h_n(void) {
    CHECK(UNIT_H_N(1) == 628);
    CHECK(UNIT_H_N(2) == 1256);
    CHECK(UNIT_H_N(3) == 1884);
}

static void test_row_y(void) {
    CHECK(ROW_Y(0) == 0);
    CHECK(ROW_Y(1) == 628);
    CHECK(ROW_Y(2) == 1256);
}

static void test_col_x(void) {
    CHECK(COL_X(0) == 24);
    CHECK(COL_X(1) == 656);
    CHECK(COL_X(5) == 3184);
}

static void test_horizontal_sum(void) {
    CHECK(PAD_LEFT + COLS * UNIT_W + PAD_RIGHT == SCR_W);
    CHECK(PAD_LEFT + UNIT_W_N(COLS) + PAD_RIGHT == 3840);
}

static void test_vertical_sum(void) {
    CHECK(PAD_TOP + ROWS * UNIT_H + FOOTER_H == SCR_H);
    CHECK(PAD_TOP + UNIT_H_N(ROWS) + FOOTER_H == 2160);
}

static void test_content_size(void) {
    /* Content within a 1×1 cell */
    CHECK(UNIT_W - 2 * CELL_PAD == 624);
    CHECK(UNIT_H - 2 * CELL_PAD == 620);
}

static void test_constants(void) {
    CHECK(SCR_W == 3840);
    CHECK(SCR_H == 2160);
    CHECK(UNIT_W == 632);
    CHECK(UNIT_H == 628);
    CHECK(CELL_PAD == 4);
    CHECK(PAD_TOP == 0);
    CHECK(PAD_LEFT == 24);
    CHECK(PAD_RIGHT == 24);
    CHECK(FOOTER_H == 276);
    CHECK(COLS == 6);
    CHECK(ROWS == 3);
}

int main(void) {
    test_constants();
    test_unit_w_n();
    test_unit_h_n();
    test_row_y();
    test_col_x();
    test_horizontal_sum();
    test_vertical_sum();
    test_content_size();

    printf("test_layout: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
