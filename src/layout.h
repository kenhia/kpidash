#ifndef LAYOUT_H
#define LAYOUT_H

/**
 * layout.h — Cell-based unit system for 3840×2160 dashboard grid.
 *
 * Each cell internalizes half the visual gap via CELL_PAD.
 * Adjacent widget content has 4+4 = 8px visual gap.
 * Multi-unit sizing: UNIT_W_N(n) = n * UNIT_W (no gap correction).
 *
 * Grid: 6 columns × 3 rows + footer.
 *   Horizontal: PAD_LEFT + 6×UNIT_W + PAD_RIGHT = 24 + 3792 + 24 = 3840
 *   Vertical:   PAD_TOP + 3×UNIT_H + FOOTER_H  = 0 + 1884 + 276 = 2160
 */

#include <stdint.h>

/* Screen resolution */
#define SCR_W 3840
#define SCR_H 2160

/* Grid dimensions */
#define COLS 6
#define ROWS 3

/* Cell size (gap-inclusive) */
#define UNIT_W 632
#define UNIT_H 628

/* Per-side widget inset within cell (adjacent content gap = 2 × CELL_PAD = 8px) */
#define CELL_PAD 4

/* Screen margins */
#define PAD_TOP 0
#define PAD_LEFT 24
#define PAD_RIGHT 24

/* Footer height below 3 unit rows */
#define FOOTER_H 276

/* ---- Composite size macros ---- */
#define UNIT_W_N(n) ((n)*UNIT_W)
#define UNIT_H_N(n) ((n)*UNIT_H)

/* ---- Position macros (0-indexed) ---- */
#define ROW_Y(r) (PAD_TOP + (r)*UNIT_H)
#define COL_X(c) (PAD_LEFT + (c)*UNIT_W)

/* ---- Compile-time layout validation ---- */
_Static_assert(PAD_LEFT + COLS * UNIT_W + PAD_RIGHT == SCR_W,
               "Horizontal layout must sum to SCR_W");
_Static_assert(PAD_TOP + ROWS * UNIT_H + FOOTER_H == SCR_H, "Vertical layout must sum to SCR_H");

#endif /* LAYOUT_H */
