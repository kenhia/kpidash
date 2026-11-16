/* lampmach.c — the lamp panel's machines and the rotation between them
 * (WI #4783, sprint 045). See lampmach.h. */
#include "lampmach.h"

#include <math.h>
#include <string.h>

#include "altair.h"
#include "pdp8.h"

/* The mock-ups' mix(): per channel, rounded half up. */
static uint32_t mix(uint32_t a, uint32_t b, double t) {
    uint32_t out = 0;
    for (int s = 16; s >= 0; s -= 8) {
        double ca = (double)(a >> s & 0xff), cb = (double)(b >> s & 0xff);
        out |= (uint32_t)floor(ca + (cb - ca) * t + 0.5) << s;
    }
    return out;
}

void lm_palette(uint32_t card, uint32_t colour, int max_brightness, uint32_t out[LM_LEVELS]) {
    uint32_t off = mix(card, colour, LM_OFF_MIX / 100.0);
    uint32_t top = mix(off, colour, max_brightness / 100.0);
    for (int i = 0; i < LM_LEVELS; i++)
        out[i] = mix(off, top, (double)i / (LM_LEVELS - 1));
}

int lm_level(float frac) {
    int lv = (int)floor((double)frac * (LM_LEVELS - 1) + 0.5);
    if (lv < 0)
        return 0;
    return lv > LM_LEVELS - 1 ? LM_LEVELS - 1 : lv;
}

static void add_label(lm_layout_t *o, const char *t, int x, int base, int font, bool centre,
                      uint32_t rgb) {
    if (o->nlabels < LM_MAX_LABELS)
        o->labels[o->nlabels++] = (lm_label_t){t, (int16_t)x, (int16_t)base, (uint8_t)font,
                                               centre, rgb};
}

/* ---- The Altair 8800 (sprint 044), unchanged behind the interface ---- */

/* 64 KB of memory: never on the stack. */
static alt_machine_t g_alt;
static uint32_t g_alt_showings;

#define ALT_SEED 0x8800u

static void alt_reset_cb(void) {
    /* The first showing plays sprint 044's game, from its seed; each later
     * one a different game rather than a replay of the same one. */
    alt_reset(&g_alt, ALT_PROG_KTB, true, ALT_SEED + g_alt_showings++);
}

static void alt_advance_cb(uint32_t ms, float *frac) {
    alt_run_ms(&g_alt, ms);
    alt_fractions(&g_alt, frac);
}

/* Sprint 044's lamp_panel.c build(), as data: the same lamps, labels, fonts
 * and positions. */
static void alt_layout_cb(int free_w, lm_layout_t *o) {
    static const int lamp_y[2] = {ALT_LAMP_Y0, ALT_LAMP_Y1};
    static const int base[2] = {ALT_LABEL_BASE0, ALT_LABEL_BASE1};
    alt_layout_t L;
    alt_layout(free_w, &L);
    memset(o, 0, sizeof(*o));
    o->drawable = L.mode != ALT_MODE_NONE;
    o->radius = ALT_LAMP_R;
    if (!o->drawable)
        return;
    for (int i = 0; i < L.nlamps; i++) {
        const alt_lamp_t *l = &L.lamps[i];
        o->lamps[o->nlamps++] = (lm_lamp_t){(int16_t)l->x, (int16_t)lamp_y[l->row],
                                            (int16_t)L.lamp_w, ALT_LAMP_H, (uint8_t)l->id, 0};
        if (L.per_lamp)
            add_label(o, l->name, l->x + L.lamp_w / 2, base[l->row], LM_FONT_20, true,
                      ALT_LABEL_RGB);
    }
    if (L.per_lamp) {
        if (L.mode == ALT_MODE_FULL)
            add_label(o, "ALTAIR 8800", L.x_name, ALT_NAME_BASE, LM_FONT_24, true,
                      ALT_NAME_RGB);
    } else {
        add_label(o, "DATA", L.x_data, base[0], LM_FONT_20, false, ALT_LABEL_RGB);
        add_label(o, "ADDRESS", L.x_address, base[1], LM_FONT_20, false, ALT_LABEL_RGB);
        if (L.mode == ALT_MODE_FULL) {
            add_label(o, "STATUS", L.x_status, base[0], LM_FONT_20, false, ALT_LABEL_RGB);
            add_label(o, "8800", L.x_status, base[1], LM_FONT_20, false, ALT_NAME_RGB);
        } else {
            add_label(o, "ALTAIR 8800", L.x_address, base[0], LM_FONT_20, false,
                      ALT_NAME_RGB);
        }
    }
}

static const lm_machine_t ALTAIR = {
    .name = "Altair 8800",
    .nlamps = ALT_LAMPS,
    .ncolours = 1,
    .colours = {ALT_LAMP_RGB},
    .max_brightness = ALT_MAX_BRIGHTNESS,
    .card_rgb = ALT_PANEL_RGB,
    .reset = alt_reset_cb,
    .advance = alt_advance_cb,
    .layout = alt_layout_cb,
};

/* ---- The PDP-8/I (sprint 045) ---- */

static pdp8_t g_pdp8;

static void pdp8_reset_cb(void) { pdp8_reset(&g_pdp8, PDP8_PROG); }

static void pdp8_advance_cb(uint32_t ms, float *frac) {
    pdp8_run_ms(&g_pdp8, ms);
    pdp8_fractions(&g_pdp8, frac);
}

static void pdp8_layout_cb(int free_w, lm_layout_t *o) {
    /* The mock-up's weights as the repo's fonts, as sprint 044 mapped them:
     * 700 20 px and 600 20 px are bold 20; the 700 22 px name is bold 24,
     * the nearest generated size at or above it. */
    static const uint8_t FONT[] = {[PDP8_F_ROW] = LM_FONT_20, [PDP8_F_NAME] = LM_FONT_24,
                                   [PDP8_F_SIDE] = LM_FONT_20};
    pdp8_layout_t L;
    pdp8_layout(free_w, &L);
    memset(o, 0, sizeof(*o));
    o->drawable = L.mode != PDP8_MODE_NONE;
    o->radius = PDP8_LAMP_R;
    for (int i = 0; i < L.nlamps; i++) {
        const pdp8_lamp_t *l = &L.lamps[i];
        o->lamps[o->nlamps++] = (lm_lamp_t){(int16_t)l->x, (int16_t)l->y, (int16_t)l->w,
                                            (int16_t)l->h, (uint8_t)l->id,
                                            (uint8_t)pdp8_colour(l->id)};
    }
    for (int i = 0; i < L.nlabels; i++) {
        const pdp8_label_t *b = &L.labels[i];
        add_label(o, b->text, b->x, b->base, FONT[b->font], false, b->rgb);
    }
}

static const lm_machine_t PDP8 = {
    .name = "PDP-8/I",
    .nlamps = PDP8_LAMPS,
    .ncolours = PDP8_NCOLOURS,
    .colours = {PDP8_RED_RGB, PDP8_YELLOW_RGB, PDP8_GREEN_RGB, PDP8_BLUE_RGB},
    .max_brightness = PDP8_MAX_BRIGHTNESS,
    .card_rgb = PDP8_PANEL_RGB,
    .reset = pdp8_reset_cb,
    .advance = pdp8_advance_cb,
    .layout = pdp8_layout_cb,
};

const lm_machine_t *const LM_MACHINES[LM_NMACHINES] = {[LM_ALTAIR] = &ALTAIR, [LM_PDP8] = &PDP8};

/* ---- Rotation ---- */

void lm_rotation_init(lm_rotation_t *r) {
    r->cur = -1;
    r->since = 0;
}

bool lm_rotation_tick(lm_rotation_t *r, uint32_t now, int period_s,
                      const bool drawable[LM_NMACHINES]) {
    if (period_s < LM_PERIOD_MIN_S)
        period_s = LM_PERIOD_MIN_S;
    if (r->cur >= 0 && drawable[r->cur]) {
        if (now - r->since < (uint32_t)period_s * 1000u)
            return false;
        for (int k = 1; k < LM_NMACHINES; k++) {
            int i = (r->cur + k) % LM_NMACHINES;
            if (drawable[i]) {
                r->cur = i;
                r->since = now;
                return true;
            }
        }
        /* The only one that fits: it stays, and keeps running. */
        r->since = now;
        return false;
    }
    int start = r->cur < 0 ? 0 : r->cur + 1;
    for (int k = 0; k < LM_NMACHINES; k++) {
        int i = (start + k) % LM_NMACHINES;
        if (drawable[i]) {
            r->cur = i;
            r->since = now;
            return true;
        }
    }
    if (r->cur < 0)
        return false;
    r->cur = -1;
    return true;
}
