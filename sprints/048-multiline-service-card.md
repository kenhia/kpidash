# 048 — Service card draws multi-line cards (`title`, `lines`, `span`)

Proposal korg:4853 covers WI 4847, and since the overseer's reply (handoff
korg:4871) WI 4870 as well: Ken's changes to the apt-temps widget after seeing
047 on the panel. It is slice 2 of 3 in program korg:4855, and
the reader half of the multi-line status card. kdashdata 028 (korg:4851, merge
`d3f2d09`) is the contract half, and argus-status (korg:4854) is the first
publisher; argus waits on this being deployed. The sprint ran as a karc leg
(kpidash-ff6e7c) on kai under an overseer, on branch
`048-multiline-service-card`.

## Goal

A status card can carry a publisher's own `title`, up to eight `lines` (each
with a size, an alignment and a colour), and a `span` of two units. The border
stays the dashboard's. A card with none of the new fields must render exactly
as it does today: there are about a dozen publishers, and none of them is
changing.

## Premise check

- **WI 4847 holds.** `service_card.c` drew a 220 × 240 card: icon, name (bold
  24), host (bold 16), then `text` in one wrapping, centred bold 20 label.
  - The reader parsed with its own cJSON code in `redis_parse_service_payload()`.
  - The `lib/kdashdata` pin was `41226c2`. `d3f2d09` carries
    `kdash_service_line_t` and the new `kdash_service_t` fields, as handoff
    korg:4857 said.
- **The row-2 slice had shipped** (047, `b762ef2`), so the footer strip no
  longer carries apt-temps cards.
- kpidash is not in the cross-project-planning routing table.
- The preflight was clean, and the karc lock is the leg's own.

## Decisions

- **The layout is read by kdashdata's parser, and acceptance stays this
  reader's.** `redis_parse_service_payload()` still decides whether a record is
  drawn, with the rules it has always had. It then runs `kdash_parse_service()`
  on the same JSON and copies `title`, `span` and `lines` out of it. That makes
  CD-32's "a malformed `lines` is dropped whole" the shared parser's behaviour,
  not a second copy of it here.
  - Side effect: kpidash accepts `"state":"OK"`, but kdashdata follows the
    schema's lowercase enum. An uppercase record still draws, as plain `text`
    with no layout. This is documented in §8a and pinned by a test.
- **Eight line labels are built hidden, up front.** A hidden flex child takes
  no room, which is what keeps a card without the new fields identical.
  - Checked by rendering the `plain` state with the pre-048 `service_card.c`
    swapped back in: same BMP, md5 `6da1f905…`.
- **A card drawing `lines` closes its row gap to 0.** The theme's 14 px gap
  left room for one of the argus card's four lines under the full header.
  - Measured: icon 59 + name 31 + host 21 px leave 59 px of the 212 px content.
  - At a 2 px gap the card was 7 px short, and the line that fell off was the
    red FAIL line.
  - At 0 the header and all four lines (S + 3 M = 96 px) fit with 5 px to
    spare. Each font's line height already includes its leading, so 0 is
    simply how a paragraph is set.
  - Cards without lines keep the theme's gap. The local style is removed, not
    overwritten, when a card goes back to plain.
- **A line is cut, never wrapped.** Each label has a fixed one-line height with
  `LV_LABEL_LONG_DOT`. LVGL cuts with three periods (`...`), not with `…`. The
  overseer's ruling asked for "…"; three periods is LVGL's built-in marker, and
  changing it would mean changing `LV_LABEL_DOT_NUM` handling for every label.
- **A `title` is held to one line, and a name is not.** The name label always
  asked for `LONG_DOT`, but its height grows with its content, so a long name
  wraps. Fixing that would change existing cards, so the one-line rule applies
  only when a publisher sends a `title`.
- **Lines that do not fit are dropped from the bottom, whole.**
  `svc_lines_fit()` (pure, in the gate) counts the lines that fit. The widget
  measures the room left under the header as LVGL laid it out.
- **The card redraws only when its inputs change.** `svc_layout_sig()` is an
  FNV-1a fingerprint over the title, span, `text`, host, icon and every line.
  - The body is laid out again only when it changes. A record republished
    unchanged every minute costs nothing.
  - The header goes into the fingerprint because a host line appearing takes
    room.
- **The lamp panel is re-placed when the strip's total card width changes.**
  It used to key on card count alone, and that was correct only while every
  card was one width.
  - The width comes from summing each card's style width, which needs no
    layout pass.
  - The strip's gap is now `SVC_STRIP_GAP`, so a span-2 card (2 × 220 + 8 =
    448) lines up with two span-1 cards.
- **`--line` takes an optional bracketed style group.** For example
  `--line "[size=S color=#1e66f5] notice"`; `--line ""` is the blank line.
  - The client validates locally against the schema: the size and align
    letters, `#rrggbb`, no newline, at most 8 lines, span 1 or 2.
  - The client is bumped 2.0.0 → 2.1.0, because `kpkg` refuses a version it
    already has.

## What shipped

- `src/svclayout.{h,c}` (new, pure): the card's width per span, line fitting,
  and the redraw fingerprint.
- `src/registry.{h,c}`: `service_entry_t` gains `title`, `span`, `n_lines`,
  `lines[8]`, the eight line-label handles and `drawn_sig`. `apply_payload`
  copies the layout.
- `src/redis.c`: the layout is read through `kdash_parse_service()`.
- `src/widgets/service_card.c`: title, lines (size, alignment and colour),
  span, bottom clipping, and redraw on change.
- `src/ui.c`: the strip gap comes from `SVC_STRIP_GAP`, and the lamp panel is
  re-placed on a width change.
- `lib/kdashdata` → `d3f2d09`.
- `clients/kpidash-client` 2.1.0: `service-status --title/--line/--span`.
- Tests:
  - `tests/test_svclayout.c` (new): 34 checks.
  - `tests/test_service_card.c`: five layout cases (absent, argus, malformed,
    acceptance unchanged, applied and then reverted), 106 checks in total.
  - `tests/render_service_card.c` (new, LVGL): states `argus`, `fail`,
    `plain`, `long` and `full`, registered as ctests. Each also checks that
    every drawn line sits inside its card at one line's height.
  - The client gained 13 tests, 172 in total.
- Docs: CLIENT-PROTOCOL §8a ("Multi-line cards" and a CLI example),
  ARCHITECTURE, and the `check-release` file list in the justfile and
  CLAUDE.md.
- Mock-ups attached to WI 4847: `img-1303` (argus, all lines OK) and
  `img-1304` (one red FAIL line). Each shows an ordinary card, the argus card
  at span 1, and the argus card at span 2.

## The apt-temps widget, restyled (WI 4870)

Added mid-sprint by the overseer after the first wrap-up. Ken's words, after
seeing the 047 widget on the panel: the temperature font is too big; use a
face between the 10G card's bold 48 and the 96 px digits if the repo has one,
otherwise bold 48; humidity under the temperature, as the old footer cards
had it; centre everything.

- **Bold 48.** The repo builds bold 14, 16, 20, 24, 28, 36 and 48, then the
  96 px digits-only face — nothing in between — and Ken's rule was "use what
  we have", so no new size was generated.
- **Zone, temperature, humidity, each centred.** Line boxes are 35 / 59 /
  35 px with 6 px gaps. The stack is placed 4 px below the zone's centre,
  because a line box carries more leading above its glyphs than below: at
  exact centre the render showed 40 px over the name and 31 under the
  humidity. Baselines are 62 / 129 / 174. A missing zone keeps the same
  stack: name, em dash, "no reading".
- **"72.4°F", not "72.4°".** The WI left it to the render, and both were
  rendered (`.scratch/at`, `.scratch/atF`). Bold 48 has letters, which the
  digits face did not; the F reads well, and it is what the old footer cards
  printed (`%.1f°F`). The string is atstate's, so `test_atstate` changed with
  it.
- **The 96 px face keeps its full stop.** Nothing draws one at 96 now; it
  stays rather than regenerate a committed font, and `generate.sh` and
  ARCHITECTURE say so.
- `render_apt_temps`' fit check now checks each line against the whole width
  (the widest is "140.0°F" and "-40.0°F").
- Renders, kept as 047's were: `sprints/planning/apt-temps-states-048.png`
  (the six states) and `row2-048.png` (Agents `target` + the widget + the
  calendar `now`), attached to WI 4870 as `img-1308` and `img-1309`.

## Repaired in passing

None.

## Gates

- `just check`: green. That covers the dashboard ctests (25), check-release
  (-O3 -Werror), the client (ruff + 172 tests), units, scripts and fonts.
- `just check-warnings-full`: green (whole tree, -O3 -Werror).
- Full native tree, `ctest --no-tests=error`: 53 of 53 pass, including the
  five `render_service_card_*` renders, the six `render_apt_temps_*` fit
  checks and `test_widget_leak`.
- All three were re-run after WI 4870 and are still green.

## Deployed

- **2026-11-20 18:35 PST:** `just publish` added kpidash-client **2.1.0** (wheel and sdist) to the package store.
- **2026-11-20 18:35 PST:** `scripts/deploy.sh` deployed from merged `main` (`27dd15f`, PR #52) to rpi53.
  - The service is active with NRestarts=0, and the running binary reports `27dd15f (2026-11-21)`.
  - The cross-build printed no compiler or linker warnings.
- **Live checks:** a `krpidss` capture from kai. The crops are in `/scratch/kpidash-048/live/`.
  - **Multi-line card:** a throwaway `kpidash:services:test048:kai` was published with the client's new flags.
    - Span 2, a title, a blue S notice with a real middle dot (U+00B7), a blank line, an OK line, and a red FAIL line.
    - It drew beside the ordinary cards with every line shown, and the middle dot rendered as a glyph.
    - Attached to WI 4847 (`img-130b` footer, `img-130c` card).
    - `kpidash-cards prune --service test048:kai` removed it, and `kpidash-cards list` no longer shows it.
  - **Row 2:** the apt-temps widget is live as rendered: Living 68.3°F / 37 %, Kitchen 67.9°F / 38 %, Bedroom 66.6–66.8°F / 42 %, centred. Attached to WI 4870 (`img-130d` row, `img-130e` widget).
  - **Journal:** `kpidash.service` logged 22 lines from the restart to the check: start-up, client registrations, one memstat, and four screenshot saves. None was a warning, so nothing repeats per redraw.
- No Redis keys were written except `test048` (now deleted), the evict command `kpidash-cards` sends, and the `kpidash:screenshot` command `krpidss` issues.
