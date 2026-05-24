/* test_icon_registry.c — Icon registry tests (T019).
 * Covers icons_lookup / icons_codepoint per FR-023, FR-026 and
 * contracts/icon-registry.md. */
#include "icons.h"

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

int main(void) {
    CHECK(ICON_REGISTRY_COUNT >= 10);

    CHECK(icons_lookup(-1) == NULL);
    CHECK(icons_lookup(99999) == NULL);
    CHECK(icons_codepoint(-1) == 0);
    CHECK(icons_codepoint(99999) == 0);

    const char *g0 = icons_lookup(0);
    CHECK(g0 != NULL);
    CHECK(strlen(g0) >= 1 && strlen(g0) <= 4);
    CHECK(icons_codepoint(0) == ICON_REGISTRY[0].codepoint);

    /* All registry entries are resolvable; codepoints in 0xF300+ encode to 3-byte UTF-8. */
    for (size_t i = 0; i < ICON_REGISTRY_COUNT; i++) {
        int idx = ICON_REGISTRY[i].index;
        const char *g = icons_lookup(idx);
        CHECK(g != NULL);
        if (g) {
            size_t len = strlen(g);
            CHECK(len == 3);
            CHECK(((unsigned char)g[0] & 0xF0) == 0xE0);
        }
        CHECK(icons_codepoint(idx) == ICON_REGISTRY[i].codepoint);
    }

    /* Indices are append-only contiguous from 0. */
    for (size_t i = 0; i < ICON_REGISTRY_COUNT; i++) {
        CHECK((size_t)ICON_REGISTRY[i].index == i);
    }

    fprintf(stderr, "test_icon_registry: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
