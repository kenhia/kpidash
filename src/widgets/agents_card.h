#ifndef AGENTS_CARD_H
#define AGENTS_CARD_H

/**
 * agents_card.h — the Agents card (WI #4695, sprint 041).
 *
 * Row 3, columns 0-3 (ROW_Y(2), COL_X(0) in code): a 4×1 left of the
 * calendar, the "Border colour" + links "On" view of
 * sprints/planning/agents-mockups.html (`borders(true)`), as Ken ruled on
 * 2026-11-14. The last 24 hours of agent sessions as bars bordered in their
 * host's colour, packed upward, legs hung under their overseer by stems,
 * ships as teal diamonds, the now line at the right edge. The model is
 * agstate; this only draws it. One object, drawn in a single DRAW_MAIN pass
 * with opaque fills only (sprint 030). It repaints when the record changes
 * and when the minute turns, never between.
 */

#include <time.h>

#include "lvgl.h"

#define AG_CARD_W 2520 /* 4 * UNIT_W - 2 * CELL_PAD */
#define AG_CARD_H 620  /* UNIT_H - 2 * CELL_PAD */

lv_obj_t *agents_card_create(lv_obj_t *parent);
/* Redraw from agstate's record at `now` (the card steps once a minute). */
void agents_card_update(lv_obj_t *card, time_t now);

#endif /* AGENTS_CARD_H */
