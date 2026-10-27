#ifndef WIDGETS_NET_CARD_H
#define WIDGETS_NET_CARD_H

#include "lvgl.h"

/**
 * net_card — the 2×1 network-health card, "B · Rail" (WI #4062, korg:4059).
 *
 * A verdict headline and three tiles over a backplane rail, then one column
 * per host: speed numeral, a 60-minute throughput sparkline with link-drop
 * ticks, LFC / rx errors, UDM · PEERS · WAN pills and the feed's age. Takes
 * Fortune's cell in the rows-2-3 pool. Draws whatever netstate.c holds; it
 * owns no state of its own beyond its LVGL objects.
 */

lv_obj_t *net_card_create(lv_obj_t *parent);

/* Repaint from netstate. `now` is unix seconds. */
void net_card_update(lv_obj_t *card, double now);

#endif /* WIDGETS_NET_CARD_H */
