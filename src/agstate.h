#ifndef AGSTATE_H
#define AGSTATE_H

/**
 * agstate.h — the Agents card's model (WI #4695, sprint 041).
 *
 * One input, kdash:agents (kdashdata CD-31): the last 24 hours of agent
 * sessions as bars, written by ksitrep. libkdash parses the record
 * (kdash_parse_agents) and says where a bar's right edge is
 * (kdash_agents_bar_end); this module turns the bars into the card's
 * geometry — the time axis, the row packing with legs hung under their
 * overseer, the row height, the hour grid — and holds the record through a
 * gap.
 *
 * Pure — no LVGL, no Redis, the clock passed in by the caller — so all of it
 * is reachable from the gate. Text width is the one thing it cannot know, so
 * the label fit takes a measuring callback (the widget passes the font's).
 *
 * Geometry is in the mock-up's 2528 × 628 frame (agents-mockups.html,
 * `borders(true)`): the plot runs from x 36 to 2492, rows from y 104 to 556
 * with 10 px between them. The right edge of the plot is the now line.
 *
 * Anti-flicker (sprint 035's rule):
 *   - an absent key or a rejected record keeps the last good record;
 *   - an old `ts` is a muted hint, and running bars stop at their own `end`
 *     (kdash_agents_bar_end's rule), so a dead publisher cannot stretch them;
 *   - the view moves once a minute: `now` is floored to the minute, so the
 *     now edge and everything packed against it change on the minute only.
 *
 * One kdash_agents_t (21,544 bytes) lives here as a static, with one static
 * scratch for parsing; nothing that size goes on the stack.
 *
 * Touched only from the LVGL thread, so there is no lock.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include <kdash/kdash_payload.h>

/* ---- Geometry, in the card's frame (the mock-up's coordinates) ---- */
#define AG_FRAME_W 2528
#define AG_FRAME_H 628
#define AG_PLOT_X0 36
#define AG_PLOT_X1 2492 /* the now line */
#define AG_ROW_TOP 104
#define AG_ROW_BOT 556
#define AG_ROW_GAP 10
#define AG_ROW_H_MAX 76
/* The smallest row that still holds a 20 px label inside a 4 px border: the
 * bold 20 face's line is 25 px, plus the border and a 3 px margin each side.
 * (452 + 10) / (36 + 10) gives 10 rows. Past 10, rows do not shrink further:
 * the oldest bars are left out instead and the header counts them (see
 * ag_view). */
#define AG_ROW_H_MIN 36
#define AG_ROWS_MAX ((AG_ROW_BOT - AG_ROW_TOP + AG_ROW_GAP) / (AG_ROW_H_MIN + AG_ROW_GAP))
/* Two bars share a row only this far apart: room for the ship diamond, which
 * reaches 14 px past its bar's end. */
#define AG_PACK_GAP_PX 16
/* A bar is never drawn narrower than this: the rounded border needs it. */
#define AG_BAR_MIN_W 16
/* Inside a bar the name starts 14 px in, and stops 8 px short of the right
 * edge (inside the 4 px border), or 18 px short with a ship: the diamond,
 * centred on the edge, reaches 16 px in with its crust outline. */
#define AG_LABEL_PAD 14
#define AG_LABEL_PAD_R 8
#define AG_LABEL_SHIP 18
/* A cut name keeps at least this many characters before its ellipsis;
 * fewer than that and the bar carries no name at all. */
#define AG_LABEL_MIN_CHARS 4

#define AG_WINDOW_S 86400 /* the card's window: last 24 hours, whatever the record's */
#define AG_HOURS_MAX 13   /* grid lines every 2 hours across 24 */

/* Host colour slot. Anything not named is AG_HOST_OTHER. */
enum { AG_HOST_CLEO = 0, AG_HOST_KAI, AG_HOST_KUBS0, AG_HOST_OTHER };

enum {
    AG_NOFEED = 0, /* the key has never published: no record at all */
    AG_EMPTY,      /* a record, with nothing in the last 24 hours    */
    AG_BARS,       /* a record with bars to draw                     */
};

typedef struct {
    int32_t x0, x1;     /* frame x, x1 exclusive: the bar's outer edges  */
    int32_t y, h;       /* frame y of the top, and the row height        */
    int row;
    int parent;         /* index into ag_view_t.bars of its overseer, or -1 */
    uint8_t host;       /* AG_HOST_*                                     */
    uint8_t state;      /* kdash_agent_state_t                           */
    bool running;       /* still open and running to now: square right end */
    bool shipped;
    bool overseer;
    char name[KDASH_AGENTS_NAME_MAX];
} ag_bar_t;

typedef struct {
    int32_t x;     /* frame x of the line */
    int hour;      /* local hour, 0..23 (even) */
    bool midnight;
} ag_hour_t;

typedef struct {
    int status;   /* AG_NOFEED / AG_EMPTY / AG_BARS */
    time_t now;   /* floored to the minute: the now edge */
    bool stale;   /* the record is past its own cadence */
    char hint[24]; /* "as of 12m" when stale, else "" */
    int hidden;   /* bars left out because the rows ran out */
    int rows;     /* rows used, 0..AG_ROWS_MAX */
    int32_t row_h;
    int nbars;
    ag_bar_t bars[KDASH_AGENTS_BARS_MAX];
    int nhours;
    ag_hour_t hours[AG_HOURS_MAX];
} ag_view_t;

/* Feed one GET: NULL means the key is absent. Returns 1 when the record
 * changed, 0 when nothing did, -1 when it was rejected (the caller logs it;
 * the previous record stays). */
int ag_observe(const char *json);
const kdash_agents_t *ag_record(void); /* NULL before the first record */
/* Bumped on every adopted record: with the minute, the view's whole key. */
unsigned ag_generation(void);

/* Everything the card draws at `now`, in the process's local timezone (the
 * hour labels). Deterministic in (record, now floored to the minute). */
void ag_view(time_t now, ag_view_t *out);

/* Host colour slot for a hostname. */
uint8_t ag_host_slot(const char *host);

/* The text to draw inside a bar `avail` px wide (already less the padding):
 * `name` whole when it fits, else cut on a character boundary with "…" when
 * at least AG_LABEL_MIN_CHARS characters survive, else "" (no label).
 * `measure` returns a string's width in px. Returns true when `out` is
 * non-empty. */
typedef int32_t (*ag_measure_fn)(const char *s, void *ctx);
bool ag_fit_label(const char *name, int32_t avail, ag_measure_fn measure, void *ctx, char *out,
                  size_t n);

void ag_reset(void); /* test hook */

#endif /* AGSTATE_H */
