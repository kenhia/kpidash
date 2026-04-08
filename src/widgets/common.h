#ifndef WIDGETS_COMMON_H
#define WIDGETS_COMMON_H

/**
 * common.h — Shared widget color palette and font constants.
 *
 * Catppuccin Mocha palette. All widgets #include this instead of
 * defining local COLOR_* / FONT_* macros.
 *
 * Prefix: WS_ (Widget Style) to avoid clashes with LVGL's LV_ namespace.
 */

#include "lv_font_custom.h"
#include "lvgl.h"

/* ---- Catppuccin Mocha color palette ---- */
#define WS_COLOR_BG lv_color_hex(0x1E1E2E)       /* Base */
#define WS_COLOR_FG lv_color_hex(0xCDD6F4)       /* Text */
#define WS_COLOR_MUTED lv_color_hex(0x6C7086)    /* Overlay0 */
#define WS_COLOR_HEADER lv_color_hex(0xF5C2E7)   /* Pink */
#define WS_COLOR_GREEN lv_color_hex(0xA6E3A1)    /* Green */
#define WS_COLOR_BLUE lv_color_hex(0x89B4FA)     /* Blue */
#define WS_COLOR_RED lv_color_hex(0xF38BA8)      /* Red */
#define WS_COLOR_ORANGE lv_color_hex(0xFAB387)   /* Peach */
#define WS_COLOR_MAUVE lv_color_hex(0xCBA6F7)    /* Mauve */
#define WS_COLOR_YELLOW lv_color_hex(0xF9E2AF)   /* Yellow */
#define WS_COLOR_SURFACE0 lv_color_hex(0x313244) /* Surface0 */
#define WS_COLOR_SURFACE1 lv_color_hex(0x45475A) /* Surface1 */

/* ---- Shared font pointers ---- */
#define WS_FONT_HDR (&lv_font_montserrat_bold_20)
#define WS_FONT_BODY (&lv_font_montserrat_20)
#define WS_FONT_SMALL (&lv_font_montserrat_16)
#define WS_FONT_TINY (&lv_font_montserrat_14)
#define WS_FONT_LARGE (&lv_font_montserrat_bold_24)

#endif /* WIDGETS_COMMON_H */
