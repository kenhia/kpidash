# Sprint 025: the net card's rx-error line shows the 24 h delta

Proposal korg:4092, covering WI 4090. Slice 5 of program korg:4068, the last
one. It ran as karc leg `kpidash-e2ca67` on kai, headless, under
`/overseen-sprint`. Every probe and capture ran **from kai**.

The overseer's first full-fleet capture (2026-10-26 18:56 PDT) showed kubs0's
column drawing `rx err 58773` with the last digit clipped. The card drew the
**cumulative** `rx_errors`: 587735, frozen since the bad 100 ft cable was
retired on 2026-10-18 (korg:3949). That number will never change, so it tells
the reader nothing, and it did not fit the 226 px column beside `LFC 28`.

## Premise check

| claim | verdict |
|---|---|
| `net_card.c` draws the cumulative `rx_errors` | **holds**: `snprintf(..., "rx err %ld", r->rx_errors)` |
| the producer already sends `rx_errors_delta_24h` | **holds**: parsed by `net_parse_json`, and the live `kdash:net:kubs0` carries `rx_errors: 587735, rx_errors_delta_24h: 0` |
| yellow already keys on the delta | **holds**: `rx_warn = live && r->rx_errors_delta_24h > 0`. Only the number was wrong |

## Decisions

- **The text moves into the pure model, `net_rx_text()` in `netstate.c`.** It
  sits beside `net_subtitle()`, the precedent for renderer text that wants a
  test. That makes the fix TDD-able without LVGL. The test failed first (an
  undefined reference), then passed.
- **`rx err —` when there is no feed or the delta is null.** A null delta means
  the producer has no 24 h window yet (CLIENT-PROTOCOL). Showing the cumulative
  value instead would bring back exactly the number this sprint removes.
- **The box is 120 px, right-aligned, the same budget as the LFC side.** It
  was 90. The two boxes overlap in the middle of the column, but both are
  transparent spangroups, and the realistic worst case (`LFC 28 (+3)` beside
  `rx err 1240`) is about 185 px of text in 194.
- **Nothing else changes.** The producer keeps sending the cumulative value
  (CD-27) for anyone else who wants it. No contract change, no client release.

## What shipped

- `src/netstate.{c,h}`: `net_rx_text()`.
- `src/widgets/net_card.c`: draws the line through it, in a 120 px box.
- `tests/test_netstate.c`: `test_rx_errors_show_the_24h_delta`, which covers
  kubs0's real shape (587735 / 0 → `rx err 0`), a live delta (`rx err 1240`), a
  null delta, a null pair, and a host that has never published.

Gates: `just check` and `just check-warnings-full`, both green.

## Repaired in passing

Nothing.

## Deployed

2026-10-26 19:13 PDT, from kai, to rpi53.

- `scripts/deploy.sh`. The dashboard reports version `8413120`, the service is
  active with `NRestarts=0`, and the aarch64 link printed no warnings
  (`.scratch/deploy.log`, grepped).
- **Live on the panel:** capture `img-1000` is on WI 4090. kubs0's column now
  reads `LFC 28 · rx err 0`, unclipped. All five columns read `rx err 0`, and
  kai still shows `LFC 2 (+1)` in yellow from Ken's cable pull.
- **Cross-checked against the key:** `GET kdash:net:kubs0` on rpi53 returned
  `rx_errors 587735, rx_errors_delta_24h 0`. So the `0` on the panel is the
  delta, not a parse failure.
