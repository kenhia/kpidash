#ifndef UI_H
#define UI_H

#ifndef KPIDASH_TEST_STUBS
#include "lvgl.h"
#endif
#include "status.h"

/* WI #4062: the network-health card takes Fortune's 2×1 cell. Fortune is
 * compiled out, not deleted — Ken brings it back once the host cards and dev
 * graphs are redesigned smaller. main.c reads this too, so that the fortune
 * engine (which shells out to `fortune` on a timer) does not run for a widget
 * that does not exist. */
#define KPIDASH_WIDGET_FORTUNE 0
#define KPIDASH_WIDGET_NETCARD 1

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
