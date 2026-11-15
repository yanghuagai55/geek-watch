"""Regenerate the Flutter UI's Source Han Serif subset.

Chinese body text uses Source Han Serif SemiBold (renamed to AT32 Serif SC for
OFL compliance); large numbers/headings use Playfair Display. This script
scans Dart string literals for every character the UI can render, rebuilds
assets/fonts/subset_chars.txt, subsets the original OTF, and renames the
internal family to AT32 Serif SC.

Usage:
    pip install fonttools
    python tool/make_font_subset.py --source <SourceHanSerifSC-SemiBold.otf>

The firmware font (ui/src/font_data.c) is a different bitmap font and is not
touched here.
"""

import argparse
import pathlib
import re

from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
CHARS_PATH = ROOT / "assets" / "fonts" / "subset_chars.txt"
FONT_OUT = ROOT / "assets" / "fonts" / "AT32SerifSC-SemiBold.otf"
STRING_RE = re.compile(r"""r?'([^'\\]|\\.)*'|r?"([^"\\]|\\.)*\"""", re.S)
RENAME = {
    "Source Han Serif SC": "AT32 Serif SC",
    "SourceHanSerifSC": "AT32SerifSC",
    "Source Han Serif": "AT32 Serif",
    "思源宋体": "AT32 Serif SC",
}


def collect_chars() -> set[str]:
    chars: set[str] = set()
    for folder in (ROOT / "lib", ROOT / "packages" / "prism_ui" / "lib"):
        for path in folder.rglob("*.dart"):
            text = path.read_text(encoding="utf-8")
            for match in STRING_RE.finditer(text):
                chars.update(ch for ch in match.group(0) if ord(ch) >= 0x20)
    chars.update(chr(code) for code in range(32, 127))
    chars.update("…·—‘’“”《》〈〉「」『』【】、，。！？；：（）％＃＠")
    return {ch for ch in chars if ord(ch) >= 0x20 and ch not in "\r\n\t"}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, help="原版 SourceHanSerifSC-SemiBold.otf")
    args = parser.parse_args()

    chars = collect_chars()
    CHARS_PATH.write_text("".join(sorted(chars)), encoding="utf-8")
    print(f"chars: {len(chars)}")

    subset.main([
        args.source,
        f"--text-file={CHARS_PATH}",
        "--no-hinting",
        f"--output-file={FONT_OUT}",
    ])

    font = TTFont(FONT_OUT)
    for record in font["name"].names:
        if record.nameID not in {1, 3, 4, 6, 16, 17, 21, 22}:
            continue
        try:
            text = record.toUnicode()
        except Exception:
            continue
        renamed = text
        for old, new in RENAME.items():
            renamed = renamed.replace(old, new)
        if renamed != text:
            record.string = renamed.encode(record.getEncoding())
    if "CFF " in font:
        font["CFF "].cff.fontNames[0] = "AT32SerifSC-SemiBold"
    font.save(FONT_OUT)
    print(f"output: {FONT_OUT} ({FONT_OUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
