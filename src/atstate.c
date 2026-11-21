/* atstate.c — the apt-temps widget's model (WI #4848). See the header. */
#include "atstate.h"

#include <stdio.h>
#include <string.h>

static const char *const SLUGS[AT_ZONES] = {"living", "kitchen", "bedroom"};
static const char *const NAMES[AT_ZONES] = {"Living", "Kitchen", "Bedroom"};

const char *at_zone_slug(int i) {
    return i >= 0 && i < AT_ZONES ? SLUGS[i] : "";
}

const char *at_zone_name(int i) {
    return i >= 0 && i < AT_ZONES ? NAMES[i] : "";
}

int at_zone_index(const char *slug) {
    if (!slug)
        return -1;
    for (int i = 0; i < AT_ZONES; i++)
        if (strcmp(slug, SLUGS[i]) == 0)
            return i;
    return -1;
}

void at_view(const apttemps_entry_t *e, int n, double now, at_view_t *out) {
    memset(out, 0, sizeof(*out));
    for (int k = 0; k < n; k++) {
        const apttemps_entry_t *a = &e[k];
        if (!a->valid)
            continue;
        const int i = at_zone_index(a->slug);
        if (i < 0) {
            if (now - a->last_payload_ts < APTTEMPS_FRESH_SECONDS)
                out->unknown = true;
            continue;
        }
        at_zone_t *z = &out->zone[i];
        z->present = true;
        z->color = apttemps_color(a, now);
        snprintf(z->temp, sizeof(z->temp), "%.1f\xC2\xB0", (double)a->temp_f);
        snprintf(z->hum, sizeof(z->hum), "%d%%", a->humidity_pct % 1000);
    }
}
