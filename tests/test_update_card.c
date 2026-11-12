/* test_update_card.c — update-issue feed reader (WI #3642).
 *
 * The staleness card's rules re-applied to kdash:update (CD-18): the key is
 * the flag, an unreadable payload costs the detail and never the flag, and
 * only a completed scan may take a card down. Plus the two things this card
 * adds: the panel's own host is shown, and the body is the `issue` line —
 * for any kind, known or not.
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

/* The schema's own examples, byte for byte. */
#define EXAMPLE_REBOOT                                                                             \
    "{\"since\":1793581200.0,\"ts\":1793667600.4,"                                                 \
    "\"issue\":\"rpi53: reboot needed for linux-image 6.12.48 (apt run 2026-11-02)\","             \
    "\"ref\":\"korg:3627\"}"
#define EXAMPLE_RING                                                                               \
    "{\"since\":1793581200.0,\"ts\":1793581200.0,"                                                 \
    "\"issue\":\"kai: apt ring stopped \xe2\x80\x94 gate failed on kubs0\"}"

static void reset_registry(void) {
    update_registry_begin_cycle();
    update_registry_commit_cycle();
    void *cs[UPDATE_REGISTRY_MAX];
    update_registry_reap(cs, UPDATE_REGISTRY_MAX);
    update_entry_t snap[UPDATE_REGISTRY_MAX];
    if (update_registry_snapshot(snap, UPDATE_REGISTRY_MAX) != 0)
        fprintf(stderr, "reset_registry: registry not empty\n");
}

static const update_entry_t *find_host(const update_entry_t *snap, int n, const char *host) {
    for (int i = 0; i < n; i++)
        if (strcmp(snap[i].host, host) == 0) return &snap[i];
    return NULL;
}

static void test_key_split(void) {
    char host[64], kind[64];
    CHECK(redis_parse_update_key("kdash:update:rpi53:reboot-awaiting", host, sizeof(host), kind,
                                 sizeof(kind)) == 0);
    CHECK(strcmp(host, "rpi53") == 0);
    CHECK(strcmp(kind, "reboot-awaiting") == 0);

    /* Off-contract shapes still raise the host. */
    CHECK(redis_parse_update_key("kdash:update:kai", host, sizeof(host), kind, sizeof(kind)) == 0);
    CHECK(strcmp(host, "kai") == 0);
    CHECK(strcmp(kind, UPDATE_KIND_UNKNOWN) == 0);
    CHECK(redis_parse_update_key("kdash:update:kai:a:b", host, sizeof(host), kind,
                                 sizeof(kind)) == 0);
    CHECK(strcmp(kind, UPDATE_KIND_UNKNOWN) == 0);

    /* Nothing to attribute the flag to. */
    CHECK(redis_parse_update_key("kdash:update:", host, sizeof(host), kind, sizeof(kind)) == -1);
    CHECK(redis_parse_update_key("kdash:update::held", host, sizeof(host), kind, sizeof(kind)) ==
          -1);
    CHECK(redis_parse_update_key("kdash:stale:kai:x", host, sizeof(host), kind, sizeof(kind)) ==
          -1);
}

static void test_payload_examples(void) {
    double since = 0;
    bool known = false;
    char issue[UPDATE_ISSUE_LEN];
    CHECK(redis_parse_update_payload(EXAMPLE_REBOOT, &since, &known, issue, sizeof(issue)) == 0);
    CHECK(known && since == 1793581200.0);
    CHECK(strcmp(issue, "rpi53: reboot needed for linux-image 6.12.48 (apt run 2026-11-02)") == 0);
    CHECK(redis_parse_update_payload(EXAMPLE_RING, &since, &known, issue, sizeof(issue)) == 0);
    CHECK(strncmp(issue, "kai: apt ring stopped", 21) == 0);
}

/* Each field stands alone: an ISO `since` (the WI 1926 slip, a schema
 * counterexample) loses the order and keeps the line. */
static void test_payload_fields_are_independent(void) {
    double since = 0;
    bool known = true;
    char issue[UPDATE_ISSUE_LEN];
    CHECK(redis_parse_update_payload("{\"since\":\"2026-11-02T04:00:00Z\",\"issue\":\"reboot "
                                     "needed\"}",
                                     &since, &known, issue, sizeof(issue)) == 0);
    CHECK(!known);
    CHECK(strcmp(issue, "reboot needed") == 0);

    CHECK(redis_parse_update_payload("{\"since\":5,\"issue\":\"\"}", &since, &known, issue,
                                     sizeof(issue)) == 0);
    CHECK(known && issue[0] == '\0');

    CHECK(redis_parse_update_payload("garbage", &since, &known, issue, sizeof(issue)) == -1);
    CHECK(!known && issue[0] == '\0');
    CHECK(redis_parse_update_payload("[1]", &since, &known, issue, sizeof(issue)) == -1);
}

/* CD-18: an unreadable payload still shows the host, "detail unknown". */
static void test_unreadable_payload_still_raises_host(void) {
    reset_registry();
    update_registry_begin_cycle();
    CHECK(redis_update_apply_record("kdash:update:kubs0:held", "not json") == 0);
    CHECK(redis_update_apply_record("kdash:update:kubsdb:held", NULL) == 0);
    update_registry_commit_cycle();

    update_entry_t snap[UPDATE_REGISTRY_MAX];
    int n = update_registry_snapshot(snap, UPDATE_REGISTRY_MAX);
    CHECK(n == 2);
    const update_entry_t *e = find_host(snap, n, "kubs0");
    CHECK(e && e->issue_count == 1 && !e->issues[0].issue_known);
    char body[512];
    update_format_body(e, body, sizeof(body));
    CHECK(strcmp(body, "held: detail unknown") == 0);
    CHECK(find_host(snap, n, "kubsdb") != NULL);
}

/* rpi53 is the point of the card, not an exclusion. */
static void test_panel_host_is_shown(void) {
    reset_registry();
    update_registry_begin_cycle();
    CHECK(redis_update_apply_record("kdash:update:rpi53:reboot-awaiting", EXAMPLE_REBOOT) == 0);
    update_registry_commit_cycle();
    update_entry_t snap[UPDATE_REGISTRY_MAX];
    int n = update_registry_snapshot(snap, UPDATE_REGISTRY_MAX);
    CHECK(n == 1 && strcmp(snap[0].host, "rpi53") == 0);
    char buf[512];
    update_format_title(&snap[0], buf, sizeof(buf));
    CHECK(strcmp(buf, "rpi53 update") == 0);
}

/* An unknown kind renders by its issue line, never dropped. */
static void test_unknown_kind_is_rendered(void) {
    reset_registry();
    update_registry_begin_cycle();
    redis_update_apply_record("kdash:update:kai:disk-full-someday",
                              "{\"since\":1,\"ts\":2,\"issue\":\"kai: something new\"}");
    update_registry_commit_cycle();
    update_entry_t snap[UPDATE_REGISTRY_MAX];
    int n = update_registry_snapshot(snap, UPDATE_REGISTRY_MAX);
    CHECK(n == 1);
    char body[512];
    update_format_body(&snap[0], body, sizeof(body));
    CHECK(strcmp(body, "kai: something new") == 0);
}

/* Oldest `since` first; unknown last; ties by kind. Hosts by name. */
static void test_ordering(void) {
    reset_registry();
    update_registry_begin_cycle();
    update_registry_observe("kai", "reboot-awaiting", 300, true, "B newer");
    update_registry_observe("kai", "zzz", 0, false, "D unknown since");
    update_registry_observe("kai", "held", 100, true, "A oldest");
    update_registry_observe("kai", "gate-failed", 300, true, "C tie");
    update_registry_observe("cleo", "held", 5, true, "cleo line");
    update_registry_commit_cycle();

    update_entry_t snap[UPDATE_REGISTRY_MAX];
    int n = update_registry_snapshot(snap, UPDATE_REGISTRY_MAX);
    CHECK(n == 2);
    CHECK(strcmp(snap[0].host, "cleo") == 0 && strcmp(snap[1].host, "kai") == 0);
    char body[1024];
    update_format_body(&snap[1], body, sizeof(body));
    CHECK(strcmp(body, "A oldest\nC tie\nB newer\nD unknown since") == 0);
}

/* A kind DELeted clears only itself; the last one clearing reaps the card. */
static void test_clearing(void) {
    reset_registry();
    update_registry_begin_cycle();
    update_registry_observe("kai", "held", 1, true, "held");
    update_registry_observe("kai", "reboot-awaiting", 2, true, "reboot");
    update_registry_commit_cycle();

    update_registry_begin_cycle();
    update_registry_observe("kai", "reboot-awaiting", 2, true, "reboot");
    update_registry_commit_cycle();
    update_entry_t snap[UPDATE_REGISTRY_MAX];
    CHECK(update_registry_snapshot(snap, UPDATE_REGISTRY_MAX) == 1);
    CHECK(snap[0].issue_count == 1);

    update_registry_begin_cycle();
    update_registry_commit_cycle();
    void *dead[UPDATE_REGISTRY_MAX];
    CHECK(update_registry_reap(dead, UPDATE_REGISTRY_MAX) == 1);
    CHECK(update_registry_snapshot(snap, UPDATE_REGISTRY_MAX) == 0);
}

/* A scan that failed part-way leaves the previous cards standing. */
static void test_abort_keeps_previous(void) {
    reset_registry();
    update_registry_begin_cycle();
    update_registry_observe("kai", "held", 1, true, "held");
    update_registry_commit_cycle();

    update_registry_begin_cycle();
    update_registry_abort_cycle();
    update_entry_t snap[UPDATE_REGISTRY_MAX];
    CHECK(update_registry_snapshot(snap, UPDATE_REGISTRY_MAX) == 1);
    void *dead[UPDATE_REGISTRY_MAX];
    CHECK(update_registry_reap(dead, UPDATE_REGISTRY_MAX) == 0);
}

static void test_duplicates_and_capacity(void) {
    reset_registry();
    update_registry_begin_cycle();
    update_registry_observe("kai", "held", 1, true, "held");
    update_registry_observe("kai", "held", 1, true, "held");
    for (int i = 0; i < UPDATE_KINDS_MAX + 3; i++) {
        char k[16];
        snprintf(k, sizeof(k), "k%d", i);
        update_registry_observe("kai", k, 1, true, "x");
    }
    for (int i = 0; i < UPDATE_REGISTRY_MAX + 3; i++) {
        char h[16];
        snprintf(h, sizeof(h), "h%02d", i);
        update_registry_observe(h, "held", 1, true, "x");
    }
    update_registry_observe("", "held", 1, true, "x");
    update_registry_commit_cycle();
    update_entry_t snap[UPDATE_REGISTRY_MAX];
    int n = update_registry_snapshot(snap, UPDATE_REGISTRY_MAX);
    CHECK(n == UPDATE_REGISTRY_MAX);
    const update_entry_t *e = find_host(snap, n, "kai");
    CHECK(e && e->issue_count == UPDATE_KINDS_MAX);

    /* A 200-character line (the schema's max) fits whole. */
    char line[UPDATE_ISSUE_LEN];
    memset(line, 'x', UPDATE_ISSUE_LEN - 1);
    line[UPDATE_ISSUE_LEN - 1] = '\0';
    reset_registry();
    update_registry_begin_cycle();
    update_registry_observe("kai", "held", 1, true, line);
    update_registry_commit_cycle();
    n = update_registry_snapshot(snap, UPDATE_REGISTRY_MAX);
    CHECK(n == 1 && strlen(snap[0].issues[0].issue) == 200);
}

int main(void) {
    test_key_split();
    test_payload_examples();
    test_payload_fields_are_independent();
    test_unreadable_payload_still_raises_host();
    test_panel_host_is_shown();
    test_unknown_kind_is_rendered();
    test_ordering();
    test_clearing();
    test_abort_keeps_previous();
    test_duplicates_and_capacity();
    fprintf(stderr, "test_update_card: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
