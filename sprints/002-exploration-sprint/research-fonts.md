# Research: Custom Fonts for kpidash

**Date**: 2026-04-03
**Goal**: Enable bold/heavy weight fonts and Nerd Font icons in the LVGL dashboard.

---

## Problem

LVGL's built-in Montserrat fonts are Regular weight only (no Bold). The dashboard needs:
1. **Bold text** for hostnames, headers, emphasis
2. **Larger readable text** for 3840×2160 display at ~2m viewing distance
3. **Icon glyphs** (OS logos, device icons, status indicators) from Nerd Fonts
4. Both **proportional** (labels, hostname) and **monospace** (times, code) variants

---

## Tool: `lv_font_conv`

Installed via npm (`npx lv_font_conv`). Converts TTF/OTF → LVGL C source.

### Key options

| Option | Value | Notes |
|--------|-------|-------|
| `--bpp` | `4` | 4-bit anti-aliasing (16 gray levels). Best quality/size tradeoff. |
| `--size` | pixels | Output font size. Need one conversion per size. |
| `--format` | `lvgl` | Generates a `.c` file with `lv_font_t` structure. |
| `--font` | path | Source TTF. **Can be repeated** to merge glyphs from multiple fonts. |
| `-r` / `--range` | hex ranges | Glyph ranges to include from the preceding `--font`. |
| `--symbols` | chars | Alternative to `-r`: list literal characters to include. |
| `--lv-font-name` | name | C variable name (e.g. `lv_font_caskaydia_bold_20`). |
| `--lv-fallback` | name | Fallback font variable (chains to another `lv_font_t`). |
| `--no-compress` | — | Disable RLE compression (larger but faster rendering). |

### Font merging

Multiple `--font` flags in one command merge glyphs into a **single** `lv_font_t`. This lets us combine a text font (Montserrat, CaskaydiaCove) with an icon font (Nerd Font Symbols) in one output file — no runtime fallback chain needed.

```sh
npx lv_font_conv --bpp 4 --size 20 --format lvgl \
  --font Montserrat-Bold.ttf -r 0x20-0x7E \
  --font SymbolsNerdFont-Regular.ttf -r 0xF300-0xF381 \
  -o lv_font_montserrat_bold_20.c --lv-font-name lv_font_montserrat_bold_20
```

### Fallback chains

LVGL also supports runtime fallback: `--lv-fallback lv_font_montserrat_20` makes the generated font fall back to the built-in Montserrat 20 for any missing glyphs. Useful for incremental adoption — custom fonts can contain only Bold + icons, falling back to built-in Regular for everything else.

---

## Font Source Options

### Option A: Montserrat Bold + Nerd Font Symbols (Recommended starting point)

**Montserrat** is already the LVGL built-in font family. LVGL ships Bold and SemiBold TTFs in `lib/lvgl/demos/multilang/assets/fonts/`:
- `Montserrat-Bold.ttf` (weight 700)
- `Montserrat-SemiBold.ttf` (weight 600)

**Pros**:
- Zero visual disruption — same font family as existing Regular weights
- Proportional (good for labels, hostnames, natural text)
- Bold provides the emphasis we need for hostnames/headers
- TTFs already in our tree — no external download needed
- Can merge with `SymbolsNerdFont-Regular.ttf` for icons

**Cons**:
- Not monospaced (not ideal for time displays, aligned numbers)
- No coding ligatures

**File sizes at 4bpp** (measured):

| Content | Size (20px) |
|---------|-------------|
| ASCII only (0x20-0x7E) | 59 KB |
| ASCII + Powerline + Font Logos | 214 KB |
| ASCII + 5 icon ranges (~1200 icons) | 1.1 MB |

### Option B: CaskaydiaCove Nerd Font (Cascadia Code + Nerd glyphs)

**Source**: Nerd Fonts v3.4.0 release, asset name `CascadiaCode.tar.xz` (3.4 MB).
Based on Microsoft's Cascadia Code (SIL Open Font License).

**Available variants** (36 TTF files):

| Variant | Description |
|---------|-------------|
| `CaskaydiaCoveNerdFont-*` | Double-width Nerd icons |
| `CaskaydiaCoveNerdFontMono-*` | Single-width (monospaced) Nerd icons |
| `CaskaydiaCoveNerdFontPropo-*` | Proportional Nerd icons |

**Available weights** (each variant × 6 weights × italic):

| Weight | Style |
|--------|-------|
| ExtraLight | 200 |
| Light | 300 |
| SemiLight | 350 |
| Regular | 400 |
| SemiBold | 600 |
| Bold | 700 |

Each weight has a regular and italic variant (12 files per variant, 36 total).

**Pros**:
- Full weight range from ExtraLight to Bold
- Nerd Font icons already embedded — no merging needed
- Coding ligatures (CaskaydiaCove) or clean mono (CaskaydiaMono)
- Modern, highly readable monospace font designed for screens
- SIL OFL license — free for any use

**Cons**:
- Monospaced — wider than proportional for natural text
- Different visual feel from current Montserrat (user sees a font change)
- Larger file sizes due to icon range already embedded (but we control which ranges to extract via `lv_font_conv -r`)

### Option C: Nerd Fonts SymbolsOnly (icons only, pair with any text font)

**Source**: `NerdFontsSymbolsOnly.tar.xz` from release. Contains:
- `SymbolsNerdFont-Regular.ttf` — proportional icon widths
- `SymbolsNerdFontMono-Regular.ttf` — fixed-width icons

**Usage**: Merge with any text font via `lv_font_conv --font ... --font SymbolsNerdFont-Regular.ttf -r <ranges>`.

This is the building block for Option A (or any hybrid).

### Option D: Hybrid — Montserrat Bold (text) + CaskaydiaCove Bold (mono) + Symbols

Use **Montserrat Bold** for proportional labels (hostname, OS name, disk labels) and **CaskaydiaCove Bold Mono** for fixed-width contexts (uptime, elapsed times). Add Nerd Font Symbols to whichever is needed. Two font files, best of both worlds.

---

## Nerd Font Icon Ranges

| Range | Name | Glyphs | Dashboard use |
|-------|------|--------|---------------|
| `0xE0A0-0xE0D4` | Powerline | 53 | Separators, arrows |
| `0xE200-0xE2A9` | FA Extension | 170 | Extra status icons |
| `0xE300-0xE3E3` | Weather | 228 | Optional: weather display |
| `0xE5FA-0xE6B7` | Seti-UI | 190 | File type icons |
| `0xE700-0xE7C5` | Devicons | 198 | Dev tool / language icons |
| `0xEA60-0xEBEB` | Codicons | 396 | VS Code icons (git, terminal, etc.) |
| `0xF000-0xF2E0` | Font Awesome | 737 | General UI icons (server, hdd, cpu, etc.) |
| `0xF300-0xF381` | Font Logos | 130 | **OS logos** (Linux, Ubuntu, Windows, Apple) |
| `0xF400-0xF533` | Octicons | 308 | GitHub icons (repo, PR, issue, etc.) |

**Recommended minimal set for kpidash**:
- Font Logos (`0xF300-0xF381`) — OS icons next to hostname
- Codicons subset — git status, terminal, server icons
- Font Awesome subset — HDD, CPU, memory, network icons

**Recommended practical set** (curated individual codepoints instead of full ranges):
Start with specific icons and expand as needed. Each icon at 20px 4bpp ≈ 200-400 bytes.

### Useful specific icons for kpidash

```
# Font Logos (OS icons)
0xF31B  Linux (Tux)
0xF31A  Ubuntu
0xF302  Apple
0xF17A  Windows (via FA)

# Font Awesome (system icons)
0xF0A0  HDD
0xF233  Server
0xF2DB  Microchip (CPU)
0xF0E8  Network (sitemap)
0xF1C0  Database
0xF0AD  Wrench (settings)
0xF011  Power
0xF017  Clock
0xF071  Warning triangle
0xF058  Check circle
0xF06A  Exclamation circle

# Codicons (dev/git)
0xEA68  Git branch
0xEA62  Terminal
0xEAD8  Activity
```

---

## Critical Findings (from implementation)

### 1. `--no-compress --no-prefilter` required for LVGL 9.2

By default `lv_font_conv` generates fonts with `bitmap_format = 1` (RLE compressed).
LVGL 9.2's built-in Montserrat fonts all use `bitmap_format = 0` (uncompressed).
The 9.2 font renderer **does not support RLE-compressed custom fonts** — they render as blank rectangles.

**Fix**: Always pass `--no-compress --no-prefilter` to `lv_font_conv`.

### 2. Guard macro definitions required in CMake

`lv_font_conv` wraps each generated `.c` file in a preprocessor guard:

```c
#ifdef LV_FONT_MONTSERRAT_BOLD_48
// ... all font data ...
#endif
```

Without defining these macros, the entire font data is compiled out and the `lv_font_t` symbol is missing at link time (or present but empty, causing blank rectangles).

**Fix**: Add `target_compile_definitions` in CMakeLists.txt for every generated font:

```cmake
target_compile_definitions(kpidash PRIVATE
    LV_FONT_MONTSERRAT_BOLD_14=1
    LV_FONT_MONTSERRAT_BOLD_16=1
    # ... etc
)
```

### 3. Correct generate command template

```sh
npx lv_font_conv --bpp 4 --size 20 --format lvgl \
  --no-compress --no-prefilter \
  --font Montserrat-Bold.ttf -r 0x20-0x7E \
  --font SymbolsNerdFont-Regular.ttf -r 0xF300-0xF381 \
  -o lv_font_montserrat_bold_20.c \
  --lv-font-name lv_font_montserrat_bold_20
```

### 4. File sizes (actual, uncompressed 4bpp)

| Size (px) | File size |
|-----------|-----------|
| 14        | 124 KB    |
| 16        | 144 KB    |
| 20        | 196 KB    |
| 24        | 256 KB    |
| 28        | 316 KB    |
| 36        | 440 KB    |
| 48        | 547 KB    |
| **Total** | **~2 MB** |

---

## Recommended Approach

### Phase 1: Montserrat Bold + Nerd Font Symbols (minimal disruption)

1. Generate `lv_font_montserrat_bold_20.c` — Montserrat Bold (ASCII 0x20-0x7E) + select Nerd Font icons
2. Generate `lv_font_montserrat_bold_24.c` — Same at 24px for larger headers
3. Add generated `.c` files to `src/fonts/` and `CMakeLists.txt`
4. Use Bold for: hostname labels, section headers, emphasis text
5. Keep built-in Regular for: body text, values, muted labels

**Estimated flash cost**: ~120-250 KB total (2 sizes × ASCII + 20-30 icons).

### Phase 2 (optional): Add CaskaydiaCove for monospace

If monospace is wanted for times/numbers:
1. Generate `lv_font_caskaydia_mono_16.c` — CaskaydiaCove Mono Regular (ASCII)
2. Use for: uptime, elapsed times, disk sizes, percentages

### Build integration

Add a `fonts/` directory with:
- Source TTFs (or symlinks to `lib/lvgl/demos/...` and `/tmp/symbols/`)
- A `generate.sh` script with the `lv_font_conv` commands
- Generated `.c` files committed to repo (so cross-compile doesn't need Node.js)

```
fonts/
├── generate.sh           # Regenerate .c files from TTF sources
├── ttf/                  # Source TTFs (Montserrat-Bold, SymbolsNerdFont, etc.)
├── lv_font_montserrat_bold_20.c
├── lv_font_montserrat_bold_24.c
└── lv_font_caskaydia_mono_16.c  (optional)
```

---

## Size Budget

Pi 5 has 4-8 GB RAM; the binary is loaded into memory. Current LVGL built-in fonts (6 sizes of Montserrat Regular) are already ~600 KB compiled. Adding 2-3 custom fonts at ~100-250 KB each is negligible.

| Font | Glyphs | Est. size |
|------|--------|-----------|
| Montserrat Bold 20px (ASCII + ~20 icons) | ~115 | ~80 KB |
| Montserrat Bold 24px (ASCII + ~20 icons) | ~115 | ~110 KB |
| Montserrat Bold 28px (ASCII only) | ~95 | ~100 KB |
| CaskaydiaCove Mono 16px (ASCII only) | ~95 | ~50 KB |
| **Total** | | **~340 KB** |

---

## Key Decisions Needed

1. **Font family**: Stick with Montserrat Bold (proportional, matches existing) or switch to CaskaydiaCove (monospace, full weight range, Nerd icons built in)?
2. **Icon scope**: Minimal (OS logos + ~10 status icons) or broad (full Font Awesome + Codicons)?
3. **Sizes needed**: Which pixel sizes to generate? Current dashboard uses 14, 16, 20, 24, 28, 36.
4. **Directory structure**: `src/fonts/` or `fonts/` at project root?

---

## References

- [Nerd Fonts](https://www.nerdfonts.com/) — v3.4.0, 67+ patched font families, 10,000+ icons
- [Nerd Fonts Cheat Sheet](https://www.nerdfonts.com/cheat-sheet) — searchable icon browser
- [Cascadia Code](https://github.com/microsoft/cascadia-code) — Microsoft, SIL OFL, weights 200-700
- [lv_font_conv](https://github.com/lvgl/lv_font_conv) — LVGL font converter (npm package)
- [LVGL Font Docs](https://docs.lvgl.io/9.2/overview/font.html) — font loading, fallback chains
