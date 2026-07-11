#ifndef WIDGETS_APT_TEMPS_CARD_H
#define WIDGETS_APT_TEMPS_CARD_H

#include "lvgl.h"

struct apttemps_entry;

/* Build an apt-temps card (zone / temp°F / humidity%) inside parent.
 * Stores LVGL handles on the entry. WI #364. */
lv_obj_t *apt_temps_card_create(lv_obj_t *parent, struct apttemps_entry *e);

/* Refresh labels and recolour the temperature by band / staleness. */
void apt_temps_card_update(struct apttemps_entry *e, double now);

#endif /* WIDGETS_APT_TEMPS_CARD_H */
