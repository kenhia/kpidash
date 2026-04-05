#include "repo_status.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lv_font_custom.h"

/* ---- Catppuccin Mocha palette ---- */
#define COLOR_BG lv_color_hex(0x1E1E2E)
#define COLOR_FG lv_color_hex(0xCDD6F4)
#define COLOR_MUTED lv_color_hex(0x6C7086)
#define COLOR_SURFACE0 lv_color_hex(0x313244)
#define COLOR_SURFACE1 lv_color_hex(0x45475A)
#define COLOR_GREEN lv_color_hex(0xA6E3A1)
#define COLOR_YELLOW lv_color_hex(0xF9E2AF)
#define COLOR_RED lv_color_hex(0xF38BA8)
#define COLOR_ORANGE lv_color_hex(0xFAB387)
#define COLOR_MAUVE lv_color_hex(0xCBA6F7)
#define COLOR_BLUE lv_color_hex(0x89B4FA)
#define COLOR_HEADER lv_color_hex(0xF5C2E7)

static const lv_font_t *FONT_HDR = &lv_font_montserrat_bold_20;
static const lv_font_t *FONT_NAME = &lv_font_montserrat_bold_20;
static const lv_font_t *FONT_SMALL = &lv_font_montserrat_bold_14;
static const lv_font_t *FONT_STATUS = &lv_font_montserrat_bold_20;
static const lv_font_t *FONT_BRANCH = &lv_font_montserrat_bold_16;

#define REPO_CARD_W 300
#define REPO_CARD_H 136
#define REPO_MAX_DISPLAY 16
/* U+F1D2 fa-square_git in UTF-8 */
#define GIT_ICON "\xEF\x87\x92"

/* ---- Age formatting ---- */
static lv_color_t age_color(double ts) {
    if (ts <= 0.0)
        return COLOR_MUTED;
    double age_s = difftime(time(NULL), (time_t)ts);
    double days = age_s / 86400.0;
    if (days < 7.0)
        return COLOR_GREEN;
    if (days < 30.0)
        return COLOR_YELLOW;
    return COLOR_RED;
}

static void format_age(double ts, char *buf, size_t len) {
    if (ts <= 0.0) {
        snprintf(buf, len, "?");
        return;
    }
    double age_s = difftime(time(NULL), (time_t)ts);
    if (age_s < 0)
        age_s = 0;
    int days = (int)(age_s / 86400.0);
    if (days < 1)
        snprintf(buf, len, "<1d");
    else if (days < 365)
        snprintf(buf, len, "%dd", days);
    else
        snprintf(buf, len, "%dy", days / 365);
}

/* ---- Build status line (line 4) ---- */
static void format_status(const repo_entry_t *r, char *buf, size_t len) {
    int pos = 0;
    /* Git icon */
    pos += snprintf(buf + pos, len - (size_t)pos, GIT_ICON " ");

    if (r->ahead > 0)
        pos += snprintf(buf + pos, len - (size_t)pos, "+%d ", r->ahead);
    if (r->behind > 0)
        pos += snprintf(buf + pos, len - (size_t)pos, "-%d ", r->behind);
    if (r->untracked_count > 0)
        pos += snprintf(buf + pos, len - (size_t)pos, "?%d ", r->untracked_count);
    if (r->changed_count > 0)
        pos += snprintf(buf + pos, len - (size_t)pos, "~%d ", r->changed_count);
    if (r->deleted_count > 0)
        pos += snprintf(buf + pos, len - (size_t)pos, "x%d ", r->deleted_count);
    if (r->renamed_count > 0)
        pos += snprintf(buf + pos, len - (size_t)pos, "r%d ", r->renamed_count);

    /* Trim trailing space */
    if (pos > 0 && buf[pos - 1] == ' ')
        buf[pos - 1] = '\0';
}

/* ---- Create individual repo card ---- */
static lv_obj_t *create_repo_card(lv_obj_t *parent, const repo_entry_t *r) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, REPO_CARD_W, REPO_CARD_H);
    lv_obj_set_style_bg_color(card, COLOR_SURFACE0, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_row(card, 2, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* Border: default is subtle. Dirty = orange left border. Explicit = thicker. */
    lv_obj_set_style_border_width(card, 0, 0);
    if (r->is_dirty) {
        lv_obj_set_style_border_side(card, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(card, COLOR_ORANGE, 0);
        lv_obj_set_style_border_width(card, (r->sort_order == 0) ? 4 : 2, 0);
    } else if (r->sort_order == 0) {
        /* Explicit but clean — thin muted left border */
        lv_obj_set_style_border_side(card, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(card, COLOR_SURFACE1, 0);
        lv_obj_set_style_border_width(card, 2, 0);
    }

    /* Line 1: hostname (dim, left) + age (right) */
    lv_obj_t *row1 = lv_obj_create(card);
    lv_obj_clear_flag(row1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_style_pad_all(row1, 0, 0);
    lv_obj_set_width(row1, LV_PCT(100));
    lv_obj_set_height(row1, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);

    lv_obj_t *host_lbl = lv_label_create(row1);
    lv_label_set_text(host_lbl, r->host);
    lv_obj_set_style_text_color(host_lbl, COLOR_MUTED, 0);
    lv_obj_set_style_text_font(host_lbl, FONT_SMALL, 0);

    char age_buf[16];
    format_age(r->last_commit_ts, age_buf, sizeof(age_buf));
    lv_obj_t *age_lbl = lv_label_create(row1);
    lv_label_set_text(age_lbl, age_buf);
    lv_obj_set_style_text_color(age_lbl, age_color(r->last_commit_ts), 0);
    lv_obj_set_style_text_font(age_lbl, FONT_SMALL, 0);

    /* Line 2: repo name (bold, center) */
    lv_obj_t *name_lbl = lv_label_create(card);
    lv_label_set_text(name_lbl, r->name);
    lv_obj_set_style_text_color(name_lbl, COLOR_FG, 0);
    lv_obj_set_style_text_font(name_lbl, FONT_NAME, 0);
    lv_obj_set_width(name_lbl, LV_PCT(100));
    lv_obj_set_style_text_align(name_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);

    /* Line 3: branch (colored, center) */
    lv_obj_t *br_lbl = lv_label_create(card);
    lv_label_set_text(br_lbl, r->branch);
    bool is_default = (r->default_branch[0] != '\0')
                          ? (strcmp(r->branch, r->default_branch) == 0)
                          : (strcmp(r->branch, "main") == 0 || strcmp(r->branch, "master") == 0);
    lv_obj_set_style_text_color(br_lbl, is_default ? COLOR_GREEN : COLOR_MAUVE, 0);
    lv_obj_set_style_text_font(br_lbl, FONT_BRANCH, 0);
    lv_obj_set_width(br_lbl, LV_PCT(100));
    lv_obj_set_style_text_align(br_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(br_lbl, LV_LABEL_LONG_DOT);

    /* Line 4: status indicators */
    char status_buf[128];
    format_status(r, status_buf, sizeof(status_buf));
    lv_obj_t *stat_lbl = lv_label_create(card);
    lv_label_set_text(stat_lbl, status_buf);
    lv_obj_set_style_text_color(stat_lbl, COLOR_BLUE, 0);
    lv_obj_set_style_text_font(stat_lbl, FONT_STATUS, 0);
    lv_obj_set_style_pad_top(stat_lbl, 14, 0);
    lv_obj_set_width(stat_lbl, LV_PCT(100));
    lv_label_set_long_mode(stat_lbl, LV_LABEL_LONG_CLIP);

    return card;
}

/* ---- Public API ---- */

lv_obj_t *repo_status_widget_create(lv_obj_t *parent) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(cont, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cont, 8, 0);
    lv_obj_set_style_border_color(cont, COLOR_SURFACE1, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_pad_all(cont, 10, 0);
    lv_obj_set_style_pad_row(cont, 6, 0);
    lv_obj_set_style_pad_column(cont, 8, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* Header */
    lv_obj_t *hdr = lv_label_create(cont);
    lv_label_set_text(hdr, "Repo Status");
    lv_obj_set_style_text_color(hdr, COLOR_HEADER, 0);
    lv_obj_set_style_text_font(hdr, FONT_HDR, 0);

    /* Card grid container */
    lv_obj_t *grid = lv_obj_create(cont);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 8, 0);
    lv_obj_set_style_pad_column(grid, 8, 0);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* Initial placeholder */
    lv_obj_t *clean = lv_label_create(grid);
    lv_label_set_text(clean, "All repos clean");
    lv_obj_set_style_text_color(clean, COLOR_GREEN, 0);
    lv_obj_set_style_text_font(clean, FONT_SMALL, 0);

    return cont;
}

/* ---- Cache: skip rebuild when data unchanged ---- */
static repo_entry_t s_cache[REPO_MAX_DISPLAY];
static int s_cache_count = -1; /* -1 = never drawn */

/* Compare repo entries ignoring the volatile `ts` field */
static bool repos_equal(const repo_entry_t *a, const repo_entry_t *b, int n) {
    for (int i = 0; i < n; i++) {
        /* Compare everything up to (but not including) the trailing `ts` field.
         * ts is the last field in repo_entry_t; offset = end - sizeof(double). */
        size_t cmp_sz = offsetof(repo_entry_t, ts);
        if (memcmp(&a[i], &b[i], cmp_sz) != 0)
            return false;
    }
    return true;
}

void repo_status_widget_update(lv_obj_t *widget, const repo_entry_t *list, int count) {
    int show = count < REPO_MAX_DISPLAY ? count : REPO_MAX_DISPLAY;

    /* Skip rebuild if data identical to last draw */
    if (show == s_cache_count && (show == 0 || repos_equal(s_cache, list, show))) {
        return;
    }

    /* Cache the new data */
    s_cache_count = show;
    if (show > 0)
        memcpy(s_cache, list, (size_t)show * sizeof(repo_entry_t));

    /* The card grid is the second child (index 1, after the header) */
    lv_obj_t *grid = lv_obj_get_child(widget, 1);
    if (!grid)
        return;

    /* Remove all children of the grid */
    uint32_t n = lv_obj_get_child_count(grid);
    for (uint32_t i = n; i > 0; i--) {
        lv_obj_delete(lv_obj_get_child(grid, i - 1));
    }

    if (count == 0) {
        lv_obj_t *clean = lv_label_create(grid);
        lv_label_set_text(clean, "All repos clean");
        lv_obj_set_style_text_color(clean, COLOR_GREEN, 0);
        lv_obj_set_style_text_font(clean, FONT_SMALL, 0);
        return;
    }

    for (int i = 0; i < show; i++) {
        create_repo_card(grid, &list[i]);
    }
}
