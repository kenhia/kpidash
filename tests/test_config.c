/**
 * test_config.c — Unit tests for config_load() (T010a)
 *
 * Tests run on the native host with no hardware required.
 * Uses setenv()/unsetenv() to simulate environment.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "config.h"

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

static void clear_env(void) {
    unsetenv("KPIDASH_REDIS_HOST");
    unsetenv("KPIDASH_REDIS_PORT");
    unsetenv("REDISCLI_AUTH");
    unsetenv("KPIDASH_DRM_DEV");
    unsetenv("KPIDASH_MAX_CLIENTS");
    unsetenv("KPIDASH_ACTIVITY_MAX");
    unsetenv("KPIDASH_LOG_FILE");
    unsetenv("KPIDASH_PRIORITY_CLIENTS");
}

static void test_defaults(void) {
    clear_env();
    kpidash_config_t cfg;
    config_load(&cfg);

    CHECK(strcmp(cfg.redis_host, "127.0.0.1") == 0);
    CHECK(cfg.redis_port == 6379);
    CHECK(cfg.redis_auth[0] == '\0');
    CHECK(strcmp(cfg.drm_dev, "/dev/dri/card1") == 0);
    CHECK(cfg.max_clients == MAX_CLIENTS);
    CHECK(cfg.activity_max == ACTIVITY_MAX_DISPLAY);
    CHECK(strcmp(cfg.log_file, "/var/log/kpidash/kpidash.log") == 0);
    CHECK(cfg.priority_client_count == 0);
}

static void test_overrides(void) {
    setenv("KPIDASH_REDIS_HOST", "192.168.1.10", 1);
    setenv("KPIDASH_REDIS_PORT", "6380", 1);
    setenv("REDISCLI_AUTH", "secret", 1);
    setenv("KPIDASH_DRM_DEV", "/dev/dri/card0", 1);
    setenv("KPIDASH_MAX_CLIENTS", "8", 1);
    setenv("KPIDASH_ACTIVITY_MAX", "5", 1);
    setenv("KPIDASH_LOG_FILE", "/tmp/kpidash.log", 1);

    kpidash_config_t cfg;
    config_load(&cfg);

    CHECK(strcmp(cfg.redis_host, "192.168.1.10") == 0);
    CHECK(cfg.redis_port == 6380);
    CHECK(strcmp(cfg.redis_auth, "secret") == 0);
    CHECK(strcmp(cfg.drm_dev, "/dev/dri/card0") == 0);
    CHECK(cfg.max_clients == 8);
    CHECK(cfg.activity_max == 5);
    CHECK(strcmp(cfg.log_file, "/tmp/kpidash.log") == 0);

    clear_env();
}

static void test_invalid_port(void) {
    clear_env();
    setenv("KPIDASH_REDIS_PORT", "99999", 1);
    kpidash_config_t cfg;
    config_load(&cfg);
    /* Out of range — should fall back to default */
    CHECK(cfg.redis_port == 6379);
    clear_env();
}

static void test_invalid_max_clients(void) {
    clear_env();
    setenv("KPIDASH_MAX_CLIENTS", "200", 1);
    kpidash_config_t cfg;
    config_load(&cfg);
    /* Exceeds MAX_CLIENTS — should fall back to default */
    CHECK(cfg.max_clients == MAX_CLIENTS);
    clear_env();
}

static void test_priority_clients(void) {
    clear_env();
    setenv("KPIDASH_PRIORITY_CLIENTS", "kubs0,kubs1, kubs2", 1);
    kpidash_config_t cfg;
    config_load(&cfg);
    CHECK(cfg.priority_client_count == 3);
    CHECK(strcmp(cfg.priority_clients[0], "kubs0") == 0);
    CHECK(strcmp(cfg.priority_clients[1], "kubs1") == 0);
    /* Leading space trimmed */
    CHECK(strcmp(cfg.priority_clients[2], "kubs2") == 0);
    clear_env();
}

int main(void) {
    test_defaults();
    test_overrides();
    test_invalid_port();
    test_invalid_max_clients();
    test_priority_clients();

    printf("test_config: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
