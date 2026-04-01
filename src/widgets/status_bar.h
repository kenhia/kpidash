#ifndef WIDGETS_STATUS_BAR_H
#define WIDGETS_STATUS_BAR_H

#include "lvgl.h"
#include "status.h"

/**
 * Create the status bar widget, initially hidden.
 * No scroll containers or interactive elements (FR-008).
 */
lv_obj_t *status_bar_create(lv_obj_t *parent);

/**
 * Make the status bar visible with severity-appropriate color.
 * warning → yellow; error → red.
 */
void status_bar_show(lv_obj_t *bar, status_severity_t severity, const char *message);

/**
 * Hide the status bar. Does not free it.
 */
void status_bar_hide(lv_obj_t *bar);

#endif /* WIDGETS_STATUS_BAR_H */
