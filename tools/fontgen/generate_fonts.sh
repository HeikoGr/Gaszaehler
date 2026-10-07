#!/usr/bin/env bash
# Regenerates lib/ui/fonts/*.h from the Inter font family (https://rsms.me/inter/, SIL Open Font License 1.1).
# Usage: tools/fontgen/generate_fonts.sh <directory with Inter TTF files>
#   The TTFs are in Inter-4.1.zip (extras/ttf/) from https://github.com/rsms/inter/releases
#   Requires Pillow: pip install pillow
set -euo pipefail
TTF_DIR=${1:?usage: $0 <directory with Inter-*.ttf>}
cd "$(dirname "$0")/../.."
OUT=lib/ui/fonts
LIC="SIL Open Font License 1.1"
ASCII=$(python3 -c 'print("".join(chr(c) for c in range(32, 127)))')
TEXT="${ASCII}³·›…–°äöüÄÖÜß"

python3 tools/fontgen/fontgen.py --license "$LIC" --ttf "$TTF_DIR/Inter-Regular.ttf"  --size 12 --name font_small --chars "$TEXT" --tabular-digits --out $OUT/font_small.h
python3 tools/fontgen/fontgen.py --license "$LIC" --ttf "$TTF_DIR/Inter-Medium.ttf"   --size 17 --name font_body  --chars "$TEXT" --tabular-digits --out $OUT/font_body.h
python3 tools/fontgen/fontgen.py --license "$LIC" --ttf "$TTF_DIR/Inter-SemiBold.ttf" --size 15 --name font_title --chars "$TEXT" --out $OUT/font_title.h
python3 tools/fontgen/fontgen.py --license "$LIC" --ttf "$TTF_DIR/InterDisplay-SemiBold.ttf" --size 30 --name font_digits --chars "0123456789.,-: " --tabular-digits --out $OUT/font_digits.h
python3 tools/fontgen/fontgen.py --license "$LIC" --ttf "$TTF_DIR/InterDisplay-SemiBold.ttf" --size 38 --name font_big --chars "0123456789.,- " --tabular-digits --out $OUT/font_big.h
