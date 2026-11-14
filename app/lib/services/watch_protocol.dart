// AT32 手表助手 · GPL-3.0-or-later
//
// 手表蓝牙协议（ui/src/link.c）：
//   帧 = A5 命令 长度 数据... 异或，异或从 A5 一直算到数据末尾
//   合法帧回一行 "ok"，校验错回 "bad"
// 本文件只做纯字节处理，不依赖 Flutter，便于单测与命令行工具复用。

import 'dart:convert';
import 'dart:typed_data';

const int watchMark = 0xA5;

/// 协议命令号（与固件 link.c 一致）。
abstract final class WatchCmd {
  static const int time = 0x01;
  static const int message = 0x02;
  static const int lyric = 0x03;
  static const int playState = 0x04;
  static const int brightness = 0x05;
  static const int snapshot = 0x06;
  static const int key = 0x07;
  static const int debug = 0x08;
  static const int perf = 0x09;
  static const int home = 0x0A;
  static const int mediaBitmap = 0x0B;
  static const int control = 0x10;
  static const int image = 0x11;
  static const int stats = 0x12;
  static const int sensor = 0x13;
  static const int setting = 0x18;
  static const int getSettings = 0x19;
  static const int settingsState = 0x1A;
  static const int sport = 0x27;
  static const int nop = 0x7F;
}

/// 手表主动回传的播放控制动作。
abstract final class WatchControl {
  static const int play = 1;
  static const int pause = 2;
  static const int next = 3;
  static const int previous = 4;
}

/// 0x1A：手表当前设置。key 与固件 ui_settings_apply 一致：
/// 1 抬腕 2 按键音 3 亮度 4 蓝牙 5 状态灯 6 熄屏。
class WatchSettings {
  const WatchSettings({
    required this.raiseWrist,
    required this.keyBeep,
    required this.brightness,
    required this.bluetooth,
    required this.ledMode,
    required this.screenOff,
  });

  final bool raiseWrist;
  final bool keyBeep;
  final int brightness;
  final bool bluetooth;
  final int ledMode;
  final int screenOff;

  static WatchSettings? fromFrame(WatchFrame frame) {
    if (frame.data.length < 6) return null;
    return WatchSettings(
      raiseWrist: frame.data[0] != 0,
      keyBeep: frame.data[1] != 0,
      brightness: frame.data[2].clamp(0, 2).toInt(),
      bluetooth: frame.data[3] != 0,
      ledMode: frame.data[4].clamp(0, 2).toInt(),
      screenOff: frame.data[5].clamp(0, 4).toInt(),
    );
  }
}

/// 0x27：手表今日运动数据。
/// 5 个 u32（步数/距离米/卡路里/活跃分钟/目标）+ 12 个 u16 桶（每 2 小时一段）。
class WatchSport {
  const WatchSport({
    required this.steps,
    required this.distanceM,
    required this.calories,
    required this.activeMin,
    required this.goal,
    required this.buckets,
  });

  final int steps;
  final int distanceM;
  final int calories;
  final int activeMin;
  final int goal;
  final List<int> buckets;

  static WatchSport? fromFrame(WatchFrame frame) {
    final data = frame.data;
    if (frame.cmd != WatchCmd.sport || data.length < 44) return null;
    int u32(int offset) =>
        data[offset] |
        (data[offset + 1] << 8) |
        (data[offset + 2] << 16) |
        (data[offset + 3] << 24);
    int u16(int offset) => data[offset] | (data[offset + 1] << 8);
    return WatchSport(
      steps: u32(0),
      distanceM: u32(4),
      calories: u32(8),
      activeMin: u32(12),
      goal: u32(16),
      buckets: <int>[for (var i = 0; i < 12; i++) u16(20 + i * 2)],
    );
  }
}

const int watchMessageMax = 23;
const int watchLyricMax = 27;

/// 0x0B 点阵：128×32 单色位图，按 4 块、每块 128 字节下发。
const int watchBitmapWidth = 128;
const int watchBitmapHeight = 32;
const int watchBitmapBytes = watchBitmapWidth * watchBitmapHeight ~/ 8;
const int watchBitmapChunk = 128;
const int watchBitmapChunks = watchBitmapBytes ~/ watchBitmapChunk;

/// 组一帧：A5 命令 长度 数据 异或。
Uint8List buildWatchFrame(int cmd, [List<int> payload = const []]) {
  final frame = Uint8List(payload.length + 4);
  frame[0] = watchMark;
  frame[1] = cmd & 0xFF;
  frame[2] = payload.length & 0xFF;
  frame.setRange(3, 3 + payload.length, payload);
  var check = 0;
  for (var i = 0; i < frame.length - 1; i++) {
    check ^= frame[i];
  }
  frame[frame.length - 1] = check;
  return frame;
}

/// 按 UTF-8 字符边界截断，避免把汉字切成半个。
Uint8List fitUtf8(String text, int limit) {
  final bytes = utf8.encode(text);
  if (bytes.length <= limit) return Uint8List.fromList(bytes);
  var end = limit;
  while (end > 0 && (bytes[end] & 0xC0) == 0x80) {
    end--;
  }
  return Uint8List.fromList(bytes.sublist(0, end));
}

/// 手表回传/请求的完整帧。
class WatchFrame {
  const WatchFrame(this.cmd, this.data);

  final int cmd;
  final Uint8List data;

  @override
  String toString() =>
      'WatchFrame(0x${cmd.toRadixString(16)}, ${data.length}B)';
}

/// 流式拆帧：普通帧按一字节长度，图像帧（11 FF）按三字节长度。
/// 帧之外的文本（ok / bad / DBG ...）按行回调。
class WatchFrameParser {
  WatchFrameParser({required this.onFrame, this.onLine});

  final void Function(WatchFrame frame) onFrame;
  final void Function(String line)? onLine;

  static const int _maxDataLength = 64;
  static const int _maxImagePayload = 4 + 128 * 160 * 2;

  final List<int> _buffer = <int>[];
  final StringBuffer _line = StringBuffer();

  void add(Uint8List bytes) {
    _buffer.addAll(bytes);
    _drain();
  }

  void reset() {
    _buffer.clear();
    _line.clear();
  }

  void _drain() {
    while (_buffer.isNotEmpty) {
      final mark = _buffer.indexOf(watchMark);
      if (mark < 0) {
        _appendText(_buffer);
        _buffer.clear();
        return;
      }
      if (mark > 0) {
        _appendText(_buffer.sublist(0, mark));
        _buffer.removeRange(0, mark);
      }
      if (_buffer.length < 3) return;

      final cmd = _buffer[1];
      final lengthByte = _buffer[2];
      final int headerLength;
      final int payloadLength;
      if (cmd == WatchCmd.image && lengthByte == 0xFF) {
        if (_buffer.length < 6) return;
        payloadLength = _buffer[3] | (_buffer[4] << 8) | (_buffer[5] << 16);
        headerLength = 6;
        if (payloadLength < 4 || payloadLength > _maxImagePayload) {
          _buffer.removeAt(0);
          continue;
        }
      } else {
        headerLength = 3;
        payloadLength = lengthByte;
        if (payloadLength > _maxDataLength) {
          _buffer.removeAt(0);
          continue;
        }
      }

      final frameLength = headerLength + payloadLength + 1;
      if (_buffer.length < frameLength) return;

      var check = 0;
      for (var i = 0; i < frameLength - 1; i++) {
        check ^= _buffer[i];
      }
      if (check != _buffer[frameLength - 1]) {
        // 校验失败：跳过这个 A5，继续往后找帧头。
        _buffer.removeAt(0);
        continue;
      }

      final data = Uint8List.fromList(
        _buffer.sublist(headerLength, headerLength + payloadLength),
      );
      _buffer.removeRange(0, frameLength);
      onFrame(WatchFrame(cmd, data));
    }
  }

  void _appendText(List<int> bytes) {
    if (bytes.isEmpty) return;
    final text = utf8.decode(bytes, allowMalformed: true);
    for (var i = 0; i < text.length; i++) {
      final char = text[i];
      if (char == '\n') {
        final line = _line.toString().trim();
        _line.clear();
        if (line.isNotEmpty) onLine?.call(line);
      } else if (char != '\r' && char.codeUnitAt(0) >= 0x20) {
        if (_line.length < 4096) _line.write(char);
      }
    }
  }
}

/// 手表每秒回传的遥测（0x12，8 个 u32 小端）。
class WatchStats {
  const WatchStats({
    required this.prepareUs,
    required this.rasterUs,
    required this.waitUs,
    required this.frameUs,
    required this.pixels,
    required this.bands,
    required this.dropped,
    required this.ramUsedBytes,
    required this.ramTotalBytes,
    required this.cpuTenths,
    required this.fps,
  });

  final int prepareUs;
  final int rasterUs;
  final int waitUs;
  final int frameUs;
  final int pixels;
  final int bands;
  final int dropped;
  final int ramUsedBytes;
  final int ramTotalBytes;
  final int cpuTenths;
  final int fps;

  double get ramUsedKb => (ramUsedBytes + 512) / 1024;
  double get ramTotalKb => (ramTotalBytes + 512) / 1024;
  double get cpu => cpuTenths / 10;

  static WatchStats? fromFrame(WatchFrame frame) {
    final data = frame.data;
    if (frame.cmd != WatchCmd.stats || data.length < 32) return null;
    int u32(int offset) =>
        data[offset] |
        (data[offset + 1] << 8) |
        (data[offset + 2] << 16) |
        (data[offset + 3] << 24);

    final bandsPacked = u32(20);
    final ramPacked = u32(24);
    final ramFpsPacked = u32(28);
    return WatchStats(
      prepareUs: u32(0),
      rasterUs: u32(4),
      waitUs: u32(8),
      frameUs: u32(12),
      pixels: u32(16),
      bands: bandsPacked & 0xFFFF,
      dropped: (bandsPacked >> 16) & 0xFFFF,
      ramUsedBytes: ramPacked & 0xFFFF,
      cpuTenths: (ramPacked >> 16) & 0xFFFF,
      ramTotalBytes: ramFpsPacked & 0xFFFF,
      fps: (ramFpsPacked >> 16) & 0xFFFF,
    );
  }
}

/// 抓屏回传的 128×160 画面（RGB565 大端，已转成 RGBA8888）。
class WatchSnapshot {
  const WatchSnapshot({
    required this.width,
    required this.height,
    required this.rgba,
  });

  final int width;
  final int height;
  final Uint8List rgba;

  static WatchSnapshot? fromFrame(WatchFrame frame) {
    final data = frame.data;
    if (frame.cmd != WatchCmd.image || data.length < 4) return null;
    final width = data[0] | (data[1] << 8);
    final height = data[2] | (data[3] << 8);
    if (width <= 0 || height <= 0) return null;
    if (data.length < 4 + width * height * 2) return null;

    final rgba = Uint8List(width * height * 4);
    for (var i = 0; i < width * height; i++) {
      final hi = data[4 + i * 2];
      final lo = data[5 + i * 2];
      final value = (hi << 8) | lo;
      final r = (value >> 11) & 0x1F;
      final g = (value >> 5) & 0x3F;
      final b = value & 0x1F;
      final offset = i * 4;
      rgba[offset] = (r << 3) | (r >> 2);
      rgba[offset + 1] = (g << 2) | (g >> 4);
      rgba[offset + 2] = (b << 3) | (b >> 2);
      rgba[offset + 3] = 0xFF;
    }
    return WatchSnapshot(width: width, height: height, rgba: rgba);
  }
}
