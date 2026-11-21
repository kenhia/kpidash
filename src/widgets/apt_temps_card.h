#ifndef WIDGETS_APT_TEMPS_CARD_H
#define WIDGETS_APT_TEMPS_CARD_H

/**
 * apt_temps_card.h — the apt-temps widget (WI #4848, sprint 047; the per-zone
 * footer cards it replaces were WI #364).
 *
 * Row 3 (ROW_Y(2) in code), between the Agents card and the time + calendar
 * card: one host column wide and the row's full height. Three zones top to
 * bottom, Living, Kitchen, Bedroom, each with its name, the temperature in
 * bold 48 and the humidity under it, all three centred (WI #4870). The
 * temperature is coloured by kdashdata's band and gray once 300 s old. A zone with no reading keeps its slot,
 * muted. Any other fresh kpidash:apttemps:* key is not drawn: it turns the
 * widget's border green. The model is atstate; this only draws it, in one
 * DRAW_MAIN pass with opaque fills only (sprint 030), repainting only when
 * the view changes.
 */

#include "layout.h"
#include "lvgl.h"

#define AT_CARD_W (ROW2_APT_W - 2 * CELL_PAD) /* 368 */
#define AT_CARD_H (UNIT_H - 2 * CELL_PAD)     /* 620 */

lv_obj_t *apt_temps_card_create(lv_obj_t *parent);
/* Redraw from the apt-temps registry at `now`, if anything visible changed. */
void apt_temps_card_update(lv_obj_t *card, double now);

#endif /* WIDGETS_APT_TEMPS_CARD_H */
