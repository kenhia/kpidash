#ifndef UI_H
#define UI_H

#include "lvgl.h"
#include "registry.h"

/**
 * Create the dashboard UI: title bar, clock, scrollable client list.
 * Call once after LVGL display is initialized.
 */
void ui_init(void);

/**
 * Update all client cards from the registry.
 * Called periodically from an LVGL timer. Locks/unlocks the registry.
 */
void ui_update(registry_t *reg);

#endif /* UI_H */
