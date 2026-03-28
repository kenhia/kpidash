#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#include "lvgl.h"
#include "src/drivers/display/drm/lv_linux_drm.h"

#include "registry.h"
#include "net.h"
#include "ui.h"
#include "protocol.h"

static volatile sig_atomic_t running = 1;

static void sigint_handler(int sig) {
    (void)sig;
    running = 0;
}

/* Global registry — shared between UDP thread and LVGL timer */
static registry_t registry;

/* LVGL timer callback (runs every 1s) */
static void timer_update_cb(lv_timer_t *t) {
    (void)t;
    ui_update(&registry);
}

int main(void) {
    /* Handle Ctrl-C gracefully */
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Parse port from env */
    int port = KPIDASH_DEFAULT_PORT;
    const char *port_env = getenv("KPIDASH_PORT");
    if (port_env) {
        int p = atoi(port_env);
        if (p > 0 && p <= 65535) port = p;
    }

    printf("kpidash: starting (port %d)\n", port);

    /* Initialize LVGL */
    lv_init();

    /* Create DRM display */
    lv_display_t *disp = lv_linux_drm_create();
    lv_linux_drm_set_file(disp, "/dev/dri/card0", -1);

    /* Initialize client registry */
    registry_init(&registry);

    /* Start UDP listener */
    if (net_start(&registry, port) != 0) {
        fprintf(stderr, "kpidash: failed to start UDP listener\n");
        return 1;
    }

    /* Build UI */
    ui_init();

    /* Timer: update client cards every 1 second */
    lv_timer_create(timer_update_cb, 1000, NULL);

    printf("kpidash: running\n");

    /* Main loop */
    while (running) {
        uint32_t sleep_ms = lv_timer_handler();
        if (sleep_ms > 100) sleep_ms = 100;
        usleep(sleep_ms * 1000);
    }

    printf("\nkpidash: shutting down\n");
    lv_deinit();

    return 0;
}
