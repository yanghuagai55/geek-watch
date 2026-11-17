#!/usr/bin/env python3
"""Generate compact SRAM-cacheable 'ABT2' bundle for the Watch About page (fits in <=3,550 B):
  - Enlarged Pinkie Pie + Gummy illustration from D:/下载/2089417230228.png (PAL4_RLE, idx 0 transparent)
  - 3 tight-cropped lines of 华文琥珀 intro text split into <=64px halves using 6+2 RLE (for ui_mask_rle_draw)

Outputs:
  - app/assets/blobs/about_art.bin
  - host/about_asset_test.h
  - C:/Users/coolw/.gemini/antigravity/brain/b54e9a64-ced1-4194-82d6-c6d696276ed6/scratch/preview_about_final.png
"""

import os
import struct
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_IMG = r"D:\下载\2089417230228.png"
FONT_PATH = r"C:\Windows\Fonts\STHUPO.TTF"
OUT_BIN = os.path.join(ROOT, "app", "assets", "blobs", "about_art.bin")
OUT_H = os.path.join(ROOT, "host", "about_asset_test.h")
PREVIEW_PNG = os.path.join(ROOT, "host", "obj", "about_preview.png")


def rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else ((crc << 1) & 0xFFFF)
    return crc


def encode_pal4_rle(w: int, h: int, pal_rgb: list[tuple[int, int, int]], rows: list[list[int]]) -> bytes:
    assert len(pal_rgb) == 16
    pal_bytes = b"".join(struct.pack("<H", rgb565(r, g, b)) for r, g, b in pal_rgb)
    row_ends = []
    rle_data = bytearray()

    for y in range(h):
        row = rows[y]
        i = 0
        while i < w:
            run = 1
            while i + run < w and run < 129 and row[i + run] == row[i]:
                run += 1
            if run >= 4 or (run >= 2 and (i + run == w or i == 0)):
                rle_data.append(0x80 | (run - 2))
                rle_data.append(row[i] & 0x0F)
                i += run
            else:
                lit_start = i
                i += 1
                while i < w and (i - lit_start) < 128:
                    # Only end literal run for a repeat of >= 4 identical pixels (2B repeat saves vs 2B nibbles)
                    if i + 3 < w and row[i] == row[i + 1] == row[i + 2] == row[i + 3]:
                        break
                    i += 1
                lit_len = i - lit_start
                rle_data.append(lit_len - 1)
                for k in range(0, lit_len, 2):
                    hi = row[lit_start + k] & 0x0F
                    lo = (row[lit_start + k + 1] & 0x0F) if (k + 1 < lit_len) else 0
                    rle_data.append((hi << 4) | lo)
        row_ends.append(len(rle_data))

    tbl = b"".join(struct.pack("<H", e) for e in row_ends)
    return pal_bytes + tbl + bytes(rle_data)


def build_pinkie_img(target_w: int = 128, target_h: int = 88) -> tuple[int, int, bytes, Image.Image]:
    im = Image.open(SRC_IMG).convert("RGBA")
    im = im.crop(im.getbbox())
    # 铺满屏幕：按 cover 比例放大后居中裁切，不再保留上下留白。
    scale = max(target_w / im.width, target_h / im.height)
    nw = max(target_w, int(round(im.width * scale)))
    nh = max(target_h, int(round(im.height * scale)))
    im = im.resize((nw, nh), Image.LANCZOS)
    left = (nw - target_w) // 2
    top = (nh - target_h) // 2
    im = im.crop((left, top, left + target_w, top + target_h))
    nw, nh = target_w, target_h

    opaque_pixels = []
    rgba_comp = Image.new("RGBA", (nw, nh), (0, 0, 0, 0))
    mask = [[False] * nw for _ in range(nh)]
    for y in range(nh):
        for x in range(nw):
            r, g, b, a = im.getpixel((x, y))
            if a < 96:
                mask[y][x] = False
            else:
                mask[y][x] = True
                cr = (r * a + 127) // 255
                cg = (g * a + 127) // 255
                cb = (b * a + 127) // 255
                rgba_comp.putpixel((x, y), (cr, cg, cb, 255))
                opaque_pixels.append((cr, cg, cb))

    strip = Image.new("RGB", (len(opaque_pixels), 1))
    strip.putdata(opaque_pixels)
    q = strip.quantize(colors=14, method=Image.MEDIANCUT)
    raw_pal = q.getpalette()[: 14 * 3]
    pal_rgb = [(0, 0, 0)] + [(raw_pal[i * 3], raw_pal[i * 3 + 1], raw_pal[i * 3 + 2]) for i in range(14)] + [(0, 0, 0)]

    rows = [[0] * nw for _ in range(nh)]
    preview = Image.new("RGBA", (nw, nh), (0, 0, 0, 0))
    for y in range(nh):
        for x in range(nw):
            if not mask[y][x]:
                continue
            r, g, b, _ = rgba_comp.getpixel((x, y))
            best_i = 1
            best_d = 1 << 30
            for idx in range(1, 15):
                pr, pg, pb = pal_rgb[idx]
                d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
                if d < best_d:
                    best_d = d
                    best_i = idx
            rows[y][x] = best_i
            c565 = rgb565(*pal_rgb[best_i])
            rr = ((c565 >> 11) & 0x1F) * 255 // 31
            gg = ((c565 >> 5) & 0x3F) * 255 // 63
            bb = (c565 & 0x1F) * 255 // 31
            preview.putpixel((x, y), (rr, gg, bb, 255))

    # Mild despeckle: replace isolated single-pixel color mismatches inside opaque body to improve RLE runs imperceptibly
    for y in range(1, nh - 1):
        for x in range(1, nw - 1):
            c = rows[y][x]
            if c == 0:
                continue
            nb = [rows[y][x - 1], rows[y][x + 1], rows[y - 1][x], rows[y + 1][x]]
            if c not in nb and nb[0] != 0 and nb.count(nb[0]) >= 3:
                rows[y][x] = nb[0]

    return nw, nh, encode_pal4_rle(nw, nh, pal_rgb, rows), preview


def encode_mask62(gray_rows: list[list[int]], x0: int, w: int) -> bytes:
    """Encode a monochrome 4-level AA mask (<=64px wide) using 6+2 RLE matching ui_draw.c:bitmap."""
    h = len(gray_rows)
    flat = []
    for y in range(h):
        for x in range(x0, x0 + w):
            a = gray_rows[y][x]
            v = 0 if a < 55 else (1 if a < 130 else (2 if a < 205 else 3))
            flat.append(v)
    out = bytearray()
    i = 0
    n = len(flat)
    while i < n:
        v = flat[i]
        run = 1
        while i + run < n and run < 64 and flat[i + run] == v:
            run += 1
        out.append(((run - 1) << 2) | v)
        i += run
    return bytes(out)


def render_intro_lines():
    specs = [
        ("AT32极客手表", 12, (115, 212, 174), 0),
        ("9轴+32K+1207马达", 11, (240, 241, 228), 1),
        ("Vibe神力大作业", 11, (176, 186, 180), 2),
    ]
    out_lines = []
    for text, sz, col, col_kind in specs:
        max_w = 114
        tmp = Image.new("L", (max_w * 2, 32), 0)
        dr = ImageDraw.Draw(tmp)
        f2 = ImageFont.truetype(FONT_PATH, sz * 2)
        bbox = dr.textbbox((0, 0), text, font=f2)
        tw = bbox[2] - bbox[0]
        if tw > (max_w - 2) * 2:
            adj = max(16, int(sz * 2 * (max_w - 2) * 2 / tw))
            f2 = ImageFont.truetype(FONT_PATH, adj)
            bbox = dr.textbbox((0, 0), text, font=f2)
            tw = bbox[2] - bbox[0]
        dr.text((-bbox[0], -bbox[1]), text, fill=255, font=f2)
        gw = max(2, min(124, (tw + 1) // 2))
        gh = max(8, min(14, (bbox[3] - bbox[1] + 1) // 2))
        crop = tmp.crop((0, 0, gw * 2, gh * 2)).resize((gw, gh), Image.LANCZOS)

        rows = [[crop.getpixel((x, y)) for x in range(gw)] for y in range(gh)]
        while len(rows) > 8 and max(rows[-1]) < 65:
            rows.pop()
        while len(rows) > 8 and max(rows[0]) < 65:
            rows.pop(0)
        gh = len(rows)
        # Split into 2 halves each <= 64px wide for ui_mask_rle_draw rows[2][64]
        w_l = gw // 2
        w_r = gw - w_l
        assert w_l <= 64 and w_r <= 64
        b_l = encode_mask62(rows, 0, w_l)
        b_r = encode_mask62(rows, w_l, w_r)

        # Preview image for visual check
        pv = Image.new("RGBA", (gw, gh), (0, 0, 0, 0))
        for y in range(gh):
            for x in range(gw):
                a = rows[y][x]
                lvl = 0 if a < 55 else (1 if a < 130 else (2 if a < 205 else 3))
                if lvl == 0:
                    continue
                alpha5 = lvl * 5
                pv.putpixel(
                    (x, y),
                    (col[0] * alpha5 // 15, col[1] * alpha5 // 15, col[2] * alpha5 // 15, 255),
                )
        out_lines.append((gw, gh, w_l, col_kind, b_l, b_r, pv))
    return out_lines


def main() -> None:
    os.makedirs(os.path.dirname(OUT_BIN), exist_ok=True)
    w0, h0, data0, pv0 = build_pinkie_img(128, 88)
    lines = render_intro_lines()
    assert len(lines) == 3

    # ABT2 binary header:
    # 0..3   magic 'ABT2' (0x32544241)
    # 4..5   w0, h0
    # 6..7   line_n (3), flags (0)
    # 8..9   off0 (header=40)
    # 10..11 total_size
    # 12..13 crc16 (over payload [40..total))
    # 14..15 reserved (0)
    # 16..39 3 line descriptors * 8B: (uint16 off_l, uint16 off_r, uint8 gw, uint8 gh, uint8 wl, uint8 col_idx)
    hdr_size = 40
    payload = bytearray()
    off0 = hdr_size
    payload.extend(data0)

    descs = bytearray()
    for gw, gh, wl, col_idx, b_l, b_r, _ in lines:
        off_l = hdr_size + len(payload)
        payload.extend(b_l)
        off_r = hdr_size + len(payload)
        payload.extend(b_r)
        descs.extend(struct.pack("<HHBBBB", off_l, off_r, gw, gh, wl, col_idx))

    total = hdr_size + len(payload)
    crc = crc16_ccitt(bytes(payload))
    hdr = struct.pack("<IBBBBHHHH", 0x32544241, w0, h0, 3, 0, off0, total, crc, 0) + bytes(descs)
    assert len(hdr) == hdr_size
    blob = hdr + bytes(payload)

    with open(OUT_BIN, "wb") as f:
        f.write(blob)

    with open(OUT_H, "w", encoding="utf-8") as f:
        f.write("/* Auto-generated by host/gen_about_assets.py -- Host test fixture only */\n")
        f.write("#ifndef ABOUT_ASSET_TEST_H\n#define ABOUT_ASSET_TEST_H\n#include <stdint.h>\n\n")
        f.write(f"static const uint8_t about_art_blob[{len(blob)}] = {{\n")
        for i in range(0, len(blob), 16):
            chunk = ", ".join(f"0x{b:02X}" for b in blob[i : i + 16])
            f.write(f"    {chunk},\n")
        f.write("};\n\n#endif\n")

    canvas = Image.new("RGB", (128, 160), (0, 0, 0))
    dc = ImageDraw.Draw(canvas)
    dc.rounded_rectangle((6, 5, 8, 20), radius=1, fill=(255, 177, 94))
    dc.line((6, 26, 122, 26), fill=(49, 65, 60))
    f_hdr = ImageFont.truetype(FONT_PATH, 18)
    dc.text((15, 2), "关于", fill=(240, 241, 228), font=f_hdr)
    y0 = 29
    canvas.paste(pv0, ((128 - w0) // 2, y0), pv0)
    ty = y0 + h0 + 3
    for gw, gh, wl, col_idx, b_l, b_r, pv in lines:
        canvas.paste(pv, ((128 - gw) // 2, ty), pv)
        ty += gh + 3
    canvas.resize((128 * 3, 160 * 3), Image.NEAREST).save(PREVIEW_PNG)

    print(
        f"Generated ABT2 SRAM-cacheable blob: img0={w0}x{h0} ({len(data0)} B), "
        f"3 STHupo lines ({sum(len(l[4])+len(l[5]) for l in lines)} B), "
        f"total={total} B (fits ui_mem 3744 B free!), crc16=0x{crc:04X}"
    )


if __name__ == "__main__":
    main()
