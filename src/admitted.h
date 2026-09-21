/* admitted.h — the admitted-hosts file (WI #3012).
 *
 * Sprint 022 made a `kpidash:clients` member earn its card by publishing
 * (WI #2524), which fixed `kwork` — a member with no client keys at all — but
 * left a regression behind it: "has published" lived only in the dashboard
 * process, so a host that was DOWN across a restart showed no card until it
 * published again, where before it showed a down card.
 *
 * Every client key is TTL'd (health 5 s, telemetry 15 s) and there is no
 * TTL-less per-host key in the protocol, so Redis cannot answer "has this
 * host ever published". This file is the answer, and it is deliberately the
 * option contained entirely in this repo: no new key in the
 * `kpidash:client:*` namespace that kdeskdash also reads, and no change any
 * publisher has to ship.
 *
 * Shape: one lowercase hostname per line, newline-terminated, no header. Small
 * enough to read and repair with `cat` — which is most of why it is not JSON.
 *
 * It is append-only, exactly like `kpidash:clients` itself: a host is never
 * removed, so a machine that comes back years later still has its card the
 * moment it publishes. Bounded by MAX_CLIENTS, because the registry it feeds
 * is.
 *
 * A missing, empty, unreadable or corrupt file is NOT an error. It means
 * "nothing admitted yet", which is exactly the state of a panel booting for
 * the first time after this deploy — the dashboard must come up either way.
 */
#ifndef KPIDASH_ADMITTED_H
#define KPIDASH_ADMITTED_H

#include <stdbool.h>

#include "protocol.h"

/* Read the admitted-hosts file into `out`, at most `max` entries.
 *
 * Returns the number of hostnames loaded, and returns 0 — never a negative
 * error — for a file that is missing, empty, unreadable or unparseable. The
 * dashboard has no way to ask a human about it and no better state to fall
 * back to, so "start empty" is the only useful answer.
 *
 * Lines that are blank, over-long, or contain anything outside the
 * protocol's lowercase hostname alphabet are skipped INDIVIDUALLY: one
 * mangled line must not throw away the hosts around it. Duplicates are
 * collapsed. */
int admitted_load(const char *path, char out[][HOSTNAME_LEN], int max);

/* Write `count` hostnames to the admitted-hosts file.
 *
 * Atomic: writes `<path>.tmp` in the same directory, fsyncs, then renames. A
 * panel loses power mid-write often enough to care, and a half-written file
 * here would drop cards on the next boot.
 *
 * Creates the parent directory if it is missing, one level, 0755 — the
 * dashboard runs as root and the first deploy has no /var/lib/kpidash.
 *
 * Returns true on success. A failure is worth logging and not worth
 * crashing over: the panel renders correctly all the way up to the next
 * restart. */
bool admitted_save(const char *path, const char hosts[][HOSTNAME_LEN], int count);

/* True if `name` is a syntactically valid hostname for this file: 1..63
 * bytes of [a-z0-9.-], per the protocol's "lowercase, matching the system
 * hostname output". Exposed for the tests and for the one caller that wants
 * to reject junk before it reaches the file. */
bool admitted_valid_hostname(const char *name);

#endif /* KPIDASH_ADMITTED_H */
