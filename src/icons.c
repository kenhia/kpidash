#include "icons.h"

#include <stddef.h>
#include <stdint.h>

/* Initial curated set of nerd-font glyphs for service status cards.
 * Indices are append-only — never renumber, never reuse. */
const icon_entry_t ICON_REGISTRY[] = {
    {  0, 0xF300, "computer" },
    {  1, 0xF301, "server"   },
    {  2, 0xF302, "database" },
    {  3, 0xF303, "queue"    },
    {  4, 0xF304, "network"  },
    {  5, 0xF305, "disk"     },
    {  6, 0xF306, "lock"     },
    {  7, 0xF307, "key"      },
    {  8, 0xF308, "cloud"    },
    {  9, 0xF309, "sync"     },
    { 10, 0xF30A, "build"    },
    { 11, 0xF30B, "log"      },
    { 12, 0xF30C, "alert"    },
    { 13, 0xF30D, "config"   },
    { 14, 0xF30E, "metric"   },
    { 15, 0xF30F, "search"   },
};

const size_t ICON_REGISTRY_COUNT = sizeof(ICON_REGISTRY) / sizeof(ICON_REGISTRY[0]);

/* Encode codepoint as UTF-8 into buf (must hold at least 5 bytes incl. NUL).
 * Returns number of bytes written (excluding NUL). */
static int encode_utf8(uint32_t cp, char *buf) {
    if (cp < 0x80) {
        buf[0] = (char)cp;
        buf[1] = '\0';
        return 1;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        buf[2] = '\0';
        return 2;
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        buf[3] = '\0';
        return 3;
    } else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        buf[4] = '\0';
        return 4;
    }
}

/* Each entry caches its UTF-8 encoding on first use. ICON_REGISTRY_COUNT is bounded. */
static char g_utf8_cache[64][5];
static int  g_utf8_cached[64];

const char *icons_lookup(int index) {
    if (index < 0)
        return NULL;
    for (size_t i = 0; i < ICON_REGISTRY_COUNT; i++) {
        if (ICON_REGISTRY[i].index == index) {
            if (i < 64 && !g_utf8_cached[i]) {
                encode_utf8(ICON_REGISTRY[i].codepoint, g_utf8_cache[i]);
                g_utf8_cached[i] = 1;
            }
            return (i < 64) ? g_utf8_cache[i] : NULL;
        }
    }
    return NULL;
}

uint32_t icons_codepoint(int index) {
    if (index < 0)
        return 0;
    for (size_t i = 0; i < ICON_REGISTRY_COUNT; i++) {
        if (ICON_REGISTRY[i].index == index)
            return ICON_REGISTRY[i].codepoint;
    }
    return 0;
}
