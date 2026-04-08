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

    /* T026: grid command */
    {
        dev_cmd_state_t s = {0};
        bool ok = redis_parse_cmd_grid_json("{\"enabled\":true,\"size\":50}", &s);
        CHECK(ok);
        CHECK(s.grid_enabled == true);
        CHECK(s.grid_size == 50);
    }
    {
        dev_cmd_state_t s = {0};
        bool ok = redis_parse_cmd_grid_json("{\"enabled\":false}", &s);
        CHECK(ok);
        CHECK(s.grid_enabled == false);
    }
    {
        dev_cmd_state_t s = {0};
        bool ok = redis_parse_cmd_grid_json(NULL, &s);  /* absent key */
        CHECK(ok);
        CHECK(s.grid_enabled == false);
        CHECK(s.grid_size == 0);
    }
    {
        dev_cmd_state_t s = {0};
        bool ok = redis_parse_cmd_grid_json("not json", &s);
        CHECK(!ok);
    }
    {
        dev_cmd_state_t s = {0};
        redis_parse_cmd_grid_json("{\"enabled\":true,\"size\":-1}", &s);
        CHECK(s.grid_size == 0);  /* non-positive size ignored */
    }

    /* T010: unit-based grid commands */
    {
        dev_cmd_state_t s = {0};
        bool ok = redis_parse_cmd_grid_json("{\"enabled\":true,\"unit\":true,\"size\":1}", &s);
        CHECK(ok);
        CHECK(s.grid_enabled == true);
        CHECK(s.grid_unit == true);
        CHECK(s.grid_unit_size >= 0.99f && s.grid_unit_size <= 1.01f);
        CHECK(s.grid_size == 0);  /* pixel size not set in unit mode */
    }
    {
        dev_cmd_state_t s = {0};
        bool ok = redis_parse_cmd_grid_json("{\"enabled\":true,\"unit\":true,\"size\":0.5}", &s);
        CHECK(ok);
        CHECK(s.grid_unit == true);
        CHECK(s.grid_unit_size >= 0.49f && s.grid_unit_size <= 0.51f);
    }
    {
        dev_cmd_state_t s = {0};
        bool ok = redis_parse_cmd_grid_json("{\"enabled\":true,\"unit\":true,\"size\":2}", &s);
        CHECK(ok);
        CHECK(s.grid_unit == true);
        CHECK(s.grid_unit_size >= 1.99f && s.grid_unit_size <= 2.01f);
    }
    {
        /* unit:true with size:0 => clamp to default 1.0 */
        dev_cmd_state_t s = {0};
        redis_parse_cmd_grid_json("{\"enabled\":true,\"unit\":true,\"size\":0}", &s);
        CHECK(s.grid_unit == true);
        CHECK(s.grid_unit_size >= 0.99f && s.grid_unit_size <= 1.01f);
    }
    {
        /* unit:false => pixel mode, unit fields unset */
        dev_cmd_state_t s = {0};
        redis_parse_cmd_grid_json("{\"enabled\":true,\"unit\":false,\"size\":100}", &s);
        CHECK(s.grid_unit == false);
        CHECK(s.grid_size == 100);
    }
    {
        /* no unit field => pixel mode (backward compatible) */
        dev_cmd_state_t s = {0};
        redis_parse_cmd_grid_json("{\"enabled\":true,\"size\":200}", &s);
        CHECK(s.grid_unit == false);
        CHECK(s.grid_size == 200);
    }

    /* T027: textsize command */
    {
        dev_cmd_state_t s = {0};
        bool ok = redis_parse_cmd_textsize_json("{\"enabled\":true}", &s);
        CHECK(ok);
        CHECK(s.textsize_enabled == true);
    }
    {
        dev_cmd_state_t s = {0};
        bool ok = redis_parse_cmd_textsize_json("{\"enabled\":false}", &s);
        CHECK(ok);
        CHECK(s.textsize_enabled == false);
    }
    {
        dev_cmd_state_t s = {0};
        bool ok = redis_parse_cmd_textsize_json(NULL, &s);  /* absent key */
        CHECK(ok);
        CHECK(s.textsize_enabled == false);
    }

    /* T038: graph command */
    {
        dev_cmd_state_t s = {0};
        bool ok = redis_parse_cmd_graph_json("{\"enabled\":true,\"client\":\"gpu-box\"}", &s);
        CHECK(ok);
        CHECK(s.graph_enabled == true);
        CHECK(strcmp(s.graph_client, "gpu-box") == 0);
    }
    {
        dev_cmd_state_t s = {0};
        bool ok = redis_parse_cmd_graph_json("{\"enabled\":false}", &s);
        CHECK(ok);
        CHECK(s.graph_enabled == false);
        CHECK(s.graph_client[0] == '\0');
    }
    {
        dev_cmd_state_t s = {0};
        bool ok = redis_parse_cmd_graph_json(NULL, &s);  /* absent key */
        CHECK(ok);
        CHECK(s.graph_enabled == false);
        CHECK(s.graph_client[0] == '\0');
    }
    {
        dev_cmd_state_t s = {0};
        bool ok = redis_parse_cmd_graph_json("not json", &s);
        CHECK(!ok);
    }
    {
        dev_cmd_state_t s = {0};
        bool ok = redis_parse_cmd_graph_json("{\"enabled\":true}", &s);
        CHECK(ok);
        CHECK(s.graph_enabled == true);
        CHECK(s.graph_client[0] == '\0');  /* no client = empty string */
    }

    /* ---- dev_telemetry parse tests ---- */
    {
        /* Full JSON with GPU */
        dev_telemetry_t dt = {0};
        bool ok = redis_parse_dev_telemetry_json(
            "{\"cpu_pct\":42.5,\"top_core_pct\":88.1,"
            "\"ram_used_mb\":8192,\"ram_total_mb\":32768,"
            "\"gpu\":{\"compute_pct\":75.0,\"vram_used_mb\":4096,\"vram_total_mb\":8192},"
            "\"ts\":1700000000.0}", &dt);
        CHECK(ok);
        CHECK(dt.valid == true);
        CHECK(dt.cpu_pct > 42.4f && dt.cpu_pct < 42.6f);
        CHECK(dt.top_core_pct > 88.0f && dt.top_core_pct < 88.2f);
        CHECK(dt.ram_used_mb == 8192);
        CHECK(dt.ram_total_mb == 32768);
        CHECK(dt.gpu_present == true);
        CHECK(dt.gpu_compute_pct > 74.9f && dt.gpu_compute_pct < 75.1f);
        CHECK(dt.gpu_vram_used_mb == 4096);
        CHECK(dt.gpu_vram_total_mb == 8192);
    }
    {
        /* JSON without GPU */
        dev_telemetry_t dt = {0};
        bool ok = redis_parse_dev_telemetry_json(
            "{\"cpu_pct\":10.0,\"top_core_pct\":25.0,"
            "\"ram_used_mb\":2048,\"ram_total_mb\":16384}", &dt);
        CHECK(ok);
        CHECK(dt.valid == true);
        CHECK(dt.gpu_present == false);
        CHECK(dt.gpu_compute_pct < 0.1f);
        CHECK(dt.ram_used_mb == 2048);
    }
    {
        /* NULL JSON (key absent) → valid=false */
        dev_telemetry_t dt = {0};
        dt.valid = true;  /* pre-set to verify it gets cleared */
        bool ok = redis_parse_dev_telemetry_json(NULL, &dt);
        CHECK(ok);
        CHECK(dt.valid == false);
    }
    {
        /* Invalid JSON */
        dev_telemetry_t dt = {0};
        bool ok = redis_parse_dev_telemetry_json("not json", &dt);
        CHECK(!ok);
        CHECK(dt.valid == false);
    }
    {
        /* GPU with null compute_pct (missing field) */
        dev_telemetry_t dt = {0};
        bool ok = redis_parse_dev_telemetry_json(
            "{\"cpu_pct\":5.0,\"gpu\":{\"vram_used_mb\":1024,\"vram_total_mb\":4096}}", &dt);
        CHECK(ok);
        CHECK(dt.valid == true);
        CHECK(dt.gpu_present == true);
        CHECK(dt.gpu_compute_pct < 0.1f);  /* 0 since field missing */
        CHECK(dt.gpu_vram_used_mb == 1024);
    }

    /* ---- repo JSON parse tests ---- */
    {
        /* Full repo JSON with all fields */
        repo_entry_t re = {0};
        bool ok = redis_parse_repo_json(
            "{\"name\":\"kpidash\",\"branch\":\"feature/x\","
            "\"default_branch\":\"main\",\"is_dirty\":true,"
            "\"detached_head\":false,\"ahead\":3,\"behind\":1,"
            "\"untracked_count\":2,\"changed_count\":5,\"deleted_count\":1,"
            "\"renamed_count\":0,\"last_commit_ts\":1700000000.5,"
            "\"explicit\":true,\"ts\":1700000001.0}", &re);
        CHECK(ok);
        CHECK(strcmp(re.name, "kpidash") == 0);
        CHECK(strcmp(re.branch, "feature/x") == 0);
        CHECK(strcmp(re.default_branch, "main") == 0);
        CHECK(re.is_dirty == true);
        CHECK(re.detached_head == false);
        CHECK(re.ahead == 3);
        CHECK(re.behind == 1);
        CHECK(re.untracked_count == 2);
        CHECK(re.changed_count == 5);
        CHECK(re.deleted_count == 1);
        CHECK(re.renamed_count == 0);
        CHECK(re.last_commit_ts > 1699999999.0);
        CHECK(re.sort_order == 0);  /* explicit=true → 0 */
        CHECK(re.ts > 1700000000.0);
    }
    {
        /* Minimal repo JSON (backward compat — only name/branch/is_dirty) */
        repo_entry_t re = {0};
        bool ok = redis_parse_repo_json(
            "{\"name\":\"old-repo\",\"branch\":\"main\","
            "\"is_dirty\":false,\"explicit\":false,\"ts\":1.0}", &re);
        CHECK(ok);
        CHECK(strcmp(re.name, "old-repo") == 0);
        CHECK(re.is_dirty == false);
        CHECK(re.ahead == 0);
        CHECK(re.behind == 0);
        CHECK(re.detached_head == false);
        CHECK(re.default_branch[0] == '\0');  /* absent → empty */
        CHECK(re.sort_order == 1);  /* explicit=false → 1 */
    }
    {
        /* NULL json → false */
        repo_entry_t re = {0};
        bool ok = redis_parse_repo_json(NULL, &re);
        CHECK(!ok);
    }
    {
        /* Invalid JSON → false */
        repo_entry_t re = {0};
        bool ok = redis_parse_repo_json("not json", &re);
        CHECK(!ok);
    }

    printf("test_redis_json: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
