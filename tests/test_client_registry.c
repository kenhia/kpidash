/* test_client_registry.c — admission of host cards (WI #2524).
 *
 * `kpidash:clients` is only ever added to: redis_client.py SADDs on every
 * health write and nothing in the fleet ever removes a member. So membership
 * alone cannot mean "render a card" — `kwork` sat in that set with no client
 * keys at all, and the panel carried a permanently-down card for it, on a
 * screen where "down" is exactly how a genuinely dead machine reads.
 *
 * The rule under test is admission-on-data: a member is admitted the first
 * time it actually publishes, and once admitted it KEEPS its card when the
 * data stops. The second half is the one that matters most — dropping a card
 * when health expires would turn every real outage into a silently vanishing
 * host, which is the failure WI #902 exists to prevent.
 */
#define KPIDASH_TEST_STUBS 1
#include "registry.h"

#include <stdio.h>
#include <string.h>

static int passed = 0;
static int failed = 0;

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr);                      \
            failed++;                                                                              \
        } else {                                                                                   \
            passed++;                                                                              \
        }                                                                                          \
    } while (0)

/* A member with no client keys is never admitted, however many poll cycles
 * it sits in the set. This is `kwork`. */
static void test_never_published_gets_no_card(void) {
    registry_init();

    for (int cycle = 0; cycle < 100; cycle++) {
        CHECK(registry_admit("kwork", false) == NULL);
    }
    CHECK(registry_find("kwork") == NULL);
    CHECK(registry_count() == 0);
}

/* The first payload admits it, and the hostname lands intact. */
static void test_first_payload_admits(void) {
    registry_init();

    CHECK(registry_admit("kai", false) == NULL);
    client_info_t *c = registry_admit("kai", true);
    CHECK(c != NULL);
    CHECK(c != NULL && strcmp(c->hostname, "kai") == 0);
    CHECK(registry_count() == 1);

    /* Same slot on the next cycle, not a second one. */
    CHECK(registry_admit("kai", true) == c);
    CHECK(registry_find("kai") == c);
    CHECK(registry_count() == 1);
}

/* Once admitted, a host keeps its card when its keys expire — that card
 * going red IS the outage report. */
static void test_admitted_host_survives_its_data_expiring(void) {
    registry_init();

    client_info_t *c = registry_admit("kubs0", true);
    CHECK(c != NULL);
    c->online = true;

    /* Health and telemetry both gone: 5s and 15s TTLs, so this is any host
     * more than fifteen seconds dead. */
    for (int cycle = 0; cycle < 100; cycle++) {
        CHECK(registry_admit("kubs0", false) == c);
    }
    CHECK(registry_count() == 1);
    CHECK(registry_find("kubs0") == c);
}

/* Admission does not disturb the prune: a member that leaves the set still
 * goes, and one that stays still stays. */
static void test_admission_and_prune_compose(void) {
    registry_init();

    CHECK(registry_admit("kai", true) != NULL);
    CHECK(registry_admit("kubs0", true) != NULL);
    CHECK(registry_admit("kwork", false) == NULL);
    CHECK(registry_count() == 2);

    const char *still_there[] = {"kai", "kwork"};
    registry_remove_absent(still_there, 2);
    CHECK(registry_count() == 1);
    CHECK(registry_find("kai") != NULL);
    CHECK(registry_find("kubs0") == NULL);
    /* kwork is in the set and still has no card — the prune does not admit. */
    CHECK(registry_find("kwork") == NULL);
}

/* registry_find never creates, and tolerates the degenerate inputs the set
 * could in principle hold. */
static void test_find_never_creates(void) {
    registry_init();

    CHECK(registry_find("nobody") == NULL);
    CHECK(registry_count() == 0);
    CHECK(registry_find(NULL) == NULL);
    CHECK(registry_find("") == NULL);
    CHECK(registry_admit(NULL, true) == NULL);
    CHECK(registry_admit("", true) == NULL);
    CHECK(registry_count() == 0);
}

/* A keyless member must not consume a MAX_CLIENTS slot, nor evict a real
 * host to make room for itself — the original complaint's second half. */
static void test_keyless_member_never_evicts(void) {
    registry_init();

    char host[HOSTNAME_LEN];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        snprintf(host, sizeof(host), "h%02d", i);
        CHECK(registry_admit(host, true) != NULL);
    }
    CHECK(registry_count() == MAX_CLIENTS);

    CHECK(registry_admit("kwork", false) == NULL);
    CHECK(registry_count() == MAX_CLIENTS);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        snprintf(host, sizeof(host), "h%02d", i);
        CHECK(registry_find(host) != NULL);
    }
}

/* WI #3012: a host admitted in an EARLIER run is admitted now, with no data
 * at all. This is the restart case — the whole reason the file exists. */
static void test_seeded_host_gets_a_card_while_down(void) {
    registry_init();

    char seed[2][HOSTNAME_LEN] = {"kai", "kubs0"};
    CHECK(registry_seed_admitted(seed, 2) == 2);

    /* Down since before the dashboard started: no health, no telemetry. */
    client_info_t *c = registry_admit("kai", false);
    CHECK(c != NULL);
    CHECK(c != NULL && strcmp(c->hostname, "kai") == 0);
    CHECK(c != NULL && c->online == false); /* red card, not a missing one */
    CHECK(registry_count() == 1);

    /* Still stable across cycles, and still the same slot when it comes back. */
    CHECK(registry_admit("kai", false) == c);
    CHECK(registry_admit("kai", true) == c);
    CHECK(registry_count() == 1);

    /* A host that was never admitted is still refused, seed or no seed. */
    CHECK(registry_admit("kwork", false) == NULL);
    CHECK(registry_count() == 1);
}

/* Seeding is not a write: it came off the disk. */
static void test_seeding_does_not_dirty_the_set(void) {
    registry_init();
    CHECK(registry_admitted_take_dirty() == false);

    char seed[2][HOSTNAME_LEN] = {"kai", "kubs0"};
    CHECK(registry_seed_admitted(seed, 2) == 2);
    CHECK(registry_admitted_take_dirty() == false);

    /* A host already in the seed publishing is not a change either. */
    CHECK(registry_admit("kai", true) != NULL);
    CHECK(registry_admitted_take_dirty() == false);

    /* A genuinely new host IS a change — once. */
    CHECK(registry_admit("kubsdb", true) != NULL);
    CHECK(registry_admitted_take_dirty() == true);
    CHECK(registry_admitted_take_dirty() == false);

    /* And a member that never publishes never dirties anything, however
     * many cycles it sits there — otherwise kwork would rewrite the file
     * once a second forever. */
    for (int i = 0; i < 50; i++) {
        CHECK(registry_admit("kwork", false) == NULL);
    }
    CHECK(registry_admitted_take_dirty() == false);
}

/* The snapshot is what gets written back, so it has to round-trip. */
static void test_admitted_snapshot(void) {
    registry_init();

    char seed[2][HOSTNAME_LEN] = {"kai", "kubs0"};
    CHECK(registry_seed_admitted(seed, 2) == 2);
    CHECK(registry_admit("kubsdb", true) != NULL);
    CHECK(registry_admit("kwork", false) == NULL);

    char out[MAX_CLIENTS][HOSTNAME_LEN];
    int n = registry_admitted_snapshot(out, MAX_CLIENTS);
    CHECK(n == 3);

    bool saw_kwork = false, saw_kubsdb = false;
    for (int i = 0; i < n; i++) {
        if (strcmp(out[i], "kwork") == 0)
            saw_kwork = true;
        if (strcmp(out[i], "kubsdb") == 0)
            saw_kubsdb = true;
    }
    CHECK(saw_kubsdb);
    CHECK(!saw_kwork); /* never published, so never persisted */

    /* Honours a smaller ceiling without writing past it. */
    char small[2][HOSTNAME_LEN];
    CHECK(registry_admitted_snapshot(small, 2) == 2);
    CHECK(registry_admitted_snapshot(out, 0) == 0);
}

/* Duplicates and junk in the file do not multiply the set. */
static void test_seed_is_deduped_and_bounded(void) {
    registry_init();

    char seed[4][HOSTNAME_LEN] = {"kai", "kai", "kubs0", ""};
    CHECK(registry_seed_admitted(seed, 4) == 2);

    char out[MAX_CLIENTS][HOSTNAME_LEN];
    CHECK(registry_admitted_snapshot(out, MAX_CLIENTS) == 2);

    CHECK(registry_seed_admitted(NULL, 4) == 0);
    CHECK(registry_seed_admitted(seed, 0) == 0);

    /* Past MAX_CLIENTS the set stops growing rather than overrunning. */
    registry_init();
    char many[MAX_CLIENTS + 8][HOSTNAME_LEN];
    memset(many, 0, sizeof(many));
    for (int i = 0; i < MAX_CLIENTS + 8; i++) {
        snprintf(many[i], HOSTNAME_LEN, "h%03d", i);
    }
    CHECK(registry_seed_admitted(many, MAX_CLIENTS + 8) == MAX_CLIENTS);
    CHECK(registry_admitted_snapshot(out, MAX_CLIENTS) == MAX_CLIENTS);
}

int main(void) {
    test_never_published_gets_no_card();
    test_first_payload_admits();
    test_admitted_host_survives_its_data_expiring();
    test_admission_and_prune_compose();
    test_find_never_creates();
    test_keyless_member_never_evicts();
    test_seeded_host_gets_a_card_while_down();
    test_seeding_does_not_dirty_the_set();
    test_admitted_snapshot();
    test_seed_is_deduped_and_bounded();

    fprintf(stderr, "test_client_registry: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
