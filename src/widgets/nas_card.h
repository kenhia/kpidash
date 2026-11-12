#ifndef WIDGETS_NAS_CARD_H
#define WIDGETS_NAS_CARD_H

#include "lvgl.h"

/* WI #3956: the NAS big panel — a 2×1 card for KPIDASH_NAS_HOST, fixed in
 * row 1, columns 2-3, next to the 10G card (sprint 032). Since sprint 033
 * (WI #4566) it is "A · Rail" in mauve: a state headline and three stat
 * boxes, the pool and root bars, and eight disk tiles on a bus, in Ken's
 * order with a use label above each (nas_disk_uses.h). Reads nasstate
 * (kdash:nas) and the host's client telemetry (root fs). One instance;
 * created once, refreshed every second. */
lv_obj_t *nas_card_create(lv_obj_t *parent);
void nas_card_update(lv_obj_t *card, double now);

#endif /* WIDGETS_NAS_CARD_H */
