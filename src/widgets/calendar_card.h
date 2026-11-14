#ifndef CALENDAR_CARD_H
#define CALENDAR_CARD_H

/**
 * calendar_card.h — the time and calendar card (WI #4665, sprint 040).
 *
 * Row 3, columns 4-5 (ROW_Y(2), COL_X(4) in code): a 2×1 at the right end of
 * the row, card A of sprints/planning/calendar-mockups.html with Ken's
 * 24-hour ruling. The left column is the clock (HH:MM), the weekday and date,
 * and the next four tagged days; the right is the month grid, marking
 * weekends, Ken's days off, holidays and special days. The model is
 * calstate; this only draws it. One object, drawn in a single DRAW_MAIN pass
 * with opaque fills only (sprint 030). The clock repaints on the minute and
 * only its own rectangle; the rest repaints when something in it changes.
 */

#include <time.h>

#include "lvgl.h"

#define CAL_CARD_W 1256 /* 2 * UNIT_W - 2 * CELL_PAD */
#define CAL_CARD_H 620  /* UNIT_H - 2 * CELL_PAD */

lv_obj_t *calendar_card_create(lv_obj_t *parent);
/* Redraw from the local clock at `now` and calstate's record. */
void calendar_card_update(lv_obj_t *card, time_t now);

#endif /* CALENDAR_CARD_H */
