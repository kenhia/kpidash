#ifndef SVCLAYOUT_H
#define SVCLAYOUT_H

/**
 * svclayout.h — the service card's geometry and line fitting (WI #4847,
 * sprint 048).
 *
 * The status card can carry a publisher's own `title`, up to eight `lines`
 * each with a size, alignment and colour, and a `span` of one or two units
 * (kdashdata CD-32). The arithmetic that decides how wide a card is and how
 * many of its lines are drawn lives here, pure — no LVGL, no Redis — so the
 * gate reaches it. The widget measures its fonts and asks.
 */

#include <stdbool.h>
#include <stdint.h>

#include "kdash/kdash_payload.h"

/* One unit of the footer strip, and the gap the strip leaves between cards.
 * ui.c sets the strip's column gap from SVC_STRIP_GAP, so a span-2 card lines
 * up with two span-1 cards above or below it. */
#define SVC_CARD_W 220
#define SVC_CARD_H 240
#define SVC_STRIP_GAP 8

/* The card's width for a `span`. Anything but 2 is one unit: span is 1 or 2
 * by contract, and a reader that draws a malformed span narrow is the safe
 * reading. */
int32_t svc_card_width(int span);

/* How many of `n` lines, of heights `h[0..n)`, fit top-down in `avail` px
 * with `gap` px between consecutive lines. Lines are dropped from the bottom
 * and never part-drawn: a line either fits whole or is not shown. */
int svc_lines_fit(const int32_t *h, int n, int32_t avail, int32_t gap);

/* A fingerprint of everything the card's body is drawn from: title, span,
 * every line's text and style, the fallback `text` — and the header the lines
 * are fitted under (`host`, `icon`), since a host line appearing takes room.
 * The widget redraws the body only when this changes, so a record republished
 * unchanged every minute costs no relayout. FNV-1a; equality is all that is
 * asked of it. */
uint32_t svc_layout_sig(const char *title, int span, const char *text,
                        const char *host, int icon,
                        const kdash_service_line_t *lines, int n_lines);

#endif /* SVCLAYOUT_H */
