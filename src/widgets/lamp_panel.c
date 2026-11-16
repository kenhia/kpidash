/* lamp_panel.c — Altair 8800 lamp panel (WI #4777, sprint 044). See the
 * header; the model is src/altair.c. */
#include "widgets/lamp_panel.h"

#include <string.h>

#include "altair.h"
#include "widgets/common.h"

#define LP_FONT_LABEL (&lv_font_montserrat_bold_20) /* the mock-up's 600 20 px */
#define LP_FONT_NAME_WIDE (&lv_font_montserrat_bold_24) /* its 700 22 px name, per-lamp mode */
#define LP_FONT_NAME (&lv_font_montserrat_bold_20)      /* its 700 20 px name, group mode */
#define LP_SEED 0x8800u

/* One panel on the screen, so its state is file-static. The machine carries
 * its 64 KB of memory: never on the stack. */
static alt_machine_t g_m;
static alt_layout_t g_lay;
static lv_obj_t *g_lamp[ALT_LAMPS];
static int8_t g_shown[ALT_LAMPS];
static float g_frac[ALT_LAMPS];
static lv_color_t g_pal[ALT_LEVELS];
static int32_t g_w = -1;
static uint32_t g_last_tick;

static void no_scroll(lv_obj_t *o) {
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

/* A label whose baseline is at `base` (the mock-up places text by its
 * baseline), left-aligned at `x` or centred on it. */
static void label(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t rgb,
                  int32_t x, int32_t base, bool centre) {
    lv_obj_t *l = lv_label_create(parent);
    no_scroll(l);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(rgb), 0);
    lv_label_set_text_static(l, txt);
    if (centre)
        x -= lv_text_get_width(txt, (uint32_t)strlen(txt), font, 0) / 2;
    lv_obj_set_pos(l, x, base - (font->line_height - font->base_line));
}

static void paint(void) {
    if (g_lay.mode == ALT_MODE_NONE)
        return;
    for (int i = 0; i < ALT_LAMPS; i++) {
        if (!g_lamp[i])
            continue;
        int lv = alt_level(g_frac[i]);
        if (g_shown[i] == lv)
            continue; /* repaint only lamps whose level changed */
        g_shown[i] = (int8_t)lv;
        lv_obj_set_style_bg_color(g_lamp[i], g_pal[lv], 0);
    }
}

static void build(lv_obj_t *panel) {
    lv_obj_clean(panel);
    memset(g_lamp, 0, sizeof(g_lamp));
    memset(g_shown, -1, sizeof(g_shown));
    if (g_lay.mode == ALT_MODE_NONE)
        return;

    static const int32_t lamp_y[2] = {ALT_LAMP_Y0, ALT_LAMP_Y1};
    static const int32_t base[2] = {ALT_LABEL_BASE0, ALT_LABEL_BASE1};
    for (int i = 0; i < g_lay.nlamps; i++) {
        const alt_lamp_t *l = &g_lay.lamps[i];
        lv_obj_t *o = lv_obj_create(panel);
        no_scroll(o);
        lv_obj_set_pos(o, l->x, lamp_y[l->row]);
        lv_obj_set_size(o, g_lay.lamp_w, ALT_LAMP_H);
        lv_obj_set_style_radius(o, ALT_LAMP_R, 0);
        lv_obj_set_style_border_width(o, 0, 0);
        lv_obj_set_style_pad_all(o, 0, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(o, g_pal[0], 0);
        g_lamp[l->id] = o;
        if (g_lay.per_lamp)
            label(panel, l->name, LP_FONT_LABEL, ALT_LABEL_RGB, l->x + g_lay.lamp_w / 2,
                  base[l->row], true);
    }

    if (g_lay.per_lamp) {
        if (g_lay.mode == ALT_MODE_FULL)
            label(panel, "ALTAIR 8800", LP_FONT_NAME_WIDE, ALT_NAME_RGB, g_lay.x_name,
                  ALT_NAME_BASE, true);
    } else {
        label(panel, "DATA", LP_FONT_LABEL, ALT_LABEL_RGB, g_lay.x_data, base[0], false);
        label(panel, "ADDRESS", LP_FONT_LABEL, ALT_LABEL_RGB, g_lay.x_address, base[1], false);
        if (g_lay.mode == ALT_MODE_FULL) {
            label(panel, "STATUS", LP_FONT_LABEL, ALT_LABEL_RGB, g_lay.x_status, base[0], false);
            label(panel, "8800", LP_FONT_NAME, ALT_NAME_RGB, g_lay.x_status, base[1], false);
        } else {
            label(panel, "ALTAIR 8800", LP_FONT_NAME, ALT_NAME_RGB, g_lay.x_address, base[0],
                  false);
        }
    }
    paint();
}

void lamp_panel_advance(lv_obj_t *panel, uint32_t ms) {
    if (!panel || lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN))
        return;
    alt_run_ms(&g_m, ms);
    alt_fractions(&g_m, g_frac);
    paint();
}

static void tick_cb(lv_timer_t *t) {
    uint32_t el = lv_tick_elaps(g_last_tick);
    g_last_tick = lv_tick_get();
    /* A stalled loop does not owe the emulator a burst (the mock-up caps the
     * catch-up at 0.6 s too). */
    lamp_panel_advance((lv_obj_t *)lv_timer_get_user_data(t), el > 600 ? 600 : el);
}

lv_obj_t *lamp_panel_create(lv_obj_t *parent) {
    uint32_t pal[ALT_LEVELS];
    alt_palette(pal);
    for (int i = 0; i < ALT_LEVELS; i++)
        g_pal[i] = lv_color_hex(pal[i]);
    alt_reset(&g_m, ALT_PROG_KTB, true, LP_SEED);
    memset(g_frac, 0, sizeof(g_frac));
    g_w = -1;

    lv_obj_t *p = lv_obj_create(parent);
    no_scroll(p);
    lv_obj_set_size(p, 0, ALT_CARD_H);
    lv_obj_set_style_bg_color(p, lv_color_hex(ALT_PANEL_RGB), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p, ALT_CARD_R, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);

    g_last_tick = lv_tick_get();
    lv_timer_create(tick_cb, ALT_REDRAW_MS, p);
    return p;
}

void lamp_panel_place(lv_obj_t *panel, int32_t x, int32_t w) {
    if (!panel)
        return;
    if (lv_obj_get_x(panel) != x)
        lv_obj_set_x(panel, x);
    if (w != g_w) {
        g_w = w;
        alt_layout((int)w, &g_lay);
        if (g_lay.mode != ALT_MODE_NONE)
            lv_obj_set_width(panel, w);
        build(panel);
    }
    bool hidden = lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN);
    if (g_lay.mode == ALT_MODE_NONE) {
        if (!hidden)
            lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    } else if (hidden) {
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }
}
