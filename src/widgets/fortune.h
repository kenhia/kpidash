#ifndef WIDGETS_FORTUNE_H
#define WIDGETS_FORTUNE_H

#include "lvgl.h"

/**
 * Create the fortune widget label (no scroll, FR-008).
 */
lv_obj_t *fortune_widget_create(lv_obj_t *parent);

/**
 * Update the displayed fortune text and font.
 * font_size_pt selects from the ladder {20, 24, 28, 36}; any other value
 * falls back to 36.
 */
void fortune_widget_update(lv_obj_t *widget, const char *text, int font_size_pt);

/**
 * Sprint 006 / T014: pool-placeable Fortune renderer.
 * Creates a 2×1 cell widget (UNIT_W_N(2) × UNIT_H_N(1)) at absolute (x, y)
 * with a larger fixed font for rows-2-3 placement. FR-007, FR-008.
 */
lv_obj_t *fortune_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y);

/**
 * Show or hide the dev-stats overlay (child 1) on the fortune widget.
 * When visible, displays "Rejects: N  Elapsed: N ms" in 14 pt blue at top-left.
 * Overlay is hidden by default; appears only when kpidash:cmd:fortune_dev is set.
 */
void fortune_widget_set_dev_stats(lv_obj_t *widget, bool show, int rejects, long elapsed_ms);

#endif /* WIDGETS_FORTUNE_H */
