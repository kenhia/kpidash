/**
 * ui.c — Dashboard layout and widget management (T017, T017a, T055)
 *
 * Layout (no title bar, FR-002a; no scroll anywhere, FR-008). Sprint 032
 * removed the dev graphs, Fortune, Activities and Repo Status, and with them
 * the rows-2-3 layout pool: two fixed cards need no placer.
 *
 *   ┌─────────────────────────────────────────────┐
 *   │  Host columns — Row 0, cols 0-5, 6×1 units  │
 *   │  (the whole row; sprint 030, WI #4543)      │
 *   ├──────────┬──────────┬───────────────────────┤
 *   │ 10G card │ NAS card │  (free)               │
 *   │  2×1     │  2×1     │                       │
 *   ├──────────┴──────────┴───────────────────────┤
 *   │  (free)                                     │
 *   ├─────────────────────────────────────────────┤
 *   │  Footer: service strip, status bar          │
 *   └─────────────────────────────────────────────┘
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "hostcols.h"
#include "layout.h"
#include "protocol.h"
#include "redis.h"
#include "nasstate.h"
#include "registry.h"
#include "widgets/apt_temps_card.h"
#include "widgets/dev_grid.h"
#include "widgets/dev_textsize.h"
#include "widgets/host_columns.h"
#include "widgets/nas_card.h"
#include "widgets/net_card.h"
#include "widgets/service_card.h"
#include "widgets/status_bar.h"

/* ---- Layout objects ---- */
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_host_columns = NULL; /* WI #4526: replaces the client card grid */
static lv_obj_t *g_netcard = NULL;
static lv_obj_t *g_nascard = NULL; /* WI #3956 */
static lv_obj_t *g_service_strip = NULL;  /* T013: footer Service-Strip container */
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
    int ns = hostcols_select(clients, n, sel, HOSTCOLS_MAX, &dropped);
    if (dropped != last_dropped) {
        /* Logged on change, not every second: the journal is for news. */
        if (dropped > 0)
            fprintf(stderr, "ui: %d host(s) past the %d-slot host columns limit are not shown\n",
                    dropped, HOSTCOLS_MAX);
        last_dropped = dropped;
    }
    double now = (double)time(NULL);
    for (int i = 0; i < ns; i++)
        hostcols_build(sel[i], now, &models[i]);
    host_columns_update(g_host_columns, models, ns);
}

/* ---- Public API ---- */

void ui_init(void) {
    g_screen = lv_screen_active();
    clear_bg(g_screen);
    /* No screen-level padding — widgets positioned via cell system */

    /* Footer sub-layout constants (status bar pinned at very bottom). */
    static const int32_t status_bar_h = 40;

    /* ---- Host columns (WI #4526, widened by WI #4543): the whole of row 0.
     * Columns to the right of the last host stay empty — what fills them is
     * Ken's later call. ---- */
    g_host_columns = host_columns_create(g_screen, COL_X(0), ROW_Y(0));

    /* ---- Row 1: the 10G card in columns 0-1, then the NAS card in columns
     * 2-3 (sprint 032). The NAS card is shown only once kdash:nas has
     * published, but its slot is fixed, so the 10G card never moves.
     * Columns 4-5 and row 2 stay empty. ---- */
    g_netcard = net_card_create(g_screen); /* WI #4062 */
    lv_obj_set_pos(g_netcard, COL_X(0), ROW_Y(1));

    g_nascard = nas_card_create(g_screen); /* WI #3956 */
    lv_obj_set_pos(g_nascard, COL_X(2) + CELL_PAD, ROW_Y(1) + CELL_PAD);
    lv_obj_add_flag(g_nascard, LV_OBJ_FLAG_HIDDEN);

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
    }

    /* WI #374: apply pending card-evict commands — destroy the card on the
     * LVGL thread, then drop the entry from its registry. Runs before the paint
     * loops so a removed entry is never repainted from a dangling pointer. */
    {
        evict_target_t ev[EVICT_TARGETS_MAX];
        int nev = redis_take_evict_targets(ev, EVICT_TARGETS_MAX);
        for (int i = 0; i < nev; i++) {
            void *c = (strcmp(ev[i].kind, "apttemps") == 0)
                          ? apttemps_registry_remove(ev[i].name)
                          : service_registry_remove(ev[i].name, ev[i].host);
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
     * before apt-temps." Hence this block sitting between the two.
     *
     * Paint order alone will not hold that, though. The strip is a flex row,
     * so child order is creation order, and a card created on a *later* poll
     * — a host that goes stale an hour from now — is appended at the end
     * whatever group it belongs to. So each of the two trailing groups walks
     * its own sorted snapshot moving every card to last: stale first, then
     * apt-temps. Whatever order the service cards ended up in, the strip
     * settles as [services…][stale, host-sorted][apt-temps, slug-sorted].
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

    /* ---- WI #3642: update-issue cards — after the staleness cards, before
     * apt-temps, by the same move-to-last walk (see the block above). Both
     * are "a host needs Ken" cards, so they sit together. ---- */
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

    /* ---- WI #364: Apt-Temps per-zone cards, painted into the footer strip
     * last — after the service cards and after any staleness and update cards. ---- */
    {
        apttemps_entry_t ats[APTTEMPS_REGISTRY_MAX];
        int nat = apttemps_registry_snapshot(ats, APTTEMPS_REGISTRY_MAX);
        /* Stable order by slug ascending. */
        for (int i = 1; i < nat; i++) {
            apttemps_entry_t tmp = ats[i];
            int j = i;
            while (j > 0 && strncmp(ats[j - 1].slug, tmp.slug, sizeof(tmp.slug)) > 0) {
                ats[j] = ats[j - 1];
                j--;
            }
            ats[j] = tmp;
        }
        double now = (double)time(NULL);
        for (int i = 0; i < nat; i++) {
            apttemps_entry_t *live = apttemps_registry_find_or_create(ats[i].slug);
            if (!live) continue;
            if (!live->container) {
                apt_temps_card_create(g_service_strip, live);
            }
            apt_temps_card_update(live, now);
            lv_obj_move_to_index(live->container,
                                 (int32_t)lv_obj_get_child_count(g_service_strip) - 1);
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
