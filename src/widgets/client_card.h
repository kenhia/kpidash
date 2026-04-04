#ifndef WIDGETS_CLIENT_CARD_H
#define WIDGETS_CLIENT_CARD_H

#include "lvgl.h"
#include "registry.h"

/**
 * Create a client card widget with arc gauges, health circle,
 * hostname/OS bar, and disk bars.  ~304px wide, fits 6 across
 * at 1920.  No scroll containers or interactive elements (FR-008).
 */
lv_obj_t *client_card_create(lv_obj_t *parent, const char *hostname);

/**
 * Update health-related fields: center circle color, uptime, OS name.
 */
void client_card_update_health(lv_obj_t *card, const client_info_t *c);

/**
 * Update telemetry arcs, text boxes, and disk bars from a
 * client_info_t snapshot.  GPU section grayed when !gpu.present.
 */
void client_card_update_telemetry(lv_obj_t *card, const client_info_t *c);

#endif /* WIDGETS_CLIENT_CARD_H */
