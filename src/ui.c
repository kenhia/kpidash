/**
 * ui.c — Dashboard layout and widget management (T017, T017a, T055)
 *
 * Layout (no title bar, FR-002a; no scroll anywhere, FR-008):
 *
 *   ┌─────────────────────────────────────────────┐
 *   │  [client cards grid — Row 0, 6×1 units]     │
 *   ├──────────┬──────────┬───────────────────────┤
 *   │ DevGraph │Activities│  Repo Status           │
 *   │  (opt)   │  2×1     │  2×1                   │
 *   ├──────────┴──────────┴───────────────────────┤
 *   │  Fortune widget (bottom strip)              │
 *   ├─────────────────────────────────────────────┤
 *   │  Status bar (hidden when no messages)       │
 *   └─────────────────────────────────────────────┘
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "fortune.h"
#include "layout.h"
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
#include "widgets/service_card.h"
#include "widgets/status_bar.h"

/* ---- Layout objects ---- */
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_card_grid = NULL;
static lv_obj_t *g_activities = NULL;
static lv_obj_t *g_repo_status = NULL;
static lv_obj_t *g_fortune = NULL;
static lv_obj_t *g_service_strip = NULL;  /* T013: footer Service-Strip container */
static lv_obj_t *g_status_bar = NULL;
static lv_obj_t *g_redis_err = NULL;

/* Track which hostnames have cards so we can add/remove dynamically */
static char g_card_hostnames[MAX_CLIENTS][HOSTNAME_LEN];
static lv_obj_t *g_cards[MAX_CLIENTS];
static int g_card_count = 0;

/* Dev overlay handles (T030) */
static lv_obj_t *g_dev_grid = NULL;
static lv_obj_t *g_dev_textsize = NULL;
static int g_dev_grid_size = 0;        /* track pixel size so we recreate on change */
static bool g_dev_grid_unit = false;   /* track unit mode */
static float g_dev_grid_unit_size = 0; /* track unit size */

/* Dev graph handles (Phase 6.1): one widget per graph_host_series_t, stored
 * directly in the series's `widget` slot. T035 retired the single-host
 * globals (g_dev_graph / g_graph_client). */

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
    /* No screen-level padding — widgets positioned via cell system */

    /* Footer sub-layout constants (status bar pinned at very bottom). */
    static const int32_t status_bar_h = 40;

    /* ---- Client card grid (Row 1, full width — FR-001) ---- */
    g_card_grid = lv_obj_create(g_screen);
    clear_bg(g_card_grid);
    lv_obj_set_size(g_card_grid, UNIT_W_N(COLS), UNIT_H);
    lv_obj_set_pos(g_card_grid, COL_X(0), ROW_Y(0));
    lv_obj_set_flex_flow(g_card_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(g_card_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(g_card_grid, 0, 0);
    lv_obj_set_style_pad_row(g_card_grid, 0, 0);

    /* ---- Rows 2-3 (FR-002): pool widgets created here, positioned by
     * ui_refresh() via layout_pool_place(). They are created once and
     * shown/hidden + repositioned each refresh. ---- */
    g_activities = activities_widget_create(g_screen);
    lv_obj_set_size(g_activities, UNIT_W_N(2), UNIT_H);
    lv_obj_set_style_pad_all(g_activities, CELL_PAD, 0);
    lv_obj_add_flag(g_activities, LV_OBJ_FLAG_HIDDEN);

    g_repo_status = repo_status_widget_create(g_screen);
    lv_obj_set_size(g_repo_status, UNIT_W_N(2), UNIT_H);
    lv_obj_set_style_pad_all(g_repo_status, CELL_PAD, 0);
    lv_obj_add_flag(g_repo_status, LV_OBJ_FLAG_HIDDEN);

    /* T014: Fortune as 2×1 pool widget. */
    g_fortune = fortune_create(g_screen, 0, 0);
    fortune_set_widget(g_fortune);
    lv_obj_add_flag(g_fortune, LV_OBJ_FLAG_HIDDEN);

    /* ---- Footer (FR-005): Service-Strip container ---- */
    g_service_strip = lv_obj_create(g_screen);
    clear_bg(g_service_strip);
    lv_obj_set_size(g_service_strip, SCR_W - 2 * PAD_LEFT, FOOTER_H);
    lv_obj_set_pos(g_service_strip, PAD_LEFT, PAD_TOP + ROWS * UNIT_H);
    lv_obj_set_flex_flow(g_service_strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_service_strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_service_strip, 8, 0);

    /* ---- Status bar (bottom of screen, hidden initially) (FR-006: unchanged) ---- */
    g_status_bar = status_bar_create(g_screen);
    lv_obj_set_size(g_status_bar, UNIT_W_N(COLS), status_bar_h);
    lv_obj_set_pos(g_status_bar, PAD_LEFT, SCR_H - status_bar_h);

    /* ---- Redis error overlay (covers full screen, hidden) ---- */
    g_redis_err = lv_obj_create(g_screen);
    lv_obj_set_size(g_redis_err, SCR_W, SCR_H);
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

    /* Sort by priority: priority clients first (in config order),
     * then remaining clients in their original registration order. */
    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            int pi = registry_priority_index(clients[i].hostname);
            int pj = registry_priority_index(clients[j].hostname);
            /* Both priority: lower index wins. Priority vs non: priority wins.
             * Both non-priority: preserve order (stable, no swap). */
            bool swap = false;
            if (pi < 0 && pj >= 0)
                swap = true; /* j is priority, i is not */
            else if (pi >= 0 && pj >= 0 && pj < pi)
                swap = true; /* both priority, j has higher precedence */
            if (swap) {
                client_info_t tmp = clients[i];
                clients[i] = clients[j];
                clients[j] = tmp;
            }
        }
    }

    /* Add new cards, remove absent ones */
    for (int i = 0; i < n; i++) {
        if (!find_card(clients[i].hostname)) {
            add_card(clients[i].hostname);
        }
    }
    remove_absent_cards(clients, n);

    /* Reorder card LVGL objects to match sorted client order. */
    for (int i = 0; i < n; i++) {
        lv_obj_t *card = find_card(clients[i].hostname);
        if (card)
            lv_obj_move_to_index(card, i);
    }

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

    /* ---- Sprint 006 / T012,T015,T035: rows-2-3 layout pool placement ----
     * Multi-host graphs: one WIDGET_GRAPH request per discovered host series,
     * sorted alphabetically (T035 / FR-013) so layout_pool_place()'s stable
     * sort preserves that order. Each request's payload is the host name. */
    {
        /* Snapshot host series, sort alphabetically by host. */
        graph_host_series_t g_snap[GRAPH_HOST_MAX];
        int n_hosts = graph_host_snapshot(g_snap, GRAPH_HOST_MAX);
        for (int i = 1; i < n_hosts; i++) {
            graph_host_series_t tmp = g_snap[i];
            int j = i;
            while (j > 0 && strcmp(g_snap[j - 1].host, tmp.host) > 0) {
                g_snap[j] = g_snap[j - 1];
                j--;
            }
            g_snap[j] = tmp;
        }

        widget_request_t reqs[GRAPH_HOST_MAX + 4];
        int nreq = 0;
        /* Host-name storage: payload pointers must outlive the placement
         * call; reuse the snapshot's storage since g_snap lives on the stack
         * through the whole placement block. */
        for (int i = 0; i < n_hosts; i++) {
            reqs[nreq].kind = WIDGET_GRAPH;
            reqs[nreq].cells = WIDGET_CELLS_GRAPH;
            reqs[nreq].payload = g_snap[i].host;
            nreq++;
        }
        reqs[nreq++] = (widget_request_t){WIDGET_ACTIVITIES, WIDGET_CELLS_ACTIVITIES, NULL};
        reqs[nreq++] = (widget_request_t){WIDGET_REPO_STATUS, WIDGET_CELLS_REPO_STATUS, NULL};
        reqs[nreq++] = (widget_request_t){WIDGET_FORTUNE, WIDGET_CELLS_FORTUNE, NULL};

        widget_request_t placed[GRAPH_HOST_MAX + 4];
        int nplaced = layout_pool_place(reqs, (size_t)nreq, placed,
                                        sizeof(placed) / sizeof(placed[0]));

        /* Track which hosts ended up placed so we can hide widgets for hosts
         * dropped by the pool (e.g. exceeded the 12-cell budget). */
        bool host_placed[GRAPH_HOST_MAX] = {0};
        bool placed_acts = false, placed_repo = false, placed_fortune = false;
        int alloc_row = 1, alloc_col = 0;
        double now_s = (double)time(NULL);
        for (int i = 0; i < nplaced; i++) {
            int cells = placed[i].cells;
            if (alloc_col + cells > COLS) { alloc_row++; alloc_col = 0; }
            int x = COL_X(alloc_col);
            int y = ROW_Y(alloc_row);
            alloc_col += cells;
            switch (placed[i].kind) {
                case WIDGET_GRAPH: {
                    const char *host = (const char *)placed[i].payload;
                    if (!host) break;
                    graph_host_series_t *live = graph_host_find_or_create(host);
                    if (!live) break;
                    if (!live->widget) {
                        live->widget = dev_graph_create(g_screen, host);
                    }
                    if (!live->widget) break;
                    lv_obj_clear_flag(live->widget, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_set_pos(live->widget, x, y);
                    if (live->telemetry.valid) {
                        dev_graph_data_t gd = {
                            .cpu_pct = live->telemetry.cpu_pct,
                            .top_core_pct = live->telemetry.top_core_pct,
                            .ram_used_mb = live->telemetry.ram_used_mb,
                            .ram_total_mb = live->telemetry.ram_total_mb,
                            .gpu_compute_pct = live->telemetry.gpu_compute_pct,
                            .gpu_vram_used_mb = live->telemetry.gpu_vram_used_mb,
                            .gpu_vram_total_mb = live->telemetry.gpu_vram_total_mb,
                        };
                        dev_graph_update(live->widget, &gd);
                        dev_graph_set_host(live->widget, host);
                    }
                    dev_graph_set_stale(live->widget, graph_host_is_stale(live, now_s));
                    /* Mark this snapshot slot as placed. */
                    for (int s = 0; s < n_hosts; s++) {
                        if (strcmp(g_snap[s].host, host) == 0) {
                            host_placed[s] = true;
                            break;
                        }
                    }
                    break;
                }
                case WIDGET_ACTIVITIES:
                    lv_obj_set_pos(g_activities, x, y);
                    lv_obj_clear_flag(g_activities, LV_OBJ_FLAG_HIDDEN);
                    placed_acts = true;
                    break;
                case WIDGET_REPO_STATUS:
                    lv_obj_set_pos(g_repo_status, x, y);
                    lv_obj_clear_flag(g_repo_status, LV_OBJ_FLAG_HIDDEN);
                    placed_repo = true;
                    break;
                case WIDGET_FORTUNE:
                    lv_obj_set_pos(g_fortune, x, y);
                    lv_obj_clear_flag(g_fortune, LV_OBJ_FLAG_HIDDEN);
                    placed_fortune = true;
                    break;
            }
        }
        /* Hide widgets for hosts that were dropped by the pool. */
        for (int s = 0; s < n_hosts; s++) {
            if (host_placed[s]) continue;
            graph_host_series_t *live = graph_host_find_or_create(g_snap[s].host);
            if (live && live->widget) {
                lv_obj_add_flag(live->widget, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (!placed_acts)    lv_obj_add_flag(g_activities, LV_OBJ_FLAG_HIDDEN);
        if (!placed_repo)    lv_obj_add_flag(g_repo_status, LV_OBJ_FLAG_HIDDEN);
        if (!placed_fortune) lv_obj_add_flag(g_fortune, LV_OBJ_FLAG_HIDDEN);
    }

    /* ---- Sprint 006 / T025: paint service cards into the footer strip.
     * Sprint 007 / T007: sort by (name, host) ascending. */
    {
        service_entry_t svcs[SERVICE_REGISTRY_MAX];
        int nsvc = service_registry_snapshot(svcs, SERVICE_REGISTRY_MAX);
        for (int i = 1; i < nsvc; i++) {
            service_entry_t tmp = svcs[i];
            int j = i;
            while (j > 0) {
                int c = strncmp(svcs[j - 1].name, tmp.name, sizeof(tmp.name));
                if (c == 0) c = strncmp(svcs[j - 1].host, tmp.host, sizeof(tmp.host));
                if (c <= 0) break;
                svcs[j] = svcs[j - 1];
                j--;
            }
            svcs[j] = tmp;
        }
        double now = (double)time(NULL);
        for (int i = 0; i < nsvc; i++) {
            service_entry_t *live = service_registry_find_or_create(svcs[i].name, svcs[i].host);
            if (!live) continue;
            if (!live->container) {
                service_card_create(g_service_strip, live);
            }
            service_card_update(live, now);
        }
    }

    if (cmd->grid_enabled) {
        bool changed = !g_dev_grid || g_dev_grid_size != cmd->grid_size ||
                       g_dev_grid_unit != cmd->grid_unit ||
                       g_dev_grid_unit_size != cmd->grid_unit_size;
        if (changed) {
            dev_grid_destroy(g_dev_grid);
            g_dev_grid =
                dev_grid_create(g_screen, cmd->grid_size, cmd->grid_unit, cmd->grid_unit_size);
            g_dev_grid_size = cmd->grid_size;
            g_dev_grid_unit = cmd->grid_unit;
            g_dev_grid_unit_size = cmd->grid_unit_size;
        }
    } else {
        if (g_dev_grid) {
            dev_grid_destroy(g_dev_grid);
            g_dev_grid = NULL;
            g_dev_grid_size = 0;
            g_dev_grid_unit = false;
            g_dev_grid_unit_size = 0;
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

    /* Sprint 006 / T035: per-host dev_graph widgets are created, positioned,
     * updated, and stale-toggled by the rows-2-3 layout pool block above
     * (one widget per graph_host_series_t slot, alphabetically ordered). The
     * legacy single-host dev_telemetry path has been retired. */
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
