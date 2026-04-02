#ifndef UI_H
#define UI_H

#ifndef KPIDASH_TEST_STUBS
#include "lvgl.h"
#endif
#include "status.h"

/**
 * Create the full dashboard screen (no title bar, FR-002a).
 * Must be called after LVGL display is initialized.
 * Registers the fortune widget with fortune.c via fortune_set_widget().
 */
void ui_init(void);

/**
 * Refresh all client cards, activities, repo status, and fortune
 * from the current registry and shared state.
 * Called every 1s from the LVGL timer in main.c.
 * No scroll containers or interactive elements anywhere (FR-008).
 */
void ui_refresh(void);

/**
 * Show/hide the Redis unavailable overlay (FR-005, C1).
 * Safe to call from any LVGL-thread context.
 */
void ui_show_redis_error(const char *msg);
void ui_hide_redis_error(void);

/**
 * Show/hide the status bar (called by status.c).
 */
void ui_status_bar_show(status_severity_t severity, const char *message);
void ui_status_bar_hide(void);

#endif /* UI_H */
