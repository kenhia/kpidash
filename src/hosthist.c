/**
 * hosthist.c — Per-host CPU / top-core / GPU history ring (WI #4523).
 * See hosthist.h for the window and the gap rules.
 */
#include "hosthist.h"

#include <math.h>
#include <pthread.h>
#include <string.h>

typedef struct {
    char host[HOSTNAME_LEN];
    float cpu[HOSTHIST_LEN];
    float core[HOSTHIST_LEN];
    float gpu[HOSTHIST_LEN];
    int head;          /* index the NEXT sample is written to */
    int count;         /* samples held, <= HOSTHIST_LEN */
    long last_slot;    /* slot of the newest sample */
    double last_now;   /* wall time of the newest record, for eviction */
} hist_t;

static hist_t g_hist[HOSTHIST_MAX_HOSTS];
static int g_nhist = 0;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

void hosthist_reset(void) {
    pthread_mutex_lock(&g_mutex);
    memset(g_hist, 0, sizeof(g_hist));
    g_nhist = 0;
    pthread_mutex_unlock(&g_mutex);
}

static hist_t *find(const char *host) {
    for (int i = 0; i < g_nhist; i++)
        if (strncmp(g_hist[i].host, host, HOSTNAME_LEN) == 0)
            return &g_hist[i];
    return NULL;
}

static hist_t *find_or_claim(const char *host) {
    hist_t *h = find(host);
    if (h)
        return h;
    if (g_nhist < HOSTHIST_MAX_HOSTS) {
        h = &g_hist[g_nhist++];
    } else {
        h = &g_hist[0];
        for (int i = 1; i < g_nhist; i++)
            if (g_hist[i].last_now < h->last_now)
                h = &g_hist[i];
    }
    memset(h, 0, sizeof(*h));
    size_t n = strnlen(host, HOSTNAME_LEN - 1);
    memcpy(h->host, host, n);
    return h;
}

static void push(hist_t *h, float cpu, float core, float gpu) {
    h->cpu[h->head] = cpu;
    h->core[h->head] = core;
    h->gpu[h->head] = gpu;
    h->head = (h->head + 1) % HOSTHIST_LEN;
    if (h->count < HOSTHIST_LEN)
        h->count++;
}

void hosthist_record(const char *host, double now, float cpu, float core, float gpu) {
    if (!host || !host[0])
        return;
    long slot = (long)floor(now / HOSTHIST_PERIOD_S);

    pthread_mutex_lock(&g_mutex);
    hist_t *h = find_or_claim(host);
    h->last_now = now;
    if (h->count > 0 && slot <= h->last_slot) {
        /* Same slot (or a clock stepping backwards): the latest reading wins. */
        int last = (h->head + HOSTHIST_LEN - 1) % HOSTHIST_LEN;
        h->cpu[last] = cpu;
        h->core[last] = core;
        h->gpu[last] = gpu;
    } else {
        if (h->count > 0) {
            long missing = slot - h->last_slot - 1;
            if (missing > 0 && missing <= HOSTHIST_GAP_FILL) {
                int last = (h->head + HOSTHIST_LEN - 1) % HOSTHIST_LEN;
                float pc = h->cpu[last], pk = h->core[last], pg = h->gpu[last];
                for (long i = 0; i < missing; i++)
                    push(h, pc, pk, pg);
            }
        }
        push(h, cpu, core, gpu);
        h->last_slot = slot;
    }
    pthread_mutex_unlock(&g_mutex);
}

int hosthist_read(const char *host, float *cpu, float *core, float *gpu, int max) {
    if (!host || !host[0] || max <= 0)
        return 0;
    pthread_mutex_lock(&g_mutex);
    const hist_t *h = find(host);
    int n = 0;
    if (h) {
        n = h->count < max ? h->count : max;
        /* Oldest of the newest n samples. */
        int start = (h->head + HOSTHIST_LEN - n) % HOSTHIST_LEN;
        for (int i = 0; i < n; i++) {
            int k = (start + i) % HOSTHIST_LEN;
            if (cpu)
                cpu[i] = h->cpu[k];
            if (core)
                core[i] = h->core[k];
            if (gpu)
                gpu[i] = h->gpu[k];
        }
    }
    pthread_mutex_unlock(&g_mutex);
    return n;
}
