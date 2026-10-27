#ifndef NETSTATE_H
#define NETSTATE_H

/**
 * netstate.h — the network-health card's model (WI #4062, program korg:4068).
 *
 * Reads kdash:net:{host} v1 (kdashdata contract, CD-27) for a FIXED list of
 * hosts and derives everything the card draws: per-column state and
 * attention, the verdict word and level, the header tiles, and a 60-minute
 * throughput history per host. Pure: no LVGL, no Redis, no clock — the poll
 * hands in the payload and `now`, so all of it is unit-testable.
 *
 * Freshness is the host cards' convention, not a second rule: the key is
 * written every ~5 s with EX 15, so a key that is ABSENT this poll means the
 * publisher is gone. The column keeps the last record it saw and renders it
 * stale (`STALE · now - ts`), never as healthy. A host that has never
 * published this run is "no feed" — a grey column, not a fault.
 *
 * Touched only from the LVGL thread (the 1 s poll and ui_refresh both run in
 * main.c's timer callback), so there is no lock.
 */

#include <stdbool.h>
#include <stddef.h>

#include "protocol.h"

#define NET_HOSTS_MAX 5   /* the card has five 226 px columns */
#define NET_HIST_N 60     /* one sample per minute: the 60-min sparkline */
#define NET_DROPS_MAX 64  /* schema maxItems */
#define NET_WINDOW_S 3600.0

typedef enum { NET_LEG_10G = 0, NET_LEG_5G, NET_LEG_1G, NET_LEG_DOWN } net_leg_t;

/* One decoded kdash:net record. Negative = the contract's null. */
typedef struct {
    net_leg_t leg;
    int speed_mbps;
    bool bond;
    long lfc, lfc_delta_24h;
    long rx_errors, rx_errors_delta_24h;
    bool reach_udm, reach_peers, reach_wan;
    double rtt_udm, rtt_wan;
    double bytes_per_s;
    double drops[NET_DROPS_MAX];
    int ndrops;
    double ts;
    char nic[17];
} net_record_t;

/* Parse one payload. 0 on success; -1 when the record is not one this
 * reader can trust (bad JSON, a missing required field, an unknown `leg`) —
 * the contract says a reader distrusts it rather than guessing. */
int net_parse_json(const char *json, net_record_t *out);

typedef enum {
    NET_COL_NOFEED = 0, /* never published this run: grey, "no publisher" */
    NET_COL_LIVE,       /* key present this poll */
    NET_COL_STALE,      /* published before, key gone now */
} net_col_state_t;

typedef enum { NET_ATTN_NONE = 0, NET_ATTN_WARN, NET_ATTN_CRIT } net_attn_t;

typedef struct {
    char host[HOSTNAME_LEN];
    net_col_state_t state;
    net_record_t rec; /* last record seen; valid unless state == NOFEED */
    /* Throughput history, Mb/s, oldest first: per-minute means. */
    float hist[NET_HIST_N];
    int hist_n;
    double minute_start;
    double acc_sum;
    int acc_n;
} net_host_t;

/* Set the host list (card order). Resets all state. */
void net_set_hosts(const char hosts[][HOSTNAME_LEN], int n);
int net_host_count(void);
const net_host_t *net_host_at(int i);

/* Feed one poll's result for one host. `json` NULL means the key was
 * absent; an untrustworthy payload counts as absent too. */
void net_observe(const char *host, const char *json, double now);

/* Column attention, per the design record (korg:4059): red = stale, no link,
 * or no path to UDM/peers/Internet; yellow = on 1G, a link drop in 24 h or
 * the last hour, or new rx errors. NOFEED is NONE: it is not a fault. */
net_attn_t net_column_attn(const net_host_t *h);

/* "Flapping": two or more drops in the hour (or LFC +2 in 24 h) while still
 * on the primary leg. */
bool net_is_flapping(const net_host_t *h);

typedef enum {
    NET_LEVEL_OK = 0,
    NET_LEVEL_WARN,
    NET_LEVEL_CRIT,
    NET_LEVEL_NONE, /* nothing has published: no verdict to give */
} net_level_t;

typedef struct {
    net_level_t level;
    /* The 48 pt headline; speed tokens are coloured by the renderer. Sized for
     * a full HOSTNAME_LEN host plus the longest suffix (" RX ERRORS"). */
    char verdict[HOSTNAME_LEN + 16];
    char why[192];    /* the one-line summary under it */
    bool udm_up, wan_up, uplink_up;
    double udm_ms, wan_ms; /* medians over live hosts that reach it; <0 none */
} net_summary_t;

void net_derive(double now, net_summary_t *out);

/* Pure helpers for the renderer (and tests). */
const char *net_leg_text(net_leg_t leg); /* "10G" "5G" "1G" "DOWN" */
/* Column subtitle; *bold set when it is an attention line. */
void net_subtitle(const net_host_t *h, double now, char *buf, size_t n, bool *bold);

#endif /* NETSTATE_H */
