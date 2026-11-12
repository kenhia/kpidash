#ifndef HOSTCOLS_H
#define HOSTCOLS_H

/**
 * hostcols.h — The pure half of the host columns widget (WI #4524, #4525):
 * the fill rule, the host order, and the per-host display model with its
 * attention levels. No LVGL, so all of it is unit-tested; the widget in
 * src/widgets/host_columns.c only draws what this decides.
 */

#include <stdbool.h>

#include "protocol.h"
#include "registry.h"

#define HOSTCOLS_COLS 10 /* full columns across the widget */
#define HOSTCOLS_PER 3   /* compact tiles per column */
#define HOSTCOLS_MAX (HOSTCOLS_COLS * HOSTCOLS_PER)
#define HOSTCOLS_DISKS 5 /* drive rows in a full column */

/* Attention thresholds. RAM is the mock-up's 80/90 (>=). The drive thresholds
 * are the retired client card's, kept as WI #4524 asks when the card already
 * encoded its own: warn above 60 %, critical above 75 %. CPU, GPU and VRAM
 * never raise attention — a loaded model holds VRAM above 90 % all day. */
#define HOSTCOLS_RAM_WARN 80
#define HOSTCOLS_RAM_CRIT 90
#define HOSTCOLS_DISK_WARN 60.0f
#define HOSTCOLS_DISK_CRIT 75.0f

typedef enum { HC_ATT_NONE = 0, HC_ATT_WARN, HC_ATT_CRIT } hc_att_t;

typedef struct {
    char name[LABEL_LEN];
    int pct;
    hc_att_t att;
} hc_disk_t;

/* Everything one column or tile draws, already formatted. */
typedef struct {
    char host[HOSTNAME_LEN];
    char os[OS_NAME_LEN];
    char status[32]; /* "up 3d 2h", or "NO DATA 4m" when nodata */
    bool nodata;     /* a non-intermittent host that stopped reporting */
    hc_att_t att;    /* the border: worst of nodata, RAM and the drives */
    int cpu;
    int core;
    int ram_pct;
    hc_att_t ram_att;
    bool gpu;
    int gpu_pct;
    char vram[32]; /* "30.3 / 31.8" (GB) */
    int vram_pct;
    int ndisks;
    hc_disk_t disks[HOSTCOLS_DISKS];
    int worst; /* index into disks of the fullest; -1 with no drives */
} hc_host_t;

/* The fill rule: n hosts -> how many full columns and how many compact tiles.
 * n <= 10 is all full; otherwise full = floor((30 - n) / 2) and the rest are
 * compact, three to a column. n is capped at HOSTCOLS_MAX. */
void hostcols_plan(int n, int *full, int *compact);

/* Host order, which is also the demotion order: KPIDASH_PRIORITY_CLIENTS
 * first, in list order (registry_priority_index), then everyone else by
 * hostname. */
int hostcols_cmp(const client_info_t *a, const client_info_t *b);

/* Choose and order the hosts that get a slot. A host that declared itself
 * intermittent and is asleep takes no slot. Writes pointers into `out`
 * (sorted, at most `max`) and returns how many; `*dropped` (may be NULL) gets
 * how many otherwise-eligible hosts did not fit. */
int hostcols_select(const client_info_t *in, int n, const client_info_t **out, int max,
                    int *dropped);

hc_att_t hostcols_ram_att(int pct);
hc_att_t hostcols_disk_att(float pct);

/* Build one host's display model at wall time `now`. */
void hostcols_build(const client_info_t *c, double now, hc_host_t *out);

#endif /* HOSTCOLS_H */
