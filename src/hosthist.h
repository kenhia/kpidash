#ifndef HOSTHIST_H
#define HOSTHIST_H

/**
 * hosthist.h — Per-host CPU / top-core / GPU history for the host columns
 * widget (WI #4523).
 *
 * An in-memory ring per host, fed from the telemetry the panel already reads
 * (kpidash:client:<host>:telemetry). No Redis contract, no producer change,
 * and nothing persisted: a panel restart starts every trace empty.
 *
 * Window: HOSTHIST_LEN samples, one per HOSTHIST_PERIOD_S seconds — 150 x 4 s
 * = 10 minutes, which is one sample per pixel of the 147 px sparkline.
 *
 * A host that stops reporting simply stops being recorded, so its trace holds
 * at the last thing it said — never a drop to zero. A short gap (a missed
 * poll or two) is bridged by holding the previous value; a long one (a host
 * that was away) is not filled, and the trace resumes where it left off.
 *
 * Thread-safe: every call takes the module's own mutex.
 */

#include "protocol.h"

#define HOSTHIST_PERIOD_S 4
#define HOSTHIST_LEN 150
/* Missing slots bridged by holding the previous value. 3 x 4 s = 12 s, inside
 * the telemetry key's own 15 s TTL: anything longer is a host that went away. */
#define HOSTHIST_GAP_FILL 3
#define HOSTHIST_MAX_HOSTS MAX_CLIENTS

/* Drop every host's history (tests; also the clean state at startup). */
void hosthist_reset(void);

/* Record one telemetry reading for `host` at wall time `now`. Readings inside
 * the same HOSTHIST_PERIOD_S slot overwrite each other (latest wins). When the
 * table is full the host recorded least recently gives up its ring. */
void hosthist_record(const char *host, double now, float cpu, float core, float gpu);

/* Copy up to `max` samples, oldest first, into the three arrays (any may be
 * NULL). Returns the number copied; 0 for a host with no history. */
int hosthist_read(const char *host, float *cpu, float *core, float *gpu, int max);

#endif /* HOSTHIST_H */
