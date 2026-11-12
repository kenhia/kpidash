#ifndef NAS_DISK_USES_H
#define NAS_DISK_USES_H

/**
 * nas_disk_uses.h — what each of gratch's disks is FOR, and where it sits on
 * the NAS card (sprint 033, WI #4566). The one place to edit when a disk is
 * replaced or its job changes.
 *
 * kdash:nas v1 carries device, model and kind, but no use and no pool
 * membership, so this table supplies them. Getting `use` from the producer
 * would be a kdashdata contract change; Ken accepted hardcoding instead.
 *
 * Matched on the EXACT model string the feed carries — checked against the
 * live record on 2026-11-12 — because nvmeN numbering is not guaranteed
 * stable across boots, while a model is the disk. The three cold HDDs share a
 * model, a use and a slot, so the device name breaks the tie (sdb, sdc, sdd).
 * A disk that matches nothing gets no label and sorts after every listed one.
 *
 * `slot` is Ken's left-to-right order: the root NVMe, the three pool NVMes,
 * the cache SSD, the three cold HDDs. Included by nasstate.c only.
 */

#include "nasstate.h"

static const struct {
    const char *model;
    nas_use_t use;
    int slot;
} NAS_DISK_USES[] = {
    {"Samsung SSD 990 PRO 1TB", NAS_USE_ROOT, 0},       /* nvme2 on 2026-11-12 */
    {"CT4000T710SSD8", NAS_USE_POOL, 1},                /* nvme0 */
    {"WD_BLACK SN7100 4TB", NAS_USE_POOL, 2},           /* nvme1 */
    {"Samsung SSD 990 EVO Plus 4TB", NAS_USE_POOL, 3},  /* nvme3 */
    {"Samsung SSD 860 EVO 500GB", NAS_USE_CACHE, 4},    /* sda */
    {"ST8000VN0022-2EL112", NAS_USE_COLD, 5},           /* sdb, sdc, sdd */
};

#endif /* NAS_DISK_USES_H */
