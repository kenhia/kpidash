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

/* OS: pthreads (for mutex support in LVGL internals) */
#define LV_USE_OS   LV_OS_PTHREAD

/* Logging — enable for POC debugging */
#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 1
#endif

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

#endif /* LV_CONF_H */
#endif /* Enable */
