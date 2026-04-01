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

#endif /* WIDGETS_FORTUNE_H */
