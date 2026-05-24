#ifndef WIDGETS_SERVICE_CARD_H
#define WIDGETS_SERVICE_CARD_H

#include "lvgl.h"

struct service_entry;

/* Build the service card (border + icon + name + text) inside parent.
 * Stores LVGL handles on the entry. */
lv_obj_t *service_card_create(lv_obj_t *parent, struct service_entry *e);

/* Recompute border colour based on freshness and last_valid_state. */
void service_card_update(struct service_entry *e, double now);

#endif /* WIDGETS_SERVICE_CARD_H */
