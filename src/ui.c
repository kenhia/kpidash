/**
 * ui.c — Dashboard layout and widget management (T017, T017a, T055)
 *
 * Layout (no title bar, FR-002a; no scroll anywhere, FR-008):
 *
 *   ┌─────────────────────────────────────────────┐
 *   │  [client cards grid — top, fills width]     │
 *   ├────────────────────┬────────────────────────┤
 *   │  Activities        │  Repo Status           │
 *   ├────────────────────┴────────────────────────┤
 *   │  Fortune widget (bottom strip)              │
 *   ├─────────────────────────────────────────────┤
 *   │  Status bar (hidden when no messages)       │
 *   └─────────────────────────────────────────────┘
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "fortune.h"
#include "protocol.h"
#include "redis.h"
#include "registry.h"
#include "widgets/activities.h"
#include "widgets/client_card.h"
#include "widgets/dev_graph.h"
#include "widgets/dev_grid.h"
#include "widgets/dev_textsize.h"
#include "widgets/fortune.h"
#include "widgets/repo_status.h"
#include "widgets/status_bar.h"

/* ---- Layout objects ---- */
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_card_grid = NULL;
static lv_obj_t *g_activities = NULL;
static lv_obj_t *g_repo_status = NULL;
static lv_obj_t *g_fortune = NULL;
static lv_obj_t *g_status_bar = NULL;
static lv_obj_t *g_redis_err = NULL;

/* Track which hostnames have cards so we can add/remove dynamically */
static char g_card_hostnames[MAX_CLIENTS][HOSTNAME_LEN];
static lv_obj_t *g_cards[MAX_CLIENTS];
static int g_card_count = 0;

/* Dev overlay handles (T030) */
static lv_obj_t *g_dev_grid = NULL;
static lv_obj_t *g_dev_textsize = NULL;
static int g_dev_grid_size = 0; /* track size so we recreate on change */

/* Dev graph handles (Phase 6.1) */
static lv_obj_t *g_dev_graph = NULL;
static char g_graph_client[HOSTNAME_LEN] = {0};

/* ---- Helpers ---- */

static void clear_bg(lv_obj_t *obj) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x11111B), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *find_card(const char *hostname) {
    for (int i = 0; i < g_card_count; i++) {
        if (strncmp(g_card_hostnames[i], hostname, HOSTNAME_LEN) == 0)
            return g_cards[i];
    }
    return NULL;
}

static void add_card(const char *hostname) {
    if (g_card_count >= MAX_CLIENTS)
        return;
    lv_obj_t *card = client_card_create(g_card_grid, hostname);
    strncpy(g_card_hostnames[g_card_count], hostname, HOSTNAME_LEN - 1);
    g_cards[g_card_count] = card;
    g_card_count++;
}

static void remove_absent_cards(const client_info_t *clients, int n) {
    for (int i = 0; i < g_card_count;) {
        bool found = false;
        for (int j = 0; j < n; j++) {
            if (strncmp(g_card_hostnames[i], clients[j].hostname, HOSTNAME_LEN) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            lv_obj_delete(g_cards[i]);
            /* Compact arrays */
            for (int k = i; k < g_card_count - 1; k++) {
                g_cards[k] = g_cards[k + 1];
                strncpy(g_card_hostnames[k], g_card_hostnames[k + 1], HOSTNAME_LEN);
            }
            g_card_count--;
        } else {
            i++;
        }
    }
}

/* ---- Public API ---- */

void ui_init(void) {
    g_screen = lv_screen_active();
    clear_bg(g_screen);
    lv_obj_set_style_pad_all(g_screen, 8, 0);
    lv_obj_set_style_pad_row(g_screen, 8, 0);
    lv_obj_set_style_pad_column(g_screen, 8, 0);

    lv_disp_t *disp = lv_display_get_default();
    int32_t scr_w = lv_display_get_horizontal_resolution(disp);
    int32_t scr_h = lv_display_get_vertical_resolution(disp);
    int32_t card_area_h = 640; /* ~624px widget + padding (6 across at 3840) */
    int32_t status_h = 40;
    int32_t fortune_h = (int32_t)(scr_h * 0.10);
    int32_t mid_h = scr_h - card_area_h - fortune_h - status_h - 40;

    /* ---- Client card grid (top 40%) ---- */
    g_card_grid = lv_obj_create(g_screen);
    clear_bg(g_card_grid);
    lv_obj_set_width(g_card_grid, scr_w - 16);
    lv_obj_set_height(g_card_grid, card_area_h);
    lv_obj_set_pos(g_card_grid, 8, 8);
    lv_obj_set_flex_flow(g_card_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(g_card_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(g_card_grid, 8, 0);
    lv_obj_set_style_pad_row(g_card_grid, 8, 0);

    /* ---- Middle row: Activities (left) + Repo Status (right) ---- */
    int32_t mid_y = card_area_h + 16;

    g_activities = activities_widget_create(g_screen);
    lv_obj_set_size(g_activities, 1256, 620);
    lv_obj_set_pos(g_activities, 8, mid_y);

    g_repo_status = repo_status_widget_create(g_screen);
    lv_obj_set_size(g_repo_status, 1256, 620);
    lv_obj_set_pos(g_repo_status, 8 + 1256 + 8, mid_y);

    /* ---- Fortune strip (bottom 10%) ---- */
    int32_t fortune_y = mid_y + mid_h + 8;
    g_fortune = fortune_widget_create(g_screen);
    lv_obj_set_size(g_fortune, scr_w - 16, fortune_h);
    lv_obj_set_pos(g_fortune, 8, fortune_y);
    fortune_set_widget(g_fortune);

    /* ---- Status bar (bottom edge, hidden initially) ---- */
    int32_t sb_y = scr_h - status_h - 4;
    g_status_bar = status_bar_create(g_screen);
    lv_obj_set_size(g_status_bar, scr_w - 16, status_h);
    lv_obj_set_pos(g_status_bar, 8, sb_y);

    /* ---- Redis error overlay (covers full screen, hidden) ---- */
    g_redis_err = lv_obj_create(g_screen);
    lv_obj_set_size(g_redis_err, scr_w, scr_h);
    lv_obj_set_pos(g_redis_err, 0, 0);
    lv_obj_set_style_bg_color(g_redis_err, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(g_redis_err, LV_OPA_90, 0);
    lv_obj_set_style_border_width(g_redis_err, 0, 0);
    lv_obj_clear_flag(g_redis_err, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_redis_err, LV_OBJ_FLAG_HIDDEN);
    /* Centered label inside overlay */
    lv_obj_t *err_lbl = lv_label_create(g_redis_err);
    lv_obj_set_style_text_color(err_lbl, lv_color_hex(0xFF5555), 0);
    lv_obj_set_style_text_font(err_lbl, &lv_font_montserrat_28, 0);
    lv_label_set_text(err_lbl, "Redis unavailable");
    lv_obj_align(err_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(err_lbl, LV_OBJ_FLAG_SCROLLABLE);

    /* FR-008 verification note: every widget created above uses
     * lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE). The card grid,
     * activities, repo status, fortune, status bar, and overlay are all
     * non-scrollable. Verified in T055. */
}

void ui_refresh(void) {
    /* Snapshot current registry */
    client_info_t clients[MAX_CLIENTS];
    int n = registry_snapshot(clients, MAX_CLIENTS);

    /* Add new cards, remove absent ones */
    for (int i = 0; i < n; i++) {
        if (!find_card(clients[i].hostname)) {
            add_card(clients[i].hostname);
        }
    }
    remove_absent_cards(clients, n);

    /* Update all cards */
    for (int i = 0; i < n; i++) {
        lv_obj_t *card = find_card(clients[i].hostname);
        if (!card)
            continue;
        client_card_update_health(card, &clients[i]);
        client_card_update_telemetry(card, &clients[i]);
    }

    /* Activities widget */
    int act_count = 0;
    const activity_t *acts = redis_get_activities(&act_count);
    activities_widget_update(g_activities, acts, act_count);

    /* Repo status widget */
    int repo_count = 0;
    const repo_entry_t *repos = redis_get_repos(&repo_count);
    repo_status_widget_update(g_repo_status, repos, repo_count);

    /* Dev command overlays (T030) */
    const dev_cmd_state_t *cmd = redis_get_dev_cmd_state();

    /* Grid overlay — create on enable, recreate on size change, destroy on disable */
    if (cmd->grid_enabled) {
        if (!g_dev_grid || g_dev_grid_size != cmd->grid_size) {
            dev_grid_destroy(g_dev_grid);
            g_dev_grid = dev_grid_create(g_screen, cmd->grid_size);
            g_dev_grid_size = cmd->grid_size;
        }
    } else {
        if (g_dev_grid) {
            dev_grid_destroy(g_dev_grid);
            g_dev_grid = NULL;
            g_dev_grid_size = 0;
        }
    }

    /* Textsize overlay */
    if (cmd->textsize_enabled) {
        if (!g_dev_textsize)
            g_dev_textsize = dev_textsize_create(g_screen);
    } else {
        if (g_dev_textsize) {
            dev_textsize_destroy(g_dev_textsize);
            g_dev_textsize = NULL;
        }
    }

    /* Dev graph in card grid (Phase 6.1) */
    if (cmd->graph_enabled && cmd->graph_client[0]) {
        /* Create or recreate if target client changed */
        if (!g_dev_graph || strncmp(g_graph_client, cmd->graph_client, HOSTNAME_LEN) != 0) {
            dev_graph_destroy(g_dev_graph);
            g_dev_graph = dev_graph_create(g_card_grid, cmd->graph_client);
            strncpy(g_graph_client, cmd->graph_client, HOSTNAME_LEN - 1);
            g_graph_client[HOSTNAME_LEN - 1] = '\0';
            /* Move to the front of the card grid so it's top-left */
            lv_obj_move_to_index(g_dev_graph, 0);
        }

        /* Feed data from dev telemetry (fast-poll key) */
        const dev_telemetry_t *dt = redis_get_dev_telemetry();
        if (dt->valid) {
            dev_graph_data_t gd = {
                .cpu_pct = dt->cpu_pct,
                .top_core_pct = dt->top_core_pct,
                .ram_used_mb = dt->ram_used_mb,
                .ram_total_mb = dt->ram_total_mb,
                .gpu_compute_pct = dt->gpu_compute_pct,
                .gpu_vram_used_mb = dt->gpu_vram_used_mb,
                .gpu_vram_total_mb = dt->gpu_vram_total_mb,
            };
            dev_graph_update(g_dev_graph, &gd);
        }
    } else {
        if (g_dev_graph) {
            dev_graph_destroy(g_dev_graph);
            g_dev_graph = NULL;
            g_graph_client[0] = '\0';
        }
    }
}

void ui_show_redis_error(const char *msg) {
    if (!g_redis_err)
        return;
    /* Update label text */
    lv_obj_t *lbl = lv_obj_get_child(g_redis_err, 0);
    if (lbl) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Redis unavailable: %s", msg);
        lv_label_set_text(lbl, buf);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    }
    lv_obj_clear_flag(g_redis_err, LV_OBJ_FLAG_HIDDEN);
}

void ui_hide_redis_error(void) {
    if (!g_redis_err)
        return;
    lv_obj_add_flag(g_redis_err, LV_OBJ_FLAG_HIDDEN);
}

void ui_status_bar_show(status_severity_t severity, const char *message) {
    if (!g_status_bar)
        return;
    status_bar_show(g_status_bar, severity, message);
}

void ui_status_bar_hide(void) {
    if (!g_status_bar)
        return;
    status_bar_hide(g_status_bar);
}
