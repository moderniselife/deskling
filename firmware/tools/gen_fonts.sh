#!/usr/bin/env bash
# Regenerates the LVGL fonts in main/ui/fonts/ from Nunito and JetBrains Mono (both SIL OFL).
# Needs: python3 + fonttools (pip install fonttools), node + lv_font_conv (npm i -g lv_font_conv).
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=main/ui/fonts
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

curl -sSfL -o "$WORK/Nunito-VF.ttf" "https://raw.githubusercontent.com/google/fonts/main/ofl/nunito/Nunito%5Bwght%5D.ttf"
curl -sSfL -o "$WORK/Mono-VF.ttf" "https://raw.githubusercontent.com/google/fonts/main/ofl/jetbrainsmono/JetBrainsMono%5Bwght%5D.ttf"

python3 - "$WORK" <<'EOF'
import sys
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
w = sys.argv[1]
for src, wght, dst in [("Nunito-VF.ttf", 800, "Nunito-800.ttf"), ("Nunito-VF.ttf", 900, "Nunito-900.ttf"), ("Nunito-VF.ttf", 1000, "Nunito-1000.ttf"), ("Mono-VF.ttf", 500, "Mono-500.ttf")]:
    f = TTFont(f"{w}/{src}")
    instantiateVariableFont(f, {"wght": wght}, inplace=True)
    f.save(f"{w}/{dst}")
EOF

# Printable ASCII plus the punctuation the UI uses: ° · … ’ “ ” – —
RANGE="0x20-0x7E,0xB0,0xB7,0x2026,0x2018,0x2019,0x201C,0x201D,0x2013,0x2014"
mkdir -p "$OUT"
gen() { # name ttf size range
  lv_font_conv --font "$WORK/$2" --size "$3" --bpp 4 --format lvgl --no-compress \
    --range "$4" --lv-include "lvgl.h" --lv-font-name "$1" -o "$OUT/$1.c"
  echo "  $1"
}
gen deskling_font_15 Nunito-800.ttf 15 "$RANGE"
gen deskling_font_17 Nunito-800.ttf 17 "$RANGE"
gen deskling_font_19 Nunito-800.ttf 19 "$RANGE"
gen deskling_font_19b Nunito-1000.ttf 19 "$RANGE" # **bold** in chat replies
gen deskling_font_22 Nunito-800.ttf 22 "$RANGE"
gen deskling_font_26 Nunito-900.ttf 26 "$RANGE"
gen deskling_font_34 Nunito-900.ttf 34 "$RANGE"
gen deskling_font_48 Nunito-900.ttf 48 "$RANGE"
gen deskling_font_72 Nunito-900.ttf 72 "0x20,0x30-0x3A,0x61,0x6D,0x70"
gen deskling_font_200 Nunito-900.ttf 200 "0x30-0x3A"
gen deskling_font_mono_22 Mono-500.ttf 22 "$RANGE"
gen deskling_font_mono_17 Mono-500.ttf 17 "$RANGE" # `code` in chat replies
# Fall back to LVGL's Montserrat for LV_SYMBOL_* icons (the fonts are const, so set it here).
for f in deskling_font_15 deskling_font_17 deskling_font_19 deskling_font_19b deskling_font_22; do
  sed -i.bak 's/\.fallback = NULL,/.fallback = \&lv_font_montserrat_20,/' "$OUT/$f.c" && rm -f "$OUT/$f.c.bak"
done
for f in deskling_font_26 deskling_font_34; do
  sed -i.bak 's/\.fallback = NULL,/.fallback = \&lv_font_montserrat_28,/' "$OUT/$f.c" && rm -f "$OUT/$f.c.bak"
done
echo "fonts written to $OUT"
