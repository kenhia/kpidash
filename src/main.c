/**
 * main.c — kpidash entry point (T016, T050)
 *
 * Initializes LVGL, Redis, registry, UI, and fortune.
 * Runs a 1-second poll timer for Redis → UI refresh.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "admitted.h"
#include "config.h"
#include "fortune.h"
#include "kpidash_version.h"
#include "logfilter.h"
#include "lvgl.h"
#include "memstat.h"
#include "protocol.h"
#include "redis.h"
#include "registry.h"
#include "src/drivers/display/drm/lv_linux_drm.h"
#include "status.h"
#include "ui.h"

static volatile sig_atomic_t g_running = 1;
static kpidash_config_t g_config;

static void sigint_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* ---- 1-second LVGL timer: poll Redis, refresh UI ---- */
/* Persist the admitted-hosts set, but only when a host has been admitted
 * that was not admitted before (WI #3012). In steady state that is never:
 * the flag is false on every one of the ~86,400 polls a day, so this costs a
 * bool read on the LVGL thread and no disk I/O at all. It fires once per new
 * host, ever. */
static void save_admitted_if_changed(void) {
    if (!registry_admitted_take_dirty())
        return;

    char hosts[MAX_CLIENTS][HOSTNAME_LEN];
    int n = registry_admitted_snapshot(hosts, MAX_CLIENTS);
    if (admitted_save(g_config.state_file, hosts, n)) {
        fprintf(stderr, "kpidash: admitted set now %d host(s), saved to %s\n", n,
                g_config.state_file);
    } else {
        /* Not fatal, and deliberately not retried: the panel renders
         * correctly for the rest of this run, and the next new host will set
         * the flag again. Worth one loud line so a read-only /var is
         * diagnosable from the journal. */
        fprintf(stderr, "kpidash: could not save admitted set to %s — host cards will not "
                        "survive a restart\n",
                g_config.state_file);
    }
}

static void timer_poll_cb(lv_timer_t *t) {
    (void)t;
    if (!redis_reconnect_if_needed())
        return;
    redis_poll();
    save_admitted_if_changed();
    ui_refresh();
}

/* ---- 60-second LVGL timer: emit memstat sample (spec 005) ---- */
static void memstat_timer_cb(lv_timer_t *t) {
    (void)t;
    memstat_sample_now();
}

/* LVGL's print sink (WI #2646). Replaces LVGL's own LV_LOG_PRINTF path —
 * same destination, same flush — so that logfilter.c can drop the repeats of
 * a missing-glyph warning that would otherwise arrive once per redraw,
 * forever, for one character a publisher typed. */
static void lv_log_print_cb(lv_log_level_t level, const char *buf) {
    (void)level;
    switch (logfilter_check(buf)) {
    case LOGFILTER_SUPPRESS:
        return;
    case LOGFILTER_EMIT_FIRST:
        fputs(buf, stdout);
        /* Say it out loud: a reader who sees one line must not conclude the
         * character was drawn once. */
        fputs("kpidash: further reports for this codepoint suppressed (WI #2646); "
              "add it to RANGE in fonts/generate.sh to render it\n",
              stdout);
        break;
    case LOGFILTER_EMIT:
    default:
        fputs(buf, stdout);
        break;
    }
    fflush(stdout);
}

int main(void) {
    /* Signal handling */
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Load config from environment */
    config_load(&g_config);

    /* LVGL initialisation */
    lv_init();

    /* After lv_init, never before: LV_GLOBAL_INIT() resets the struct the
     * callback pointer lives in, so an earlier registration is thrown away. */
    lv_log_register_print_cb(lv_log_print_cb);

    /* DRM/KMS display — A3: verify refresh period is set (≈30fps) */
    lv_display_t *disp = lv_linux_drm_create();
    lv_linux_drm_set_file(disp, g_config.drm_dev, -1);

    /* Registry */
    registry_init();
    if (g_config.priority_client_count > 0)
        registry_set_priority_clients(g_config.priority_clients, g_config.priority_client_count);

    /* WI #3012: which hosts have ever published. Without this, a host that is
     * down when the dashboard starts gets no card at all — sprint 022 made a
     * card something a host earns by publishing, and the earning has to
     * outlive the process. A missing file is the normal first-boot state and
     * loads as empty. */
    {
        char admitted[MAX_CLIENTS][HOSTNAME_LEN];
        int n = admitted_load(g_config.state_file, admitted, MAX_CLIENTS);
        registry_seed_admitted(admitted, n);
        fprintf(stderr, "kpidash: %d previously-admitted host(s) from %s\n", n,
                g_config.state_file);
    }

    /* Build UI — must be after LVGL display init */
    ui_init();

    /* Redis connection */
    if (!redis_connect(g_config.redis_host, g_config.redis_port, g_config.redis_auth)) {
        ui_show_redis_error("initial connection failed — retrying");
        fprintf(stderr, "kpidash: Redis connection failed, will retry\n");
    } else {
        /* T050: Publish log path and version on startup */
        redis_write_system_info(g_config.log_file, KPIDASH_BUILD_VERSION);
        redis_roundtrip_check();
    }

    /* Register 1-second poll timer */
    lv_timer_create(timer_poll_cb, 1000, NULL);

    /* Memory telemetry (spec 005): one immediate sample for SC-004, then 60s cadence. */
    memstat_init();
    memstat_sample_now();
    lv_timer_create(memstat_timer_cb, 60000, NULL);

    /* Fortune (after ui_init so widget exists) */
    fortune_init(&g_config);

    printf("kpidash: running (Redis %s:%d, DRM %s)\n", g_config.redis_host, g_config.redis_port,
           g_config.drm_dev);

    /* Main loop */
    while (g_running) {
        uint32_t sleep_ms = lv_timer_handler();
        if (sleep_ms > 100)
            sleep_ms = 100;
        usleep(sleep_ms * 1000);
    }

    printf("\nkpidash: shutting down\n");
    redis_disconnect();
    lv_deinit();
    return 0;
}
