#!/usr/bin/env python3
"""Generate the on-demand SUBSET of ui/src/font_data.c (stdlib only).

The full dot-matrix font (~25 KB Flash) is dominated by CJK glyphs needed
only for dynamic phone messages. The static UI needs ASCII plus a few
dozen CJK. This script:

  1. Parses the glyphs[] table {cp,w,h,advance,offset} and the 1bpp bits[]
     blob from the full font table.
  2. Builds a whitelist = ASCII 0x20..0x7E PLUS every non-ASCII codepoint
     that appears inside C string literals in ui/src/*.c and host/*.c
     (UTF-8 decoded; comments and char literals are ignored).
     Exception: host/gen_font.c is skipped -- it embeds the generator's
     complete ~520-char CJK charset as string literals, which would make
     the "subset" identical to the full table. Its strings are build-time
     console text, never rendered by the watch UI.
  3. Writes the ORIGINAL untouched full table to ui/src/font_data_full.c
     and overwrites ui/src/font_data.c with the subset (bits blob rebuilt
     by concatenating only the selected glyphs, offsets recomputed).
     ui_font_find / ui_font_bits are copied verbatim, so signatures and
     the binary search (glyphs stay sorted by cp) are unchanged.

Idempotent: if font_data_full.c already exists it is used as the canonical
full source, so re-running never shrinks the whitelist input.

Build flags: default builds link the subset (font_data.c); passing `full`
to host/build.bat or stm32/build.bat links font_data_full.c instead.
"""

import glob
import os
import re
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UI_SRC = os.path.join(ROOT, "ui", "src")
HOST_DIR = os.path.join(ROOT, "host")
FULL_PATH = os.path.join(UI_SRC, "font_data_full.c")
SUBSET_PATH = os.path.join(UI_SRC, "font_data.c")

# Static-UI CJK that must survive subsetting (sanity check only).
REQUIRED_CJK = "运动消息设置调试模式亮度关于版本表盘低中高开关状态灯闪烁传感器数值"

HEX = "0123456789abcdefABCDEF"
GLYPH_STRUCT_SIZE = 12  # uint32 + 3*uint8 + pad + uint16, aligned to 4


def fail(msg):
    sys.stderr.write("subset_font: ERROR: %s\n" % msg)
    sys.exit(1)


# ---------------------------------------------------------------- parsing

def parse_font(text):
    """Return (header, bits_bytes, glyphs, tail) from a font_data.c text."""
    mb = re.search(r"static\s+const\s+uint8_t\s+bits\s*\[\s*\]\s*=\s*\{", text)
    if not mb:
        fail("bits[] declaration not found")
    bits_open = mb.end()
    bits_close = text.index("};", bits_open)
    bits_body = text[bits_open:bits_close]

    mg = re.search(r"static\s+const\s+ui_glyph_t\s+glyphs\s*\[\s*\]\s*=\s*\{", text)
    if not mg or mg.start() < bits_close:
        fail("glyphs[] declaration not found after bits[]")
    glyphs_open = mg.end()
    glyphs_close = text.index("};", glyphs_open)
    glyphs_body = text[glyphs_open:glyphs_close]

    header = text[:mb.start()]
    tail = text[glyphs_close + 2:]

    # bits: strip /* U+XXXX */ comments, then every token must be a byte.
    stripped = re.sub(r"/\*.*?\*/", "", bits_body, flags=re.S)
    if re.sub(r"0[xX][0-9a-fA-F]{2}|[,\s]", "", stripped):
        fail("unexpected tokens in bits[] body")
    bits = bytes(int(h, 16) for h in re.findall(r"0[xX]([0-9a-fA-F]{2})", stripped))

    # glyphs: {0x0020u, 8, 16, 8, 0},
    entry_re = re.compile(
        r"\{\s*0[xX]([0-9a-fA-F]+)u?\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}")
    glyphs = []
    for m in entry_re.finditer(glyphs_body):
        cp, w, h, adv, off = (int(x, 16) if i == 0 else int(x)
                              for i, x in enumerate(m.groups()))
        glyphs.append({"cp": cp, "w": w, "h": h, "advance": adv,
                       "offset": off, "size": ((w + 7) // 8) * h})
    if not glyphs:
        fail("no glyph entries parsed")
    if re.sub(r"\{[^{}]*\}|[,\s]", "", glyphs_body):
        fail("unexpected tokens in glyphs[] body")

    # Validate the full table: sorted cps, contiguous offsets, exact size.
    pos = 0
    prev_cp = -1
    for g in glyphs:
        if g["cp"] <= prev_cp:
            fail("glyphs[] not strictly sorted at U+%04X" % g["cp"])
        prev_cp = g["cp"]
        if g["offset"] != pos:
            fail("offset mismatch at U+%04X: %d != %d" % (g["cp"], g["offset"], pos))
        if g["offset"] + g["size"] > len(bits):
            fail("bits[] too short for U+%04X" % g["cp"])
        pos += g["size"]
    if pos != len(bits):
        fail("bits[] length %d != sum of glyph sizes %d" % (len(bits), pos))
    return header, bits, glyphs, tail


# ------------------------------------------------- string-literal scanner

def collect_string_codepoints(text):
    """Codepoints > 0x7F inside C string literals (comments/chars skipped)."""
    cps = set()
    i, n, state = 0, len(text), "code"
    while i < n:
        c = text[i]
        if state == "code":
            if c == "/" and i + 1 < n and text[i + 1] == "/":
                state, i = "line", i + 2
            elif c == "/" and i + 1 < n and text[i + 1] == "*":
                state, i = "block", i + 2
            elif c == '"':
                state, i = "str", i + 1
            elif c == "'":
                state, i = "chr", i + 1
            else:
                i += 1
        elif state == "line":
            if c == "\n":
                state = "code"
            i += 1
        elif state == "block":
            if c == "*" and i + 1 < n and text[i + 1] == "/":
                state, i = "code", i + 2
            else:
                i += 1
        else:  # str / chr
            if c == "\\":
                e = text[i + 1] if i + 1 < n else ""
                if state == "str" and e in ("u", "U"):
                    width = 4 if e == "u" else 8
                    hx = text[i + 2:i + 2 + width]
                    if len(hx) == width and all(ch in HEX for ch in hx):
                        cp = int(hx, 16)
                        if cp > 0x7F:
                            cps.add(cp)
                        i += 2 + width
                        continue
                if state == "str" and e == "x":
                    # Runs of \xHH are raw bytes (often UTF-8 text, e.g.
                    # "\xE8\xBF\x90" == U+8FD0); decode the run as UTF-8.
                    j, buf = i, bytearray()
                    while j + 1 < n and text[j] == "\\" and text[j + 1] == "x":
                        hx = text[j + 2:j + 4]
                        if len(hx) != 2 or not all(ch in HEX for ch in hx):
                            break
                        buf.append(int(hx, 16))
                        j += 4
                    if buf:
                        try:
                            for ch in buf.decode("utf-8"):
                                if ord(ch) > 0x7F:
                                    cps.add(ord(ch))
                        except UnicodeDecodeError:
                            pass  # not text (protocol bytes, partial seq)
                        i = j
                        continue
                i += 2  # ordinary escape (\n, \", \\, escaped newline, ...)
            elif (state == "str" and c == '"') or (state == "chr" and c == "'"):
                state, i = "code", i + 1
            else:
                if state == "str" and ord(c) > 0x7F:
                    cps.add(ord(c))
                i += 1
    return cps


def build_whitelist():
    whitelist = set(range(0x20, 0x7F))
    files = sorted(glob.glob(os.path.join(UI_SRC, "*.c"))) + \
        sorted(glob.glob(os.path.join(HOST_DIR, "*.c")))
    files = [p for p in files if os.path.basename(p) != "gen_font.c"]
    for path in files:
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
        whitelist |= collect_string_codepoints(text)
    return whitelist, files


# ---------------------------------------------------------------- emitting

def emit_subset(header, bits, glyphs, tail, whitelist):
    selected = [g for g in glyphs if g["cp"] in whitelist]
    if not selected:
        fail("whitelist selected zero glyphs")

    new_bits = bytearray()
    entries = []
    off = 0
    for g in selected:
        new_bits += bits[g["offset"]:g["offset"] + g["size"]]
        entries.append((g["cp"], g["w"], g["h"], g["advance"], off, g["size"]))
        off += g["size"]

    note = ("/* SUBSET table generated by host/subset_font.py -- do not edit by hand.\n"
            "   Keeps ASCII 0x20..0x7E plus the non-ASCII codepoints used in string\n"
            "   literals of ui/src/*.c and host/*.c. The complete table lives in\n"
            "   font_data_full.c; link it with the \"full\" build flag. */\n\n")

    lines = [header.rstrip("\n"), "", note.rstrip("\n"), "",
             "static const uint8_t bits[] = {"]
    pos = 0
    for cp, w, h, adv, o, size in entries:
        lines.append("    /* U+%04X */" % cp)
        for k in range(0, size, 12):
            row = new_bits[pos + k:pos + min(k + 12, size)]
            lines.append("    " + ",".join("0x%02X" % b for b in row) + ",")
        pos += size
    lines.append("};")
    lines.append("")
    lines.append("static const ui_glyph_t glyphs[] = {")
    for cp, w, h, adv, o, size in entries:
        lines.append("    {0x%04Xu, %d, %d, %d, %d}," % (cp, w, h, adv, o))
    lines.append("};")
    text = "\n".join(lines) + tail
    return text, selected, bytes(new_bits)


def main():
    # Canonical full source: prefer the preserved full file on re-runs.
    if os.path.exists(FULL_PATH):
        src_path = FULL_PATH
    else:
        src_path = SUBSET_PATH
    with open(src_path, "r", encoding="utf-8", newline="") as f:
        original = f.read()
    if "\r\n" in original:
        fail("expected LF line endings in %s" % src_path)

    header, bits, glyphs, tail = parse_font(original)
    whitelist, scanned = build_whitelist()
    subset_text, selected, new_bits = emit_subset(header, bits, glyphs, tail, whitelist)

    # Self-check the emitted subset: re-parse and verify offsets/sizes.
    h2, b2, g2, t2 = parse_font(subset_text)
    if t2 != tail:
        fail("trailing ui_font_find/ui_font_bits code changed")
    if [g["cp"] for g in g2] != [g["cp"] for g in selected]:
        fail("subset glyph list mismatch after re-parse")

    have = {g["cp"] for g in selected}
    missing_required = [c for c in REQUIRED_CJK if ord(c) not in have]
    if missing_required:
        fail("static UI CJK missing from subset: " +
             " ".join("U+%04X" % ord(c) for c in missing_required))
    absent = sorted(cp for cp in whitelist if cp > 0x7F and cp not in
                    {g["cp"] for g in glyphs})

    # Preserve the original full file byte-for-byte, then write the subset.
    if src_path == SUBSET_PATH:
        shutil.copyfile(SUBSET_PATH, FULL_PATH)
    with open(SUBSET_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write(subset_text)

    full_total = len(bits) + len(glyphs) * GLYPH_STRUCT_SIZE
    sub_total = len(new_bits) + len(selected) * GLYPH_STRUCT_SIZE
    print("scanned %d C files for string literals" % len(scanned))
    print("whitelist: %d ASCII + %d non-ASCII codepoints" %
          (0x7F - 0x20, len(whitelist) - (0x7F - 0x20)))
    print("full   : %4d glyphs | bits %6d B | glyphs[] %5d B | total %6d B" %
          (len(glyphs), len(bits), len(glyphs) * GLYPH_STRUCT_SIZE, full_total))
    print("subset : %4d glyphs | bits %6d B | glyphs[] %5d B | total %6d B" %
          (len(selected), len(new_bits), len(selected) * GLYPH_STRUCT_SIZE, sub_total))
    print("saved  : %d glyphs, %d Flash bytes (~%.1f KB)" %
          (len(glyphs) - len(selected), full_total - sub_total,
           (full_total - sub_total) / 1024.0))
    if absent:
        print("note: %d whitelisted codepoints absent from the font "
              "(rendered as hollow boxes), e.g. %s" %
              (len(absent), " ".join("U+%04X" % cp for cp in absent[:12])))
    print("static UI CJK check: OK (%d chars present)" % len(REQUIRED_CJK))
    print("wrote %s" % FULL_PATH)
    print("wrote %s" % SUBSET_PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())