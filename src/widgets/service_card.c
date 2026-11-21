/* service_card.c — Service Status Card widget (T024).
 * Renders one card per registry entry: thick coloured border + optional
 * icon + name + status text. Border colour is driven by service_color().
 *
 * WI #4847: a publisher may also send a `title` (drawn in place of the name),
 * up to eight `lines` (drawn in place of `text`, one label each) and a `span`
 * of two units (kdashdata CD-32). The border stays this side's: state plus
 * freshness, whatever the record asked for. */
#include "widgets/service_card.h"

#include "icons.h"
#include "registry.h"
#include "svclayout.h"
#include "widgets/common.h"

#include <stdio.h>
#include <string.h>

#define SC_BORDER_THICK 6
#define SC_RADIUS 8
#define SC_PAD 8
#define SC_WIDTH SVC_CARD_W
#define SC_HEIGHT SVC_CARD_H
#define SC_LINES_GAP 0 /* row gap on a card drawing `lines` (WI #4847): a font's line height already carries its leading */

static lv_color_t color_for(service_color_t c) {
    switch (c) {
        case SERVICE_COLOR_GREEN:  return lv_color_hex(0x40A02B);
        case SERVICE_COLOR_YELLOW: return lv_color_hex(0xDF8E1D);
        case SERVICE_COLOR_BLUE:   return lv_color_hex(0x1E66F5);
        case SERVICE_COLOR_RED:    return lv_color_hex(0xD20F39);
        case SERVICE_COLOR_GRAY:
        default:                   return lv_color_hex(0x6C7086);
    }
}

lv_obj_t *service_card_create(lv_obj_t *parent, struct service_entry *e) {
    if (!parent || !e) return NULL;

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(cont, SC_WIDTH, SC_HEIGHT);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x181825), 0);
    lv_obj_set_style_radius(cont, SC_RADIUS, 0);
    lv_obj_set_style_border_color(cont, color_for(SERVICE_COLOR_GRAY), 0);
    lv_obj_set_style_border_width(cont, SC_BORDER_THICK, 0);
    lv_obj_set_style_pad_all(cont, SC_PAD, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *icon = lv_label_create(cont);
    lv_obj_set_style_text_font(icon, &lv_font_icons_56, 0);
    lv_obj_set_style_text_color(icon, WS_COLOR_FG, 0);
    const char *glyph = icons_lookup(e->icon_index);
    if (glyph) {
        lv_label_set_text(icon, glyph);
    } else {
        lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *name = lv_label_create(cont);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_bold_24, 0);
    lv_obj_set_style_text_color(name, WS_COLOR_FG, 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, LV_PCT(95));
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(name, e->name[0] ? e->name : "(service)");

    lv_obj_t *host = lv_label_create(cont);
    lv_obj_set_style_text_font(host, &lv_font_montserrat_bold_16, 0);
    lv_obj_set_style_text_color(host, lv_color_hex(0xA6ADC8), 0);
    lv_label_set_long_mode(host, LV_LABEL_LONG_DOT);
    lv_obj_set_width(host, LV_PCT(95));
    lv_obj_set_style_text_align(host, LV_TEXT_ALIGN_CENTER, 0);
    if (e->host[0] && strcmp(e->host, "_") != 0) {
        lv_label_set_text(host, e->host);
    } else {
        lv_obj_add_flag(host, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *text = lv_label_create(cont);
    lv_obj_set_style_text_font(text, &lv_font_montserrat_bold_20, 0);
    lv_obj_set_style_text_color(text, WS_COLOR_FG, 0);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(text, LV_PCT(95));
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(text, e->text[0] ? e->text : "");

    /* WI #4847: one label per publisher line, all built now and hidden until
     * a record carries `lines`. A hidden flex child takes no room, so a card
     * without the new fields lays out exactly as it did before them. */
    for (int i = 0; i < KDASH_SVC_LINES_MAX; i++) {
        lv_obj_t *ln = lv_label_create(cont);
        /* One line each, cut with LVGL's "..." at the card's width — never
         * wrapped, so the publisher's line count is the card's line count. */
        lv_label_set_long_mode(ln, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ln, LV_PCT(100));
        lv_obj_add_flag(ln, LV_OBJ_FLAG_HIDDEN);
        e->line_labels[i] = ln;
    }

    e->container = cont;
    e->border = cont;
    e->icon_label = icon;
    e->name_label = name;
    e->host_label = host;
    e->text_label = text;
    e->drawn_sig = 0;
    return cont;
}

static const lv_font_t *line_font(kdash_line_size_t sz) {
    switch (sz) {
        case KDASH_LINE_S: return &lv_font_montserrat_bold_16;
        case KDASH_LINE_L: return &lv_font_montserrat_bold_24;
        case KDASH_LINE_M:
        default:           return &lv_font_montserrat_bold_20;
    }
}

static lv_text_align_t line_align(kdash_line_align_t a) {
    switch (a) {
        case KDASH_ALIGN_L: return LV_TEXT_ALIGN_LEFT;
        case KDASH_ALIGN_R: return LV_TEXT_ALIGN_RIGHT;
        case KDASH_ALIGN_C:
        default:            return LV_TEXT_ALIGN_CENTER;
    }
}

/* Draw the body — width, title, and `lines` or `text` — from the entry. Runs
 * only when svc_layout_sig() says something it draws from has changed. */
static void draw_body(struct service_entry *e) {
    lv_obj_t *cont = e->container;
    lv_obj_set_width(cont, svc_card_width(e->span));

    const char *title = e->title[0] ? e->title : (e->name[0] ? e->name : "(service)");
    if (e->name_label) {
        /* A publisher's title is held to one line and cut, as its lines are:
         * a wrapped title would take the room the lines are fitted into. A
         * name keeps the height it always had. */
        lv_obj_set_height(e->name_label,
                          e->title[0] ? lv_font_get_line_height(&lv_font_montserrat_bold_24)
                                      : LV_SIZE_CONTENT);
        lv_label_set_text(e->name_label, title);
    }

    int n = e->n_lines < KDASH_SVC_LINES_MAX ? e->n_lines : KDASH_SVC_LINES_MAX;
    if (n <= 0) {
        /* The theme's row gap, exactly as before the layout fields existed. */
        lv_obj_remove_local_style_prop(cont, LV_STYLE_PAD_ROW, LV_PART_MAIN);
        if (e->text_label) {
            lv_label_set_text(e->text_label, e->text);
            lv_obj_clear_flag(e->text_label, LV_OBJ_FLAG_HIDDEN);
        }
        for (int i = 0; i < KDASH_SVC_LINES_MAX; i++)
            if (e->line_labels[i]) lv_obj_add_flag(e->line_labels[i], LV_OBJ_FLAG_HIDDEN);
        return;
    }

    /* Lines are set as a block of text, not spaced as separate items. Under
     * the full header (icon 59 + name 31 + host 21 px) the theme's 14 px row
     * gap leaves room for one line of the argus card's four; closed up, the
     * header and all four (S + 3 M = 96 px) fit in the 212 px with 5 to spare. */
    lv_obj_set_style_pad_row(cont, SC_LINES_GAP, 0);
    if (e->text_label) lv_obj_add_flag(e->text_label, LV_OBJ_FLAG_HIDDEN);
    int32_t h[KDASH_SVC_LINES_MAX];
    for (int i = 0; i < KDASH_SVC_LINES_MAX; i++) {
        lv_obj_t *ln = e->line_labels[i];
        if (!ln) continue;
        lv_obj_add_flag(ln, LV_OBJ_FLAG_HIDDEN);
        if (i >= n) continue;
        const kdash_service_line_t *l = &e->lines[i];
        const lv_font_t *f = line_font(l->size);
        h[i] = lv_font_get_line_height(f);
        lv_obj_set_style_text_font(ln, f, 0);
        lv_obj_set_style_text_align(ln, line_align(l->align), 0);
        lv_obj_set_style_text_color(ln, l->has_color ? lv_color_hex(l->color) : WS_COLOR_FG, 0);
        /* A fixed one-line height is what makes LONG_DOT cut rather than
         * wrap; "" still holds its line, as the blank spacer it is. */
        lv_obj_set_height(ln, h[i]);
        lv_label_set_text(ln, l->t);
    }

    /* Lines that do not fit under the header are dropped from the bottom,
     * whole: the panel has no input to scroll with, and a half-drawn line
     * reads as a different line. Measure the header as laid out. */
    lv_obj_update_layout(cont);
    int32_t gap = lv_obj_get_style_pad_row(cont, 0);
    int32_t top = 0;
    lv_obj_t *hdr[] = {e->icon_label, e->name_label, e->host_label};
    for (size_t i = 0; i < sizeof(hdr) / sizeof(hdr[0]); i++) {
        if (!hdr[i] || lv_obj_has_flag(hdr[i], LV_OBJ_FLAG_HIDDEN)) continue;
        int32_t bottom = lv_obj_get_y(hdr[i]) + lv_obj_get_height(hdr[i]);
        if (bottom > top) top = bottom;
    }
    int32_t avail = lv_obj_get_content_height(cont) - top - (top > 0 ? gap : 0);
    int fit = svc_lines_fit(h, n, avail, gap);
    for (int i = 0; i < fit; i++)
        if (e->line_labels[i]) lv_obj_clear_flag(e->line_labels[i], LV_OBJ_FLAG_HIDDEN);
}

void service_card_update(struct service_entry *e, double now) {
    if (!e || !e->container) return;
    service_color_t c = service_color(e, now);
    lv_obj_set_style_border_color(e->container, color_for(c), 0);
    if (e->host_label) {
        if (e->host[0] && strcmp(e->host, "_") != 0) {
            lv_label_set_text(e->host_label, e->host);
            lv_obj_clear_flag(e->host_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(e->host_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (e->icon_label) {
        const char *glyph = icons_lookup(e->icon_index);
        if (glyph) {
            lv_label_set_text(e->icon_label, glyph);
            lv_obj_clear_flag(e->icon_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(e->icon_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    /* After the header, which the body is measured against. */
    uint32_t sig = svc_layout_sig(e->title, e->span, e->text, e->host, e->icon_index,
                                  e->lines, e->n_lines);
    if (sig != e->drawn_sig) {
        e->drawn_sig = sig;
        draw_body(e);
    }
}

/* ---- WI #1903: host staleness card ----
 *
 * Deliberately built from the same constants and the same colour_for() as the
 * service card above: WI #1903 asks for "one card per stale host in the
 * service row, same size as the other service cards", and the surest way to
 * keep that true is for there to be one definition of the size.
 *
 * Differences from the service card, and why:
 *  - No icon. Nothing in the feed names one, and the space buys another line
 *    of deployer names.
 *  - The title WRAPS rather than eliding. "komarchy stale" is close to the
 *    card's width at this font, and the panel has no input devices — a title
 *    that elides to "komarchy sta..." cannot be revealed by any means.
 *  - The border is fixed at the warn band. There is no freshness computation
 *    to do: the key's presence is the whole signal.
 */

/* Body sized for STALE_DEPLOYERS_MAX names plus separators. */
#define STALE_BODY_BUF 512

lv_obj_t *stale_card_create(lv_obj_t *parent, struct stale_entry *e) {
    if (!parent || !e) return NULL;

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(cont, SC_WIDTH, SC_HEIGHT);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x181825), 0);
    lv_obj_set_style_radius(cont, SC_RADIUS, 0);
    lv_obj_set_style_border_color(cont, color_for(SERVICE_COLOR_YELLOW), 0);
    lv_obj_set_style_border_width(cont, SC_BORDER_THICK, 0);
    lv_obj_set_style_pad_all(cont, SC_PAD, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    char buf[STALE_BODY_BUF];

    lv_obj_t *title = lv_label_create(cont);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_bold_24, 0);
    lv_obj_set_style_text_color(title, WS_COLOR_FG, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    stale_format_title(e, buf, sizeof(buf));
    lv_label_set_text(title, buf);

    lv_obj_t *body = lv_label_create(cont);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_bold_20, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0xA6ADC8), 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    stale_format_body(e, buf, sizeof(buf));
    lv_label_set_text(body, buf);

    e->container = cont;
    e->title_label = title;
    e->body_label = body;
    return cont;
}

void stale_card_update(struct stale_entry *e) {
    if (!e || !e->container) return;
    char buf[STALE_BODY_BUF];
    if (e->title_label) {
        stale_format_title(e, buf, sizeof(buf));
        lv_label_set_text(e->title_label, buf);
    }
    if (e->body_label) {
        stale_format_body(e, buf, sizeof(buf));
        lv_label_set_text(e->body_label, buf);
    }
}

/* ---- WI #3642: update-issue card ----
 *
 * The staleness card above with the issue lines as its body. Lines are up to
 * 200 characters by contract, so the body drops to 16 px and wraps; on a
 * 220x240 card that holds roughly three typical lines. There is no input to
 * scroll with, so an overlong body is clipped — the oldest issue is first,
 * which is the one to see. */

#define UPDATE_BODY_BUF (UPDATE_KINDS_MAX * (UPDATE_ISSUE_LEN + 1) + 64)

lv_obj_t *update_card_create(lv_obj_t *parent, struct update_entry *e) {
    if (!parent || !e) return NULL;

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(cont, SC_WIDTH, SC_HEIGHT);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x181825), 0);
    lv_obj_set_style_radius(cont, SC_RADIUS, 0);
    lv_obj_set_style_border_color(cont, color_for(SERVICE_COLOR_YELLOW), 0);
    lv_obj_set_style_border_width(cont, SC_BORDER_THICK, 0);
    lv_obj_set_style_pad_all(cont, SC_PAD, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    static char buf[UPDATE_BODY_BUF];

    lv_obj_t *title = lv_label_create(cont);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_bold_24, 0);
    lv_obj_set_style_text_color(title, WS_COLOR_FG, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    update_format_title(e, buf, sizeof(buf));
    lv_label_set_text(title, buf);

    lv_obj_t *body = lv_label_create(cont);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_bold_16, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0xA6ADC8), 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    update_format_body(e, buf, sizeof(buf));
    lv_label_set_text(body, buf);

    e->container = cont;
    e->title_label = title;
    e->body_label = body;
    return cont;
}

void update_card_update(struct update_entry *e) {
    if (!e || !e->container) return;
    static char buf[UPDATE_BODY_BUF];
    if (e->title_label) {
        update_format_title(e, buf, sizeof(buf));
        lv_label_set_text(e->title_label, buf);
    }
    if (e->body_label) {
        update_format_body(e, buf, sizeof(buf));
        lv_label_set_text(e->body_label, buf);
    }
}
