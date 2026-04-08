#ifndef WIDGETS_DEV_GRID_H
#define WIDGETS_DEV_GRID_H

#include "lvgl.h"

/**
 * Create a full-screen grid overlay at the top of the z-order.
 *
 * Pixel mode (unit_mode=false): lines every grid_size pixels.
 * Unit mode  (unit_mode=true):  lines at interior cell boundaries
 *   using layout.h constants, skipping screen edges.
 *   unit_size is a multiplier (0.5, 1, 2).
 *
 * Returns the overlay root object (child of parent).
 * Call dev_grid_destroy() to remove it.
 */
lv_obj_t *dev_grid_create(lv_obj_t *parent, int grid_size, bool unit_mode, float unit_size);

/**
 * Destroy the grid overlay created by dev_grid_create().
 * Safe to call with a NULL argument.
 */
void dev_grid_destroy(lv_obj_t *grid);

#endif /* WIDGETS_DEV_GRID_H */
