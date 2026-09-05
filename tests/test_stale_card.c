/* test_stale_card.c — host staleness feed reader (WI #1903).
 *
 * Covers the three things this reader can get wrong, in the order they matter:
 *
 *  1. CD-18's inversion — an unreadable record must NOT clear the flag.
 *     Everywhere else in this codebase an unparseable payload is skipped
 *     whole; skipping THIS one renders as all-clear, so a parse bug would
 *     report a three-week outage as healthy.
 *  2. The oldest-`since`-first sort. Only testable at all because the two
 *     live writers flag a host seconds apart rather than simultaneously —
 *     against a single key every ordering looks correct.
 *  3. The cycle protocol. A scan that failed half way must abort, not
 *     commit: a partial commit drops hosts nothing had cleared.
 */
#define KPIDASH_TEST_STUBS 1
#include "protocol.h"
#include "registry.h"
#include "redis.h"

#include <stdio.h>
#include <string.h>

#ifdef KPIDASH_TEST_STUBS
void ui_show_redis_error(const char *msg) { (void)msg; }
void ui_hide_redis_error(void) {}
void status_redis_check_ack(void *ctx) { (void)ctx; }
void fortune_on_pushed(const char *json) { (void)json; }
bool screenshot_save(const char *path) { (void)path; return false; }
#endif

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

/* Drop every live entry so each test starts from a known-empty registry.
 * An empty commit clears the live set; the reap collects what it emptied. */
static void reset_registry(void) {
    stale_registry_begin_cycle();
    stale_registry_commit_cycle();
    void *cs[STALE_REGISTRY_MAX];
    stale_registry_reap(cs, STALE_REGISTRY_MAX);
    stale_entry_t snap[STALE_REGISTRY_MAX];
    if (stale_registry_snapshot(snap, STALE_REGISTRY_MAX) != 0)
        fprintf(stderr, "reset_registry: registry not empty\n");
}

static const stale_entry_t *find_host(const stale_entry_t *snap, int n, const char *host) {
    for (int i = 0; i < n; i++)
        if (strcmp(snap[i].host, host) == 0) return &snap[i];
    return NULL;
}

/* ---- 1. Key parsing ------------------------------------------------- */

static void test_key_split_contract_shape(void) {
    char host[64], dep[64];
    CHECK(redis_parse_stale_key("kdash:stale:komarchy:agent-skills", host, sizeof(host), dep,
                                sizeof(dep)) == 0);
    CHECK(strcmp(host, "komarchy") == 0);
    CHECK(strcmp(dep, "agent-skills") == 0);

    CHECK(redis_parse_stale_key("kdash:stale:kubs0:k-homelab", host, sizeof(host), dep,
                                sizeof(dep)) == 0);
    CHECK(strcmp(host, "kubs0") == 0);
    CHECK(strcmp(dep, "k-homelab") == 0);
}

/* An off-contract key still raises its host — the deployer name is what is
 * lost, not the signal. The publisher wrappers do not police segment count,
 * so these are reachable in production, not hypothetical. */
static void test_key_split_off_contract_still_raises_host(void) {
    char host[64], dep[64];

    /* Deployer segment omitted entirely. */
    CHECK(redis_parse_stale_key("kdash:stale:komarchy", host, sizeof(host), dep, sizeof(dep)) == 0);
    CHECK(strcmp(host, "komarchy") == 0);
    CHECK(strcmp(dep, STALE_DEPLOYER_UNKNOWN) == 0);

    /* Too many segments — host is still unambiguous, the deployer is not. */
    CHECK(redis_parse_stale_key("kdash:stale:komarchy:a:b", host, sizeof(host), dep,
                                sizeof(dep)) == 0);
    CHECK(strcmp(host, "komarchy") == 0);
    CHECK(strcmp(dep, STALE_DEPLOYER_UNKNOWN) == 0);
}

/* The only unrecoverable case: no host to attribute the flag to. */
static void test_key_split_rejects_hostless(void) {
    char host[64], dep[64];
    CHECK(redis_parse_stale_key("kdash:stale:", host, sizeof(host), dep, sizeof(dep)) == -1);
    CHECK(redis_parse_stale_key("kdash:stale::agent-skills", host, sizeof(host), dep,
                                sizeof(dep)) == -1);
    CHECK(redis_parse_stale_key("kpidash:services:foo:kai", host, sizeof(host), dep,
                                sizeof(dep)) == -1);
    CHECK(redis_parse_stale_key(NULL, host, sizeof(host), dep, sizeof(dep)) == -1);
}

/* ---- 2. Payload parsing --------------------------------------------- */

static void test_payload_valid(void) {
    double since = -1.0;
    /* The real payload, byte for byte, as read off rpi53 on 2026-09-05. */
    CHECK(redis_parse_stale_payload(
              "{\"reason\":\"audit skipped: unreachable\",\"since\":1788594159,"
              "\"stale\":true,\"ts\":1788594159}",
              &since) == 0);
    CHECK(since == 1788594159.0);
}

/* `since` is Unix seconds as a bare JSON number. An ISO-8601 string is the
 * trap slices 3 and 4 were each warned about: nothing upstream validates the
 * payload, so a writer that published one would reach Redis looking healthy
 * and break HERE. Reject it rather than coercing it to 0 or 2026. */
static void test_payload_rejects_iso_string_since(void) {
    double since = -1.0;
    CHECK(redis_parse_stale_payload("{\"stale\":true,\"since\":\"2026-09-05T07:42:39Z\"}",
                                    &since) == -1);
    /* Also a number-shaped string — still the wrong type. */
    CHECK(redis_parse_stale_payload("{\"stale\":true,\"since\":\"1788594159\"}", &since) == -1);
}

static void test_payload_rejects_unreadable(void) {
    double since = -1.0;
    CHECK(redis_parse_stale_payload("", &since) == -1);
    CHECK(redis_parse_stale_payload("not json at all", &since) == -1);
    CHECK(redis_parse_stale_payload("{\"stale\":true}", &since) == -1);   /* no since */
    CHECK(redis_parse_stale_payload("[1788594159]", &since) == -1);        /* not an object */
    CHECK(redis_parse_stale_payload(NULL, &since) == -1);
}

/* `stale` is pinned const:true by the schema, so a writer publishing
 * `stale:false` is off-contract — but publishing anything leaves the key up,
 * and the KEY's presence is the flag. The reader must therefore not consult
 * `stale` at all: it reads `since` and nothing else. */
static void test_payload_ignores_stale_field(void) {
    double since = -1.0;
    CHECK(redis_parse_stale_payload("{\"stale\":false,\"since\":1788594159}", &since) == 0);
    CHECK(since == 1788594159.0);
    since = -1.0;
    CHECK(redis_parse_stale_payload("{\"since\":1788594159}", &since) == 0);
    CHECK(since == 1788594159.0);
}

/* ---- 3. CD-18: an unreadable record must not clear the flag ---------- */

static void test_unreadable_payload_still_shows_host_stale(void) {
    reset_registry();
    stale_registry_begin_cycle();
    stale_registry_observe("komarchy", "k-homelab", 0.0, false); /* payload unreadable */
    stale_registry_commit_cycle();

    stale_entry_t snap[STALE_REGISTRY_MAX];
    int n = stale_registry_snapshot(snap, STALE_REGISTRY_MAX);
    CHECK(n == 1);
    const stale_entry_t *e = find_host(snap, n, "komarchy");
    CHECK(e != NULL);
    /* The host is raised and the deployer is still named — only `since` is
     * lost. Rendering nothing here would read as all-clear. */
    CHECK(e && e->deployer_count == 1);
    CHECK(e && strcmp(e->deployers[0].name, "k-homelab") == 0);
    CHECK(e && e->deployers[0].since_known == false);

    char title[128], body[256];
    stale_format_title(e, title, sizeof(title));
    stale_format_body(e, body, sizeof(body));
    CHECK(strcmp(title, "komarchy stale") == 0);
    CHECK(strcmp(body, "k-homelab") == 0);
}

/* The same rule at the level the poll actually works at. The registry test
 * above proves the registry can hold an unreadable record; these prove the
 * READER hands it one instead of dropping it on the floor, which is where the
 * bug would actually live. */
static void test_apply_record_raises_host_when_payload_unreadable(void) {
    const char *k = "kdash:stale:komarchy:k-homelab";
    const char *unreadable[] = {
        NULL,                                          /* GET failed / wrong type */
        "",                                            /* empty value */
        "not json at all",                             /* corrupt */
        "{\"stale\":true}",                            /* no since */
        "{\"stale\":true,\"since\":\"2026-09-05T07:42:39Z\"}", /* the ISO trap */
        "[1788594159]",                                /* not an object */
    };
    for (size_t i = 0; i < sizeof(unreadable) / sizeof(unreadable[0]); i++) {
        reset_registry();
        stale_registry_begin_cycle();
        CHECK(redis_stale_apply_record(k, unreadable[i]) == 0);
        stale_registry_commit_cycle();

        stale_entry_t snap[STALE_REGISTRY_MAX];
        int n = stale_registry_snapshot(snap, STALE_REGISTRY_MAX);
        /* The whole point: a card, not an empty row. */
        if (n != 1) {
            fprintf(stderr, "FAIL: payload #%zu cleared the flag\n", i);
            failed++;
            continue;
        }
        passed++;
        CHECK(strcmp(snap[0].host, "komarchy") == 0);
        CHECK(snap[0].deployer_count == 1);
        CHECK(strcmp(snap[0].deployers[0].name, "k-homelab") == 0);
        CHECK(snap[0].deployers[0].since_known == false);
    }
}

static void test_apply_record_keeps_since_when_payload_is_good(void) {
    reset_registry();
    stale_registry_begin_cycle();
    CHECK(redis_stale_apply_record("kdash:stale:komarchy:k-homelab",
                                   "{\"reason\":\"audit skipped: unreachable\","
                                   "\"since\":1788594159,\"stale\":true,\"ts\":1788594159}") == 0);
    CHECK(redis_stale_apply_record("kdash:stale:komarchy:agent-skills",
                                   "{\"reason\":\"fleet-deploy skipped: unreachable\","
                                   "\"since\":1788594170,\"stale\":true,\"ts\":1788594170}") == 0);
    stale_registry_commit_cycle();

    stale_entry_t snap[STALE_REGISTRY_MAX];
    int n = stale_registry_snapshot(snap, STALE_REGISTRY_MAX);
    CHECK(n == 1);
    const stale_entry_t *e = find_host(snap, n, "komarchy");
    CHECK(e && e->deployer_count == 2);
    CHECK(e && e->deployers[0].since_known && e->deployers[0].since == 1788594159.0);
    CHECK(e && e->deployers[1].since_known && e->deployers[1].since == 1788594170.0);
    char body[256];
    stale_format_body(e, body, sizeof(body));
    CHECK(strcmp(body, "k-homelab\nagent-skills") == 0);
}

static void test_apply_record_ignores_hostless_and_excluded(void) {
    reset_registry();
    stale_registry_begin_cycle();
    CHECK(redis_stale_apply_record("kdash:stale:", "{\"since\":1}") == -1);
    CHECK(redis_stale_apply_record("kpidash:services:foo:kai", "{\"since\":1}") == -1);
    /* rpi53 parses fine as a key — it is dropped for being the panel itself. */
    CHECK(redis_stale_apply_record("kdash:stale:" STALE_EXCLUDED_HOST ":k-homelab",
                                   "{\"since\":1}") == 0);
    stale_registry_commit_cycle();
    stale_entry_t snap[STALE_REGISTRY_MAX];
    CHECK(stale_registry_snapshot(snap, STALE_REGISTRY_MAX) == 0);
}

/* ---- 4. The sort: oldest `since` first ------------------------------- */

/* The live values from rpi53 on 2026-09-05: k-homelab flagged at 1788594159,
 * agent-skills 11 seconds later at 1788594170. Observed in the OPPOSITE order
 * to the one expected out, so a reader that just echoed scan order would fail. */
static void test_deployers_sorted_oldest_since_first(void) {
    reset_registry();
    stale_registry_begin_cycle();
    stale_registry_observe("komarchy", "agent-skills", 1788594170.0, true);
    stale_registry_observe("komarchy", "k-homelab", 1788594159.0, true);
    stale_registry_commit_cycle();

    stale_entry_t snap[STALE_REGISTRY_MAX];
    int n = stale_registry_snapshot(snap, STALE_REGISTRY_MAX);
    const stale_entry_t *e = find_host(snap, n, "komarchy");
    CHECK(e != NULL);
    CHECK(e && e->deployer_count == 2);
    CHECK(e && strcmp(e->deployers[0].name, "k-homelab") == 0);
    CHECK(e && strcmp(e->deployers[1].name, "agent-skills") == 0);

    char body[256];
    stale_format_body(e, body, sizeof(body));
    CHECK(strcmp(body, "k-homelab\nagent-skills") == 0);
}

/* Reversing the observation order must not reverse the output. */
static void test_sort_is_by_since_not_observation_order(void) {
    reset_registry();
    stale_registry_begin_cycle();
    stale_registry_observe("komarchy", "k-homelab", 1788594159.0, true);
    stale_registry_observe("komarchy", "agent-skills", 1788594170.0, true);
    stale_registry_commit_cycle();

    stale_entry_t snap[STALE_REGISTRY_MAX];
    int n = stale_registry_snapshot(snap, STALE_REGISTRY_MAX);
    const stale_entry_t *e = find_host(snap, n, "komarchy");
    CHECK(e && strcmp(e->deployers[0].name, "k-homelab") == 0);
    CHECK(e && strcmp(e->deployers[1].name, "agent-skills") == 0);
}

/* A deployer whose `since` could not be read carries no ordering
 * information, so it sorts after every known one rather than claiming to be
 * the oldest. Ties break by name so the row never flickers between polls. */
static void test_unknown_since_sorts_last_and_ties_break_by_name(void) {
    reset_registry();
    stale_registry_begin_cycle();
    stale_registry_observe("kubs0", "zeta", 0.0, false);
    stale_registry_observe("kubs0", "beta", 500.0, true);
    stale_registry_observe("kubs0", "alpha", 500.0, true);
    stale_registry_observe("kubs0", "aardvark", 0.0, false);
    stale_registry_commit_cycle();

    stale_entry_t snap[STALE_REGISTRY_MAX];
    int n = stale_registry_snapshot(snap, STALE_REGISTRY_MAX);
    const stale_entry_t *e = find_host(snap, n, "kubs0");
    CHECK(e && e->deployer_count == 4);
    char body[256];
    stale_format_body(e, body, sizeof(body));
    CHECK(strcmp(body, "alpha\nbeta\naardvark\nzeta") == 0);
}

/* ---- 5. Presence-owned: absence is the only all-clear ---------------- */

static void test_host_gone_from_next_cycle_is_reaped(void) {
    reset_registry();
    stale_registry_begin_cycle();
    stale_registry_observe("komarchy", "k-homelab", 100.0, true);
    stale_registry_commit_cycle();
    stale_entry_t snap[STALE_REGISTRY_MAX];
    CHECK(stale_registry_snapshot(snap, STALE_REGISTRY_MAX) == 1);

    /* Next cycle sees nothing — the deployer cleared its key. */
    stale_registry_begin_cycle();
    stale_registry_commit_cycle();
    int n = stale_registry_snapshot(snap, STALE_REGISTRY_MAX);
    CHECK(n == 1);                       /* still present, awaiting reap */
    CHECK(snap[0].deployer_count == 0);  /* but with nothing to show */

    void *cs[STALE_REGISTRY_MAX];
    CHECK(stale_registry_reap(cs, STALE_REGISTRY_MAX) == 1);
    CHECK(stale_registry_snapshot(snap, STALE_REGISTRY_MAX) == 0);
}

/* One deployer clearing while another stays must shrink the body, not the
 * card: the host is still stale for the remaining deployer. */
static void test_one_deployer_clearing_leaves_the_card_up(void) {
    reset_registry();
    stale_registry_begin_cycle();
    stale_registry_observe("komarchy", "k-homelab", 100.0, true);
    stale_registry_observe("komarchy", "agent-skills", 111.0, true);
    stale_registry_commit_cycle();

    stale_registry_begin_cycle();
    stale_registry_observe("komarchy", "agent-skills", 111.0, true);
    stale_registry_commit_cycle();

    stale_entry_t snap[STALE_REGISTRY_MAX];
    int n = stale_registry_snapshot(snap, STALE_REGISTRY_MAX);
    CHECK(n == 1);
    const stale_entry_t *e = find_host(snap, n, "komarchy");
    CHECK(e && e->deployer_count == 1);
    char body[256];
    stale_format_body(e, body, sizeof(body));
    CHECK(strcmp(body, "agent-skills") == 0);

    void *cs[STALE_REGISTRY_MAX];
    CHECK(stale_registry_reap(cs, STALE_REGISTRY_MAX) == 0); /* nothing to reap */
}

/* THE one that matters most. A scan that dies half way must leave the live
 * set alone. Committing a partial scan would take down cards that nothing
 * had cleared, and a missing card reads as "this host is fine". */
static void test_aborted_cycle_keeps_the_previous_state(void) {
    reset_registry();
    stale_registry_begin_cycle();
    stale_registry_observe("komarchy", "k-homelab", 100.0, true);
    stale_registry_observe("komarchy", "agent-skills", 111.0, true);
    stale_registry_commit_cycle();

    /* A cycle that saw one key and then hit a Redis error. */
    stale_registry_begin_cycle();
    stale_registry_observe("komarchy", "k-homelab", 100.0, true);
    stale_registry_abort_cycle();

    stale_entry_t snap[STALE_REGISTRY_MAX];
    int n = stale_registry_snapshot(snap, STALE_REGISTRY_MAX);
    CHECK(n == 1);
    const stale_entry_t *e = find_host(snap, n, "komarchy");
    CHECK(e && e->deployer_count == 2); /* both, not the one the partial scan saw */
    void *cs[STALE_REGISTRY_MAX];
    CHECK(stale_registry_reap(cs, STALE_REGISTRY_MAX) == 0);
}

/* An aborted cycle that saw NOTHING must also change nothing — this is the
 * shape a connection failure takes, and it is the one that would blank the
 * whole row. */
static void test_aborted_empty_cycle_keeps_the_previous_state(void) {
    reset_registry();
    stale_registry_begin_cycle();
    stale_registry_observe("komarchy", "k-homelab", 100.0, true);
    stale_registry_commit_cycle();

    stale_registry_begin_cycle();
    stale_registry_abort_cycle();

    stale_entry_t snap[STALE_REGISTRY_MAX];
    CHECK(stale_registry_snapshot(snap, STALE_REGISTRY_MAX) == 1);
    CHECK(snap[0].deployer_count == 1);
}

/* ---- 6. Host rules --------------------------------------------------- */

/* rpi53 is the panel's own host: if it is behind there is no dashboard. */
static void test_dashboard_host_is_excluded(void) {
    reset_registry();
    stale_registry_begin_cycle();
    stale_registry_observe(STALE_EXCLUDED_HOST, "k-homelab", 100.0, true);
    stale_registry_observe("komarchy", "k-homelab", 100.0, true);
    stale_registry_commit_cycle();

    stale_entry_t snap[STALE_REGISTRY_MAX];
    int n = stale_registry_snapshot(snap, STALE_REGISTRY_MAX);
    CHECK(n == 1);
    CHECK(find_host(snap, n, STALE_EXCLUDED_HOST) == NULL);
    CHECK(find_host(snap, n, "komarchy") != NULL);
}

static void test_hosts_sorted_by_name(void) {
    reset_registry();
    stale_registry_begin_cycle();
    stale_registry_observe("kubs0", "k-homelab", 100.0, true);
    stale_registry_observe("komarchy", "k-homelab", 100.0, true);
    stale_registry_observe("cleo", "k-homelab", 100.0, true);
    stale_registry_commit_cycle();

    stale_entry_t snap[STALE_REGISTRY_MAX];
    int n = stale_registry_snapshot(snap, STALE_REGISTRY_MAX);
    CHECK(n == 3);
    CHECK(strcmp(snap[0].host, "cleo") == 0);
    CHECK(strcmp(snap[1].host, "komarchy") == 0);
    CHECK(strcmp(snap[2].host, "kubs0") == 0);
}

/* The same key seen twice in one cycle (SCAN can repeat keys across cursor
 * pages) must not list the deployer twice. */
static void test_duplicate_observation_is_idempotent(void) {
    reset_registry();
    stale_registry_begin_cycle();
    stale_registry_observe("komarchy", "k-homelab", 100.0, true);
    stale_registry_observe("komarchy", "k-homelab", 100.0, true);
    stale_registry_commit_cycle();

    stale_entry_t snap[STALE_REGISTRY_MAX];
    int n = stale_registry_snapshot(snap, STALE_REGISTRY_MAX);
    CHECK(n == 1);
    CHECK(snap[0].deployer_count == 1);
}

static void test_observe_rejects_empty_host(void) {
    reset_registry();
    stale_registry_begin_cycle();
    stale_registry_observe(NULL, "k-homelab", 100.0, true);
    stale_registry_observe("", "k-homelab", 100.0, true);
    stale_registry_commit_cycle();
    stale_entry_t snap[STALE_REGISTRY_MAX];
    CHECK(stale_registry_snapshot(snap, STALE_REGISTRY_MAX) == 0);
}

/* ---- 7. Formatting --------------------------------------------------- */

static void test_format_title_and_body(void) {
    reset_registry();
    stale_registry_begin_cycle();
    stale_registry_observe("komarchy", "k-homelab", 1788594159.0, true);
    stale_registry_observe("komarchy", "agent-skills", 1788594170.0, true);
    stale_registry_commit_cycle();

    stale_entry_t snap[STALE_REGISTRY_MAX];
    int n = stale_registry_snapshot(snap, STALE_REGISTRY_MAX);
    const stale_entry_t *e = find_host(snap, n, "komarchy");
    char title[128], body[256];
    stale_format_title(e, title, sizeof(title));
    stale_format_body(e, body, sizeof(body));
    CHECK(strcmp(title, "komarchy stale") == 0);
    CHECK(strcmp(body, "k-homelab\nagent-skills") == 0);

    /* NULL entry must not write past the buffer or leave it uninitialised. */
    char t2[128] = "dirty", b2[128] = "dirty";
    stale_format_title(NULL, t2, sizeof(t2));
    stale_format_body(NULL, b2, sizeof(b2));
    CHECK(t2[0] == '\0');
    CHECK(b2[0] == '\0');
}

/* ---- 8. Capacity ----------------------------------------------------- */

static void test_capacity_limits_do_not_overflow(void) {
    reset_registry();
    stale_registry_begin_cycle();
    char host[64], dep[64];
    for (int i = 0; i < STALE_REGISTRY_MAX + 4; i++) {
        snprintf(host, sizeof(host), "host%02d", i);
        stale_registry_observe(host, "k-homelab", 100.0, true);
    }
    for (int i = 0; i < STALE_DEPLOYERS_MAX + 4; i++) {
        snprintf(dep, sizeof(dep), "dep%02d", i);
        stale_registry_observe("host00", dep, 100.0 + i, true);
    }
    stale_registry_commit_cycle();

    stale_entry_t snap[STALE_REGISTRY_MAX];
    int n = stale_registry_snapshot(snap, STALE_REGISTRY_MAX);
    CHECK(n == STALE_REGISTRY_MAX);
    const stale_entry_t *e = find_host(snap, n, "host00");
    CHECK(e && e->deployer_count == STALE_DEPLOYERS_MAX);
}

int main(void) {
    test_key_split_contract_shape();
    test_key_split_off_contract_still_raises_host();
    test_key_split_rejects_hostless();
    test_payload_valid();
    test_payload_rejects_iso_string_since();
    test_payload_rejects_unreadable();
    test_payload_ignores_stale_field();
    test_unreadable_payload_still_shows_host_stale();
    test_apply_record_raises_host_when_payload_unreadable();
    test_apply_record_keeps_since_when_payload_is_good();
    test_apply_record_ignores_hostless_and_excluded();
    test_deployers_sorted_oldest_since_first();
    test_sort_is_by_since_not_observation_order();
    test_unknown_since_sorts_last_and_ties_break_by_name();
    test_host_gone_from_next_cycle_is_reaped();
    test_one_deployer_clearing_leaves_the_card_up();
    test_aborted_cycle_keeps_the_previous_state();
    test_aborted_empty_cycle_keeps_the_previous_state();
    test_dashboard_host_is_excluded();
    test_hosts_sorted_by_name();
    test_duplicate_observation_is_idempotent();
    test_observe_rejects_empty_host();
    test_format_title_and_body();
    test_capacity_limits_do_not_overflow();

    fprintf(stderr, "test_stale_card: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
