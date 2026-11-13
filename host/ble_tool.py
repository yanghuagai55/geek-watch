#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""AT32 手表蓝牙调试工具（HC-08，BLE 直连，不需要 COM 口 / 不需要 HC-PC）

协议（ui/src/link.c）：
    帧 = A5 命令 长度 数据... 异或        异或从 A5 算到数据末尾
    每条合法命令手表回一行 "ok"，校验错回 "bad"

命令：
    ping                连通性测试（发一条空命令，回 ok 就是通）
    mon                 实时打印手表发来的内容（Ctrl+C 退出）
    key 1|2|4           按键：1=上一项 2=确定 4=下一项（位掩码，可组合如 6）
    debug 1|0           调试模式开关
    bright 0|1|2        亮度
    time                用电脑时间给手表校时
    snap [文件.bmp]     抓一帧屏幕，默认 host/obj/shot_ble.bmp
    msg 0|1|2 "文本"     写入第几个消息槽
    lyric "文本"        歌词
    raw "AT"            原样发字符串（调 HC-08 的 AT 指令用）
    send 06             只发命令字节，数据为空

可选参数（放最前）：
    --addr AA:BB:CC:DD:EE:FF   指定地址，默认 64:1C:10:B6:52:C9
    --name HC-08               按名字找

依赖：pip install bleak
"""

import argparse
import asyncio
import os
import struct
import time

from bleak import BleakClient, BleakScanner

CHR = "0000ffe1-0000-1000-8000-00805f9b34fb"
DEFAULT_ADDR = "64:1C:10:B6:52:C9"
DEFAULT_NAME = "HC-08"

MARK = 0xA5
CMD_TIME = 0x01
CMD_MSG = 0x02
CMD_LYRIC = 0x03
CMD_BRIGHT = 0x05
CMD_SNAP = 0x06
CMD_KEY = 0x07
CMD_DEBUG = 0x08
CMD_IMG = 0x11
CMD_NOP = 0x7F          # 固件不认识的命令：只回 ok，不改状态，拿来当 ping


def frame(cmd: int, payload: bytes = b"") -> bytes:
    body = bytes([MARK, cmd & 0xFF, len(payload) & 0xFF]) + payload
    x = 0
    for b in body:
        x ^= b
    return body + bytes([x])


def write_bmp(path: str, w: int, h: int, px: bytes) -> None:
    """px = RGB565 大端，w*h*2 字节，写成 24 位 BMP。"""
    row = (w * 3 + 3) & ~3
    size = 54 + row * h
    d = os.path.dirname(path)
    if d:
        os.makedirs(d, exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"BM")
        f.write(struct.pack("<IHHI", size, 0, 0, 54))
        f.write(struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, row * h, 2835, 2835, 0, 0))
        pad = b"\x00" * (row - w * 3)
        for y in range(h - 1, -1, -1):
            line = bytearray()
            base = y * w * 2
            for x in range(w):
                i = base + x * 2
                v = (px[i] << 8) | px[i + 1]
                r = (v >> 11) & 0x1F
                g = (v >> 5) & 0x3F
                b = v & 0x1F
                line += bytes((b << 3 | b >> 2, g << 2 | g >> 4, r << 3 | r >> 2))
            f.write(line)
            f.write(pad)


class Link:
    def __init__(self, cli: BleakClient):
        self.cli = cli
        self.buf = bytearray()

    def _on_data(self, _sender, data: bytearray):
        self.buf.extend(data)

    async def open(self):
        await self.cli.start_notify(CHR, self._on_data)

    def take(self) -> bytes:
        out = bytes(self.buf)
        self.buf.clear()
        return out

    async def send(self, data: bytes):
        await self.cli.write_gatt_char(CHR, data, response=False)

    async def wait_for(self, needle: bytes, timeout: float) -> bool:
        end = time.time() + timeout
        while time.time() < end:
            if needle in self.buf:
                return True
            await asyncio.sleep(0.02)
        return False


async def pick_device(args):
    def match(d, _ad):
        if args.addr and (d.address or "").upper() == args.addr.upper():
            return True
        return (d.name or "") == args.name

    return await BleakScanner.find_device_by_filter(match, timeout=12.0)


async def main() -> int:
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("--addr", default=DEFAULT_ADDR)
    ap.add_argument("--name", default=DEFAULT_NAME)
    ap.add_argument("cmd", nargs="?")
    ap.add_argument("rest", nargs="*")
    try:
        args = ap.parse_args()
    except SystemExit:
        print(__doc__)
        return 1

    if not args.cmd or args.cmd in ("-h", "--help", "help"):
        print(__doc__)
        return 1

    if args.cmd == "unpair":
        from winrt.windows.devices.bluetooth import BluetoothLEDevice
        d = await BluetoothLEDevice.from_bluetooth_address_async(
            int(args.addr.replace(":", ""), 16))
        if d is None:
            print("系统里没有这台设备")
            return 2
        info = d.device_information
        if info.pairing.is_paired:
            await info.pairing.unpair_async()
            print("已解除配对（Windows 会重新发现它）")
        else:
            print("本来就没配对")
        return 0

    dev = await pick_device(args)
    if dev is None:
        print("找不到 %s（确认模块上电、没被 HC-PC 占用）" % args.name)
        return 2

    async with BleakClient(dev, timeout=15.0) as cli:
        link = Link(cli)
        await link.open()
        await asyncio.sleep(0.2)
        link.take()

        cmd, rest = args.cmd, args.rest

        if cmd == "mon":
            print("监听中（Ctrl+C 退出）...")
            try:
                while True:
                    data = link.take()
                    if data:
                        print(data.decode("utf-8", "replace").rstrip("\r\n"))
                    await asyncio.sleep(0.05)
            except KeyboardInterrupt:
                return 0

        if cmd == "ping":
            t0 = time.time()
            await link.send(frame(CMD_NOP))
            ok = await link.wait_for(b"ok", 2.0)
            dt = (time.time() - t0) * 1000
            print("ok  %.0f ms" % dt if ok else "超时：没收到 ok（检查波特率是否 9600）")
            return 0 if ok else 3

        if cmd == "snap":
            out = rest[0] if rest else "host/obj/shot_ble.bmp"
            await link.send(frame(CMD_SNAP))
            t0 = time.time()
            deadline = t0 + 180
            hdr = bytes([MARK, CMD_IMG, 0xFF])
            start, total, last = -1, 0, -10
            while time.time() < deadline:
                if start < 0:
                    i = link.buf.find(hdr)
                    if i >= 0 and len(link.buf) >= i + 6:
                        start = i
                        total = link.buf[i + 3] | (link.buf[i + 4] << 8) | (link.buf[i + 5] << 16)
                        print("帧头: %d 字节" % total)
                else:
                    got = len(link.buf) - start - 6
                    pct = max(0, min(100, got * 100 // max(1, total)))
                    if pct - last >= 10:
                        last = pct
                        print("  %3d%%  %d/%d" % (pct, got, total))
                    if len(link.buf) >= start + 6 + total + 1:
                        break
                await asyncio.sleep(0.05)
            if start < 0 or len(link.buf) < start + 6 + total + 1:
                print("超时或数据不全")
                return 4
            payload = bytes(link.buf[start + 6: start + 6 + total])
            w = payload[0] | (payload[1] << 8)
            h = payload[2] | (payload[3] << 8)
            px = payload[4:]
            if len(px) != w * h * 2:
                print("像素长度不对：%d != %dx%dx2" % (len(px), w, h))
                return 5
            write_bmp(out, w, h, px)
            print("%s  %dx%d  %.1fs" % (out, w, h, time.time() - t0))
            return 0

        if cmd == "key":
            data = frame(CMD_KEY, bytes([int(rest[0], 0) & 7]))
        elif cmd == "debug":
            data = frame(CMD_DEBUG, bytes([1 if int(rest[0]) else 0]))
        elif cmd == "bright":
            data = frame(CMD_BRIGHT, bytes([int(rest[0]) & 3]))
        elif cmd == "time":
            t = time.localtime()
            data = frame(CMD_TIME, struct.pack("<HBBBBB", t.tm_year, t.tm_mon, t.tm_mday,
                                               t.tm_hour, t.tm_min, t.tm_sec))
        elif cmd == "msg":
            data = frame(CMD_MSG, bytes([int(rest[0]) & 3]) + rest[1].encode("utf-8")[:23])
        elif cmd == "lyric":
            data = frame(CMD_LYRIC, rest[0].encode("utf-8")[:27])
        elif cmd == "raw":
            await link.send(rest[0].encode("latin-1"))
            await asyncio.sleep(0.8)
            print("raw ->", link.take())
            return 0
        elif cmd == "send":
            data = frame(int(rest[0], 16))
        else:
            print("未知命令：%s" % cmd)
            print(__doc__)
            return 1

        await link.send(data)
        ok = await link.wait_for(b"ok", 2.0)
        print("%-28s -> %s" % (data.hex(" "), "ok" if ok else "无回应"))
        return 0 if ok else 3


if __name__ == "__main__":
    try:
        raise SystemExit(asyncio.run(main()))
    except KeyboardInterrupt:
        pass
