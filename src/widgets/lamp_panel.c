/* lamp_panel.c — the footer's lamp panel: the Altair 8800 (WI #4777, sprint
 * 044) and the PDP-8/I (WI #4783, sprint 045), one at a time. See the
 * header; the machines and the rotation are src/lampmach.c. */
#include "widgets/lamp_panel.h"

#include <string.h>

#include "lampmach.h"
#include "widgets/common.h"

/* The mock-ups' weights as the repo's fonts (lampmach.c maps each machine's
 * labels onto these two). */
static const lv_font_t *font_of(int f) {
    return f == LM_FONT_24 ? &lv_font_montserrat_bold_24 : &lv_font_montserrat_bold_20;
}

/* One panel on the screen, so its state is file-static. */
static lm_layout_t g_lay[LM_NMACHINES];               /* each machine at the current width */
static lv_color_t g_pal[LM_NMACHINES][LM_MAX_COLOURS][LM_LEVELS]; /* mixed once */
static lv_obj_t *g_lamp[LM_MAX_LAMPS];                 /* by layout index */
static int8_t g_shown[LM_MAX_LAMPS];
static float g_frac[LM_MAX_LAMPS];
static lm_rotation_t g_rot;
static int g_period_s = LM_PERIOD_DEFAULT_S;
static int32_t g_w = -1;
static uint32_t g_last_tick;

static void no_scroll(lv_obj_t *o) {
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

/* A label whose baseline is at `base` (the mock-ups place text by its
 * baseline), left-aligned at `x` or centred on it. */
static void label(lv_obj_t *parent, const lm_label_t *b) {
    const lv_font_t *font = font_of(b->font);
    lv_obj_t *l = lv_label_create(parent);
    no_scroll(l);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(b->rgb), 0);
    lv_label_set_text_static(l, b->text);
    int32_t x = b->x;
    if (b->centre)
        x -= lv_text_get_width(b->text, (uint32_t)strlen(b->text), font, 0) / 2;
    lv_obj_set_pos(l, x, b->base - (font->line_height - font->base_line));
}

static void paint(void) {
    if (g_rot.cur < 0)
        return;
    const lm_layout_t *L = &g_lay[g_rot.cur];
    for (int i = 0; i < L->nlamps; i++) {
        int lv = lm_level(g_frac[L->lamps[i].id]);
        if (g_shown[i] == lv)
            continue; /* repaint only lamps whose level changed */
        g_shown[i] = (int8_t)lv;
        lv_obj_set_style_bg_color(g_lamp[i], g_pal[g_rot.cur][L->lamps[i].pal][lv], 0);
    }
}

/* Clear the card and draw the machine on screen: a plain cut. */
static void build(lv_obj_t *panel) {
    lv_obj_clean(panel);
    memset(g_lamp, 0, sizeof(g_lamp));
    memset(g_shown, -1, sizeof(g_shown));
    if (g_rot.cur < 0) {
        lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const lm_machine_t *M = LM_MACHINES[g_rot.cur];
    const lm_layout_t *L = &g_lay[g_rot.cur];
    lv_obj_set_width(panel, g_w);
    lv_obj_set_style_bg_color(panel, lv_color_hex(M->card_rgb), 0);
    for (int i = 0; i < L->nlamps; i++) {
        const lm_lamp_t *l = &L->lamps[i];
        lv_obj_t *o = lv_obj_create(panel);
        no_scroll(o);
        lv_obj_set_pos(o, l->x, l->y);
        lv_obj_set_size(o, l->w, l->h);
        lv_obj_set_style_radius(o, L->radius, 0);
        lv_obj_set_style_border_width(o, 0, 0);
        lv_obj_set_style_pad_all(o, 0, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(o, g_pal[g_rot.cur][l->pal][0], 0);
        g_lamp[i] = o;
    }
    for (int i = 0; i < L->nlabels; i++)
        label(panel, &L->labels[i]);
    paint();
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);
}

/* Let the rotation decide what is on screen; on a change, start the new
 * machine from reset and redraw. `relaid` forces the redraw (the width
 * changed under the machine that stays). */
static void rotate(lv_obj_t *panel, bool relaid) {
    bool drawable[LM_NMACHINES];
    for (int i = 0; i < LM_NMACHINES; i++)
        drawable[i] = g_lay[i].drawable;
    bool changed = lm_rotation_tick(&g_rot, lv_tick_get(), g_period_s, drawable);
    if (changed && g_rot.cur >= 0) {
        LM_MACHINES[g_rot.cur]->reset();
        memset(g_frac, 0, sizeof(g_frac));
    }
    if (changed || relaid)
        build(panel);
}

void lamp_panel_advance(lv_obj_t *panel, uint32_t ms) {
    if (!panel || g_rot.cur < 0 || lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN))
        return;
    LM_MACHINES[g_rot.cur]->advance(ms, g_frac);
    paint();
}

static void tick_cb(lv_timer_t *t) {
    lv_obj_t *panel = (lv_obj_t *)lv_timer_get_user_data(t);
    uint32_t el = lv_tick_elaps(g_last_tick);
    g_last_tick = lv_tick_get();
    rotate(panel, false);
    /* A stalled loop does not owe the emulator a burst (the mock-ups cap the
     * catch-up at 0.6 s too). */
    lamp_panel_advance(panel, el > LM_CATCHUP_MS ? LM_CATCHUP_MS : el);
}

lv_obj_t *lamp_panel_create(lv_obj_t *parent) {
    for (int m = 0; m < LM_NMACHINES; m++) {
        const lm_machine_t *M = LM_MACHINES[m];
        for (int c = 0; c < M->ncolours; c++) {
            uint32_t pal[LM_LEVELS];
            lm_palette(M->card_rgb, M->colours[c], M->max_brightness, pal);
            for (int i = 0; i < LM_LEVELS; i++)
                g_pal[m][c][i] = lv_color_hex(pal[i]);
        }
        g_lay[m].drawable = false;
    }
    lm_rotation_init(&g_rot);
    memset(g_frac, 0, sizeof(g_frac));
    g_w = -1;

    lv_obj_t *p = lv_obj_create(parent);
    no_scroll(p);
    lv_obj_set_size(p, 0, LAMP_PANEL_H);
    lv_obj_set_style_bg_color(p, lv_color_hex(LM_MACHINES[0]->card_rgb), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p, LAMP_PANEL_R, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);

    g_last_tick = lv_tick_get();
    lv_timer_create(tick_cb, LM_REDRAW_MS, p);
    return p;
}

void lamp_panel_place(lv_obj_t *panel, int32_t x, int32_t w) {
    if (!panel)
        return;
    if (lv_obj_get_x(panel) != x)
        lv_obj_set_x(panel, x);
    if (w == g_w)
        return;
    g_w = w;
    for (int m = 0; m < LM_NMACHINES; m++)
        LM_MACHINES[m]->layout((int)w, &g_lay[m]);
    rotate(panel, true);
}

void lamp_panel_set_period(lv_obj_t *panel, int period_s) {
    (void)panel;
    /* Zero is a dev-command state that never saw a poll, not a period. */
    g_period_s = period_s > 0 ? period_s : LM_PERIOD_DEFAULT_S;
}

void lamp_panel_show(lv_obj_t *panel, int machine) {
    if (!panel || machine < 0 || machine >= LM_NMACHINES || !g_lay[machine].drawable)
        return;
    if (g_rot.cur == machine)
        return;
    g_rot.cur = machine;
    g_rot.since = lv_tick_get();
    LM_MACHINES[machine]->reset();
    memset(g_frac, 0, sizeof(g_frac));
    build(panel);
}
