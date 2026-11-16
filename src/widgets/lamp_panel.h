#ifndef WIDGETS_LAMP_PANEL_H
#define WIDGETS_LAMP_PANEL_H

#include <stdint.h>

#include "lvgl.h"

/**
 * The footer's lamp panel: decoration from the end of the last status card
 * to the right edge, at the status cards' 240 px height. It shows one
 * machine's front panel at a time — the Altair 8800 (WI #4777, sprint 044)
 * or the PDP-8/I (WI #4783, sprint 045) — and rotates between them. The
 * mock-ups Ken approved are sprints/planning/altair-mockups.html and
 * pdp8-mockups.html; the machines (emulators, layouts, colours) and the
 * rotation are src/lampmach.c, and this file only draws.
 *
 * Plain objects: an opaque Mocha card, one opaque rounded rectangle per lamp
 * and the machine's labels. Every lamp colour is one of LM_LEVELS pre-mixed
 * opaque fills per colour per machine, computed once (no opacity styles: on
 * the Pi a translucent fill reads back every pixel it covers). A redraw sets
 * the fill only on lamps whose level changed, so LVGL repaints just those.
 *
 * An LVGL timer every LM_REDRAW_MS asks the rotation which machine is on
 * screen, runs that one for the time that passed and repaints. A switch is a
 * plain cut: clear the card, start the next machine from reset, draw it.
 * Only the machine on screen runs, and nothing runs while the panel is
 * hidden.
 */

#define LAMP_PANEL_H 240 /* the status cards' height */
#define LAMP_PANEL_R 12

lv_obj_t *lamp_panel_create(lv_obj_t *parent);

/* Give the panel the free span [x, x + w) at its current y. Lays the
 * machines out again only when `w` changes; a machine that no longer fits is
 * replaced at once, and the panel hides when none fits. */
void lamp_panel_place(lv_obj_t *panel, int32_t x, int32_t w);

/* Each machine's time on screen, in seconds (already defaulted and clamped:
 * redis_parse_lamp_period). Read at every timer tick, so a change applies
 * within LM_REDRAW_MS. */
void lamp_panel_set_period(lv_obj_t *panel, int period_s);

/* Run `ms` of emulated time and repaint the lamps that changed. The timer
 * calls this; the render harness calls it directly, for a deterministic run. */
void lamp_panel_advance(lv_obj_t *panel, uint32_t ms);

/* Put machine `machine` (LM_ALTAIR, LM_PDP8) on screen now, from reset, if it
 * fits. For the render harness; the rotation does this itself. */
void lamp_panel_show(lv_obj_t *panel, int machine);

#endif /* WIDGETS_LAMP_PANEL_H */
