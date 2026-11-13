#ifndef WIDGETS_WEATHER_CARD_H
#define WIDGETS_WEATHER_CARD_H

#include "lvgl.h"

/**
 * Weather filler (WI #4576, sprint 034): the "C · Range" mock-up Ken picked on
 * 2026-11-12 (sprints/planning/weather-mockups.html, function vC). Drawn in
 * the columns the host row leaves free, at one to four host-column widths:
 * current conditions on the left, a low-to-high bar per forecast day on one
 * shared temperature scale, and the local day as a bar along the bottom.
 *
 * One plain object drawn by a callback, like the host columns: an update
 * allocates nothing, and a repaint is limited to what changed — the whole
 * card for a new record or width, only the age line or only the sun bar's
 * "now" marker otherwise, so the minute-by-minute changes cost a strip.
 * The ten condition icons are drawn from circles, lines and triangles rather
 * than pre-rendered images: every fill is opaque, they recolour for the stale
 * state for free, and the card repaints them only when a record arrives.
 *
 * The model is wxstate; this file only draws.
 */

lv_obj_t *weather_card_create(lv_obj_t *parent);

/* Size the card for `ncols` host columns (1-4) and repaint from the current
 * wxstate record at wall time `now`. The caller hides the card when there is
 * no record or no room. */
void weather_card_update(lv_obj_t *card, int ncols, double now);

#endif /* WIDGETS_WEATHER_CARD_H */
