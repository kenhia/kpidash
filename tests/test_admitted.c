/* test_admitted.c — the admitted-hosts file (WI #3012).
 *
 * The load side is the one that must never fail: it runs at startup on a
 * panel with no keyboard, so every way a file can be wrong has to end in
 * "start with what you could read" rather than in an error nobody will see.
 */
#include "admitted.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static char g_dir[] = "/tmp/kpidash-admitted-XXXXXX";
static char g_path[512];

static void write_file(const char *contents, size_t len) {
    FILE *f = fopen(g_path, "wb");
    if (!f) {
        fprintf(stderr, "FAIL: cannot write fixture %s\n", g_path);
        failed++;
        return;
    }
    fwrite(contents, 1, len, f);
    fclose(f);
}

static bool has_host(char hosts[][HOSTNAME_LEN], int n, const char *want) {
    for (int i = 0; i < n; i++) {
        if (strcmp(hosts[i], want) == 0)
            return true;
    }
    return false;
}

/* --- hostname validation ------------------------------------------------ */
static void test_valid_hostname(void) {
    CHECK(admitted_valid_hostname("kai"));
    CHECK(admitted_valid_hostname("kubs0"));
    CHECK(admitted_valid_hostname("rpi53"));
    CHECK(admitted_valid_hostname("host-with-dash"));
    CHECK(admitted_valid_hostname("host.with.dots"));

    CHECK(!admitted_valid_hostname(NULL));
    CHECK(!admitted_valid_hostname(""));
    /* The protocol says hostnames are lowercase; upper case is drift, not a
     * second spelling of the same host, and admitting both would render two
     * cards for one machine. */
    CHECK(!admitted_valid_hostname("KAI"));
    CHECK(!admitted_valid_hostname("kai kubs0"));
    CHECK(!admitted_valid_hostname("kai\t"));
    CHECK(!admitted_valid_hostname("../../etc/passwd"));
    CHECK(!admitted_valid_hostname("host;rm -rf /"));

    char toolong[HOSTNAME_LEN + 8];
    memset(toolong, 'a', sizeof(toolong) - 1);
    toolong[sizeof(toolong) - 1] = '\0';
    CHECK(!admitted_valid_hostname(toolong));
}

/* --- round trip --------------------------------------------------------- */
static void test_round_trip(void) {
    char in[4][HOSTNAME_LEN] = {"kai", "kubs0", "kubsdb", "rpi53"};
    CHECK(admitted_save(g_path, in, 4));

    char out[MAX_CLIENTS][HOSTNAME_LEN];
    int n = admitted_load(g_path, out, MAX_CLIENTS);
    CHECK(n == 4);
    CHECK(has_host(out, n, "kai"));
    CHECK(has_host(out, n, "kubs0"));
    CHECK(has_host(out, n, "kubsdb"));
    CHECK(has_host(out, n, "rpi53"));
    CHECK(!has_host(out, n, "kwork"));
}

/* Saving nothing is legal and produces a file that loads as nothing. */
static void test_empty_save(void) {
    char in[1][HOSTNAME_LEN] = {""};
    CHECK(admitted_save(g_path, in, 0));

    char out[MAX_CLIENTS][HOSTNAME_LEN];
    CHECK(admitted_load(g_path, out, MAX_CLIENTS) == 0);
}

/* --- every way the file can be wrong ends in "start with what you got" --- */
static void test_missing_file_is_empty_not_error(void) {
    char missing[512];
    snprintf(missing, sizeof(missing), "%s/definitely-not-here", g_dir);
    unlink(missing);

    char out[MAX_CLIENTS][HOSTNAME_LEN];
    CHECK(admitted_load(missing, out, MAX_CLIENTS) == 0);
    /* And the load must not have created it. */
    CHECK(access(missing, F_OK) != 0);
}

static void test_null_and_empty_path(void) {
    char out[MAX_CLIENTS][HOSTNAME_LEN];
    CHECK(admitted_load(NULL, out, MAX_CLIENTS) == 0);
    CHECK(admitted_load("", out, MAX_CLIENTS) == 0);
    CHECK(admitted_load(g_path, out, 0) == 0);

    char in[1][HOSTNAME_LEN] = {"kai"};
    CHECK(!admitted_save(NULL, in, 1));
    CHECK(!admitted_save("", in, 1));
}

static void test_corrupt_lines_are_skipped_individually(void) {
    /* Good hosts on either side of every kind of junk: the point is that one
     * mangled line does not cost the lines around it. */
    const char *contents = "kai\n"
                           "\n"
                           "   \n"
                           "KUBS0\n"                     /* wrong case */
                           "host with spaces\n"          /* invalid */
                           "../../etc/passwd\n"          /* invalid */
                           "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
                           "kubsdb\n"
                           "kai\n" /* duplicate */
                           "rpi53";                      /* no trailing newline */
    write_file(contents, strlen(contents));

    char out[MAX_CLIENTS][HOSTNAME_LEN];
    int n = admitted_load(g_path, out, MAX_CLIENTS);
    CHECK(n == 3);
    CHECK(has_host(out, n, "kai"));
    CHECK(has_host(out, n, "kubsdb"));
    /* The last line has no newline — it must still be read. */
    CHECK(has_host(out, n, "rpi53"));
    CHECK(!has_host(out, n, "KUBS0"));
    CHECK(!has_host(out, n, "kubs0"));
}

static void test_binary_garbage_does_not_crash(void) {
    char junk[4096];
    for (size_t i = 0; i < sizeof(junk); i++) {
        junk[i] = (char)(i * 7 + 3);
    }
    write_file(junk, sizeof(junk));

    char out[MAX_CLIENTS][HOSTNAME_LEN];
    int n = admitted_load(g_path, out, MAX_CLIENTS);
    CHECK(n >= 0 && n <= MAX_CLIENTS);
    /* Whatever it found, every entry is a legal hostname. */
    for (int i = 0; i < n; i++) {
        CHECK(admitted_valid_hostname(out[i]));
    }
}

static void test_a_file_with_no_newlines_at_all(void) {
    char blob[8192];
    memset(blob, 'x', sizeof(blob));
    write_file(blob, sizeof(blob));

    char out[MAX_CLIENTS][HOSTNAME_LEN];
    /* One over-long line, so nothing valid — and no overrun. */
    CHECK(admitted_load(g_path, out, MAX_CLIENTS) == 0);
}

/* --- bounded ------------------------------------------------------------ */
static void test_load_is_bounded(void) {
    FILE *f = fopen(g_path, "wb");
    CHECK(f != NULL);
    if (f) {
        for (int i = 0; i < MAX_CLIENTS * 4; i++) {
            fprintf(f, "h%03d\n", i);
        }
        fclose(f);
    }

    char out[MAX_CLIENTS][HOSTNAME_LEN];
    int n = admitted_load(g_path, out, MAX_CLIENTS);
    CHECK(n == MAX_CLIENTS);

    /* A smaller ceiling is honoured too — nothing writes past `max`. */
    char small[4][HOSTNAME_LEN];
    CHECK(admitted_load(g_path, small, 4) == 4);
}

static void test_save_skips_invalid_and_duplicates(void) {
    char in[5][HOSTNAME_LEN];
    memset(in, 0, sizeof(in));
    strcpy(in[0], "kai");
    strcpy(in[1], "BAD");
    strcpy(in[2], "kai");
    strcpy(in[3], "");
    strcpy(in[4], "kubs0");
    CHECK(admitted_save(g_path, in, 5));

    char out[MAX_CLIENTS][HOSTNAME_LEN];
    int n = admitted_load(g_path, out, MAX_CLIENTS);
    CHECK(n == 2);
    CHECK(has_host(out, n, "kai"));
    CHECK(has_host(out, n, "kubs0"));
}

/* --- the write is atomic and leaves no litter --------------------------- */
static void test_save_is_atomic_and_tidy(void) {
    char in[2][HOSTNAME_LEN] = {"kai", "kubs0"};
    CHECK(admitted_save(g_path, in, 2));

    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.tmp", g_path);
    CHECK(access(tmp, F_OK) != 0); /* renamed away, not left behind */

    /* Overwriting in place keeps exactly one file's worth of content. */
    char in2[1][HOSTNAME_LEN] = {"rpi53"};
    CHECK(admitted_save(g_path, in2, 1));
    char out[MAX_CLIENTS][HOSTNAME_LEN];
    int n = admitted_load(g_path, out, MAX_CLIENTS);
    CHECK(n == 1);
    CHECK(has_host(out, n, "rpi53"));
    CHECK(!has_host(out, n, "kai"));
}

/* The first deploy has no /var/lib/kpidash; the save creates it. */
static void test_save_creates_the_parent_directory(void) {
    char subdir[600];
    char path[700];
    snprintf(subdir, sizeof(subdir), "%s/fresh", g_dir);
    snprintf(path, sizeof(path), "%s/admitted", subdir);
    rmdir(subdir);

    char in[1][HOSTNAME_LEN] = {"kai"};
    CHECK(admitted_save(path, in, 1));

    struct stat st;
    CHECK(stat(subdir, &st) == 0 && S_ISDIR(st.st_mode));

    char out[MAX_CLIENTS][HOSTNAME_LEN];
    CHECK(admitted_load(path, out, MAX_CLIENTS) == 1);

    unlink(path);
    rmdir(subdir);
}

/* An unwritable destination is reported, not fatal, and not silent. */
static void test_unwritable_destination_reports_false(void) {
    char in[1][HOSTNAME_LEN] = {"kai"};
    CHECK(!admitted_save("/proc/kpidash-cannot-exist/admitted", in, 1));
}

int main(void) {
    if (!mkdtemp(g_dir)) {
        fprintf(stderr, "FAIL: mkdtemp\n");
        return 1;
    }
    snprintf(g_path, sizeof(g_path), "%s/admitted", g_dir);

    test_valid_hostname();
    test_round_trip();
    test_empty_save();
    test_missing_file_is_empty_not_error();
    test_null_and_empty_path();
    test_corrupt_lines_are_skipped_individually();
    test_binary_garbage_does_not_crash();
    test_a_file_with_no_newlines_at_all();
    test_load_is_bounded();
    test_save_skips_invalid_and_duplicates();
    test_save_is_atomic_and_tidy();
    test_save_creates_the_parent_directory();
    test_unwritable_destination_reports_false();

    unlink(g_path);
    rmdir(g_dir);

    fprintf(stderr, "test_admitted: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
