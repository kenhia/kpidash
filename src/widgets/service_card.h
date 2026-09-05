#ifndef WIDGETS_SERVICE_CARD_H
#define WIDGETS_SERVICE_CARD_H

#include "lvgl.h"

struct service_entry;
struct stale_entry;

/* Build the service card (border + icon + name + text) inside parent.
 * Stores LVGL handles on the entry. */
lv_obj_t *service_card_create(lv_obj_t *parent, struct service_entry *e);

/* Recompute border colour based on freshness and last_valid_state. */
void service_card_update(struct service_entry *e, double now);

/* WI #1903: the host-staleness card — "<host> stale" over the deployers that
 * flagged it. Same module rather than a new widget class, so it shares this
 * card's footprint and colour band by construction instead of by two sets of
 * constants drifting apart.
 *
 * It takes no `now`: the staleness feed is presence-owned and has no
 * freshness window, so the card's colour never depends on the clock. */
lv_obj_t *stale_card_create(lv_obj_t *parent, struct stale_entry *e);
void stale_card_update(struct stale_entry *e);

#endif /* WIDGETS_SERVICE_CARD_H */
