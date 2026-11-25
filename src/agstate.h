#ifndef AGSTATE_H
#define AGSTATE_H

/**
 * agstate.h — the Agents card's model (WI #4695, sprint 041; v2 WI #4733,
 * sprint 042).
 *
 * One input, kdash:agents (kdashdata CD-31): the last 24 hours of agent
 * sessions as bars, written by ksitrep. libkdash parses the record
 * (kdash_parse_agents) and says where a bar's right edge is
 * (kdash_agents_bar_end); this module turns the bars into the card's
 * geometry and holds the record through a gap.
 *
 * Since sprint 042 (WI #4733) the card has two parts. The detail area shows
 * the last 3 hours: the time axis, each name inside its bar or left of it,
 * the row packing with legs hung under their overseer, the row height, the
 * half-hour grid. The day strip under it shows all 24 hours as thin lanes,
 * one per host, with no names, and a lighter band where the 3 hours are.
 *
 * Pure — no LVGL, no Redis, the clock passed in by the caller — so all of it
 * is reachable from the gate. Text width is the one thing it cannot know, so
 * the view takes a measuring callback (the widget passes the font's).
 *
 * Geometry is in the card's frame, the mock-up's (agents-mockups-v2.html,
 * `focus(3)`) narrowed by one host column in sprint 047 (WI #4849): 2152 ×
 * 628, where it was 2528. The plot runs from x 36 to 2116, 36 px short of
 * the frame's right edge as it was, so the 3 hours are 2080 px (11.6 px a
 * minute; 13.6 before). Detail rows run from y 104 to 430 with 8 px between
 * them, the strip's lanes from y 486. The right edge of both is the now
 * line. Nothing vertical changed.
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

#include "layout.h"

/* ---- Geometry, in the card's frame (agents-mockups-v2.html, `focus(3)`) ---- */
#define AG_FRAME_W ROW2_AGENTS_W /* 2152 since sprint 047; the mock-up's 2528 */
#define AG_FRAME_H 628
#define AG_PLOT_X0 36
#define AG_PLOT_X1 (AG_FRAME_W - 36) /* the now line, in the detail area and the strip */

/* The detail area: the last AG_DETAIL_S seconds, bars with names. */
#define AG_DETAIL_S 10800 /* 3 hours (Ken, 2026-11-14: "B, last 3 hours") */
#define AG_PLOT_Y0 96     /* the backdrop and the grid lines: rows' top less 8 */
#define AG_PLOT_Y1 438    /* rows' bottom plus 8 */
#define AG_ROW_TOP 104
#define AG_ROW_BOT 430
#define AG_ROW_GAP 8
#define AG_ROW_H_MAX 64
/* The smallest row that still holds a 20 px label inside a 4 px border: the
 * bold 20 face's line is 25 px, plus the border and a 3 px margin each side.
 * (326 + 8) / (36 + 8) gives 7 rows. Past 7, rows do not shrink further:
 * the oldest bars are left out instead and the header counts them (see
 * ag_view). */
#define AG_ROW_H_MIN 36
#define AG_ROWS_MAX ((AG_ROW_BOT - AG_ROW_TOP + AG_ROW_GAP) / (AG_ROW_H_MIN + AG_ROW_GAP))
#define AG_TICK_S 1800 /* detail grid: every 30 minutes, the hours heavier */
#define AG_TICKS_MAX (AG_DETAIL_S / AG_TICK_S + 1)
/* Two bars share a row only this far apart: room for the ship diamond, which
 * reaches 14 px past its bar's end. A name written left of a bar counts as
 * part of the bar, so the gap holds before the name too. */
#define AG_PACK_GAP_PX 16
/* A bar is never drawn narrower than this: the rounded border needs it. */
#define AG_BAR_MIN_W 16
/* Inside a bar the name starts 14 px in, and stops 8 px short of the right
 * edge (inside the 4 px border), or 18 px short with a ship: the diamond,
 * centred on the edge, reaches 16 px in with its crust outline. */
#define AG_LABEL_PAD 14
#define AG_LABEL_PAD_R 8
#define AG_LABEL_SHIP 18
/* A Codex bar (provider "codex", CD-31 addendum; sprint 049, WI #4941) carries
 * its marker at the left end: a Codex-green dot on a Crust patch, AG_CODEX_PATCH
 * square, AG_CODEX_PATCH_X in from the bar's outer left edge. Its inside name
 * starts AG_CODEX_LABEL_DX further right than a Claude bar's, and a Codex bar
 * is never drawn narrower than AG_CODEX_MIN_W, so the patch always fits inside
 * the border with room to spare: the marker is never dropped. Claude and
 * every other provider stay unmarked. */
#define AG_CODEX_PATCH 18
#define AG_CODEX_PATCH_X 8
#define AG_CODEX_LABEL_DX (AG_CODEX_PATCH_X + AG_CODEX_PATCH + 8 - AG_LABEL_PAD)
#define AG_CODEX_MIN_W (AG_CODEX_PATCH_X + AG_CODEX_PATCH + 8)
/* A name is never shortened while there is room to write it whole (the
 * overseer's ruling on korg:4734). In order of preference:
 *   1. whole, inside the bar;
 *   2. whole, left of the bar, right-aligned AG_LABEL_OUT_GAP from it, when
 *      it stays right of AG_PLOT_X0 and the row the bar takes has room (a
 *      name never pushes its bar down a row);
 *   3. a name longer than AG_LABEL_OUT_MAX characters, cut to that many
 *      with "…", left of the bar on the same terms, unless the inside cut
 *      keeps at least AG_LABEL_OUT_MAX characters;
 *   4. cut with "…" inside, keeping at least AG_LABEL_MIN_CHARS (041's rule);
 *   5. no name. */
#define AG_LABEL_MIN_CHARS 4
#define AG_LABEL_OUT_GAP 8
#define AG_LABEL_OUT_MAX 20 /* characters, the ellipsis included */

/* The day strip: all of AG_WINDOW_S, one thin lane per host slot. */
#define AG_WINDOW_S 86400 /* last 24 hours, whatever the record's window */
#define AG_HOURS_MAX 13   /* strip grid lines every 2 hours across 24 */
#define AG_STRIP_Y 486    /* top of the first lane */
#define AG_LANE_H 14
#define AG_LANE_GAP 4
#define AG_LANES 4
#define AG_STRIP_PAD 8    /* the band reaches this far above and below the lanes */
#define AG_STRIP_Y1 (AG_STRIP_Y + AG_LANES * AG_LANE_H + (AG_LANES - 1) * AG_LANE_GAP)
#define AG_STRIP_MIN_W 4

/* Host colour slot. Anything not named is AG_HOST_OTHER. */
enum { AG_HOST_CLEO = 0, AG_HOST_KAI, AG_HOST_KUBS0, AG_HOST_OTHER };

enum {
    AG_NOFEED = 0, /* the key has never published: no record at all     */
    AG_EMPTY,      /* a record, with nothing in the last 24 hours        */
    AG_QUIET,      /* bars in the strip, none in the last 3 hours        */
    AG_BARS,       /* bars in the detail area (and so in the strip)      */
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
    bool codex;         /* provider "codex": the green-dot marker (sprint 049) */
    bool label_out;     /* the label is left of the bar, right-aligned at
                           x0 - AG_LABEL_OUT_GAP; else inside             */
    char name[KDASH_AGENTS_NAME_MAX];
    char label[KDASH_AGENTS_NAME_MAX + 4]; /* "" when no name is drawn   */
} ag_bar_t;

/* One bar of the day strip: no name, no border, no links. */
typedef struct {
    int32_t x0, x1;     /* frame x, x1 exclusive */
    uint8_t lane;       /* AG_HOST_*: lane y is AG_STRIP_Y + lane * (H + GAP) */
    bool waiting;       /* yellow */
} ag_strip_t;

typedef struct {
    int32_t x;     /* frame x of the line */
    int hour;      /* local hour, 0..23 (even) */
    bool midnight;
} ag_hour_t;

typedef struct {
    int32_t x;     /* frame x of the line */
    int hour, min; /* local time on the half hour */
    bool on_hour;
} ag_tick_t;

typedef struct {
    int status;   /* AG_NOFEED / AG_EMPTY / AG_BARS */
    time_t now;   /* floored to the minute: the now edge */
    bool stale;   /* the record is past its own cadence */
    char hint[24]; /* "as of 12m" when stale, else "" */
    int hidden;   /* bars left out because the rows ran out */
    int rows;     /* rows used, 0..AG_ROWS_MAX */
    int32_t row_h;
    int nbars;      /* the detail area */
    ag_bar_t bars[KDASH_AGENTS_BARS_MAX];
    int nticks;     /* the detail area's grid */
    ag_tick_t ticks[AG_TICKS_MAX];
    int nstrip;     /* the day strip */
    ag_strip_t strip[KDASH_AGENTS_BARS_MAX];
    int32_t band_x0; /* strip x where the detail window opens: the lighter band */
    int nhours;     /* the strip's grid */
    ag_hour_t hours[AG_HOURS_MAX];
} ag_view_t;

/* Feed one GET: NULL means the key is absent. Returns 1 when the record
 * changed, 0 when nothing did, -1 when it was rejected (the caller logs it;
 * the previous record stays). */
int ag_observe(const char *json);
const kdash_agents_t *ag_record(void); /* NULL before the first record */
/* Bumped on every adopted record: with the minute, the view's whole key. */
unsigned ag_generation(void);

/* Measures a string's width in px (the widget passes the bar face's). */
typedef int32_t (*ag_measure_fn)(const char *s, void *ctx);

/* Everything the card draws at `now`, in the process's local timezone (the
 * grid labels). Deterministic in (record, now floored to the minute,
 * measure). `measure` decides where each name goes, which the packing needs,
 * so it is the one input from the font. */
void ag_view(time_t now, ag_measure_fn measure, void *ctx, ag_view_t *out);

/* Host colour slot for a hostname. */
uint8_t ag_host_slot(const char *host);

/* The text to draw inside a bar `avail` px wide (already less the padding):
 * `name` whole when it fits, else cut on a character boundary with "…" when
 * at least AG_LABEL_MIN_CHARS characters survive, else "" (no label).
 * `measure` returns a string's width in px. Returns true when `out` is
 * non-empty. */
bool ag_fit_label(const char *name, int32_t avail, ag_measure_fn measure, void *ctx, char *out,
                  size_t n);

void ag_reset(void); /* test hook */

#endif /* AGSTATE_H */
