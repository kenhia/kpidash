#ifndef FORTUNE_H
#define FORTUNE_H

#include "config.h"
#ifndef KPIDASH_TEST_STUBS
#include "lvgl.h"
#endif

/**
 * Check for availability of the `fortune` binary and register the
 * rotation timer (FORTUNE_INTERVAL_S seconds).
 * Must be called after ui_init() so the fortune widget exists.
 * Runs the first fortune immediately.
 */
void fortune_init(const kpidash_config_t *cfg);

/**
 * Tell fortune.c which LVGL widget to update.
 * Called from ui_init() after creating the fortune widget.
 */
#ifndef KPIDASH_TEST_STUBS
void fortune_set_widget(lv_obj_t *widget);
#endif

/**
 * Called by redis.c when kpidash:fortune:pushed is found.
 * Parses JSON {"text":"..."} and updates the fortune widget immediately.
 */
void fortune_on_pushed(const char *json);

#endif /* FORTUNE_H */
