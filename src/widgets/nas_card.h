#ifndef WIDGETS_NAS_CARD_H
#define WIDGETS_NAS_CARD_H

#include "lvgl.h"

/* WI #3956: the NAS big panel — a 2×1 pool widget for KPIDASH_NAS_HOST, in
 * the slot that host's dev graph would otherwise take. Left: pool usage bars,
 * the root filesystem, and a small CPU/RAM activity chart. Right: one row per
 * disk — temperature, spin state, SMART, wear. Reads nasstate (kdash:nas),
 * the host's client telemetry (root fs) and its dev_telemetry series. One
 * instance; created once, refreshed every second. */
lv_obj_t *nas_card_create(lv_obj_t *parent);
void nas_card_update(lv_obj_t *card, double now);

#endif /* WIDGETS_NAS_CARD_H */
