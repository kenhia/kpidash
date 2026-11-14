#ifndef KORG_KLAMS_CARD_H
#define KORG_KLAMS_CARD_H

/**
 * korg_klams_card.h — korg and klams sharing one 1×1 (WI #4613, sprint 038).
 *
 * Row 2, column 4 (ROW_Y(1), COL_X(4) in code). The top half is korg
 * (lavender): the open count, its 30-day line, Awaiting Ken and active, the
 * top project and a bar with one segment per project. The bottom half is klams (sapphire): its health dot and
 * knowledge-chunk count, one row per scanner, and the subsystem dots. Each
 * half lives on its own feed and goes grey on its own. The model is kkstate;
 * this only draws it. One object, drawn in a single DRAW_MAIN pass with opaque
 * fills only, and a change repaints only the half it is in.
 */

#include "lvgl.h"

#define KK_CARD_W 624 /* UNIT_W - 2 * CELL_PAD */
#define KK_CARD_H 620 /* UNIT_H - 2 * CELL_PAD */

lv_obj_t *korg_klams_card_create(lv_obj_t *parent);
/* Redraw from kkstate's records at `now`. */
void korg_klams_card_update(lv_obj_t *card, double now);

#endif /* KORG_KLAMS_CARD_H */
