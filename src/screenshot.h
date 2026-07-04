/**
 * @file screenshot.h
 * Device self-screenshot: render the active LVGL screen to a memory buffer
 * (lv_snapshot) and write it as a 24-bit BMP. Triggered one-shot via the
 * control Redis key `kpidash:screenshot` (see redis_poll) so a pixel-perfect
 * shot can be taken without photographing the panel.
 */
#ifndef KPIDASH_SCREENSHOT_H
#define KPIDASH_SCREENSHOT_H

#include <stdbool.h>

/* Default output path when the trigger key carries no path of its own. */
#define SCREENSHOT_DEFAULT_PATH "/tmp/kpidash-shot.bmp"

/* Snapshot the active screen to `path` (BMP). Runs on the UI thread; the
 * render takes a few tens of ms — fine for a one-shot trigger.
 * Returns false (and logs to stderr) on snapshot or file failure. */
bool screenshot_save(const char *path);

#endif /* KPIDASH_SCREENSHOT_H */
