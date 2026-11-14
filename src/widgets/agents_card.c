/**
 * agents_card.c — the Agents card (WI #4695). See the header.
 *
 * Coordinates are the mock-up's (sprints/planning/agents-mockups.html,
 * `borders(true)`, `hours`, `legend`, `ship`, `label`), in its 2528 × 628
 * frame, at real panel pixels. The object sits CELL_PAD inside that frame, so
 * every draw call offsets by the frame's origin. Text y values are the SVG's
 * baselines, which text_at() converts to LVGL's top-of-line.
 *
 * Every fill is opaque and every tint pre-mixed (sprint 030): the mock-up's
 * mix() is lv_color_mix() here, done once per colour, never a blend.
 */
#include "agents_card.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agstate.h"
#include "common.h"
#include "layout.h"

/* Mocha colours the palette does not carry yet. */
#define AG_CRUST lv_color_hex(0x11111B)
#define AG_MANTLE lv_color_hex(0x181825)
#define AG_SUBTEXT0 lv_color_hex(0xA6ADC8)

#define FONT_TITLE (&lv_font_montserrat_bold_28)
#define FONT_SUBTITLE (&lv_font_montserrat_24)
#define FONT_HOST (&lv_font_montserrat_bold_24)
#define FONT_LEGEND (&lv_font_montserrat_20)
#define FONT_HOUR (&lv_font_montserrat_20)
#define FONT_HOUR_B (&lv_font_montserrat_bold_20)
#define FONT_BAR (&lv_font_montserrat_bold_20)
#define FONT_NOTE (&lv_font_montserrat_24)

/* ---- Geometry (mock-up `borders`) ---- */
#define HEAD_BASE 56
#define SUBTITLE_X 170
#define HOSTS_X 470
#define NOTE_X 1000
#define LEGEND_X 1500
#define PLOT_Y0 96  /* the backdrop and the grid lines: rows' top less 8 */
#define PLOT_Y1 564 /* rows' bottom plus 8 */
#define HOUR_BASE 598
#define BAR_R 10    /* the SVG's rx 8 on a rect inset by half its 4 px stroke */
#define BAR_BORDER 4
#define STEM_W 4
#define STEM_DX 10  /* the stem hangs this far right of the leg's left edge */
#define SHIP_SIDE 20
#define CARD_R 20

typedef struct {
    ag_view_t cur;
    ag_view_t next; /* scratch: ag_view zeroes it, so memcmp sees only content */
    bool valid;
    unsigned gen;   /* the key the view was built from: record and minute */
    time_t minute;
    char label[KDASH_AGENTS_BARS_MAX][KDASH_AGENTS_NAME_MAX + 4];
} ag_widget_t;

static const char *const HOST_NAMES[4] = {"cleo", "kai", "kubs0", "other"};

static lv_color_t host_color(int slot) {
    switch (slot) {
    case AG_HOST_CLEO:
        return WS_COLOR_BLUE;
    case AG_HOST_KAI:
        return WS_COLOR_ORANGE; /* Peach */
    case AG_HOST_KUBS0:
        return WS_COLOR_GREEN;
    default:
        return WS_COLOR_MAUVE;
    }
}

/* JS mix(a, b, t): `t` of a, the rest b. Opaque. */
static lv_color_t mix(lv_color_t a, lv_color_t b, double t) {
    return lv_color_mix(a, b, (uint8_t)lround(t * 255));
}

/* ---- Drawing helpers ---- */

static void rect(lv_layer_t *layer, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t c,
                 int32_t radius) {
    if (w <= 0 || h <= 0)
        return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    d.radius = radius;
    lv_area_t a = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(layer, &d, &a);
}

/* A filled rectangle with an opaque border inside its edge, on `sides`. */
static void ring_rect(lv_layer_t *layer, int32_t x, int32_t y, int32_t w, int32_t h,
                      lv_color_t fill, lv_color_t border, int32_t bw, int32_t radius,
                      lv_border_side_t sides) {
    if (w <= 0 || h <= 0)
        return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = fill;
    d.bg_opa = LV_OPA_COVER;
    d.border_color = border;
    d.border_width = bw;
    d.border_opa = LV_OPA_COVER;
    d.border_side = sides;
    d.radius = radius;
    lv_area_t a = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(layer, &d, &a);
}

/* A square of side `side` turned 45°, centred on (cx, cy): a butt-ended line
 * along one diagonal as wide as it is long. One draw task, no seam. */
static void diamond(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t side, lv_color_t c) {
    const int32_t h = (int32_t)lround(side / (2 * M_SQRT2));
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = c;
    d.width = side;
    d.opa = LV_OPA_COVER;
    d.p1.x = cx - h;
    d.p1.y = cy + h;
    d.p2.x = cx + h;
    d.p2.y = cy - h;
    lv_draw_line(layer, &d);
}

static int32_t text_w(const lv_font_t *font, const char *s, int32_t ls) {
    return lv_text_get_width(s, (uint32_t)strlen(s), font, ls);
}

/* Text whose BASELINE is at y, `ls` px between letters. `x` is the left edge,
 * or the centre for LV_TEXT_ALIGN_CENTER. Returns the width drawn. */
static int32_t text_at(lv_layer_t *layer, const lv_font_t *font, int32_t x, int32_t baseline,
                       const char *s, lv_color_t c, lv_text_align_t align, int32_t ls) {
    if (!s || !s[0])
        return 0;
    int32_t w = text_w(font, s, ls);
    if (align == LV_TEXT_ALIGN_CENTER)
        x -= w / 2;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = s;
    d.text_local = 1; /* callers pass stack buffers; the draw task outlives them */
    d.font = font;
    d.color = c;
    d.opa = LV_OPA_COVER;
    d.letter_space = ls;
    int32_t top = baseline - (font->line_height - font->base_line);
    lv_area_t a = {x, top, x + w, top + font->line_height - 1};
    lv_draw_label(layer, &d, &a);
    return w;
}

/* ---- Bar names ---- */

/* One UTF-8 code point from s[*i]; malformed bytes come back as U+FFFD. */
static uint32_t utf8_next(const char *s, size_t *i) {
    const unsigned char *p = (const unsigned char *)s + *i;
    int n = p[0] < 0x80 ? 1 : (p[0] & 0xE0) == 0xC0 ? 2 : (p[0] & 0xF0) == 0xE0 ? 3
            : (p[0] & 0xF8) == 0xF0 ? 4 : 0;
    if (n == 0) {
        (*i)++;
        return 0xFFFD;
    }
    uint32_t cp = n == 1 ? p[0] : p[0] & (0x7F >> n);
    for (int k = 1; k < n; k++) {
        if ((p[k] & 0xC0) != 0x80) {
            *i += (size_t)k;
            return 0xFFFD;
        }
        cp = (cp << 6) | (p[k] & 0x3F);
    }
    *i += (size_t)n;
    return cp;
}

/* `name` with every character the bar face cannot draw replaced by '?'.
 * Names are any UTF-8 (CD-31) and the panel's faces are Latin: an absent
 * glyph would draw as nothing, or a box, and log a warning on every redraw
 * (WI #2646). Swapped here once per change, so width and drawing agree. */
static void sanitize(const char *name, char *out, size_t n) {
    size_t i = 0, k = 0, len = strlen(name);
    while (i < len && k + 1 < n) {
        size_t start = i;
        uint32_t cp = utf8_next(name, &i);
        lv_font_glyph_dsc_t g;
        bool ok = cp != 0xFFFD && lv_font_get_glyph_dsc(FONT_BAR, &g, cp, 0) && !g.is_placeholder;
        size_t w = ok ? i - start : 1;
        if (k + w + 1 > n)
            break;
        if (ok)
            memcpy(out + k, name + start, w);
        else
            out[k] = '?';
        k += w;
    }
    out[k] = '\0';
}

static int32_t measure_bar(const char *s, void *ctx) {
    (void)ctx;
    return text_w(FONT_BAR, s, 0);
}

/* ---- The pieces ---- */

static void draw_header(lv_layer_t *layer, int32_t ox, int32_t oy, const ag_view_t *v) {
    text_at(layer, FONT_TITLE, ox + AG_PLOT_X0, oy + HEAD_BASE, "AGENTS", WS_COLOR_LAVENDER,
            LV_TEXT_ALIGN_LEFT, 1);
    text_at(layer, FONT_SUBTITLE, ox + SUBTITLE_X, oy + HEAD_BASE, "last 24 hours", AG_SUBTEXT0,
            LV_TEXT_ALIGN_LEFT, 0);
    int32_t hx = HOSTS_X;
    for (int i = 0; i < 4; i++) {
        text_at(layer, FONT_HOST, ox + hx, oy + HEAD_BASE, HOST_NAMES[i], host_color(i),
                LV_TEXT_ALIGN_LEFT, 0);
        hx += (int32_t)strlen(HOST_NAMES[i]) * 15 + 44; /* the mock-up's spacing */
    }

    /* Muted notes between the hosts and the legend: an old ts, and how many
     * bars the rows had no room for. Neither changes anything else. */
    int32_t nx = NOTE_X;
    if (v->hint[0])
        nx += text_at(layer, FONT_NOTE, ox + nx, oy + HEAD_BASE, v->hint, WS_COLOR_MUTED,
                      LV_TEXT_ALIGN_LEFT, 0) + 40;
    if (v->hidden > 0) {
        char s[32];
        snprintf(s, sizeof(s), "%d older not shown", v->hidden % 1000);
        text_at(layer, FONT_NOTE, ox + nx, oy + HEAD_BASE, s, WS_COLOR_MUTED, LV_TEXT_ALIGN_LEFT,
                0);
    }

    /* The state legend (mock-up `legend`). */
    static const char *const ITEMS[4] = {"working", "finished", "needs you", "shipped"};
    const lv_color_t base = WS_COLOR_BG;
    int32_t x = LEGEND_X;
    for (int i = 0; i < 4; i++) {
        const int32_t lx = ox + x, ly = oy + 38;
        if (i == 0)
            rect(layer, lx, ly, 18, 18, AG_SUBTEXT0, 4);
        else if (i == 1)
            ring_rect(layer, lx - 1, ly - 1, 20, 20, base, AG_SUBTEXT0, 3, 5, LV_BORDER_SIDE_FULL);
        else if (i == 2)
            rect(layer, lx, ly, 18, 18, WS_COLOR_YELLOW, 4);
        else
            diamond(layer, lx + 9, ly + 9, 16, WS_COLOR_TEAL);
        text_at(layer, FONT_LEGEND, lx + 30, oy + 55, ITEMS[i], AG_SUBTEXT0, LV_TEXT_ALIGN_LEFT,
                0);
        x += (int32_t)lround(strlen(ITEMS[i]) * 12.4 + 78);
    }
}

static void draw_grid(lv_layer_t *layer, int32_t ox, int32_t oy, const ag_view_t *v) {
    rect(layer, ox + AG_PLOT_X0, oy + PLOT_Y0, AG_PLOT_X1 - AG_PLOT_X0, PLOT_Y1 - PLOT_Y0,
         AG_MANTLE, 10);
    for (int i = 0; i < v->nhours; i++) {
        const ag_hour_t *h = &v->hours[i];
        const int32_t w = h->midnight ? 3 : 2;
        rect(layer, ox + h->x - w / 2, oy + PLOT_Y0, w, PLOT_Y1 - PLOT_Y0,
             h->midnight ? WS_COLOR_SURFACE1 : WS_COLOR_SURFACE0, 0);
        char s[8];
        snprintf(s, sizeof(s), "%02d:00", h->hour % 24);
        const lv_font_t *f = h->midnight ? FONT_HOUR_B : FONT_HOUR;
        /* A label that would cross the card's edge is left off; its line stays. */
        const int32_t half = text_w(f, s, 0) / 2;
        if (h->x - half < 8 + 4 || h->x + half > AG_FRAME_W - 8 - 4)
            continue;
        text_at(layer, f, ox + h->x, oy + HOUR_BASE, s,
                h->midnight ? AG_SUBTEXT0 : WS_COLOR_MUTED, LV_TEXT_ALIGN_CENTER, 0);
    }
}

static void draw_bar(lv_layer_t *layer, int32_t ox, int32_t oy, const ag_bar_t *b,
                     const char *label) {
    const lv_color_t base = WS_COLOR_BG, c = host_color(b->host);
    /* Finished: the host colour pre-mixed 14 % into Base. Working: solid host
     * colour. Waiting on Ken: yellow. A working overseer keeps the dark fill
     * so its peach name stays readable; its square end says it is working. */
    lv_color_t fill = mix(c, base, 0.14);
    if (b->state == KDASH_AGENT_WAITING)
        fill = WS_COLOR_YELLOW;
    else if (b->state == KDASH_AGENT_WORKING && !b->overseer)
        fill = c;
    const int32_t x = ox + b->x0, y = oy + b->y, w = b->x1 - b->x0;
    ring_rect(layer, x, y, w, b->h, fill, c, BAR_BORDER, BAR_R, LV_BORDER_SIDE_FULL);
    /* Still in progress: the right end is square at the now line. A square
     * patch over the rounded end, bordered on three sides, does it with no
     * one-sided radius. */
    if (b->running && w > 2 * BAR_R)
        ring_rect(layer, x + w - BAR_R - 4, y, BAR_R + 4, b->h, fill, c, BAR_BORDER, 0,
                  LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT);
    if (label[0]) {
        /* An overseer's name is Claude peach, to catch the eye, on its dark
         * fill; dark text on the solid and yellow fills (peach on yellow
         * would not read, so a waiting overseer's name is dark too). */
        const bool dark = b->state == KDASH_AGENT_WAITING ||
                          (b->state == KDASH_AGENT_WORKING && !b->overseer);
        const lv_color_t tc = dark ? AG_CRUST : b->overseer ? WS_COLOR_ORANGE : WS_COLOR_FG;
        text_at(layer, FONT_BAR, x + AG_LABEL_PAD, y + b->h / 2 + 7, label, tc,
                LV_TEXT_ALIGN_LEFT, 0);
    }
    if (b->shipped) {
        const int32_t cx = x + w, cy = y + b->h / 2;
        diamond(layer, cx, cy, SHIP_SIDE + 4, AG_CRUST);
        diamond(layer, cx, cy, SHIP_SIDE, WS_COLOR_TEAL);
    }
}

static void draw_card(lv_layer_t *layer, const lv_area_t *a, const ag_widget_t *w) {
    const ag_view_t *v = &w->cur;
    /* The mock-up's frame origin: the object is CELL_PAD inside it. */
    const int32_t ox = a->x1 - CELL_PAD, oy = a->y1 - CELL_PAD;
    rect(layer, a->x1, a->y1, AG_CARD_W, AG_CARD_H, AG_CRUST, 0);
    rect(layer, a->x1, a->y1, AG_CARD_W, AG_CARD_H, WS_COLOR_BG, CARD_R);
    draw_header(layer, ox, oy, v);
    draw_grid(layer, ox, oy, v);

    if (v->status != AG_BARS) {
        const char *msg = v->status == AG_NOFEED ? "no agents feed"
                                                 : "no sessions in the last 24 hours";
        /* On a patch of the backdrop, so no grid line runs through it. */
        const int32_t cx = (AG_PLOT_X0 + AG_PLOT_X1) / 2, base = (AG_ROW_TOP + AG_ROW_BOT) / 2 + 8;
        const int32_t tw = text_w(FONT_NOTE, msg, 0);
        rect(layer, ox + cx - tw / 2 - 24, oy + base - 34, tw + 48, 48, AG_MANTLE, 0);
        text_at(layer, FONT_NOTE, ox + cx, oy + base, msg, WS_COLOR_MUTED, LV_TEXT_ALIGN_CENTER,
                0);
    } else {
        /* Stems first, under every bar: from the overseer's bottom edge to
         * the leg's top edge, at the leg's start, in the overseer's colour. */
        for (int i = 0; i < v->nbars; i++) {
            const ag_bar_t *b = &v->bars[i];
            if (b->parent < 0)
                continue;
            const ag_bar_t *p = &v->bars[b->parent];
            const int32_t y0 = p->y + p->h, y1 = b->y;
            rect(layer, ox + b->x0 + STEM_DX - STEM_W / 2, oy + y0, STEM_W, y1 - y0,
                 host_color(p->host), 0);
        }
        for (int i = 0; i < v->nbars; i++)
            draw_bar(layer, ox, oy, &v->bars[i], w->label[i]);
    }

    /* The now line, last, over any bar that runs into it. */
    rect(layer, ox + AG_PLOT_X1 - 1, oy + PLOT_Y0, 2, PLOT_Y1 - PLOT_Y0, WS_COLOR_FG, 0);
}

static void draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    ag_widget_t *w = lv_obj_get_user_data(obj);
    if (!w || !w->valid)
        return;
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    draw_card(lv_event_get_layer(e), &a, w);
}

static void delete_cb(lv_event_t *e) {
    free(lv_obj_get_user_data(lv_event_get_target(e)));
}

/* ---- Public API ---- */

lv_obj_t *agents_card_create(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o, AG_CARD_W, AG_CARD_H);
    ag_widget_t *w = calloc(1, sizeof(*w));
    lv_obj_set_user_data(o, w);
    lv_obj_add_event_cb(o, delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    return o;
}

void agents_card_update(lv_obj_t *card, time_t now) {
    ag_widget_t *w = card ? lv_obj_get_user_data(card) : NULL;
    if (!w)
        return;
    /* The view is a function of the record and the minute: between those,
     * the 59 polls of every minute cost a compare and nothing else. */
    const time_t minute = now - (now % 60 + 60) % 60;
    const unsigned gen = ag_generation();
    if (w->valid && w->gen == gen && w->minute == minute)
        return;
    w->gen = gen;
    w->minute = minute;
    ag_view(now, &w->next);
    if (w->valid && memcmp(&w->next, &w->cur, sizeof(w->cur)) == 0)
        return;
    w->cur = w->next;
    w->valid = true;
    for (int i = 0; i < w->cur.nbars; i++) {
        const ag_bar_t *b = &w->cur.bars[i];
        char clean[KDASH_AGENTS_NAME_MAX];
        sanitize(b->name, clean, sizeof(clean));
        const int32_t avail =
            (b->x1 - b->x0) - AG_LABEL_PAD - (b->shipped ? AG_LABEL_SHIP : AG_LABEL_PAD_R);
        ag_fit_label(clean, avail, measure_bar, NULL, w->label[i], sizeof(w->label[i]));
    }
    lv_obj_invalidate(card);
}
