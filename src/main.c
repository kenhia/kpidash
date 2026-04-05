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

#include "config.h"
#include "fortune.h"
#include "lvgl.h"
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
static void timer_poll_cb(lv_timer_t *t) {
    (void)t;
    if (!redis_reconnect_if_needed())
        return;
    redis_poll();
    ui_refresh();
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

    /* DRM/KMS display — A3: verify refresh period is set (≈30fps) */
    lv_display_t *disp = lv_linux_drm_create();
    lv_linux_drm_set_file(disp, g_config.drm_dev, -1);

    /* Registry */
    registry_init();

    /* Build UI — must be after LVGL display init */
    ui_init();

    /* Redis connection */
    if (!redis_connect(g_config.redis_host, g_config.redis_port, g_config.redis_auth)) {
        ui_show_redis_error("initial connection failed — retrying");
        fprintf(stderr, "kpidash: Redis connection failed, will retry\n");
    } else {
        /* T050: Publish log path and version on startup */
        redis_write_system_info(g_config.log_file, KPIDASH_VERSION);
        redis_roundtrip_check();
    }

    /* Register 1-second poll timer */
    lv_timer_create(timer_poll_cb, 1000, NULL);

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
