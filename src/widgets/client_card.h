#ifndef WIDGETS_CLIENT_CARD_H
#define WIDGETS_CLIENT_CARD_H

#include "lvgl.h"
#include "registry.h"

/**
 * Create a client card widget as a child of parent.
 * No scroll containers or interactive elements (FR-008).
 * Returns the card's root lv_obj_t.
 */
lv_obj_t *client_card_create(lv_obj_t *parent, const char *hostname);

/**
 * Update the health indicator (LED) and uptime text.
 * online=true → green; online=false → red.
 */
void client_card_update_health(lv_obj_t *card, bool online, float uptime_s);

/**
 * Update telemetry section from a client_info_t snapshot.
 * GPU section is shown only when c->gpu.present == true.
 * Disk rows are rendered for each entry in c->disks[].
 */
void client_card_update_telemetry(lv_obj_t *card, const client_info_t *c);

#endif /* WIDGETS_CLIENT_CARD_H */
