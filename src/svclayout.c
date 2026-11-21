/* svclayout.c — the service card's geometry and line fitting (WI #4847). */
#include "svclayout.h"

#include <string.h>

int32_t svc_card_width(int span) {
    return span == 2 ? 2 * SVC_CARD_W + SVC_STRIP_GAP : SVC_CARD_W;
}

int svc_lines_fit(const int32_t *h, int n, int32_t avail, int32_t gap) {
    if (!h || n <= 0 || avail <= 0) return 0;
    int32_t used = 0;
    int fit = 0;
    for (int i = 0; i < n; i++) {
        int32_t need = h[i] + (i > 0 ? gap : 0);
        if (used + need > avail) break;
        used += need;
        fit++;
    }
    return fit;
}

static uint32_t fnv(uint32_t s, const void *p, size_t n) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) {
        s ^= b[i];
        s *= 16777619u;
    }
    return s;
}

static uint32_t fnv_str(uint32_t s, const char *str) {
    /* The terminator goes in too, so "ab"+"c" and "a"+"bc" differ. */
    return str ? fnv(s, str, strlen(str) + 1) : fnv(s, "", 1);
}

static uint32_t fnv_int(uint32_t s, int32_t v) {
    return fnv(s, &v, sizeof(v));
}

uint32_t svc_layout_sig(const char *title, int span, const char *text,
                        const char *host, int icon,
                        const kdash_service_line_t *lines, int n_lines) {
    uint32_t s = 2166136261u;
    s = fnv_str(s, title);
    s = fnv_int(s, span);
    s = fnv_str(s, text);
    s = fnv_str(s, host);
    s = fnv_int(s, icon);
    s = fnv_int(s, n_lines);
    for (int i = 0; lines && i < n_lines; i++) {
        s = fnv_str(s, lines[i].t);
        s = fnv_int(s, (int32_t)lines[i].size);
        s = fnv_int(s, (int32_t)lines[i].align);
        s = fnv_int(s, lines[i].has_color ? 1 : 0);
        s = fnv_int(s, (int32_t)lines[i].color);
    }
    return s;
}
