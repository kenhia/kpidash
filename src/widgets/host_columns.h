#ifndef WIDGETS_HOST_COLUMNS_H
#define WIDGETS_HOST_COLUMNS_H

#include "hostcols.h"
#include "lvgl.h"

/**
 * Host columns (WI #4524, #4525; widened in WI #4543): one widget across the
 * whole top row, 3792 x 628, holding up to ten full host columns, demoting the
 * lowest priorities to compact tiles three to a column when more hosts report
 * — thirty at most. Attention is border colour only: nothing grows.
 *
 * Pixel spec: sprints/planning/hosts-wide-mockups.html (`colFull`, `tile`,
 * `widget`), sprint 030. Colours are Ken's (WI #4543 and its comment): the top
 * core in WS_COLOR_TOP_CORE, the RAM figure in the RAM bar's mauve, the VRAM
 * figures in the VRAM bar's peach.
 *
 * Every column and tile is one plain object drawn by a callback, so an update
 * allocates nothing and a layout change only moves, resizes and hides.
 * No scroll, no interaction (FR-008).
 */

/* Create the widget at (x, y) — the top-left of its 6x1 cell block. */
lv_obj_t *host_columns_create(lv_obj_t *parent, int32_t x, int32_t y);

/* Lay out and repaint from `hosts`, already in priority order (see
 * hostcols_select). Each host's sparklines are read from hosthist. Hosts past
 * HOSTCOLS_MAX are ignored. Only slots whose content changed are redrawn. */
void host_columns_update(lv_obj_t *w, const hc_host_t *hosts, int n);

#endif /* WIDGETS_HOST_COLUMNS_H */
