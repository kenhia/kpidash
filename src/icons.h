#ifndef ICONS_H
#define ICONS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int         index;
    uint32_t    codepoint;
    const char *label;
} icon_entry_t;

extern const icon_entry_t ICON_REGISTRY[];
extern const size_t       ICON_REGISTRY_COUNT;

/* Returns NUL-terminated UTF-8 string for the glyph, or NULL if index unknown. */
const char *icons_lookup(int index);

/* Returns the codepoint, or 0 if index unknown. */
uint32_t icons_codepoint(int index);

#endif /* ICONS_H */
