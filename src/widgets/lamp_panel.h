#ifndef WIDGETS_LAMP_PANEL_H
#define WIDGETS_LAMP_PANEL_H

#include <stdint.h>

#include "lvgl.h"

/**
 * Altair 8800 lamp panel (WI #4777, sprint 044): decoration in the footer,
 * from the end of the last status card to the right edge, at the status
 * cards' 240 px height. The mock-up Ken approved is
 * sprints/planning/altair-mockups.html; the model (the 8080, the lamp
 * accumulators, the layout, the colours) is src/altair.c, and this file only
 * draws.
 *
 * Plain objects: an opaque Mocha card, one opaque rounded rectangle per lamp
 * and a label per lamp or per group. Every colour is one of ALT_LEVELS
 * pre-mixed opaque fills, computed once (no opacity styles: on the Pi a
 * translucent fill reads back every pixel it covers). A redraw sets the fill
 * only on lamps whose level changed, so LVGL repaints just those.
 *
 * An LVGL timer every ALT_REDRAW_MS runs the emulator for the time that
 * passed and repaints. Nothing runs while the panel is hidden.
 */

lv_obj_t *lamp_panel_create(lv_obj_t *parent);

/* Give the panel the free span [x, x + w) at its current y. Lays the lamps
 * out again only when `w` changes; hides the panel when it is too narrow. */
void lamp_panel_place(lv_obj_t *panel, int32_t x, int32_t w);

/* Run `ms` of emulated time and repaint the lamps that changed. The timer
 * calls this; the render harness calls it directly, for a deterministic run. */
void lamp_panel_advance(lv_obj_t *panel, uint32_t ms);

#endif /* WIDGETS_LAMP_PANEL_H */
