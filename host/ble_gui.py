#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""AT32 手表调试台（GUI，Windows / WinRT 版）

不扫描、不配对 —— 直接用 Windows 已经维护的 BLE 连接（BluetoothLEDevice.FromBluetoothAddress），
所以模块「不广播 / 设置里看不到名字」也能连。

协议（ui/src/link.c）：A5 命令 长度 数据... 异或，合法命令回 ok

依赖：pip install winrt-Windows.Devices.Bluetooth winrt-Windows.Devices.Bluetooth.GenericAttributeProfile winrt-Windows.Storage.Streams pillow
运行：python host/ble_gui.py
"""

import asyncio
import queue
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from PIL import Image, ImageTk
from bleak import BleakClient, BleakScanner
from winrt.windows.devices.bluetooth import BluetoothLEDevice
from winrt.windows.devices.bluetooth.genericattributeprofile import (
    GattClientCharacteristicConfigurationDescriptorValue,
)
from winrt.windows.storage.streams import DataWriter

DEF_ADDR = "C4:26:01:08:02:0C"   # HC-04BLE；HC-08 是 64:1C:10:B6:52:C9
CHR = "0000ffe1-0000-1000-8000-00805f9b34fb"

MARK = 0xA5
CMD_TIME, CMD_MSG, CMD_LYRIC = 0x01, 0x02, 0x03
CMD_BRIGHT, CMD_SNAP, CMD_KEY, CMD_DEBUG, CMD_IMG, CMD_NOP = 0x05, 0x06, 0x07, 0x08, 0x11, 0x7F

BG = "#1e1f22"
PANEL = "#26282c"
FG = "#e8e8e8"
ACC = "#e8a24a"
DIM = "#9aa0a6"


def frame(cmd: int, payload: bytes = b"") -> bytes:
    body = bytes([MARK, cmd & 0xFF, len(payload) & 0xFF]) + payload
    x = 0
    for b in body:
        x ^= b
    return body + bytes([x])


def to_buf(data: bytes):
    w = DataWriter()
    w.write_bytes(bytes(data))
    return w.detach_buffer()


def addr_to_int(text: str) -> int:
    return int(text.replace(":", "").replace("-", ""), 16)


def snap_to_image(payload: bytes):
    w = payload[0] | (payload[1] << 8)
    h = payload[2] | (payload[3] << 8)
    px = payload[4:]
    if len(px) != w * h * 2:
        return None
    rgb = bytearray(w * h * 3)
    for i in range(w * h):
        v = (px[i * 2] << 8) | px[i * 2 + 1]
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        rgb[i * 3] = (r << 3) | (r >> 2)
        rgb[i * 3 + 1] = (g << 2) | (g >> 4)
        rgb[i * 3 + 2] = (b << 3) | (b >> 2)
    return Image.frombytes("RGB", (w, h), bytes(rgb))


class Worker(threading.Thread):
    """所有 GATT 操作跑在这条线程的 asyncio 循环里。"""

    def __init__(self, ui_q: queue.Queue):
        super().__init__(daemon=True)
        self.q = ui_q
        self.loop = asyncio.new_event_loop()
        self.device = None
        self.char = None
        self.bleak = None
        self.backend = None
        self.buf = bytearray()
        self.addr = DEF_ADDR

    def emit(self, kind, payload=None):
        self.q.put((kind, payload))

    def log(self, text):
        self.emit("log", "%s  %s" % (time.strftime("%H:%M:%S"), text))

    def submit(self, coro):
        if self.loop.is_closed():
            return None
        return asyncio.run_coroutine_threadsafe(coro, self.loop)

    def run(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    def _ready(self):
        if self.backend == "bleak":
            return self.bleak is not None
        return self.char is not None

    def _on_bleak(self, _c, data):
        self.buf.extend(data)

    def _on_value(self, _sender, args):
        try:
            self.buf.extend(bytes(args.characteristic_value))
        except Exception:
            pass

    async def connect(self):
        self.emit("status", "connecting")
        # 1) 能广播的（如 HC-04BLE）：扫描直连，带宽更好
        try:
            dev = await BleakScanner.find_device_by_filter(
                lambda d, _a: (d.address or "").upper() == self.addr.upper(), timeout=6.0)
            if dev is not None:
                self.bleak = BleakClient(dev, timeout=15.0)
                await self.bleak.connect()
                await self.bleak.start_notify(CHR, self._on_bleak)
                self.backend = "bleak"
                self.log("已连接 %s（%s，扫描通道）" % (dev.name or "?", dev.address))
                self.emit("status", True)
                await asyncio.sleep(0.15)
                self.buf.clear()
                await self.ping()
                return
        except Exception as e:
            self.log("扫描连接没成：%s" % e)
        # 2) 不广播但系统已连着的（如 HC-08）：WinRT 直连
        try:
            self.backend = "winrt"
            self.device = await BluetoothLEDevice.from_bluetooth_address_async(addr_to_int(self.addr))
            if self.device is None:
                self.log("系统里没有 %s：先把模块上电，或先用 HC-PC 连一次让系统认识它" % self.addr)
                self.emit("status", False)
                return
            svcs = await self.device.get_gatt_services_async()
            svc = next((s for s in svcs.services if str(s.uuid).lower().startswith("0000ffe0")), None)
            if svc is None:
                self.log("找不到 FFE0 服务，这个设备可能不是 HC-08")
                self.emit("status", False)
                return
            chars = await svc.get_characteristics_async()
            self.char = next((c for c in chars.characteristics
                              if str(c.uuid).lower().startswith("0000ffe1")), None)
            if self.char is None:
                self.log("找不到 FFE1 特征值")
                self.emit("status", False)
                return
            self.char.add_value_changed(self._on_value)
            await self.char.write_client_characteristic_configuration_descriptor_async(
                GattClientCharacteristicConfigurationDescriptorValue.NOTIFY)
            self.log("已挂上 %s（连接状态 %s）" % (self.device.name or "HC-08", self.device.connection_status))
            self.emit("status", True)
            await asyncio.sleep(0.15)
            self.buf.clear()
            await self.ping()
        except Exception as e:
            self.log("连接失败：%s: %s" % (type(e).__name__, e))
            self.emit("status", False)

    async def unpair(self):
        """清掉系统配对，修 start notify Unreachable（不需要管理员）。"""
        try:
            d = await BluetoothLEDevice.from_bluetooth_address_async(addr_to_int(self.addr))
            if d is None:
                self.log("系统里没有该设备，无法解绑")
                return
            info = d.device_information
            if info.pairing.is_paired:
                await info.pairing.unpair_async()
                self.log("已解除系统配对，准备重连")
            else:
                self.log("没有配对记录，跳过解绑")
        except Exception as e:
            self.log("解绑失败：%s" % e)

    async def disconnect(self):
        try:
            if self.bleak is not None:
                try:
                    await self.bleak.disconnect()
                except Exception:
                    pass
                self.bleak = None
            if self.char:
                self.char.remove_value_changed(self._on_value)
            if self.device:
                self.device.close()
            self.char = None
            self.device = None
            self.log("已断开")
        except Exception as e:
            self.log("断开出错：%s" % e)
        self.emit("status", False)

    async def send(self, data: bytes, wait_ack=True):
        if not self._ready():
            self.log("未连接")
            return False
        try:
            self.buf.clear()
            if self.backend == "bleak":
                await self.bleak.write_gatt_char(CHR, data, response=False)
            else:
                await self.char.write_value_with_result_async(to_buf(data))
        except Exception as e:
            self.log("写入失败：%s" % e)
            return False
        self.log("TX " + data.hex(" "))
        if not wait_ack:
            return True
        t0 = time.time()
        while time.time() - t0 < 2.0:
            if b"ok" in self.buf:
                self.log("RX ok  (%.0f ms)" % ((time.time() - t0) * 1000))
                return True
            await asyncio.sleep(0.02)
        self.log("无回应")
        return False

    async def ping(self):
        return await self.send(frame(CMD_NOP))

    async def tap(self, mask, hold_ms=90):
        """按下 + 松开。固件靠边沿触发，只发按下不生效第二次。"""
        await self.send(frame(CMD_KEY, bytes([mask & 7])), wait_ack=False)
        await asyncio.sleep(hold_ms / 1000.0)
        await self.send(frame(CMD_KEY, b"\x00"), wait_ack=False)

    async def snap(self):
        if not self._ready():
            self.log("未连接")
            return
        self.buf.clear()
        await self.send(frame(CMD_SNAP), wait_ack=False)
        hdr = bytes([MARK, CMD_IMG, 0xFF])
        t0 = time.time()
        start, total = -1, 0
        while time.time() - t0 < 180:
            b = self.buf
            if start < 0:
                i = b.find(hdr)
                if i >= 0 and len(b) >= i + 6:
                    start = i
                    total = b[i + 3] | (b[i + 4] << 8) | (b[i + 5] << 16)
                    self.log("帧头 %d 字节" % total)
            else:
                got = len(b) - start - 6
                self.emit("progress", max(0, min(100, got * 100 // max(1, total))))
                if len(b) >= start + 6 + total + 1:
                    break
            await asyncio.sleep(0.05)
        if start < 0 or len(self.buf) < start + 6 + total + 1:
            self.log("抓屏失败：超时或数据不全")
            self.emit("progress", 0)
            return
        payload = bytes(self.buf[start + 6: start + 6 + total])
        img = snap_to_image(payload)
        self.emit("progress", 0)
        if img is None:
            self.log("像素长度不对")
            return
        self.emit("image", img)
        self.log("抓屏完成 %dx%d，%.1f 秒" % (img.width, img.height, time.time() - t0))


class SerialWorker:
    """CH340 / 任意 USB-TTL 的串口通道。同步实现，调用方负责放线程。"""

    def __init__(self, ui_q: queue.Queue):
        self.q = ui_q
        self.ser = None
        self.port = "COM7"
        self.baud = 115200
        self.buf = bytearray()
        self.lock = threading.Lock()
        self.running = False

    def emit(self, kind, payload=None):
        self.q.put((kind, payload))

    def log(self, text):
        self.emit("log", "%s  %s" % (time.strftime("%H:%M:%S"), text))

    def submit(self, fn):
        """兼容 Worker.submit 的调用形状：直接放线程跑。"""
        threading.Thread(target=fn, daemon=True).start()

    def _reader(self):
        while self.running:
            try:
                d = self.ser.read(512)
            except Exception:
                break
            if d:
                with self.lock:
                    self.buf.extend(d)

    def connect(self):
        import serial
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=0.05)
        except Exception as e:
            self.log("打开 %s 失败：%s" % (self.port, e))
            self.emit("status", False)
            return
        self.running = True
        threading.Thread(target=self._reader, daemon=True).start()
        self.log("已打开 %s @ %d" % (self.port, self.baud))
        self.emit("status", True)
        self.ping()

    def disconnect(self):
        self.running = False
        try:
            if self.ser:
                self.ser.close()
            self.log("串口已关闭")
        except Exception as e:
            self.log("关闭出错：%s" % e)
        self.ser = None
        self.emit("status", False)

    def send(self, data: bytes, wait_ack=True):
        if not self.ser:
            self.log("未连接")
            return False
        with self.lock:
            self.buf.clear()
        self.ser.write(data)
        self.log("TX " + data.hex(" "))
        if not wait_ack:
            return True
        t0 = time.time()
        while time.time() - t0 < 1.5:
            with self.lock:
                if b"ok" in self.buf:
                    self.log("RX ok  (%.0f ms)" % ((time.time() - t0) * 1000))
                    return True
            time.sleep(0.01)
        self.log("无回应")
        return False

    def ping(self):
        return self.send(frame(CMD_NOP))

    def tap(self, mask, hold_ms=90):
        self.send(frame(CMD_KEY, bytes([mask & 7])), wait_ack=False)
        time.sleep(hold_ms / 1000.0)
        self.send(frame(CMD_KEY, b"\x00"), wait_ack=False)

    def snap(self):
        if not self.ser:
            self.log("未连接")
            return
        with self.lock:
            self.buf.clear()
        self.ser.write(frame(CMD_SNAP))
        hdr = bytes([MARK, CMD_IMG, 0xFF])
        t0 = time.time()
        start, total = -1, 0
        while time.time() - t0 < 60:
            with self.lock:
                b = bytes(self.buf)
            if start < 0:
                i = b.find(hdr)
                if i >= 0 and len(b) >= i + 6:
                    start = i
                    total = b[i + 3] | (b[i + 4] << 8) | (b[i + 5] << 16)
                    self.log("帧头 %d 字节" % total)
            else:
                got = len(b) - start - 6
                self.emit("progress", max(0, min(100, got * 100 // max(1, total))))
                if len(b) >= start + 6 + total + 1:
                    break
            time.sleep(0.02)
        if start < 0 or len(b) < start + 6 + total + 1:
            self.log("抓屏失败：超时或数据不全")
            self.emit("progress", 0)
            return
        img = snap_to_image(b[start + 6: start + 6 + total])
        self.emit("progress", 0)
        if img is None:
            self.log("像素长度不对")
            return
        self.emit("image", img)
        self.log("抓屏完成 %dx%d，%.2f 秒" % (img.width, img.height, time.time() - t0))


class App:
    def __init__(self, root):
        self.root = root
        self.q = queue.Queue()
        self.worker = Worker(self.q)
        self.worker.start()
        self.img = None
        self._build()
        self.root.after(50, self._pump)
        self.root.after(600, self.on_connect)

    def _build(self):
        r = self.root
        r.title("AT32 手表 · 调试台")
        r.configure(bg=BG)
        r.geometry("860x660+60+40")
        r.minsize(800, 600)
        style = ttk.Style(r)
        style.theme_use("clam")
        style.configure("TButton", background=PANEL, foreground=FG, borderwidth=0, padding=6)
        style.map("TButton", background=[("active", "#33363b")])

        top = tk.Frame(r, bg=BG)
        top.pack(fill="x", padx=12, pady=(10, 4))
        self.mode = tk.StringVar(value="ble")
        tk.Radiobutton(top, text="蓝牙", variable=self.mode, value="ble", bg=BG, fg=FG,
                       selectcolor=PANEL, activebackground=BG, activeforeground=ACC).pack(side="left")
        tk.Radiobutton(top, text="串口", variable=self.mode, value="ser", bg=BG, fg=FG,
                       selectcolor=PANEL, activebackground=BG, activeforeground=ACC).pack(side="left", padx=(0, 8))
        self.lbl_where = tk.Label(top, text="地址", bg=BG, fg=DIM)
        self.lbl_where.pack(side="left")
        self.addr_var = tk.StringVar(value=DEF_ADDR)
        tk.Entry(top, textvariable=self.addr_var, width=22, bg=PANEL, fg=FG,
                 insertbackground=FG, relief="flat").pack(side="left", padx=6, ipady=4)
        self.lbl_baud = tk.Label(top, text="波特率", bg=BG, fg=DIM)
        self.lbl_baud.pack(side="left")
        self.baud_var = tk.StringVar(value="115200")
        tk.Entry(top, textvariable=self.baud_var, width=8, bg=PANEL, fg=FG,
                 insertbackground=FG, relief="flat").pack(side="left", padx=6, ipady=4)
        self.mode.trace_add("write", lambda *_: self._mode_changed())
        ttk.Button(top, text="连接", command=self.on_connect).pack(side="left", padx=4)
        ttk.Button(top, text="断开", command=self.on_disconnect).pack(side="left", padx=4)
        self.lbl_state = tk.Label(top, text="● 未连接", bg=BG, fg="#c0564a", font=("", 10, "bold"))
        self.lbl_state.pack(side="left", padx=12)

        body = tk.Frame(r, bg=BG)
        body.pack(fill="both", expand=True, padx=12, pady=6)
        left = tk.Frame(body, bg=BG)
        left.pack(side="left", fill="y")

        def card(title):
            f = tk.Frame(left, bg=PANEL, padx=10, pady=8)
            f.pack(fill="x", pady=6)
            tk.Label(f, text=title, bg=PANEL, fg=ACC).pack(anchor="w")
            return f

        keys = card("按键")
        row = tk.Frame(keys, bg=PANEL)
        row.pack(pady=4)
        for text, mask in (("上一项", 1), ("确定", 2), ("下一项", 4)):
            ttk.Button(row, text=text, width=7,
                       command=lambda m=mask: self._dispatch(self.worker.tap, m)).pack(side="left", padx=3)

        ttk.Button(keys, text="长按确定（退菜单）", width=20,
                   command=lambda: self._dispatch(self.worker.tap, 2, 700)).pack(pady=(2, 0))

        light = card("亮度 / 系统")
        row2 = tk.Frame(light, bg=PANEL)
        row2.pack(pady=4)
        for t, v in (("低", 0), ("中", 1), ("高", 2)):
            ttk.Button(row2, text=t, width=5,
                       command=lambda n=v: self.on_send(frame(CMD_BRIGHT, bytes([n])))).pack(side="left", padx=3)
        row3 = tk.Frame(light, bg=PANEL)
        row3.pack(pady=4)
        ttk.Button(row3, text="校时", width=7, command=self.on_time).pack(side="left", padx=3)
        ttk.Button(row3, text="调试开", width=7,
                   command=lambda: self.on_send(frame(CMD_DEBUG, b"\x01"))).pack(side="left", padx=3)
        ttk.Button(row3, text="调试关", width=7,
                   command=lambda: self.on_send(frame(CMD_DEBUG, b"\x00"))).pack(side="left", padx=3)

        txt = card("消息 / 歌词")
        self.msg_var = tk.StringVar(value="Hello")
        tk.Entry(txt, textvariable=self.msg_var, bg=BG, fg=FG, relief="flat",
                 insertbackground=FG).pack(fill="x", pady=4, ipady=4)
        r2 = tk.Frame(txt, bg=PANEL)
        r2.pack(fill="x")
        for i in range(3):
            ttk.Button(r2, text="消息%d" % (i + 1), width=6,
                       command=lambda n=i: self.on_msg(n)).pack(side="left", padx=2)
        ttk.Button(r2, text="歌词", width=6, command=self.on_lyric).pack(side="left", padx=2)

        raw = card("原始命令（十六进制）")
        self.raw_var = tk.StringVar(value="06")
        tk.Entry(raw, textvariable=self.raw_var, bg=BG, fg=FG, relief="flat",
                 insertbackground=FG).pack(fill="x", pady=4, ipady=4)
        ttk.Button(raw, text="发送", command=self.on_raw).pack(anchor="e")

        right = tk.Frame(body, bg=BG)
        right.pack(side="left", fill="both", expand=True, padx=(12, 0))
        head = tk.Frame(right, bg=BG)
        head.pack(fill="x")
        ttk.Button(head, text="抓屏", command=self.on_snap).pack(side="left")
        ttk.Button(head, text="保存 PNG", command=self.on_save).pack(side="left", padx=6)
        self.pbar = ttk.Progressbar(head, length=220, maximum=100)
        self.pbar.pack(side="left", padx=10)

        holder = tk.Frame(right, bg="#111214", width=256, height=320)
        holder.pack(pady=8)
        holder.pack_propagate(False)
        self.canvas = tk.Label(holder, bg="#111214", text="尚无画面", fg=DIM)
        self.canvas.pack(fill="both", expand=True)

        tk.Label(r, text="日志", bg=BG, fg=ACC).pack(anchor="w", padx=12)
        self.logtxt = tk.Text(r, height=9, bg="#141518", fg="#cfd3d7", relief="flat",
                              insertbackground=FG)
        self.logtxt.pack(fill="x", padx=12, pady=(0, 10))

    def _pump(self):
        try:
            while True:
                kind, payload = self.q.get_nowait()
                if kind == "log":
                    self.logtxt.insert("end", payload + "\n")
                    self.logtxt.see("end")
                elif kind == "status":
                    if payload == "connecting":
                        self.lbl_state.config(text="● 连接中", fg=ACC)
                    else:
                        self.connected = bool(payload)
                        if payload:
                            self.retries = 0
                        self.lbl_state.config(text="● 已连接" if payload else "● 未连接",
                                              fg="#5cb85c" if payload else "#c0564a")
                elif kind == "image":
                    self.img = payload
                    photo = ImageTk.PhotoImage(payload.resize((256, 320), Image.NEAREST))
                    self.canvas.config(image=photo, text="")
                    self.canvas.image = photo
                elif kind == "progress":
                    self.pbar["value"] = payload
        except queue.Empty:
            pass
        self.root.after(50, self._pump)

    def _mode_changed(self):
        if self.mode.get() == "ser":
            self.lbl_where.config(text="端口")
            if self.addr_var.get().strip().upper().startswith("COM") is False:
                self.addr_var.set("COM7")
        else:
            self.lbl_where.config(text="地址")
            if self.addr_var.get().strip().upper().startswith("COM"):
                self.addr_var.set(DEF_ADDR)

    def _dispatch(self, method, *args):
        if isinstance(self.worker, SerialWorker):
            self.worker.submit(lambda m=method, a=args: m(*a))
        else:
            self.worker.submit(method(*args))

    def _auto_tick(self):
        """开机时蓝牙可能还没就绪：没连上就每 5 秒重试，最多 15 次。"""
        if self.auto and not self.connected and self.retries < 15:
            self.retries += 1
            if self.retries == 3 and not self.unpaired:
                self.unpaired = True
                self.worker.log("连续失败，试着解除系统配对（自愈）")
                self._dispatch(self.worker.unpair)
            self.worker.log("自动重连 %d/15" % self.retries)
            self.on_connect()
        self.root.after(5000, self._auto_tick)

    def on_connect(self):
        if self.mode.get() == "ser":
            w = SerialWorker(self.q)
            w.port = self.addr_var.get().strip() or "COM7"
            try:
                w.baud = int(self.baud_var.get().strip())
            except ValueError:
                w.baud = 115200
            self.worker = w
            w.submit(w.connect)
        else:
            w = Worker(self.q)
            w.addr = self.addr_var.get().strip() or DEF_ADDR
            w.start()
            self.worker = w
            w.submit(w.connect())

    def on_disconnect(self):
        self.auto = False
        self.retries = 15
        self._dispatch(self.worker.disconnect)

    def on_send(self, data):
        self._dispatch(self.worker.send, data)

    def on_time(self):
        t = time.localtime()
        self.on_send(frame(CMD_TIME, bytes([t.tm_year & 0xFF, t.tm_year >> 8, t.tm_mon, t.tm_mday,
                                            t.tm_hour, t.tm_min, t.tm_sec])))

    def on_msg(self, idx):
        body = self.msg_var.get().encode("utf-8")[:23]
        self.on_send(frame(CMD_MSG, bytes([idx]) + body))

    def on_lyric(self):
        body = self.msg_var.get().encode("utf-8")[:27]
        self.on_send(frame(CMD_LYRIC, body))

    def on_raw(self):
        try:
            cmd = int(self.raw_var.get().strip(), 16)
        except ValueError:
            messagebox.showerror("命令", "十六进制命令字节，例如 06")
            return
        self.on_send(frame(cmd))

    def on_snap(self):
        self._dispatch(self.worker.snap)

    def on_save(self):
        if self.img is None:
            messagebox.showinfo("保存", "先抓一帧")
            return
        path = filedialog.asksaveasfilename(defaultextension=".png",
                                            filetypes=[("PNG", "*.png")])
        if path:
            self.img.save(path)
            self.worker.log("已保存 %s" % path)


if __name__ == "__main__":
    root = tk.Tk()
    App(root)
    root.mainloop()
