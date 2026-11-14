#ifndef LIMITS_CARD_H
#define LIMITS_CARD_H

/**
 * limits_card.h — the Claude limits card (WI #4609, sprint 037).
 *
 * A 1×1 in row 2, column 5: two rings for the 5-hour and 7-day windows with a
 * pace tick each, and a bar for the model-scoped weekly window. The model is
 * limstate; this only draws it. One object, drawn in a single DRAW_MAIN pass
 * with opaque fills only. A change repaints the card; the header age, which
 * changes every second, repaints its own strip.
 */

#include "lvgl.h"

#define LIM_CARD_W 624 /* UNIT_W - 2 * CELL_PAD */
#define LIM_CARD_H 620 /* UNIT_H - 2 * CELL_PAD */

lv_obj_t *limits_card_create(lv_obj_t *parent);
/* Redraw from limstate's record at `now`. Does nothing before a record. */
void limits_card_update(lv_obj_t *card, double now);

#endif /* LIMITS_CARD_H */
