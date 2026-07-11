#!/usr/bin/env bash
# fonts/generate.sh — Generate LVGL C font files from TTF sources
#
# Prerequisites: npx lv_font_conv (npm install lv_font_conv)
# Usage: cd fonts && bash generate.sh
#
# Generated .c files are committed to the repo so cross-compile
# does not require Node.js.

set -euo pipefail
cd "$(dirname "$0")"

BOLD_TTF="ttf/Montserrat-Bold.ttf"
SYMBOLS_TTF="ttf/SymbolsNerdFont-Regular.ttf"
BPP=4
# 0x20-0x7E: ASCII printable. 0xB0: ° DEGREE SIGN (temp cards, WI #363).
RANGE="0x20-0x7E,0xB0"

# Sizes to generate (match built-in Montserrat Regular sizes + extras)
SIZES=(14 16 20 24 28 36 48)

for sz in "${SIZES[@]}"; do
    name="lv_font_montserrat_bold_${sz}"
    out="${name}.c"
    echo "Generating ${out} ..."
    npx lv_font_conv \
        --bpp "${BPP}" \
        --size "${sz}" \
        --format lvgl \
        --no-compress \
        --no-prefilter \
        --font "${BOLD_TTF}" -r "${RANGE}" \
        --font "${SYMBOLS_TTF}" -r 0xF300-0xF381 \
        --font "${SYMBOLS_TTF}" -r 0xF1D2-0xF1D3 \
        -o "${out}" \
        --lv-font-name "${name}"
done

echo "Done. Generated ${#SIZES[@]} font files."

# Sprint 006 / T010: large icon-only font for service status cards.
# Symbols-only (no ASCII); used by widgets/service_card.c.
echo "Generating lv_font_icons_56.c ..."
npx lv_font_conv \
    --bpp "${BPP}" \
    --size 56 \
    --format lvgl \
    --no-compress \
    --no-prefilter \
    --font "${SYMBOLS_TTF}" -r 0xF300-0xF381 \
    --font "${SYMBOLS_TTF}" -r 0xF1D2-0xF1D3 \
    -o "lv_font_icons_56.c" \
    --lv-font-name "lv_font_icons_56"
echo "Done. Generated lv_font_icons_56.c."
