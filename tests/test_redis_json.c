/**
 * test_redis_json.c — Unit tests for redis.c JSON parse helpers (T010b)
 *
 * Tests redis_parse_health_json() and redis_parse_telemetry_json()
 * without a live Redis connection. No hardware required.
 *
 * KPIDASH_TEST_STUBS=1 enables stub definitions for ui/status/fortune
 * symbols that redis.c references at link time.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

/* Pull in headers that redis.c depends on */
#include "redis.h"
#include "registry.h"
#include "protocol.h"

/* ---- Stubs for symbols redis.c calls that aren't under test ---- */
#ifdef KPIDASH_TEST_STUBS
void ui_show_redis_error(const char *msg) { (void)msg; }
void ui_hide_redis_error(void) {}
void status_redis_check_ack(void *ctx) { (void)ctx; }
void fortune_on_pushed(const char *json) { (void)json; }
#endif

static int passed = 0;
static int failed = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
            failed++; \
        } else { \
            passed++; \
        } \
    } while (0)

static void test_health_valid(void) {
    const char *json = "{\"hostname\":\"kubs0\",\"last_seen_ts\":1743292800.5,"
                       "\"uptime_seconds\":86400.0,"
                       "\"os_name\":\"Linux 5.15.0-173-generic\"}";
    client_info_t c = {0};
    bool ok = redis_parse_health_json(json, &c);
    CHECK(ok);
    CHECK(c.online == true);
    CHECK(c.last_seen_ts > 1743292800.0);
    CHECK(c.uptime_seconds > 86399.0f);
    CHECK(strcmp(c.os_name, "Linux 5.15.0-173-generic") == 0);
}

static void test_health_missing_uptime(void) {
    /* uptime_seconds and os_name are optional */
    const char *json = "{\"hostname\":\"kubs1\",\"last_seen_ts\":1743292801.0}";
    client_info_t c = {0};
    bool ok = redis_parse_health_json(json, &c);
    CHECK(ok);
    CHECK(c.online == true);
    CHECK(c.uptime_seconds == 0.0f);
    CHECK(c.os_name[0] == '\0');  /* absent os_name → empty string */
}

static void test_health_os_name_empty(void) {
    const char *json = "{\"hostname\":\"kubs2\",\"last_seen_ts\":1.0,"
                       "\"os_name\":\"\"}";
    client_info_t c = {0};
    bool ok = redis_parse_health_json(json, &c);
    CHECK(ok);
    CHECK(c.os_name[0] == '\0');  /* empty os_name → empty string */
}

static void test_health_malformed(void) {
    client_info_t c = {0};
    bool ok = redis_parse_health_json("not json at all", &c);
    CHECK(!ok);
}

static void test_telemetry_full(void) {
    const char *json =
        "{"
        "\"hostname\":\"kubs0\","
        "\"ts\":1743292802.0,"
        "\"cpu_pct\":45.2,"
        "\"top_core_pct\":78.1,"
        "\"ram_used_mb\":6144,"
        "\"ram_total_mb\":16384,"
        "\"gpu\":{"
        "  \"name\":\"NVIDIA GeForce RTX 4090\","
        "  \"vram_used_mb\":3072,"
        "  \"vram_total_mb\":24576,"
        "  \"compute_pct\":32.5"
        "},"
        "\"disks\":["
        "  {\"label\":\"root\",\"type\":\"nvme\","
        "   \"used_gb\":237.4,\"total_gb\":953.9,\"pct\":24.9}"
        "]"
        "}";

    client_info_t c = {0};
    bool ok = redis_parse_telemetry_json(json, &c);
    CHECK(ok);
    CHECK(c.cpu_pct > 45.0f && c.cpu_pct < 46.0f);
    CHECK(c.top_core_pct > 78.0f);
    CHECK(c.ram_used_mb == 6144);
    CHECK(c.ram_total_mb == 16384);
    CHECK(c.gpu.present == true);
    CHECK(strcmp(c.gpu.name, "NVIDIA GeForce RTX 4090") == 0);
    CHECK(c.gpu.vram_used_mb == 3072);
    CHECK(c.gpu.vram_total_mb == 24576);
    CHECK(c.gpu.compute_pct > 32.0f);
    CHECK(c.disk_count == 1);
    CHECK(c.disks[0].type == DISK_NVME);
    CHECK(c.disks[0].used_gb > 237.0f);
    CHECK(c.disks[0].total_gb > 953.0f);
}

static void test_telemetry_no_gpu(void) {
    const char *json =
        "{\"hostname\":\"kubs1\",\"ts\":1.0,"
        "\"cpu_pct\":10.0,\"top_core_pct\":20.0,"
        "\"ram_used_mb\":1024,\"ram_total_mb\":4096,"
        "\"gpu\":null,\"disks\":[]}";
    client_info_t c = {0};
    bool ok = redis_parse_telemetry_json(json, &c);
    CHECK(ok);
    CHECK(c.gpu.present == false);
    CHECK(c.disk_count == 0);
}

static void test_telemetry_disk_types(void) {
    const char *json =
        "{\"hostname\":\"kubs2\",\"ts\":1.0,"
        "\"cpu_pct\":0,\"top_core_pct\":0,"
        "\"ram_used_mb\":0,\"ram_total_mb\":0,"
        "\"gpu\":null,"
        "\"disks\":["
        "  {\"label\":\"d1\",\"type\":\"ssd\",\"used_gb\":1,\"total_gb\":2,\"pct\":50},"
        "  {\"label\":\"d2\",\"type\":\"hdd\",\"used_gb\":1,\"total_gb\":2,\"pct\":50},"
        "  {\"label\":\"d3\",\"type\":\"other\",\"used_gb\":1,\"total_gb\":2,\"pct\":50}"
        "]}";
    client_info_t c = {0};
    bool ok = redis_parse_telemetry_json(json, &c);
    CHECK(ok);
    CHECK(c.disk_count == 3);
    CHECK(c.disks[0].type == DISK_SSD);
    CHECK(c.disks[1].type == DISK_HDD);
    CHECK(c.disks[2].type == DISK_OTHER);
}

int main(void) {
    registry_init();

    test_health_valid();
    test_health_missing_uptime();
    test_health_os_name_empty();
    test_health_malformed();
    test_telemetry_full();
    test_telemetry_no_gpu();
    test_telemetry_disk_types();

    printf("test_redis_json: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
