#ifndef WIDGETS_DEV_GRID_H
#define WIDGETS_DEV_GRID_H

#include "lvgl.h"

/**
 * Create a full-screen grid overlay at the top of the z-order.
 * Lines are drawn every grid_size pixels (horizontal + vertical).
 * Returns the overlay root object (already a child of parent).
 * Call dev_grid_destroy() to remove it.
 */
lv_obj_t *dev_grid_create(lv_obj_t *parent, int grid_size);

/**
 * Destroy the grid overlay created by dev_grid_create().
 * Safe to call with a NULL argument.
 */
void dev_grid_destroy(lv_obj_t *grid);

#endif /* WIDGETS_DEV_GRID_H */
