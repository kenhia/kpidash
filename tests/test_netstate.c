/* test_netstate.c — the network card's model (WI #4062).
 *
 * The parser is held to the contract itself: every `examples` entry in
 * kdashdata's kdash-net.schema.json must parse, and every
 * `x-counterexamples` record must be distrusted. The schema file is read at
 * run time from the submodule, so a contract change that this reader does
 * not follow fails here rather than on the panel.
 *
 * Then the states the design record (korg:4059) names, each of which must
 * look distinct at a glance: healthy, a host on 1G, a flapping host, uplink
 * lost, WAN down, a stale publisher — plus "no feed", which must NOT read as
 * a fault, and stale, which must never read as healthy.
 */
#include "netstate.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
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

#define NOW 1793062800.0

/* ---- The contract's own examples ---- */

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    if (buf)
        buf[n] = '\0';
    fclose(f);
    return buf;
}

static void test_schema_examples(void) {
    char *text = slurp(KDASH_NET_SCHEMA);
    CHECK(text != NULL); /* the submodule is part of the gate: fail, never skip */
    if (!text)
        return;
    cJSON *schema = cJSON_Parse(text);
    free(text);
    CHECK(schema != NULL);
    if (!schema)
        return;

    const cJSON *ex;
    int n = 0;
    cJSON_ArrayForEach(ex, cJSON_GetObjectItem(schema, "examples")) {
        char *js = cJSON_PrintUnformatted(ex);
        net_record_t r;
        CHECK(net_parse_json(js, &r) == 0);
        free(js);
        n++;
    }
    CHECK(n >= 5);

    const cJSON *cx;
    int m = 0;
    cJSON_ArrayForEach(cx, cJSON_GetObjectItem(schema, "x-counterexamples")) {
        char *js = cJSON_PrintUnformatted(cJSON_GetObjectItem(cx, "record"));
        net_record_t r;
        int rc = net_parse_json(js, &r);
        if (rc == 0)
            fprintf(stderr, "counterexample parsed: %s\n",
                    cJSON_GetStringValue(cJSON_GetObjectItem(cx, "why")));
        CHECK(rc != 0);
        free(js);
        m++;
    }
    CHECK(m >= 6);
    cJSON_Delete(schema);
}

/* ---- Fixtures ---- */

static char g_buf[1024];

static const char *rec(const char *leg, int bond, int lfc, int lfc_d, int rx_d, int udm, int peers,
                       int wan, const char *drops, double ts) {
    char lfcs[32], lfcd[32], speed[16];
    if (bond) {
        snprintf(lfcs, sizeof(lfcs), "%d", lfc);
        snprintf(lfcd, sizeof(lfcd), "%d", lfc_d);
    } else {
        strcpy(lfcs, "null");
        strcpy(lfcd, "null");
    }
    snprintf(speed, sizeof(speed), "%s",
             strcmp(leg, "10g") == 0  ? "10000"
             : strcmp(leg, "5g") == 0 ? "5000"
             : strcmp(leg, "1g") == 0 ? "1000"
                                      : "null");
    snprintf(g_buf, sizeof(g_buf),
             "{\"leg\":\"%s\",\"speed_mbps\":%s,\"bond\":%s,\"lfc\":%s,\"lfc_delta_24h\":%s,"
             "\"rx_errors\":0,\"rx_errors_delta_24h\":%d,"
             "\"reach\":{\"udm\":%s,\"peers\":%s,\"wan\":%s},"
             "\"rtt_ms\":{\"udm\":%s,\"wan\":%s},\"bytes_per_s\":1250000,"
             "\"drops\":%s,\"ts\":%.1f}",
             leg, speed, bond ? "true" : "false", lfcs, lfcd, rx_d, udm ? "true" : "false",
             peers ? "true" : "false", wan ? "true" : "false", udm ? "0.4" : "null",
             wan ? "12.0" : "null", drops ? drops : "[]", ts);
    return g_buf;
}

static const char HOSTS[][HOSTNAME_LEN] = {"kubs0", "kubsdb", "kai", "gratch", "cleo"};

static void all_healthy(double now) {
    net_set_hosts(HOSTS, 5);
    for (int i = 0; i < 4; i++)
        net_observe(HOSTS[i], rec("10g", 1, 1, 0, 0, 1, 1, 1, NULL, now - 2), now);
    net_observe("cleo", rec("5g", 0, 0, 0, 0, 1, 1, 1, NULL, now - 2), now);
}

static const net_host_t *host(const char *name) {
    for (int i = 0; i < net_host_count(); i++)
        if (strcmp(net_host_at(i)->host, name) == 0)
            return net_host_at(i);
    return NULL;
}

/* ---- States ---- */

static void test_healthy(void) {
    all_healthy(NOW);
    net_summary_t s;
    net_derive(NOW, &s);
    CHECK(s.level == NET_LEVEL_OK);
    CHECK(strcmp(s.verdict, "ALL 10G") == 0);
    CHECK(strcmp(s.why, "4 bonds on 10G · cleo 5G · WAN 12 ms") == 0);
    CHECK(s.udm_up && s.wan_up && s.uplink_up);
    CHECK(s.udm_ms > 0.39 && s.udm_ms < 0.41);
    for (int i = 0; i < 5; i++)
        CHECK(net_column_attn(net_host_at(i)) == NET_ATTN_NONE);
    char sub[64];
    bool bold;
    net_subtitle(host("kai"), NOW, sub, sizeof(sub), &bold);
    CHECK(strcmp(sub, "bond primary") == 0 && !bold);
    net_subtitle(host("cleo"), NOW, sub, sizeof(sub), &bold);
    CHECK(strcmp(sub, "single NIC") == 0);
}

static void test_host_on_1g(void) {
    all_healthy(NOW);
    net_observe("kai", rec("1g", 1, 2, 1, 0, 1, 1, 1, "[1793062320.0]", NOW - 1), NOW);
    net_summary_t s;
    net_derive(NOW, &s);
    CHECK(s.level == NET_LEVEL_WARN);
    CHECK(strcmp(s.verdict, "kai ON 1G") == 0);
    CHECK(net_column_attn(host("kai")) == NET_ATTN_WARN);
    CHECK(!net_is_flapping(host("kai"))); /* on the backup, not flapping */
    char sub[64];
    bool bold;
    net_subtitle(host("kai"), NOW, sub, sizeof(sub), &bold);
    CHECK(strcmp(sub, "10G down · on 1G backup") == 0 && bold);
}

static void test_flapping(void) {
    all_healthy(NOW);
    net_observe("kubs0",
                rec("10g", 1, 28, 3, 1240, 1, 1, 1, "[1793061000.0,1793061900.5,1793062500.1]",
                    NOW - 1),
                NOW);
    net_summary_t s;
    net_derive(NOW, &s);
    CHECK(s.level == NET_LEVEL_WARN);
    CHECK(strcmp(s.verdict, "kubs0 FLAPPING") == 0);
    CHECK(strcmp(s.why, "3 link drops in 60 min · rx errors +1240 · still on 10G") == 0);
    CHECK(net_is_flapping(host("kubs0")));
    char sub[64];
    bool bold;
    net_subtitle(host("kubs0"), NOW, sub, sizeof(sub), &bold);
    CHECK(strcmp(sub, "FLAPPING · 3 drops") == 0);
}

static void test_uplink_lost(void) {
    net_set_hosts(HOSTS, 5);
    for (int i = 0; i < 4; i++)
        net_observe(HOSTS[i], rec("10g", 1, 1, 0, 0, 0, 1, 0, NULL, NOW - 1), NOW);
    net_observe("cleo", rec("down", 0, 0, 0, 0, 0, 0, 0, NULL, NOW - 1), NOW);
    net_summary_t s;
    net_derive(NOW, &s);
    CHECK(s.level == NET_LEVEL_CRIT);
    CHECK(strcmp(s.verdict, "UPLINK LOST") == 0);
    CHECK(!s.uplink_up && !s.udm_up && !s.wan_up);
    CHECK(s.udm_ms < 0 && s.wan_ms < 0);
}

static void test_wan_down(void) {
    net_set_hosts(HOSTS, 5);
    for (int i = 0; i < 4; i++)
        net_observe(HOSTS[i], rec("10g", 1, 1, 0, 0, 1, 1, 0, NULL, NOW - 1), NOW);
    net_observe("cleo", rec("5g", 0, 0, 0, 0, 1, 1, 0, NULL, NOW - 1), NOW);
    net_summary_t s;
    net_derive(NOW, &s);
    CHECK(s.level == NET_LEVEL_CRIT);
    CHECK(strcmp(s.verdict, "WAN DOWN") == 0);
    CHECK(s.uplink_up && s.udm_up && !s.wan_up);
}

static void test_stale_is_never_healthy(void) {
    all_healthy(NOW - 94);
    /* gratch's key expires; everyone else keeps publishing. */
    for (int i = 0; i < 5; i++)
        if (strcmp(HOSTS[i], "gratch") != 0)
            net_observe(HOSTS[i],
                        strcmp(HOSTS[i], "cleo") == 0
                            ? rec("5g", 0, 0, 0, 0, 1, 1, 1, NULL, NOW - 1)
                            : rec("10g", 1, 1, 0, 0, 1, 1, 1, NULL, NOW - 1),
                        NOW);
    net_observe("gratch", NULL, NOW);
    const net_host_t *g = host("gratch");
    CHECK(g->state == NET_COL_STALE);
    CHECK(net_column_attn(g) == NET_ATTN_CRIT);
    net_summary_t s;
    net_derive(NOW, &s);
    CHECK(s.level == NET_LEVEL_WARN);
    CHECK(strcmp(s.verdict, "gratch STALE") == 0);
    CHECK(strcmp(s.why, "no feed for 96 s") == 0);
    char sub[64];
    bool bold;
    net_subtitle(g, NOW, sub, sizeof(sub), &bold);
    CHECK(strcmp(sub, "STALE · 96 s") == 0);
    /* An untrustworthy payload is absence, not a fresh record. */
    net_observe("kai", "{\"leg\":\"1G\"}", NOW);
    CHECK(host("kai")->state == NET_COL_STALE);
}

static void test_no_feed_is_not_a_fault(void) {
    net_set_hosts(HOSTS, 5);
    for (int i = 0; i < 4; i++)
        net_observe(HOSTS[i], rec("10g", 1, 1, 0, 0, 1, 1, 1, NULL, NOW - 1), NOW);
    net_observe("cleo", NULL, NOW); /* slice 3.5 has not shipped */
    CHECK(host("cleo")->state == NET_COL_NOFEED);
    CHECK(net_column_attn(host("cleo")) == NET_ATTN_NONE);
    net_summary_t s;
    net_derive(NOW, &s);
    CHECK(s.level == NET_LEVEL_OK);
    CHECK(strcmp(s.why, "4 bonds on 10G · WAN 12 ms · cleo no feed") == 0);

    net_set_hosts(HOSTS, 5);
    net_derive(NOW, &s);
    CHECK(s.level == NET_LEVEL_NONE);
    CHECK(strcmp(s.verdict, "NO FEED") == 0);
}

static void test_down_and_no_path(void) {
    all_healthy(NOW);
    net_observe("kubsdb", rec("down", 1, 5, 1, 0, 0, 0, 0, "[1793062790.0]", NOW - 1), NOW);
    net_observe("kai", rec("10g", 1, 1, 0, 0, 0, 1, 1, NULL, NOW - 1), NOW);
    net_summary_t s;
    net_derive(NOW, &s);
    CHECK(s.level == NET_LEVEL_CRIT);
    CHECK(strncmp(s.verdict, "kubsdb DOWN", 11) == 0 || strncmp(s.verdict, "kai NO PATH", 11) == 0);
    CHECK(strstr(s.why, "+1 more") != NULL);
    CHECK(net_column_attn(host("kubsdb")) == NET_ATTN_CRIT);
    CHECK(net_column_attn(host("kai")) == NET_ATTN_CRIT);
}

static void test_throughput_history_is_per_minute(void) {
    net_set_hosts(HOSTS, 5);
    for (int t = 0; t <= 600; t++)
        net_observe("kai", rec("10g", 1, 1, 0, 0, 1, 1, 1, NULL, NOW + t), NOW + t);
    const net_host_t *k = host("kai");
    CHECK(k->hist_n == 10);
    CHECK(k->hist[0] > 9.99f && k->hist[0] < 10.01f); /* 1.25 MB/s = 10 Mb/s */
    for (int t = 601; t <= 601 + 3600 * 2; t += 5)
        net_observe("kai", rec("10g", 1, 1, 0, 0, 1, 1, 1, NULL, NOW + t), NOW + t);
    CHECK(k->hist_n == NET_HIST_N);
}

static void test_hosts_are_capped_at_the_columns(void) {
    static const char many[][HOSTNAME_LEN] = {"a", "b", "c", "d", "e", "f", "g"};
    net_set_hosts(many, 7);
    CHECK(net_host_count() == NET_HOSTS_MAX);
    net_observe("g", rec("10g", 1, 1, 0, 0, 1, 1, 1, NULL, NOW), NOW); /* ignored */
}

/* WI #4090: the column draws the 24 h delta, never the cumulative counter.
 * kubs0's frozen 587735 (a cable retired 2026-10-18) overflowed the column
 * and meant nothing; the delta is the number that changes. */
static void rx_case(const char *rx, const char *rx_d, const char *want) {
    char json[512], buf[32];
    snprintf(json, sizeof(json),
             "{\"leg\":\"10g\",\"speed_mbps\":10000,\"bond\":true,\"lfc\":28,"
             "\"lfc_delta_24h\":0,\"rx_errors\":%s,\"rx_errors_delta_24h\":%s,"
             "\"reach\":{\"udm\":true,\"peers\":true,\"wan\":true},"
             "\"rtt_ms\":{\"udm\":0.4,\"wan\":12.0},\"bytes_per_s\":1250000,"
             "\"drops\":[],\"ts\":%.1f}",
             rx, rx_d, NOW - 2);
    net_set_hosts(HOSTS, 5);
    net_observe("kubs0", json, NOW);
    net_rx_text(host("kubs0"), buf, sizeof(buf));
    if (strcmp(buf, want) != 0)
        fprintf(stderr, "  rx_text(%s, %s) = \"%s\", want \"%s\"\n", rx, rx_d, buf, want);
    CHECK(strcmp(buf, want) == 0);
}

static void test_rx_errors_show_the_24h_delta(void) {
    rx_case("587735", "0", "rx err 0");
    rx_case("587735", "1240", "rx err 1240");
    rx_case("587735", "null", "rx err —");
    rx_case("null", "null", "rx err —");

    char buf[32];
    net_set_hosts(HOSTS, 5); /* never published: no number to show */
    net_rx_text(host("kubs0"), buf, sizeof(buf));
    CHECK(strcmp(buf, "rx err —") == 0);
}

int main(void) {
    test_schema_examples();
    test_healthy();
    test_host_on_1g();
    test_flapping();
    test_uplink_lost();
    test_wan_down();
    test_stale_is_never_healthy();
    test_no_feed_is_not_a_fault();
    test_down_and_no_path();
    test_throughput_history_is_per_minute();
    test_hosts_are_capped_at_the_columns();
    test_rx_errors_show_the_24h_delta();
    printf("test_netstate: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
