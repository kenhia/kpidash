/* admitted.c — see admitted.h for why this exists (WI #3012). */
#include "admitted.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

bool admitted_valid_hostname(const char *name) {
    if (!name || !name[0])
        return false;

    size_t len = strlen(name);
    if (len >= HOSTNAME_LEN)
        return false;

    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
        if (!ok)
            return false;
    }
    return true;
}

static bool already_have(char out[][HOSTNAME_LEN], int n, const char *name) {
    for (int i = 0; i < n; i++) {
        if (strcmp(out[i], name) == 0)
            return true;
    }
    return false;
}

/* A `[name]` line names the section the lines after it belong to (WI #4085).
 * Writes the name into `name` and returns true; false for any other line. */
static bool section_header(const char *line, char *name, size_t n) {
    size_t len = strlen(line);
    if (len < 3 || line[0] != '[' || line[len - 1] != ']' || len - 2 >= n)
        return false;
    memcpy(name, line + 1, len - 2);
    name[len - 2] = '\0';
    return true;
}

int admitted_load(const char *path, char out[][HOSTNAME_LEN], int max) {
    return admitted_load_section(path, NULL, out, max);
}

int admitted_load_section(const char *path, const char *section, char out[][HOSTNAME_LEN],
                          int max) {
    if (!path || !path[0] || !out || max <= 0)
        return 0;
    const char *want = section ? section : "";

    FILE *f = fopen(path, "r");
    if (!f) {
        /* Missing is the normal first-boot state, and unreadable is not
         * something a panel with no keyboard can act on. Either way: empty. */
        return 0;
    }

    int n = 0;
    /* One byte more than a hostname can be, so an over-long line is
     * recognisably over-long rather than silently truncated into a valid
     * name — truncation would admit a host that does not exist. */
    char line[HOSTNAME_LEN + 2];
    /* The headerless lines at the top are section "": the host cards' set,
     * exactly as every file written before sections existed. */
    char cur[HOSTNAME_LEN] = "";

    while (n < max && fgets(line, (int)sizeof(line), f)) {
        size_t len = strlen(line);
        bool complete = (len > 0 && line[len - 1] == '\n');

        if (complete) {
            line[--len] = '\0';
        } else if (!feof(f)) {
            /* No newline and not the end of the file: this line was longer
             * than the buffer. Drop it, and drop the rest of it, so the
             * tail does not get read as a line of its own. */
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') {
                /* discard */
            }
            continue;
        }

        /* Tolerate CRLF, which is what happens the first time somebody edits
         * this file from a Windows box. */
        if (len > 0 && line[len - 1] == '\r')
            line[--len] = '\0';

        if (section_header(line, cur, sizeof(cur)))
            continue;
        if (strcmp(cur, want) != 0)
            continue;
        if (!admitted_valid_hostname(line))
            continue;
        if (already_have(out, n, line))
            continue;

        /* admitted_valid_hostname() has already guaranteed the length is
         * under HOSTNAME_LEN, so this copies the whole name and its
         * terminator. strncpy here trips -Wstringop-truncation under -O2 —
         * the flow-sensitive class `just check-release` exists to catch
         * (WI #1934), and it did. */
        size_t namelen = strlen(line);
        memcpy(out[n], line, namelen);
        out[n][namelen] = '\0';
        n++;
    }

    fclose(f);
    return n;
}

/* mkdir the one directory `path` lives in. Best effort: a failure here just
 * means the open below fails too, and reports honestly. */
static void ensure_parent_dir(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash || slash == path)
        return;

    size_t dirlen = (size_t)(slash - path);
    char dir[512];
    if (dirlen >= sizeof(dir))
        return;

    memcpy(dir, path, dirlen);
    dir[dirlen] = '\0';
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        /* Reported by the caller's open failure; nothing useful to add. */
    }
}

/* One section's names, validated and deduped on the way out as well as the
 * way in: the file is small enough to be hand-edited, so it is worth being
 * the same shape whichever side produced it. False on a write error. */
static bool write_names(FILE *f, const char hosts[][HOSTNAME_LEN], int count) {
    char seen[MAX_CLIENTS][HOSTNAME_LEN];
    int nseen = 0;

    for (int i = 0; i < count && nseen < MAX_CLIENTS; i++) {
        if (!admitted_valid_hostname(hosts[i]))
            continue;
        if (already_have(seen, nseen, hosts[i]))
            continue;
        if (fprintf(f, "%s\n", hosts[i]) < 0)
            return false;
        /* Both are char[HOSTNAME_LEN] and the name was validated above, so
         * this is an exact element copy. See the note in admitted_load(). */
        memcpy(seen[nseen], hosts[i], HOSTNAME_LEN);
        nseen++;
    }
    return true;
}

bool admitted_save(const char *path, const char hosts[][HOSTNAME_LEN], int count,
                   const char net[][HOSTNAME_LEN], int net_count) {
    if (!path || !path[0])
        return false;
    if (count < 0 || net_count < 0)
        return false;
    if ((count > 0 && !hosts) || (net_count > 0 && !net))
        return false;

    char tmp[600];
    if ((size_t)snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= sizeof(tmp))
        return false;

    ensure_parent_dir(path);

    FILE *f = fopen(tmp, "w");
    if (!f) {
        fprintf(stderr, "admitted: cannot write %s: %s\n", tmp, strerror(errno));
        return false;
    }

    /* Host cards first and headerless, so a binary from before sections
     * reads its own set unchanged. An empty [net] is not written at all. */
    bool ok = write_names(f, hosts, count);
    if (ok && net_count > 0)
        ok = fprintf(f, "[net]\n") >= 0 && write_names(f, net, net_count);

    /* fflush before fsync: fsync knows nothing about stdio's buffer, and a
     * panel loses power often enough for the difference to be real. */
    if (ok && fflush(f) != 0)
        ok = false;
    if (ok && fsync(fileno(f)) != 0)
        ok = false;
    if (fclose(f) != 0)
        ok = false;

    if (!ok) {
        unlink(tmp);
        fprintf(stderr, "admitted: failed writing %s\n", tmp);
        return false;
    }

    if (rename(tmp, path) != 0) {
        fprintf(stderr, "admitted: cannot rename %s -> %s: %s\n", tmp, path, strerror(errno));
        unlink(tmp);
        return false;
    }

    return true;
}
