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

/* Attention thresholds, the mock-up's: RAM and every drive warn at >= 80 %
 * and are critical at >= 90 %, judged on the rounded percent that is drawn so
 * the figure and its colour always agree. (The retired card coloured drives
 * from 60 / 75; the overseer ruled for the mock-up on korg:4527 — a laptop
 * drive at 63 % raising a border is the noise the redesign removes.) CPU, GPU
 * and VRAM never raise attention — a loaded model holds VRAM above 90 % all
 * day. */
#define HOSTCOLS_RAM_WARN 80
#define HOSTCOLS_RAM_CRIT 90
#define HOSTCOLS_DISK_WARN 80
#define HOSTCOLS_DISK_CRIT 90

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
    /* One drive row (WI #4544): the fullest drive by percent, so the drive
     * that set the border is always the one drawn. The border itself still
     * comes from every drive. */
    bool has_disk;
    hc_disk_t disk;
    int disk_more; /* other drives at or over the warn line: the "+N" */
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
hc_att_t hostcols_disk_att(int pct);

/* The full column's OS name (korg:4558): the OS or distro name, then its
 * version cut to two numeric parts — "Ubuntu 24.04.5 LTS" -> "Ubuntu 24.04",
 * "Debian GNU/Linux 13 (trixie)" -> "Debian 13". Drops "GNU/Linux",
 * parenthesised codenames and everything after the version. Windows keeps its
 * edition and drops the feature update and build ("Win 11 Pro 26H2
 * 26300.9818" -> "Win 11 Pro"). A name with no version ("Arch Linux") is kept
 * whole, and a string that would come out empty passes through unchanged.
 * Display only: the published os_name is never altered. */
void hostcols_short_os(const char *in, char *out, size_t len);

/* Build one host's display model at wall time `now`. */
void hostcols_build(const client_info_t *c, double now, hc_host_t *out);

#endif /* HOSTCOLS_H */
