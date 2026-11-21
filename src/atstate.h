#ifndef ATSTATE_H
#define ATSTATE_H

/**
 * atstate.h — the apt-temps widget's model (WI #4848, sprint 047).
 *
 * One widget in row 2, beside the Agents card, replaces the per-zone cards
 * the footer strip used to carry. It always shows the same three zones, top
 * to bottom: Living, Kitchen, Bedroom. A zone with no reading keeps its slot,
 * drawn muted, so the widget never changes shape.
 *
 * Any other fresh kpidash:apttemps:* key is not drawn. It turns the widget's
 * border green instead: a new sensor has registered itself in apt-temps and
 * wants naming or disabling. A stale one does not count.
 *
 * Pure — no LVGL, no Redis, the clock passed in — so all of it is reachable
 * from the gate. The input is a snapshot of the apt-temps registry; the band
 * colour and the 300 s freshness rule are apttemps_color()'s, unchanged.
 */

#include <stdbool.h>

#include "registry.h"

#define AT_ZONES 3

/* The zones in the order they are drawn, top to bottom. */
const char *at_zone_slug(int i);
const char *at_zone_name(int i);
/* The slot a slug is drawn in, or -1 when it is not one of the three. */
int at_zone_index(const char *slug);

typedef struct {
    bool present;          /* a valid reading exists, fresh or not */
    apttemps_color_t color; /* GRAY when stale, or absent */
    char temp[16];         /* "72.4°F" (WI #4870: bold 48 has the F) */
    char hum[8];           /* "45%" */
} at_zone_t;

typedef struct {
    at_zone_t zone[AT_ZONES];
    bool unknown; /* a fresh reading from a slug that is not one of the three */
} at_view_t;

/* Build the view from `n` registry entries at `now`. Zeroes `out` first, so
 * two views of the same state compare equal with memcmp. */
void at_view(const apttemps_entry_t *e, int n, double now, at_view_t *out);

#endif /* ATSTATE_H */
