#ifndef WIDGETS_DEV_TEXTSIZE_H
#define WIDGETS_DEV_TEXTSIZE_H

#include "lvgl.h"

/**
 * Create a floating panel showing sample text at the font sizes
 * available in this build (14, 16, 20, 24, 28px).
 * The panel is positioned top-right and raised to the foreground.
 * Call dev_textsize_destroy() to remove it.
 */
lv_obj_t *dev_textsize_create(lv_obj_t *parent);

/**
 * Destroy the textsize panel. Safe to call with NULL.
 */
void dev_textsize_destroy(lv_obj_t *panel);

#endif /* WIDGETS_DEV_TEXTSIZE_H */
