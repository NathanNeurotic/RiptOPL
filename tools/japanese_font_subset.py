#!/usr/bin/env python3
"""Rebuild third_party/fonts/MPLUS1p-Regular-JIS.ttf, the Japanese font of the language pack.

The upstream language repository's font_Japanese.otf carries only the 325 characters the menu
strings use, so game titles drew boxes for most kanji and for the brackets, bullets and tildes
Japanese titles use. This font is M PLUS 1p Regular (SIL OFL 1.1, no Reserved Font Name), cut to
JIS X 0208 (both kanji levels, kana, punctuation and symbols) plus ASCII, Latin-1, the fullwidth
and halfwidth forms, circled numbers and Roman numerals. Hinting and layout tables are dropped:
the menu renders with FreeType and reads neither.

Source: https://raw.githubusercontent.com/google/fonts/main/ofl/mplus1p/MPLUS1p-Regular.ttf
  sha256 2f294ad496432b1608f070d310e3aa2adcf1de4af429f4901df97ec4bd361ed1
Needs fontTools (pip install fonttools; 4.68.0 made the committed file).

    python tools/japanese_font_subset.py MPLUS1p-Regular.ttf third_party/fonts/MPLUS1p-Regular-JIS.ttf
"""
import subprocess
import sys
import tempfile


def characters():
    out = set(range(0x20, 0x7F)) | set(range(0xA0, 0x100))
    for row in range(1, 85):  # JIS X 0208 rows 1-84, as EUC-JP byte pairs
        for cell in range(1, 95):
            try:
                out.add(ord(bytes([0xA0 + row, 0xA0 + cell]).decode("euc_jp")))
            except UnicodeDecodeError:
                pass
    out |= set(range(0xFF01, 0xFF5F)) | set(range(0xFF61, 0xFFA0))  # fullwidth and halfwidth forms
    out |= set(range(0x2460, 0x2474)) | set(range(0x2160, 0x216A))  # circled 1-20, Roman I-X
    out |= {0x301C, 0x2015, 0x2010, 0x3005, 0x30F4, 0x30F5, 0x30F6, 0x2122, 0x00A9, 0x00AE}
    return sorted(out)


def main(source, destination):
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as listing:
        listing.write("\n".join("U+%04X" % c for c in characters()))
    subprocess.run([sys.executable, "-m", "fontTools.subset", source, "--unicodes-file=" + listing.name,
                    "--no-hinting", "--layout-features=", "--name-IDs=*", "--notdef-outline",
                    "--output-file=" + destination], check=True)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
