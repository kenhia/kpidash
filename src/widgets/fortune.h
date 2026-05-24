#ifndef WIDGETS_FORTUNE_H
#define WIDGETS_FORTUNE_H

#include "lvgl.h"

/**
 * Create the fortune widget label (no scroll, FR-008).
 */
lv_obj_t *fortune_widget_create(lv_obj_t *parent);

/**
 * Update the displayed fortune text with word-wrap.
 */
void fortune_widget_update(lv_obj_t *widget, const char *text);

/**
 * Sprint 006 / T014: pool-placeable Fortune renderer.
 * Creates a 2×1 cell widget (UNIT_W_N(2) × UNIT_H_N(1)) at absolute (x, y)
 * with a larger fixed font for rows-2-3 placement. FR-007, FR-008.
 */
lv_obj_t *fortune_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y);

#endif /* WIDGETS_FORTUNE_H */
