/**
 * @file lv_conf.h
 * Configuration file for LVGL v9.2.2 — KPI Dashboard
 */

#if 1 /* Enable */

#ifndef LV_CONF_H
#define LV_CONF_H

/* Color depth: 32 = XRGB8888, matching DRM framebuffer */
#define LV_COLOR_DEPTH 32

/* Use standard C library */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/* HAL */
#define LV_DEF_REFR_PERIOD  33      /* ~30 fps */
#define LV_DPI_DEF 160              /* reasonable for 4K at ~28" viewing distance */

/* OS: none — the software draw unit executes each draw task INLINE.
 *
 * WI #1798: with LV_OS_PTHREAD and the default LV_DRAW_SW_DRAW_UNIT_CNT of 1,
 * lv_draw_sw.c hands every task to a render thread one at a time over a futex
 * signal/wait round-trip (lv_draw_sw.c:459 `lv_thread_sync_signal`). The three
 * dev_graph cards refresh 3 x 9 series x 300 points = ~8,100 lv_draw_line tasks
 * a second, so that handshake ran ~8,300 times a second — and because
 * lv_draw_finalize_task_creation() calls lv_draw_dispatch() after every task,
 * the walk over the layer's pending-task list went roughly quadratic.
 *
 * LV_OS_NONE takes the `#else` at lv_draw_sw.c:463: execute_drawing_unit()
 * runs the task immediately, so the pending list never grows and the dispatch
 * walk stays O(1). Nothing in src/ uses LVGL's threading primitives — the
 * registries carry their own pthread mutexes, and the Redis poll is a
 * synchronous step on the one LVGL thread — so there is no LVGL-internal
 * locking to lose. */
#define LV_USE_OS   LV_OS_NONE

/* Logging — enable for POC debugging.
 *
 * LV_LOG_PRINTF is 0 deliberately (WI #2646): lv_log_add() runs its printf
 * path and any registered callback BOTH, so leaving it at 1 would print
 * every line twice and dedupe neither. main.c registers the printing half
 * with lv_log_register_print_cb(); src/logfilter.c decides what reaches
 * stdout, which is how one missing glyph stops writing 3,844 journal lines
 * in two hours. Nothing is lost by the swap — the callback prints in the
 * same place, in the same format, with the same fflush. */
#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 0
#endif

/* Note (spec 005): memstat calls lv_mem_monitor() directly; that function
 * is unconditionally available in lib/lvgl/src/stdlib/lv_mem.c. The
 * LV_USE_MEM_MONITOR flag only controls the SYSMON on-screen overlay,
 * which we do not use. */

/* Fonts */
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_MONTSERRAT_28  1
#define LV_FONT_MONTSERRAT_36  1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

/* Widgets */
#define LV_USE_LED 1

/* Drivers — Linux DRM for direct KMS rendering */
#define LV_USE_LINUX_DRM 1

/* Filesystem — POSIX for loading images from disk */
#define LV_USE_FS_POSIX 1
#if LV_USE_FS_POSIX
    #define LV_FS_POSIX_LETTER 'A'
    #define LV_FS_POSIX_PATH ""
    #define LV_FS_POSIX_CACHE_SIZE 0
#endif

/* Image decoders */
#define LV_USE_LIBPNG 1

/* Snapshot — render the active screen to a buffer for the device
 * self-screenshot (see src/screenshot.c, kpidash:screenshot control key) */
#define LV_USE_SNAPSHOT 1

#endif /* LV_CONF_H */
#endif /* Enable */
