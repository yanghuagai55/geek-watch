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
REQUIRED_CJK = "运动消息设置调试模式亮度关于表盘低中高开关状态灯闪烁传感器数值指南针东南西北"
# 常驻片内的常用中文（设置页全部字样 + 开关值）；其余中文位图放片外，按需读。
ONCHIP_CJK = "设置调试模式抬腕亮屏震动亮度蓝牙状态灯熄屏关于开关低中高常亮闪烁关闭秒分钟"

HEX = "0123456789abcdefABCDEF"
GLYPH_STRUCT_SIZE = 6  # uint16 cp + uint16 offset + uint8 w + uint8 h


def fail(msg):
    sys.stderr.write("subset_font: ERROR: %s\n" % msg)
    sys.exit(1)


# ---------------------------------------------------------------- parsing

def rle_encode_1bpp(raw):
    """PackBits RLE for 1bpp glyph bytes (ctrl>=0x80: repeat next byte (ctrl&0x7f)+2; else lit ctrl+1)."""
    out = bytearray()
    i, n = 0, len(raw)
    while i < n:
        # Check run >= 2 identical bytes
        run = 1
        while i + run < n and raw[i + run] == raw[i] and run < 129:
            run += 1
        if run >= 2:
            out.append(0x80 | (run - 2))
            out.append(raw[i])
            i += run
        else:
            lit_start = i
            lit_len = 1
            i += 1
            while i < n and lit_len < 128:
                if i + 1 < n and raw[i] == raw[i + 1]:
                    break
                lit_len += 1
                i += 1
            out.append(lit_len - 1)
            out.extend(raw[lit_start:lit_start + lit_len])
    return bytes(out)


def rle_decode_1bpp(comp, expected_len):
    out = bytearray()
    p = 0
    while len(out) < expected_len:
        ctrl = comp[p]
        p += 1
        if ctrl & 0x80:
            cnt = (ctrl & 0x7F) + 2
            v = comp[p]
            p += 1
            out.extend([v] * cnt)
        else:
            cnt = ctrl + 1
            out.extend(comp[p:p + cnt])
            p += cnt
    return bytes(out[:expected_len])


def trim_rle_encode_1bpp(raw, bpr):
    """Trim empty top/bottom rows + hybrid PackBits RLE matching unpack_font_rle."""
    h = len(raw) // bpr
    y0 = 0
    while y0 < h and all(b == 0 for b in raw[y0 * bpr:(y0 + 1) * bpr]):
        y0 += 1
    y1 = h
    while y1 > y0 and all(b == 0 for b in raw[(y1 - 1) * bpr:y1 * bpr]):
        y1 -= 1
    if y0 >= y1:
        return b"\xFF"
    bh = y1 - y0
    mid = raw[y0 * bpr:y1 * bpr]
    comp = rle_encode_1bpp(mid)
    if len(comp) < len(mid):
        return bytes([y0, (bh - 1) | 0x80]) + comp
    return bytes([y0, bh - 1]) + mid


def trim_rle_decode_1bpp(comp, expected_len, bpr):
    out = bytearray(expected_len)
    if comp[0] == 0xFF:
        return bytes(out)
    y0 = comp[0]
    h1 = comp[1]
    bh = (h1 & 0x0F) + 1
    start = y0 * bpr
    count = min(bh * bpr, expected_len - start)
    if h1 & 0x80:
        out[start:start + count] = rle_decode_1bpp(comp[2:], count)
    else:
        out[start:start + count] = comp[2:2 + count]
    return bytes(out)


def parse_font(text, compressed=False):
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

    # glyphs: {0x0020u, 0, 8, 16},
    entry_re = re.compile(
        r"\{\s*0[xX]([0-9a-fA-F]+)u?\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}")
    glyphs = []
    for m in entry_re.finditer(glyphs_body):
        cp, off, w, h = (int(x, 16) if i == 0 else int(x)
                         for i, x in enumerate(m.groups()))
        glyphs.append({"cp": cp, "w": w, "h": h, "advance": w,
                       "offset": off, "size": ((w + 7) // 8) * h})
    if not glyphs:
        fail("no glyph entries parsed")
    if re.sub(r"\{[^{}]*\}|[,\s]", "", glyphs_body):
        fail("unexpected tokens in glyphs[] body")

    prev_cp = -1
    if compressed:
        for idx, g in enumerate(glyphs):
            if g["cp"] <= prev_cp:
                fail("glyphs[] not strictly sorted at U+%04X" % g["cp"])
            prev_cp = g["cp"]
            if g["offset"] & 0x8000:
                continue  # 片外字模，偏移指向 font_cjk.bin
            end_off = glyphs[idx + 1]["offset"] if idx + 1 < len(glyphs) else len(bits)
            if end_off & 0x8000:
                continue
            if g["offset"] >= end_off or end_off > len(bits):
                fail("compressed span invalid at U+%04X" % g["cp"])
    else:
        pos = 0
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
    whitelist = set(range(0x20, 0x7F)) | {ord(c) for c in REQUIRED_CJK}
    files = sorted(glob.glob(os.path.join(UI_SRC, "*.c")))
    files = [p for p in files if os.path.exists(p)]
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
    ext_blob = bytearray()
    entries = []
    raw_by_cp = {}
    comp_by_cp = {}
    on_off = 0
    ext_off = 0
    for g in selected:
        raw = bits[g["offset"]:g["offset"] + g["size"]]
        bpr = (g["w"] + 7) // 8
        comp = trim_rle_encode_1bpp(raw, bpr)
        raw_by_cp[g["cp"]] = raw
        comp_by_cp[g["cp"]] = comp
        if g["cp"] < 0x80 or chr(g["cp"]) in ONCHIP_CJK:
            off = on_off
            new_bits += comp
            on_off += len(comp)
        else:
            off = 0x8000 | ext_off
            ext_blob += comp
            ext_off += len(comp)
        entries.append((g["cp"], g["w"], g["h"], off, len(comp)))

    note = ("/* SUBSET + Trim-RLE table generated by host/subset_font.py -- do not edit by hand.\n"
            "   On-chip bits = ASCII + 设置等常用字；其余中文位图在片外 font_cjk.bin，\n"
            "   offset bit15=1 表示片外偏移。font_data_full.c 为完整字库。*/\n\n")

    lines = [header.rstrip("\n"), "", note.rstrip("\n"), "",
             "static const uint8_t bits[] = {"]
    for cp, w, h, o, comp_len in entries:
        if o & 0x8000:
            continue
        lines.append("    /* U+%04X (rle %d B) */" % (cp, comp_len))
        for k in range(0, comp_len, 12):
            row = new_bits[o + k:o + min(k + 12, comp_len)]
            lines.append("    " + ",".join("0x%02X" % b for b in row) + ",")
    lines.append("};")
    lines.append("")
    lines.append("static const ui_glyph_t glyphs[] = {")
    for cp, w, h, o, comp_len in entries:
        lines.append("    {0x%04Xu, %d, %d, %d}," % (cp, o, w, h))
    lines.append("};")
    lines.append("")
    lines.append("uint16_t ui_font_ext_total(void) { return %d; }" % len(ext_blob))

    if re.search(r"int\s+ui_font_is_rle\s*\(\s*void\s*\)", tail):
        out_tail = re.sub(
            r"(int\s+ui_font_is_rle\s*\(\s*void\s*\)\s*\{\s*return\s+)0(\s*;\s*\})",
            r"\g<1>1\g<2>",
            tail,
        )
    else:
        out_tail = tail.rstrip("\n") + "\n\nint ui_font_is_rle(void) {\n    return 1;\n}\n"

    text = "\n".join(lines) + out_tail
    return text, selected, bytes(new_bits), out_tail, raw_by_cp, bytes(ext_blob), comp_by_cp


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
    subset_text, selected, new_bits, out_tail, raw_by_cp, ext_blob, comp_by_cp = emit_subset(
        header, bits, glyphs, tail, whitelist)

    # Self-check the emitted RLE subset: re-parse and verify lossless decode.
    h2, b2, g2, t2 = parse_font(subset_text, compressed=True)
    if not t2.endswith(out_tail):
        fail("trailing ui_font_find/ui_font_bits code changed unexpectedly")
    if [g["cp"] for g in g2] != [g["cp"] for g in selected]:
        fail("subset glyph list mismatch after re-parse")
    for g in selected:
        comp = comp_by_cp[g["cp"]]
        decoded = trim_rle_decode_1bpp(comp, g["size"], (g["w"] + 7) // 8)
        if decoded != raw_by_cp[g["cp"]]:
            fail("RLE round-trip mismatch at U+%04X" % g["cp"])

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
    ext_path = os.path.join(ROOT, "app", "assets", "blobs", "font_cjk.bin")
    os.makedirs(os.path.dirname(ext_path), exist_ok=True)
    with open(ext_path, "wb") as f:
        f.write(ext_blob)

    raw_sub_bytes = sum(g["size"] for g in selected)
    full_total = len(bits) + len(glyphs) * GLYPH_STRUCT_SIZE
    sub_total = len(new_bits) + len(selected) * GLYPH_STRUCT_SIZE
    print("scanned %d C files for string literals" % len(scanned))
    print("whitelist: %d ASCII + %d non-ASCII codepoints" %
          (0x7F - 0x20, len(whitelist) - (0x7F - 0x20)))
    print("full   : %4d glyphs | bits %6d B | glyphs[] %5d B | total %6d B" %
          (len(glyphs), len(bits), len(glyphs) * GLYPH_STRUCT_SIZE, full_total))
    print("subset : %4d glyphs | raw  %6d B | on-chip rle %5d B | ext %5d B | glyphs[] %5d B | total %6d B" %
          (len(selected), raw_sub_bytes, len(new_bits), len(ext_blob),
           len(selected) * GLYPH_STRUCT_SIZE, sub_total))
    print("saved  : %d glyphs, %d Flash bytes vs full (~%.1f KB), RLE saved %d B" %
          (len(glyphs) - len(selected), full_total - sub_total,
           (full_total - sub_total) / 1024.0, raw_sub_bytes - len(new_bits)))
    if absent:
        print("note: %d whitelisted codepoints absent from the font "
              "(rendered as hollow boxes), e.g. %s" %
              (len(absent), " ".join("U+%04X" % cp for cp in absent[:12])))
    print("static UI CJK + RLE round-trip check: OK (%d chars present)" % len(REQUIRED_CJK))
    print("wrote %s" % FULL_PATH)
    print("wrote %s" % SUBSET_PATH)
    print("wrote %s (%d B)" % (ext_path, len(ext_blob)))
    return 0


if __name__ == "__main__":
    sys.exit(main())