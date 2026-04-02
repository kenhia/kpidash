#ifndef WIDGETS_ACTIVITIES_H
#define WIDGETS_ACTIVITIES_H

#include "lvgl.h"
#include "registry.h"

/**
 * Create the Activities widget container (no scroll, FR-008).
 */
lv_obj_t *activities_widget_create(lv_obj_t *parent);

/**
 * Redraw activity rows from the provided list.
 * Active entries show a live elapsed timer via LVGL timer;
 * done entries show static total duration.
 */
void activities_widget_update(lv_obj_t *widget, const activity_t *list, int count);

#endif /* WIDGETS_ACTIVITIES_H */
