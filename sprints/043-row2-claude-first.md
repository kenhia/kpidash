# 043 — Row 2 order: Claude usage first

Proposal korg:4756 covers WI 4752. It ran as a karc leg (kpidash-807cf8) on
kai, with the overseer on cleo. Branch `043-row2-claude-first`.

## Goal

Ken, 2026-11-14: move the Claude usage (limits) card to the start of row 2,
so the row reads Claude usage, All-10G, NAS, korg. "Row 2" counts from one;
in code it is `ROW_Y(1)`.

| Card          | Before        | After         |
|---------------|---------------|---------------|
| Claude limits | column 5      | column 0      |
| 10G           | columns 0-1   | columns 1-2   |
| NAS           | columns 2-3   | columns 3-4   |
| korg + klams  | column 4      | column 5      |

Positions only. No card changes size or content.

## Premise check

- **The premise holds.** `src/ui.c` placed the four cards at `COL_X(0)`,
  `COL_X(2)`, `COL_X(4)` and `COL_X(5)`, as the WI says.
- **Nothing else assumed the old columns in code.** `tests/test_layout.c`
  checks only the grid macros (`COL_X`, `ROW_Y`), not card positions. No widget
  reads its own x, and every row-1 card sizes itself.

## What shipped

- `src/ui.c`: the four `lv_obj_set_pos` calls moved, and the limits card is
  now created first, so the code reads in screen order. The cards do not
  overlap, so the creation order changes no drawing.
- The 10G card keeps its zero `CELL_PAD` (it fills its two cells). The other
  three keep theirs.
- Every slot is still fixed. The limits card stays hidden until
  `claude:limits` has published, and the NAS card until `kdash:nas` has.
  Their slots stay reserved either way, so no card moves when another one
  appears. Before the first `claude:limits` publish, column 0 is now an empty
  cell at the left of the row, not the right.
- Docs: the layout diagrams in `README.md`, `docs/ARCHITECTURE.md` and the
  header of `src/ui.c`, plus every "row 1, column N" mention in
  `docs/ARCHITECTURE.md`, `docs/CLIENT-PROTOCOL.md` §8f and the four widget
  headers.

## Repaired in passing

- The `src/ui.c` header diagram still showed row 2 as "(free)", although
  sprints 040-042 had filled it with the agents and calendar cards. It now
  shows them. The fix is a comment, checked by eye.
