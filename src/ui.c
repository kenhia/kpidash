/**
 * ui.c — Dashboard layout and widget management (T017, T017a, T055)
 *
 * Layout (no title bar, FR-002a; no scroll anywhere, FR-008). Sprint 032
 * removed the dev graphs, Fortune, Activities and Repo Status, and with them
 * the rows-2-3 layout pool: two fixed cards need no placer.
 *
 *   ┌─────────────────────────────────────────────┐
 *   │  Host columns — Row 0, cols 0-5, 6×1 units  │
 *   │  (the whole row; sprint 030, WI #4543), the │
 *   │  weather filler in what they leave (034)    │
 *   ├───────────┬──────────┬──────────┬───────────┤
 *   │  Claude   │ 10G card │ NAS card │  korg +   │
 *   │  limits   │  2×1     │  2×1     │  klams    │
 *   ├───────────┴──────────┴─────┬────┼───────────┤
 *   │  Agents, 3 h + day strip   │apt │  time +   │
 *   │  (041/042; 047: one host   │tem-│  calendar │
 *   │  column narrower)          │ps  │           │
 *   ├────────────────────────────┴────┴───────────┤
 *   │  Footer: service strip, then the lamp panel │
 *   │  in what it leaves (Altair 044, PDP-8 045); │
 *   │  status bar                                 │
 *   └─────────────────────────────────────────────┘
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "hostcols.h"
#include "layout.h"
#include "limstate.h"
#include "protocol.h"
#include "redis.h"
#include "nasstate.h"
#include "registry.h"
#include "svclayout.h"
#include "widgets/apt_temps_card.h"
#include "widgets/dev_grid.h"
#include "widgets/dev_textsize.h"
#include "widgets/host_columns.h"
#include "widgets/korg_klams_card.h"
#include "widgets/lamp_panel.h"
#include "widgets/calendar_card.h"
#include "widgets/agents_card.h"
#include "widgets/limits_card.h"
#include "widgets/nas_card.h"
#include "widgets/net_card.h"
#include "widgets/service_card.h"
#include "widgets/status_bar.h"
#include "widgets/weather_card.h"
#include "wxstate.h"

/* ---- Layout objects ---- */
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_host_columns = NULL; /* WI #4526: replaces the client card grid */
static lv_obj_t *g_weather = NULL;      /* WI #4576: the filler in the hosts' spare columns */
static lv_obj_t *g_netcard = NULL;
static lv_obj_t *g_nascard = NULL; /* WI #3956 */
static lv_obj_t *g_korgklams = NULL; /* WI #4613: korg + klams; "row 2, column 5" counts rows from 1 */
static lv_obj_t *g_limits = NULL;  /* WI #4609: Claude limits; "row 2, column 0" counts rows from 1 */
static lv_obj_t *g_agents = NULL;   /* WI #4695: agents timeline; "row 3, columns 0-3" counts rows from 1 */
static lv_obj_t *g_apttemps = NULL; /* WI #4848: the three zones, in the column the Agents card gave up */
static lv_obj_t *g_calendar = NULL; /* WI #4665: time and calendar; "row 3, columns 4-5" counts rows from 1 */
static lv_obj_t *g_service_strip = NULL;  /* T013: footer Service-Strip container */
static lv_obj_t *g_lamp_panel = NULL;     /* WI #4777, #4783: the lamp panel after the status cards */
static lv_obj_t *g_status_bar = NULL;
static lv_obj_t *g_redis_err = NULL;

/* Dev overlay handles (T030) */
static lv_obj_t *g_dev_grid = NULL;
static lv_obj_t *g_dev_textsize = NULL;
static int g_dev_grid_size = 0;        /* track pixel size so we recreate on change */
static bool g_dev_grid_unit = false;   /* track unit mode */
static float g_dev_grid_unit_size = 0; /* track unit size */

/* ---- Helpers ---- */

static void clear_bg(lv_obj_t *obj) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x11111B), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

/* WI #4576: the weather filler takes min(4, 10 - host columns) of the row,
 * starting right after the hosts (centred in what is left when six or fewer
 * report). Not drawn with no room, and not drawn before kdash:weather:home has
 * ever published — like the NAS card. Position and visibility change only on
 * a real transition, for the reason the NAS card's flag does (ui_refresh). */
static void refresh_weather(int host_cols, double now) {
    int n = wx_columns(host_cols);
    bool hidden = lv_obj_has_flag(g_weather, LV_OBJ_FLAG_HIDDEN);
    if (n == 0 || !wx_has_record()) {
        if (!hidden) lv_obj_add_flag(g_weather, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    int32_t x = COL_X(0) + HOSTCOLS_X0 + wx_left(host_cols);
    if (lv_obj_get_x(g_weather) != x) lv_obj_set_x(g_weather, x);
    weather_card_update(g_weather, n, now);
    if (hidden) lv_obj_clear_flag(g_weather, LV_OBJ_FLAG_HIDDEN);
}

/* WI #4524/#4525: snapshot the registry, pick and order the hosts that get a
 * slot, and hand the widget their display models. Static buffers: with
 * MAX_CLIENTS at 32, client_info_t snapshots are too large to keep putting on
 * the stack every second. */
static void refresh_host_columns(void) {
    static client_info_t clients[MAX_CLIENTS];
    static hc_host_t models[HOSTCOLS_MAX];
    static int last_dropped = 0;

    int n = registry_snapshot(clients, MAX_CLIENTS);
    const client_info_t *sel[HOSTCOLS_MAX];
    int dropped = 0;
    double now = (double)time(NULL);
    int ns = hostcols_select(clients, n, now, sel, HOSTCOLS_MAX, &dropped);
    if (dropped != last_dropped) {
        /* Logged on change, not every second: the journal is for news. */
        if (dropped > 0)
            fprintf(stderr, "ui: %d host(s) past the %d-slot host columns limit are not shown\n",
                    dropped, HOSTCOLS_MAX);
        last_dropped = dropped;
    }
    for (int i = 0; i < ns; i++)
        hostcols_build(sel[i], now, &models[i]);
    host_columns_update(g_host_columns, models, ns);
    refresh_weather(hostcols_used(ns), now);
}

/* ---- Public API ---- */

void ui_init(void) {
    g_screen = lv_screen_active();
    clear_bg(g_screen);
    /* No screen-level padding — widgets positioned via cell system */

    /* Footer sub-layout constants (status bar pinned at very bottom). */
    static const int32_t status_bar_h = 40;

    /* ---- Host columns (WI #4526, widened by WI #4543): the whole of row 0.
     * The columns to the right of the last host take the weather filler
     * (sprint 034). ---- */
    g_host_columns = host_columns_create(g_screen, COL_X(0), ROW_Y(0));

    /* ---- The weather filler (WI #4576) over the columns the hosts leave.
     * Created after the host columns so it draws above their background. ---- */
    g_weather = weather_card_create(g_screen);
    lv_obj_set_y(g_weather, ROW_Y(0) + HOSTCOLS_Y0);
    lv_obj_add_flag(g_weather, LV_OBJ_FLAG_HIDDEN);

    /* ---- Row 1 (sprint 043, WI #4752): the Claude limits card in column 0
     * (sprint 037), the 10G card in columns 1-2, the NAS card in columns 3-4
     * (sprint 032) and the korg + klams card in column 5 (sprint 038). The
     * limits card is shown only once claude:limits has published and the
     * NAS card only once kdash:nas has, but every slot is fixed, so no card
     * moves when another appears. ---- */
    g_limits = limits_card_create(g_screen); /* WI #4609 */
    lv_obj_set_pos(g_limits, COL_X(0) + CELL_PAD, ROW_Y(1) + CELL_PAD);
    lv_obj_add_flag(g_limits, LV_OBJ_FLAG_HIDDEN);

    g_netcard = net_card_create(g_screen); /* WI #4062 */
    lv_obj_set_pos(g_netcard, COL_X(1), ROW_Y(1));

    g_nascard = nas_card_create(g_screen); /* WI #3956 */
    lv_obj_set_pos(g_nascard, COL_X(3) + CELL_PAD, ROW_Y(1) + CELL_PAD);
    lv_obj_add_flag(g_nascard, LV_OBJ_FLAG_HIDDEN);

    g_korgklams = korg_klams_card_create(g_screen); /* WI #4613 */
    lv_obj_set_pos(g_korgklams, COL_X(5) + CELL_PAD, ROW_Y(1) + CELL_PAD);

    /* ---- Row 2: the Agents card in columns 0-3 (sprint 041) less one host
     * column, the apt-temps widget in that column (sprint 047), then the
     * time and calendar card, a 2×1 in columns 4-5 (sprint 040). All three
     * always shown: the agents card draws its grid and "no agents feed"
     * before kdash:agents has ever published, the apt-temps widget draws
     * every zone's slot with or without a reading, and the calendar's clock
     * and grid come from the panel's own clock. ---- */
    g_agents = agents_card_create(g_screen); /* WI #4695 */
    lv_obj_set_pos(g_agents, COL_X(0) + CELL_PAD, ROW_Y(2) + CELL_PAD);

    g_apttemps = apt_temps_card_create(g_screen); /* WI #4848 */
    lv_obj_set_pos(g_apttemps, ROW2_APT_X + CELL_PAD, ROW_Y(2) + CELL_PAD);

    g_calendar = calendar_card_create(g_screen); /* WI #4665 */
    lv_obj_set_pos(g_calendar, COL_X(4) + CELL_PAD, ROW_Y(2) + CELL_PAD);

    /* ---- Footer (FR-005): Service-Strip container ---- */
    g_service_strip = lv_obj_create(g_screen);
    clear_bg(g_service_strip);
    lv_obj_set_size(g_service_strip, SCR_W - 2 * PAD_LEFT, FOOTER_H);
    lv_obj_set_pos(g_service_strip, PAD_LEFT, PAD_TOP + ROWS * UNIT_H);
    lv_obj_set_flex_flow(g_service_strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_service_strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_service_strip, SVC_STRIP_GAP, 0); /* a span-2 card is 2 units + this */

    /* ---- The lamp panel (WI #4777, #4783) in the strip's free width,
     * at the status cards' height and centred on the strip as they are.
     * Placed by ui_refresh once the cards are counted. ---- */
    g_lamp_panel = lamp_panel_create(g_screen);
    lv_obj_set_y(g_lamp_panel, PAD_TOP + ROWS * UNIT_H + (FOOTER_H - LAMP_PANEL_H) / 2);

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
     * lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE). The host columns,
     * status bar, and overlay are all non-scrollable. Verified in T055. */
}

void ui_refresh(void) {
    refresh_host_columns();

    /* Dev command overlays (T030) */
    const dev_cmd_state_t *cmd = redis_get_dev_cmd_state();

    /* ---- Row 1: the 10G and NAS cards (fixed slots, sprint 032). The NAS
     * card's flag changes only on a real transition: clearing HIDDEN
     * invalidates the whole object even when it is already shown, which the
     * old pool placer paid for on both cards every second. ---- */
    {
        double now_s = (double)time(NULL);
        net_card_update(g_netcard, now_s);
        bool nas_hidden = lv_obj_has_flag(g_nascard, LV_OBJ_FLAG_HIDDEN);
        if (nas_has_record()) {
            if (nas_hidden) lv_obj_clear_flag(g_nascard, LV_OBJ_FLAG_HIDDEN);
            nas_card_update(g_nascard, now_s);
        } else if (!nas_hidden) {
            lv_obj_add_flag(g_nascard, LV_OBJ_FLAG_HIDDEN);
        }

        /* WI #4613: always drawn. Each half shows its own feed, or its title
         * and "no feed" before that feed has ever published, so the slot is
         * never empty and neither half moves when the other arrives. */
        korg_klams_card_update(g_korgklams, now_s);

        /* WI #4665: always drawn. The clock repaints on the minute, the rest
         * only when the date or the tags change; the last good calendar
         * record is held through any gap, so nothing here flashes. */
        calendar_card_update(g_calendar, (time_t)now_s);

        /* WI #4695: always drawn. It repaints when the record changes or the
         * minute turns, nothing between; the last good record is held
         * through a gap, and an old one stops its running bars instead. */
        agents_card_update(g_agents, (time_t)now_s);

        /* WI #4848: always drawn, repainted only when a zone's reading, its
         * colour or the green-border signal changes. */
        apt_temps_card_update(g_apttemps, now_s);

        /* WI #4609: not drawn before claude:limits has ever published. Once
         * it has, it stays — limstate keeps the last record through a gap and
         * greys it by its own stamps, so the card never blinks out. */
        if (lim_has_record()) {
            limits_card_update(g_limits, now_s);
            if (lv_obj_has_flag(g_limits, LV_OBJ_FLAG_HIDDEN))
                lv_obj_clear_flag(g_limits, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* WI #374: apply pending card-evict commands — destroy a service card on
     * the LVGL thread, then drop the entry from its registry. Runs before the
     * paint loops so a removed entry is never repainted from a dangling
     * pointer. An apt-temps entry owns no card since sprint 047: dropping it
     * is enough, and the widget stops seeing it on its next update. */
    {
        evict_target_t ev[EVICT_TARGETS_MAX];
        int nev = redis_take_evict_targets(ev, EVICT_TARGETS_MAX);
        for (int i = 0; i < nev; i++) {
            if (strcmp(ev[i].kind, "apttemps") == 0) {
                apttemps_registry_remove(ev[i].name);
                continue;
            }
            void *c = service_registry_remove(ev[i].name, ev[i].host);
            if (c) lv_obj_delete((lv_obj_t *)c);
        }
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

    /* ---- WI #1903: host staleness cards.
     *
     * Ken, after looking at the panel (2026-09-05): "After service cards,
     * before apt-temps." Hence this block after the service cards. (The
     * apt-temps cards left the strip in sprint 047, WI #4848.)
     *
     * Paint order alone will not hold that, though. The strip is a flex row,
     * so child order is creation order, and a card created on a *later* poll
     * — a host that goes stale an hour from now — is appended at the end
     * whatever group it belongs to. So each trailing group walks its own
     * sorted snapshot moving every card to last: stale first, then update.
     * Whatever order the service cards ended up in, the strip settles as
     * [services…][stale, host-sorted][update, host-sorted].
     *
     * This is also what keeps several stale hosts ordered among themselves
     * rather than in the order they happened to go stale, and it is why the
     * group is placed by moving the ones after it rather than by moving each
     * new card to index 0 — that would reverse the hosts against each other.
     *
     * Cost is a reorder within one parent's child array for a strip holding a
     * handful of cards, on a loop that already repaints all of them.
     *
     * The reap runs before the paint loop, like the evict block above, so a
     * host whose last key vanished has its card destroyed rather than
     * repainted from a dangling pointer. redis_poll_stale has already
     * committed (or aborted) this second's cycle by the time we get here. ---- */
    {
        void *dead[STALE_REGISTRY_MAX];
        int ndead = stale_registry_reap(dead, STALE_REGISTRY_MAX);
        for (int i = 0; i < ndead; i++)
            if (dead[i]) lv_obj_delete((lv_obj_t *)dead[i]);

        /* Snapshot is already host-sorted by stale_registry_commit_cycle. */
        stale_entry_t st[STALE_REGISTRY_MAX];
        int nst = stale_registry_snapshot(st, STALE_REGISTRY_MAX);
        for (int i = 0; i < nst; i++) {
            stale_entry_t *live = stale_registry_find(st[i].host);
            if (!live) continue;
            if (!live->container) {
                stale_card_create(g_service_strip, live);
            }
            stale_card_update(live);
            lv_obj_move_to_index(live->container,
                                 (int32_t)lv_obj_get_child_count(g_service_strip) - 1);
        }
    }

    /* ---- WI #3642: update-issue cards — after the staleness cards, by the
     * same move-to-last walk (see the block above). Both are "a host needs
     * Ken" cards, so they sit together. ---- */
    {
        void *dead[UPDATE_REGISTRY_MAX];
        int ndead = update_registry_reap(dead, UPDATE_REGISTRY_MAX);
        for (int i = 0; i < ndead; i++)
            if (dead[i]) lv_obj_delete((lv_obj_t *)dead[i]);

        update_entry_t up[UPDATE_REGISTRY_MAX];
        int nup = update_registry_snapshot(up, UPDATE_REGISTRY_MAX);
        for (int i = 0; i < nup; i++) {
            update_entry_t *live = update_registry_find(up[i].host);
            if (!live) continue;
            if (!live->container) {
                update_card_create(g_service_strip, live);
            }
            update_card_update(live);
            lv_obj_move_to_index(live->container,
                                 (int32_t)lv_obj_get_child_count(g_service_strip) - 1);
        }
    }

    /* ---- WI #4777: the lamp panel takes the strip from the end of the last
     * status card (plus the strip's column gap) to its right edge. Placed
     * again only when the cards' total width changes, so forcing the strip's
     * flex layout to read it back costs nothing on the other polls. Until
     * WI #4847 the cards were all one width and the count was the whole of
     * the geometry; a span-2 card broke that, so the sum of the widths the
     * cards were set to (their style, which needs no layout) is the key. ---- */
    {
        static int32_t last_n = -1;
        static int32_t last_w = -1;
        int32_t n = (int32_t)lv_obj_get_child_count(g_service_strip);
        int32_t w = 0;
        for (int32_t i = 0; i < n; i++)
            w += lv_obj_get_style_width(lv_obj_get_child(g_service_strip, i), 0);
        if (n != last_n || w != last_w) {
            last_n = n;
            last_w = w;
            int32_t x0 = lv_obj_get_x(g_service_strip);
            int32_t x1 = x0 + lv_obj_get_width(g_service_strip);
            if (n > 0) {
                lv_obj_update_layout(g_service_strip);
                lv_obj_t *last = lv_obj_get_child(g_service_strip, n - 1);
                x0 += lv_obj_get_x(last) + lv_obj_get_width(last) +
                      lv_obj_get_style_pad_column(g_service_strip, 0);
            }
            lamp_panel_place(g_lamp_panel, x0, x1 - x0);
        }
        /* WI #4783: each machine's time on screen, from Redis every poll. */
        lamp_panel_set_period(g_lamp_panel, cmd->lamp_period_s);
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
