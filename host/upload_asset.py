#!/usr/bin/env python3
"""Upload a static asset into the watch's external 32 KB pool via command 0x2F.

Wire format (see ui/src/link.c ext_asset_chunk):
  A5 2F len | id(1) chunk(1) total(2 LE) crc16(2 LE) data(<=128) | XOR

The watch ACKs every valid frame with "ok"; on the final chunk it also sends
the 0x2E pool-state frame, and sends "bad" if the CRC check fails.

Usage:
  python host/upload_asset.py --id 9 --file app/assets/blobs/font_cjk.bin
  python host/upload_asset.py --id 9 --file ... --port COM7   (wired CH340)
  python host/upload_asset.py --id 9 --file ... --ble          (BLE back end)
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ble_tool as bt  # noqa: E402

CHUNK = 128


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else ((crc << 1) & 0xFFFF)
    return crc


def main() -> int:
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--id", type=int, required=True, help="asset id, e.g. 9 = font tail")
    ap.add_argument("--file", required=True, help="binary file to upload")
    ap.add_argument("--timeout", type=float, default=4.0, help="per-chunk ack timeout")
    ap.add_argument("--port", help="serial port (CH340), e.g. COM7")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--ble", action="store_true", help="use BLE instead of SPP")
    ap.add_argument("--addr", default=bt.DEF_BLE_ADDR)
    ap.add_argument("--name", default=bt.DEF_BLE_NAME)
    args = ap.parse_args()

    with open(args.file, "rb") as f:
        data = f.read()
    total = len(data)
    if total == 0 or total > 0xFFFF:
        print("bad file size", total)
        return 2
    crc = crc16_ccitt(data)
    print("upload id=%d  %d B  crc=0x%04X" % (args.id, total, crc))

    if args.port:
        link = bt.SerialLink(args.port, args.baud)
    elif args.ble:
        link = bt.BleLink(args.addr, args.name)
    else:
        link = bt.SppLink(bt.DEF_SPP_ADDR, 1)

    chunks = (total + CHUNK - 1) // CHUNK
    for i, off in enumerate(range(0, total, CHUNK)):
        body = (bytes([args.id & 0xFF, i & 0xFF]) +
                total.to_bytes(2, "little") + crc.to_bytes(2, "little") +
                data[off:off + CHUNK])
        link.take()
        link.send(bt.frame(0x2F, body))
        if not link.wait_for(b"ok", args.timeout):
            print("chunk %d/%d no ack" % (i + 1, chunks))
            return 3
        print("  %d/%d" % (i + 1, chunks))
        if off + CHUNK >= total:
            time.sleep(0.4)
            tail = link.take()
            if b"bad" in tail:
                print("watch rejected CRC")
                return 4
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
