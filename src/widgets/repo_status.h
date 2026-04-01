#ifndef WIDGETS_REPO_STATUS_H
#define WIDGETS_REPO_STATUS_H

#include "lvgl.h"
#include "registry.h"

/**
 * Create the Repo Status widget container (no scroll, FR-008).
 */
lv_obj_t *repo_status_widget_create(lv_obj_t *parent);

/**
 * Redraw repo rows. If count == 0, shows "all clean" placeholder.
 */
void repo_status_widget_update(lv_obj_t *widget, const repo_entry_t *list, int count);

#endif /* WIDGETS_REPO_STATUS_H */
