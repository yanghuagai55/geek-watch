"""Measure the STM32's on-demand render telemetry over a Windows serial port."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import functools
import hashlib
import json
from pathlib import Path
import statistics
import struct
import time


class DCB(c.Structure):
    _fields_ = [('length', w.DWORD), ('baud', w.DWORD), ('flags', w.DWORD),
                ('reserved', w.WORD), ('xon_lim', w.WORD), ('xoff_lim', w.WORD),
                ('bytes', w.BYTE), ('parity', w.BYTE), ('stop', w.BYTE),
                ('xon', c.c_char), ('xoff', c.c_char), ('error', c.c_char),
                ('eof', c.c_char), ('event', c.c_char), ('reserved1', w.WORD)]


class Timeouts(c.Structure):
    _fields_ = [(n, w.DWORD) for n in ('interval', 'read_mult', 'read_const',
                                    'write_mult', 'write_const')]


class Serial:
    def __init__(self, port):
        self.k = c.WinDLL('kernel32', use_last_error=True)
        signatures = {
            'CreateFileW': ([w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p,
                             w.DWORD, w.DWORD, w.HANDLE], w.HANDLE),
            'GetCommState': ([w.HANDLE, c.POINTER(DCB)], w.BOOL),
            'SetCommState': ([w.HANDLE, c.POINTER(DCB)], w.BOOL),
            'GetCommTimeouts': ([w.HANDLE, c.POINTER(Timeouts)], w.BOOL),
            'SetCommTimeouts': ([w.HANDLE, c.POINTER(Timeouts)], w.BOOL),
            'ReadFile': ([w.HANDLE, c.c_void_p, w.DWORD, c.POINTER(w.DWORD), c.c_void_p], w.BOOL),
            'WriteFile': ([w.HANDLE, c.c_void_p, w.DWORD, c.POINTER(w.DWORD), c.c_void_p], w.BOOL),
            'CloseHandle': ([w.HANDLE], w.BOOL),
        }
        for name, (args, result) in signatures.items():
            fn = getattr(self.k, name)
            fn.argtypes, fn.restype = args, result
        self.h = self.k.CreateFileW('\\\\.\\' + port, 0xC0000000, 0, None, 3, 0, None)
        if self.h == c.c_void_p(-1).value:
            raise c.WinError(c.get_last_error())
        self.saved = False
        self.old, self.old_to = DCB(), Timeouts()
        self.old.length = c.sizeof(DCB)
        self.buffer = bytearray()
        try:
            self.check(self.k.GetCommState(self.h, c.byref(self.old)))
            self.check(self.k.GetCommTimeouts(self.h, c.byref(self.old_to)))
            self.saved = True
            new = DCB.from_buffer_copy(self.old)
            new.baud, new.bytes, new.parity, new.stop = 115200, 8, 0, 0
            if not (new.flags & 1):
                raise RuntimeError('Binary mode must already be enabled')
            self.check(self.k.SetCommState(self.h, c.byref(new)))
            self.check(self.k.SetCommTimeouts(self.h, c.byref(Timeouts(0xFFFFFFFF, 0, 5, 0, 2000))))
        except Exception:
            self.close()
            raise

    @staticmethod
    def check(ok):
        if not ok:
            raise c.WinError(c.get_last_error())

    def close(self):
        if self.saved:
            self.k.SetCommState(self.h, c.byref(self.old))
            self.k.SetCommTimeouts(self.h, c.byref(self.old_to))
        self.k.CloseHandle(self.h)

    def read(self):
        buf, n = c.create_string_buffer(8192), w.DWORD()
        self.check(self.k.ReadFile(self.h, buf, len(buf), c.byref(n), None))
        return buf.raw[:n.value]

    def send(self, command, payload=b''):
        frame = bytes((0xA5, command, len(payload))) + payload
        frame += bytes((functools.reduce(int.__xor__, frame, 0),))
        n = w.DWORD()
        self.check(self.k.WriteFile(self.h, frame, len(frame), c.byref(n), None))
        if n.value != len(frame):
            raise RuntimeError('Short serial write')

    def receive(self, command, timeout=3):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            at = self.buffer.find(bytes((0xA5, command)))
            if at >= 0 and len(self.buffer) >= at + 3:
                length, header = self.buffer[at + 2], 3
                if length == 0xFF:
                    if len(self.buffer) < at + 6:
                        self.buffer.extend(self.read())
                        continue
                    length = int.from_bytes(self.buffer[at + 3:at + 6], 'little')
                    header = 6
                    if not 4 <= length <= 4 + 240 * 240 * 2:
                        raise RuntimeError('Invalid screenshot length')
                end = at + header + length + 1
                if len(self.buffer) >= end:
                    frame = self.buffer[at:end]
                    del self.buffer[:end]
                    if functools.reduce(int.__xor__, frame, 0):
                        raise RuntimeError('Response XOR mismatch')
                    return bytes(frame[header:-1])
            self.buffer.extend(self.read())
            if len(self.buffer) > 200000:
                raise RuntimeError('Unexpected serial stream')
        raise TimeoutError(f'No valid response 0x{command:02x}')

    def stats(self):
        self.send(9)
        data = self.receive(0x12)
        if len(data) != 32:
            raise RuntimeError('Invalid performance packet length')
        values = struct.unpack('<8I', data)
        result = dict(zip(('prepare_us', 'raster_us', 'wait_us', 'frame_us',
                           'pixels', 'band_drop', 'ram_used', 'ram_budget'), values))
        packed = result.pop('band_drop')
        result.update(bands=packed & 65535, dropped=packed >> 16,
                      received_at=time.monotonic())
        return result

    def screenshot(self, path):
        self.send(6)
        data = self.receive(0x11, 15)
        width, height = struct.unpack_from('<HH', data)
        if width <= 0 or height <= 0 or len(data) != 4 + width * height * 2:
            raise RuntimeError('Invalid screenshot dimensions')
        row = (width * 3 + 3) & ~3
        pixels = bytearray(row * height)
        for y in range(height):
            for x in range(width):
                p = struct.unpack_from('>H', data, 4 + 2 * (y * width + x))[0]
                r, g, b = (p >> 11) & 31, (p >> 5) & 63, p & 31
                i = (height - 1 - y) * row + x * 3
                pixels[i:i + 3] = bytes(((b << 3) | (b >> 2), (g << 2) | (g >> 4), (r << 3) | (r >> 2)))
        header = struct.pack('<2sIHHIIIIHHIIIIII', b'BM', 54 + len(pixels), 0, 0,
                             54, 40, width, height, 1, 24, 0, len(pixels), 0, 0, 0, 0)
        path.write_bytes(header + pixels)
        return {'width': width, 'height': height, 'rgb565_sha256': hashlib.sha256(data[4:]).hexdigest()}


def summarize(rows):
    result = {'samples': len(rows), 'zero_pixel_frames': sum(r['pixels'] == 0 for r in rows)}
    for field in ('prepare_us', 'raster_us', 'wait_us', 'frame_us', 'pixels'):
        values = sorted(r[field] for r in rows)
        result[field] = {'median': statistics.median(values),
                         'p95': values[min(len(values) - 1, (len(values) * 95 + 99) // 100 - 1)],
                         'max': max(values)}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM5')
    parser.add_argument('--out', type=Path, default=Path(__file__).resolve().parent / 'obj' / 'device-optimized')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    serial = Serial(args.port)
    result = {'port': args.port, 'time': time.strftime('%Y-%m-%dT%H:%M:%S%z'),
              'note': 'MCU render times exclude UART reply and frame pacing; screenshots are software output, not LCD readback.',
              'samples': [], 'screenshots': {}}
    try:
        serial.stats()
        serial.send(8, b'\x01')
        def sample(stage, count):
            for _ in range(count):
                row = serial.stats()
                row['stage'] = stage
                result['samples'].append(row)
                if row['dropped']:
                    raise RuntimeError('Draw commands dropped')
        def tap(bit, name):
            serial.send(7, bytes((bit,)))
            sample(name, 2)
            serial.send(7, b'\x00')
            sample(name, 24)
        def shot(name):
            result['screenshots'][name] = serial.screenshot(args.out / (name + '.bmp'))
            sample('post_screenshot', 36)
        sample('idle_face', 80)
        shot('face')
        tap(4, 'slide_sport')
        shot('sport')
        tap(2, 'open_sport')
        shot('sport_open')
        serial.send(7, b'\x02')
        sample('long_back', 45)
        serial.send(7, b'\x00')
        sample('back_settle', 30)
        tap(4, 'slide_messages')
        tap(2, 'open_messages')
        shot('messages')
        tap(2, 'push_detail')
        shot('message_detail')
        for _ in range(2):
            serial.send(7, b'\x02')
            sample('long_back', 42)
            serial.send(7, b'\x00')
            sample('back_settle', 24)
        tap(4, 'slide_settings')
        tap(2, 'open_settings')
        shot('settings')
        serial.send(7, b'\x02')
        sample('long_back', 42)
        serial.send(7, b'\x00')
        sample('back_settle', 24)
        for _ in range(8):
            for bit in (1, 4):
                serial.send(7, bytes((bit,)))
                sample('rapid_reverse', 2)
                serial.send(7, b'\x00')
                sample('rapid_reverse', 2)
        sample('final_settle', 40)
        for _ in range(3):
            tap(1, 'return_face')
        serial.send(8, b'\x00')
        sample('idle_clean', 80)
        shot('face_clean')
    finally:
        try:
            serial.send(7, b'\x00')
            serial.send(8, b'\x00')
        finally:
            serial.close()
        if result['samples']:
            stages = sorted({r['stage'] for r in result['samples']})
            result['summary'] = {s: summarize([r for r in result['samples'] if r['stage'] == s]) for s in stages}
            active = [r for r in result['samples'] if r['pixels'] and r['stage'] != 'post_screenshot']
            if active:
                result['active'] = summarize(active)
        (args.out / 'metrics.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({'active': result.get('active'),
                      'idle': result['summary'].get('idle_clean'),
                      'ram': result['samples'][-1]['ram_used'],
                      'budget': result['samples'][-1]['ram_budget']}, indent=2))


if __name__ == '__main__':
    main()
