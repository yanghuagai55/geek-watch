#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""AT32 手表 · 电脑通讯工具（经典 SPP / BLE / 串口）

协议（ui/src/link.c）：
    帧 = A5 命令 长度 数据... 异或        异或从 A5 算到数据末尾
    每条合法命令手表回一行 "ok"，校验错回 "bad"

链路（三选一，默认经典 SPP）：
    （不加链路参数）    经典蓝牙 SPP：默认 04:26:01:08:02:0C，通道 1
    --ble               BLE：默认找 HC-04 / C4:26:01:08:02:0C
    --port COM7         串口：CH340 直连手表 PA2/PA3，或模块测试架的 UART

旁路监听口（可选，和主链路互不影响）：
    --mon-port COM7     额外打开一个 COM 口，用于观察模块 UART / 从 UART 发数据
    --mon-baud 115200

发送选项（写在命令之前）：
    --hex               raw / comsend 的内容按十六进制解析
    --crlf              raw / comsend 末尾追加 \\r\\n

命令：
    ping                连通性测试，回 ok 即通
    mon                 实时监听，hex + ASCII 同时显示；带 --mon-port 时双通道，
                        回车可直接发送（! 前缀发主链路，hex 前缀按十六进制）
    key 1|2|4           按键：1=上一项 2=确定 4=下一项（自动按下+松开）
    longkey 2           长按：用于从子页返回，默认长按 KEY2 600ms
    debug 1|0           调试角标开关
    bright 0|1|2        亮度 低/中/高
    time                用电脑时间给手表校时
    snap [文件.bmp]     抓一帧屏幕（图传），默认 host/obj/shot_ble.bmp
    msg 0|1|2 "文本"     写入消息槽，UTF-8 超长自动按字符截断
    lyric "文本"        歌词，≤27 字节
    state 0|1           播放态：0 暂停 1 播放
    music "文本" [0|1]  一次发送歌词+播放态
    lyrics 文件 [秒]    逐行发送歌词文件，默认 2 秒一行
    sensor              读传感器调试快照（I2C / QMC 寄存器 / 电池）
    demo                依次跑 ping/校时/亮度/消息/歌词/播放态/抓屏
    raw "AT"            原样发主链路（配 --crlf / --hex）
    comsend "456"       原样发监听口（配 --crlf / --hex）
    send 06             只发命令字节，数据为空（自动套 A5 帧）
    unpair              解除 Windows 的 BLE 配对（仅 BLE）

示例：
    python host/ble_tool.py ping
    python host/ble_tool.py --ble ping
    python host/ble_tool.py --port COM7 ping
    python host/ble_tool.py --port COM7 msg 0 "你好，手表"
    python host/ble_tool.py --port COM7 snap host/obj/shot.bmp

    # 测试架：PC 走经典连模块，同时监听/发送模块 UART（COM7）
    python host/ble_tool.py --mon-port COM7 mon
    #   mon 里直接输入 456 回车 = 从 COM7 发；!123 回车 = 从经典链路发
    python host/ble_tool.py --hex --crlf --mon-port COM7 comsend "34 35 36"

依赖：pip install pyserial bleak winrt-Windows.Devices.Bluetooth
"""

import argparse
import asyncio
import os
import queue
import socket
import struct
import sys
import threading
import time

CHR = "0000ffe1-0000-1000-8000-00805f9b34fb"
DEF_BLE_ADDR = "C4:26:01:08:02:0C"
DEF_BLE_NAME = "HC-04"
DEF_SPP_ADDR = "04:26:01:08:02:0C"

MARK = 0xA5
CMD_TIME = 0x01
CMD_MSG = 0x02
CMD_LYRIC = 0x03
CMD_STATE = 0x04
CMD_BRIGHT = 0x05
CMD_SNAP = 0x06
CMD_KEY = 0x07
CMD_DEBUG = 0x08
CMD_PERF = 0x09
CMD_CTRL = 0x10
CMD_IMG = 0x11
CMD_STATS = 0x12
CMD_SENSOR = 0x13
CMD_NOP = 0x7F

CTRL_NAMES = {1: "播放", 2: "暂停", 3: "下一首", 4: "上一首"}

MSG_MAX = 23           # LINK_MSG_LEN 24，含结尾 NUL
LYRIC_MAX = 27         # LINK_LYRIC_LEN 28，含结尾 NUL


def frame(cmd: int, payload: bytes = b"") -> bytes:
    body = bytes([MARK, cmd & 0xFF, len(payload) & 0xFF]) + payload
    x = 0
    for b in body:
        x ^= b
    return body + bytes([x])


def fit_text(text: str, limit: int) -> bytes:
    """按 UTF-8 字符边界截断，避免半个汉字。"""
    while text and len(text.encode("utf-8")) > limit:
        text = text[:-1]
    return text.encode("utf-8")


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


def fmt(data: bytes, limit: int = 64) -> str:
    """hex + 可打印 ASCII 一行显示，超长只显示前 limit 字节。"""
    if not data:
        return "(空)"
    head = data[:limit]
    hx = head.hex(" ").upper()
    txt = "".join(chr(b) if 32 <= b < 127 else "." for b in head)
    more = "" if len(data) <= limit else " ...(+%d)" % (len(data) - limit)
    return "%s  |%s|%s" % (hx, txt, more)


def build_payload(args, rest) -> bytes:
    """raw / comsend 的内容：默认 UTF-8，--hex 解析十六进制，--crlf 追加换行。"""
    text = " ".join(rest)
    if not text:
        raise RuntimeError("内容为空")
    if args.hex:
        cleaned = text.replace("0x", "").replace("0X", "").replace(",", " ")
        cleaned = "".join(cleaned.split())
        if len(cleaned) % 2:
            raise RuntimeError("HEX 需要偶数个十六进制字符")
        data = bytes.fromhex(cleaned)
    else:
        data = text.encode("utf-8")
    if args.crlf:
        data += b"\r\n"
    return data


def link_tag(args) -> str:
    if args.port:
        return "串口"
    if args.ble:
        return "BLE"
    return "SPP"


class Link:
    """三路链路共用的接收缓冲。后台线程只负责收，命令层同步轮询。"""

    def __init__(self):
        self.buf = bytearray()
        self.lock = threading.Lock()
        self.closed = False

    def _feed(self, data: bytes):
        if not data:
            return
        with self.lock:
            self.buf.extend(data)

    def take(self) -> bytes:
        with self.lock:
            out = bytes(self.buf)
            self.buf.clear()
        return out

    def snapshot_buf(self) -> bytes:
        with self.lock:
            return bytes(self.buf)

    def wait_for(self, needle: bytes, timeout: float) -> bool:
        end = time.time() + timeout
        while time.time() < end:
            with self.lock:
                if needle in self.buf:
                    return True
            time.sleep(0.02)
        return False

    def wait_text(self, prefix: str, timeout: float) -> str:
        end = time.time() + timeout
        text = ""
        while time.time() < end:
            text += self.take().decode("utf-8", "replace")
            if prefix in text:
                return text
            time.sleep(0.03)
        return text

    def send(self, data: bytes):
        raise NotImplementedError

    def close(self):
        self.closed = True


class SerialLink(Link):
    def __init__(self, port: str, baud: int = 115200):
        super().__init__()
        import serial  # pyserial
        self.ser = serial.Serial(port, baud, timeout=0.02)
        self.thread = threading.Thread(target=self._reader, daemon=True)
        self.thread.start()

    def _reader(self):
        while not self.closed:
            try:
                d = self.ser.read(256)
            except Exception:
                break
            if d:
                self._feed(d)

    def send(self, data: bytes):
        self.ser.write(data)
        self.ser.flush()

    def close(self):
        self.closed = True
        try:
            self.ser.close()
        except Exception:
            pass


class SppLink(Link):
    AF_BTH = 32
    BTPROTO_RFCOMM = 3

    def __init__(self, addr: str, channel: int = 1):
        super().__init__()
        # 部分 Python 版本不导出 AF_BTH，但 Windows 仍支持 32 / RFCOMM 3
        last = None
        for _ in range(3):
            try:
                self.sock = socket.socket(self.AF_BTH, socket.SOCK_STREAM, self.BTPROTO_RFCOMM)
                self.sock.settimeout(8.0)
                self.sock.connect((addr, channel))
                break
            except OSError as e:
                last = e
                try:
                    self.sock.close()
                except Exception:
                    pass
                time.sleep(0.5)
        else:
            raise last
        self.sock.settimeout(0.02)
        self.thread = threading.Thread(target=self._reader, daemon=True)
        self.thread.start()

    def _reader(self):
        while not self.closed:
            try:
                d = self.sock.recv(1024)
            except socket.timeout:
                continue
            except OSError:
                break
            if not d:
                break
            self._feed(d)

    def send(self, data: bytes):
        self.sock.sendall(data)

    def close(self):
        self.closed = True
        try:
            self.sock.close()
        except Exception:
            pass


class BleLink(Link):
    def __init__(self, addr: str, name: str):
        super().__init__()
        from bleak import BleakClient, BleakScanner  # 延迟导入，串口模式不需要 bleak

        self.loop = asyncio.new_event_loop()
        self.loop_thread = threading.Thread(target=self.loop.run_forever, daemon=True)
        self.loop_thread.start()
        self.cli = None

        async def go():
            def match(d, _ad):
                if addr and (d.address or "").upper() == addr.upper():
                    return True
                n = d.name or ""
                return bool(name) and (n == name or n.startswith(name))

            dev = await BleakScanner.find_device_by_filter(match, timeout=12.0)
            if dev is None:
                raise RuntimeError("找不到 %s / %s（确认模块上电、没被手机占用）" % (name, addr))
            self.cli = BleakClient(dev, timeout=15.0)
            await self.cli.connect()
            await self.cli.start_notify(CHR, lambda _s, d: self._feed(bytes(d)))

        try:
            asyncio.run_coroutine_threadsafe(go(), self.loop).result(35)
        except Exception:
            self.close()
            raise

    def send(self, data: bytes):
        if self.cli is None:
            raise RuntimeError("BLE 未连接")
        fut = asyncio.run_coroutine_threadsafe(
            self.cli.write_gatt_char(CHR, data, response=False), self.loop)
        fut.result(6)

    def close(self):
        self.closed = True
        if self.cli is not None:
            try:
                asyncio.run_coroutine_threadsafe(self.cli.disconnect(), self.loop).result(6)
            except Exception:
                pass
            self.cli = None
        try:
            self.loop.call_soon_threadsafe(self.loop.stop)
        except Exception:
            pass


def open_link(args) -> Link:
    picks = int(bool(args.port)) + int(args.spp is not None) + int(bool(args.ble))
    if picks > 1:
        raise RuntimeError("--port / --spp / --ble 只能选一个")
    if args.port:
        return SerialLink(args.port, args.baud)
    if args.ble:
        return BleLink(args.addr, args.name)
    return SppLink(args.spp or DEF_SPP_ADDR, args.channel)


def time_payload() -> bytes:
    t = time.localtime()
    return struct.pack("<HBBBBB", t.tm_year, t.tm_mon, t.tm_mday,
                       t.tm_hour, t.tm_min, t.tm_sec)


def send_ack(link: Link, cmd: int, payload: bytes = b"", timeout: float = 3.0) -> bool:
    data = frame(cmd, payload)
    link.take()
    link.send(data)
    ok = link.wait_for(b"ok", timeout)
    link.take()
    print("%-30s -> %s" % (data.hex(" "), "ok" if ok else "无回应"))
    return ok


def grab_snapshot(link: Link, out: str, timeout: float) -> bool:
    link.take()
    link.send(frame(CMD_SNAP))
    t0 = time.time()
    deadline = t0 + timeout
    hdr = bytes([MARK, CMD_IMG, 0xFF])
    start, total, last = -1, 0, -10
    while time.time() < deadline:
        buf = link.snapshot_buf()
        if start < 0:
            i = buf.find(hdr)
            if i >= 0 and len(buf) >= i + 6:
                start = i
                total = buf[i + 3] | (buf[i + 4] << 8) | (buf[i + 5] << 16)
                print("帧头: %d 字节" % total)
        else:
            got = len(buf) - start - 6
            pct = max(0, min(100, got * 100 // max(1, total)))
            if pct - last >= 10:
                last = pct
                print("  %3d%%  %d/%d" % (pct, got, total))
            if len(buf) >= start + 6 + total + 1:
                break
        time.sleep(0.05)

    buf = link.snapshot_buf()
    if start < 0 or len(buf) < start + 6 + total + 1:
        print("抓屏超时或数据不全")
        link.take()
        return False
    payload = buf[start + 6:start + 6 + total]
    link.take()
    if len(payload) < 4:
        print("图像载荷太短")
        return False
    w = payload[0] | (payload[1] << 8)
    h = payload[2] | (payload[3] << 8)
    px = payload[4:]
    if len(px) != w * h * 2:
        print("像素长度不对：%d != %dx%dx2" % (len(px), w, h))
        return False
    write_bmp(out, w, h, px)
    print("%s  %dx%d  %.1fs" % (out, w, h, time.time() - t0))
    return True


def print_watch_data(data: bytes):
    if not data:
        return
    if bytes([MARK, CMD_IMG, 0xFF]) in data:
        print("[手表] 图像帧（请用 snap 接收）")
        return
    binary = False
    i = data.find(bytes([MARK, CMD_CTRL]))
    if i >= 0 and len(data) >= i + 5:
        action = data[i + 3]
        print("[手表] 播放控制：%s" % CTRL_NAMES.get(action, str(action)))
        binary = True
    i = data.find(bytes([MARK, CMD_STATS, 0x20]))
    if i >= 0 and len(data) >= i + 36:
        vals = struct.unpack("<8I", data[i + 3:i + 35])
        ram = vals[6]
        ram_used = ram & 0xFFFF
        cpu_tenths = ram >> 16
        ram_total = vals[7] & 0xFFFF
        fps = vals[7] >> 16
        print("[手表] 性能：prepare=%dus raster=%dus frame=%dus ram=%d/%dKB cpu=%.1f%% fps=%d" %
              (vals[0], vals[1], vals[3],
               (ram_used + 512) // 1024, (ram_total + 512) // 1024,
               cpu_tenths / 10.0, fps))
        binary = True
    if binary:
        return
    text = data.decode("utf-8", "replace")
    for line in text.splitlines():
        line = line.strip()
        if line:
            print("[手表] %s" % line)


def _stdin_lines(q: queue.Queue):
    for line in sys.stdin:
        q.put(line.rstrip("\r\n"))


def _parse_line(line: str):
    """mon 里的输入：默认发监听口；! 前缀发主链路；hex 前缀按十六进制。"""
    to_main = line.startswith("!")
    if to_main:
        line = line[1:].strip()
    if line.lower().startswith("hex "):
        cleaned = "".join(line[4:].replace("0x", "").replace("0X", "").replace(",", " ").split())
        data = bytes.fromhex(cleaned)
    else:
        data = line.encode("utf-8") + b"\r\n"
    return to_main, data


def do_mon(link: Link, tag: str, mon: Link = None):
    extra = "，同步监听口" if mon is not None else ""
    print("监听中（%s%s，Ctrl+C 退出）..." % (tag, extra))
    if mon is not None:
        print("输入内容回车 = 从监听口发送；前缀 ! = 从主链路发送；hex 34 35 36 = 发十六进制")
    else:
        print("输入内容回车 = 从主链路发送；hex 34 35 36 = 发十六进制")
    lines = queue.Queue()
    threading.Thread(target=_stdin_lines, args=(lines,), daemon=True).start()
    try:
        while True:
            data = link.take()
            if data:
                print("[%s 收] %s" % (tag, fmt(data)))
                print_watch_data(data)
            if mon is not None:
                d = mon.take()
                if d:
                    print("[监听 收] %s" % fmt(d))
            try:
                line = lines.get_nowait()
            except queue.Empty:
                line = None
            if line:
                try:
                    to_main, payload = _parse_line(line)
                    use_main = to_main or mon is None
                    dst = link if use_main else mon
                    dst.send(payload)
                    print("[%s 发] %s" % (tag if use_main else "监听", fmt(payload)))
                except Exception as e:
                    print("发送失败：%s" % e)
            time.sleep(0.05)
    except KeyboardInterrupt:
        print()


def do_demo(link: Link, out: str, snap_timeout: float) -> bool:
    print("=== 通讯 Demo ===")
    results = []
    results.append(("ping", send_ack(link, CMD_NOP)))
    results.append(("time", send_ack(link, CMD_TIME, time_payload())))
    results.append(("bright", send_ack(link, CMD_BRIGHT, b"\x01")))
    results.append(("msg0", send_ack(link, CMD_MSG, b"\x00" + fit_text("HELLO WATCH", MSG_MAX))))
    results.append(("msg1", send_ack(link, CMD_MSG, b"\x01" + fit_text("PC LINK OK", MSG_MAX))))
    results.append(("lyric", send_ack(link, CMD_LYRIC, fit_text("DEMO LYRIC", LYRIC_MAX))))
    results.append(("state", send_ack(link, CMD_STATE, b"\x01")))
    snap_ok = grab_snapshot(link, out, snap_timeout)
    print("=== 结果 ===")
    for name, ok in results:
        print("  %-8s %s" % (name, "PASS" if ok else "FAIL"))
    print("  %-8s %s" % ("snap", "PASS" if snap_ok else "FAIL"))
    return all(ok for _, ok in results) and snap_ok


def unpair(addr: str) -> int:
    try:
        from winrt.windows.devices.bluetooth import BluetoothLEDevice
    except ImportError:
        print("缺少 winrt，无法解除配对")
        return 2

    async def go():
        d = await BluetoothLEDevice.from_bluetooth_address_async(
            int(addr.replace(":", ""), 16))
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

    return asyncio.run(go())


def main() -> int:
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("--port", help="串口，例如 COM7")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--spp", nargs="?", const=DEF_SPP_ADDR, default=None,
                    help="经典蓝牙 SPP 地址（默认 %s）" % DEF_SPP_ADDR)
    ap.add_argument("--channel", type=int, default=1)
    ap.add_argument("--addr", default=DEF_BLE_ADDR)
    ap.add_argument("--name", default=DEF_BLE_NAME)
    ap.add_argument("--ble", action="store_true", help="走 BLE")
    ap.add_argument("--mon-port", help="旁路监听/发送的 COM 口，例如 COM7")
    ap.add_argument("--mon-baud", type=int, default=115200)
    ap.add_argument("--hex", action="store_true", help="raw/comsend 按十六进制解析")
    ap.add_argument("--crlf", action="store_true", help="raw/comsend 末尾追加 \\r\\n")
    ap.add_argument("--timeout", type=float, default=3.0)
    ap.add_argument("--snap-timeout", type=float, default=180.0)
    ap.add_argument("cmd", nargs="?")
    ap.add_argument("rest", nargs="*")
    try:
        args = ap.parse_args()
    except SystemExit:
        print(__doc__)
        return 1

    if not args.cmd:
        print(__doc__)
        return 1
    if args.cmd in ("-h", "--help", "help"):
        print(__doc__)
        return 0

    if args.cmd == "unpair":
        if args.port or args.spp is not None:
            print("unpair 只用于 BLE")
            return 1
        return unpair(args.addr)

    link = None
    mon = None
    try:
        link = open_link(args)
        tag = link_tag(args)
        if args.mon_port:
            if args.port and args.mon_port.upper() == args.port.upper():
                raise RuntimeError("--mon-port 不能和 --port 是同一个口")
            mon = SerialLink(args.mon_port, args.mon_baud)
            print("监听口: %s @ %d" % (args.mon_port, args.mon_baud))
        cmd, rest = args.cmd, args.rest

        if cmd == "mon":
            do_mon(link, tag, mon)
            return 0

        if cmd == "ping":
            t0 = time.time()
            ok = send_ack(link, CMD_NOP, timeout=args.timeout)
            print("延迟 %.0f ms" % ((time.time() - t0) * 1000) if ok else "超时")
            return 0 if ok else 3

        if cmd == "sensor":
            link.take()
            link.send(frame(CMD_SENSOR))
            text = link.wait_text("DBG", 3.0)
            for line in text.splitlines():
                if line.startswith("DBG"):
                    print(line)
                    return 0
            print(text.strip() or "无回应")
            return 0 if text.strip() else 3

        if cmd == "snap":
            out = rest[0] if rest else "host/obj/shot_ble.bmp"
            return 0 if grab_snapshot(link, out, args.snap_timeout) else 4

        if cmd in ("key", "longkey"):
            if not rest:
                print("用法：key 1|2|4 / longkey 2")
                return 1
            mask = int(rest[0], 0) & 7
            hold = 0.60 if cmd == "longkey" else 0.09
            # 固件的蓝牙按键只在调试模式生效
            send_ack(link, CMD_DEBUG, b"\x01", args.timeout)
            link.take()
            link.send(frame(CMD_KEY, bytes([mask])))
            time.sleep(hold)
            link.send(frame(CMD_KEY, b"\x00"))
            time.sleep(0.20)
            data = link.take()
            if data:
                print_watch_data(data)
            else:
                print("%s %d -> (无 ack)" % (cmd, mask))
            return 0

        if cmd == "msg":
            if len(rest) < 2:
                print("用法：msg 0|1|2 \"文本\"")
                return 1
            slot = int(rest[0]) & 3
            text = " ".join(rest[1:])
            body = fit_text(text, MSG_MAX)
            if len(body) < len(text.encode("utf-8")):
                print("警告：消息超过 %d 字节，已按字符截断" % MSG_MAX)
            return 0 if send_ack(link, CMD_MSG, bytes([slot]) + body, args.timeout) else 3

        if cmd == "lyric":
            text = " ".join(rest)
            body = fit_text(text, LYRIC_MAX)
            if len(body) < len(text.encode("utf-8")):
                print("警告：歌词超过 %d 字节，已按字符截断" % LYRIC_MAX)
            return 0 if send_ack(link, CMD_LYRIC, body, args.timeout) else 3

        if cmd == "state":
            if not rest:
                print("用法：state 0|1")
                return 1
            on = 1 if int(rest[0]) else 0
            return 0 if send_ack(link, CMD_STATE, bytes([on]), args.timeout) else 3

        if cmd == "music":
            if not rest:
                print("用法：music \"歌词\" [0|1]")
                return 1
            text = rest[0]
            on = 1
            if len(rest) > 1:
                on = 1 if int(rest[1]) else 0
            body = fit_text(text, LYRIC_MAX)
            ok1 = send_ack(link, CMD_LYRIC, body, args.timeout)
            ok2 = send_ack(link, CMD_STATE, bytes([on]), args.timeout)
            return 0 if ok1 and ok2 else 3

        if cmd == "lyrics":
            if not rest:
                print("用法：lyrics 文件 [秒]")
                return 1
            path = rest[0]
            interval = float(rest[1]) if len(rest) > 1 else 2.0
            with open(path, "r", encoding="utf-8-sig") as f:
                lines = [ln.strip() for ln in f if ln.strip()]
            if not lines:
                print("文件里没有歌词")
                return 1
            send_ack(link, CMD_STATE, b"\x01", args.timeout)
            try:
                for i, line in enumerate(lines):
                    body = fit_text(line, LYRIC_MAX)
                    print("[%d/%d] %s" % (i + 1, len(lines), line))
                    link.take()
                    link.send(frame(CMD_LYRIC, body))
                    link.wait_for(b"ok", args.timeout)
                    link.take()
                    if i + 1 < len(lines):
                        time.sleep(interval)
            except KeyboardInterrupt:
                print()
            return 0

        if cmd == "time":
            return 0 if send_ack(link, CMD_TIME, time_payload(), args.timeout) else 3

        if cmd == "bright":
            if not rest:
                print("用法：bright 0|1|2")
                return 1
            return 0 if send_ack(link, CMD_BRIGHT, bytes([int(rest[0]) & 3]), args.timeout) else 3

        if cmd == "debug":
            if not rest:
                print("用法：debug 0|1")
                return 1
            return 0 if send_ack(link, CMD_DEBUG, bytes([1 if int(rest[0]) else 0]), args.timeout) else 3

        if cmd == "raw":
            if not rest:
                print("用法：raw \"AT\"（可配 --crlf / --hex）")
                return 1
            data = build_payload(args, rest)
            link.take()
            link.send(data)
            print("[%s 发] %s" % (tag, fmt(data)))
            time.sleep(0.8)
            print("[%s 收] %s" % (tag, fmt(link.take())))
            return 0

        if cmd == "comsend":
            if mon is None:
                print("用法：--mon-port COM7 comsend \"456\"（可配 --crlf / --hex）")
                return 1
            data = build_payload(args, rest)
            mon.send(data)
            print("[监听 发] %s" % fmt(data))
            return 0

        if cmd == "send":
            if not rest:
                print("用法：send 06")
                return 1
            return 0 if send_ack(link, int(rest[0], 16), b"", args.timeout) else 3

        if cmd == "demo":
            out = rest[0] if rest else "host/obj/demo.bmp"
            return 0 if do_demo(link, out, args.snap_timeout) else 5

        print("未知命令：%s" % cmd)
        print(__doc__)
        return 1
    except KeyboardInterrupt:
        return 0
    except Exception as e:
        print("错误：%s" % e)
        return 2
    finally:
        if mon is not None:
            mon.close()
        if link is not None:
            link.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        pass
