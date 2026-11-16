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
  static const int msgListBmp = 0x22;
  static const int msgDetailBmp = 0x23;
  static const int msgClear = 0x24;
  static const int msgCount = 0x25;
  static const int maskRle = 0x26;
  static const int sport = 0x27;
  static const int extImgMeta = 0x28;
  static const int extImgData = 0x29;
  static const int setConfig = 0x2A;
  static const int getConfig = 0x2B;
  static const int configState = 0x2C;
  static const int extOp = 0x2D;
  static const int extPoolState = 0x2E;
  static const int extAsset = 0x2F;
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
/// 1 抬腕 2 震动(1207马达) 3 亮度 4 蓝牙 5 状态灯 6 熄屏。
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

/// 手表物理按键掩码（与 `ui/hal.h` 中 `HAL_KEY1/2/3` 一致）。
abstract final class WatchKeyMask {
  static const int key1 = 0x01; // 返回 / 退出传感器界面
  static const int key2 = 0x02; // 确认 / KEY2 磁强计 360° 校准启停
  static const int key3 = 0x04; // 上移 / KEY3 零偏姿态复位
}

/// 9 轴姿态与传感器调试快照（对应固件 `0x13` 下发后的 `DBG ...` 文本行）。
class WatchSensorData {
  const WatchSensorData({
    required this.rawLine,
    required this.mpuOk,
    required this.magOk,
    required this.magChip,
    required this.ax,
    required this.ay,
    required this.az,
    required this.mx,
    required this.my,
    required this.mz,
    required this.fusedAxes,
    required this.rollDeg,
    required this.pitchDeg,
    required this.headingDeg,
    required this.batteryMv,
    required this.timestamp,
    this.magCalState,
    this.gyroCalState,
  });

  final String rawLine;
  final bool mpuOk;
  final bool magOk;
  final String magChip;
  final int ax;
  final int ay;
  final int az;
  final int mx;
  final int my;
  final int mz;
  final int fusedAxes;
  final double rollDeg;
  final double pitchDeg;
  final double headingDeg;
  final int batteryMv;
  final DateTime timestamp;
  final int? magCalState;
  final int? gyroCalState;

  String get dofLabel => switch (fusedAxes) {
    9 => '9-DOF',
    6 => '6-DOF',
    3 => '3-DOF',
    _ => '--',
  };

  /// 从串口/蓝牙返回文本行解析 9 轴快照。非 `DBG` 行或无效行返回 null。
  static WatchSensorData? tryParseDebugLine(
    String line, {
    DateTime? timestamp,
  }) {
    final trimmed = line.trim();
    if (!trimmed.toUpperCase().startsWith('DBG ')) return null;
    final body = trimmed.substring(4).trim();
    if (body.isEmpty) return null;

    var hasTelemetry = false;
    var mpuOk = false;
    var magOk = false;
    var magChip = '';
    var ax = 0;
    var ay = 0;
    var az = 1000;
    var mx = 0;
    var my = 0;
    var mz = 0;
    var hasMagVec = false;
    var fusedAxes = 0;
    var rollDeg = 0.0;
    var pitchDeg = 0.0;
    var headingDeg = 0.0;
    var batteryMv = 0;
    int? magCalState;
    int? gyroCalState;

    List<int>? parseTriplet(String raw) {
      final parts = raw.split(',');
      if (parts.length != 3) return null;
      final v0 = int.tryParse(parts[0].trim());
      final v1 = int.tryParse(parts[1].trim());
      final v2 = int.tryParse(parts[2].trim());
      if (v0 == null || v1 == null || v2 == null) return null;
      return <int>[v0, v1, v2];
    }

    for (final rawToken in body.split(';')) {
      final token = rawToken.trim();
      if (token.isEmpty) continue;
      if (token.startsWith('MPU=')) {
        mpuOk = token.substring(4) == '1';
        hasTelemetry = true;
      } else if (token.startsWith('QMC') || token.startsWith('MAG')) {
        magChip = token;
        if (token.contains('@')) magOk = true;
      } else if (token.startsWith('A=')) {
        final v = parseTriplet(token.substring(2));
        if (v != null) {
          ax = v[0];
          ay = v[1];
          az = v[2];
          hasTelemetry = true;
        }
      } else if (token.startsWith('M=')) {
        final v = parseTriplet(token.substring(2));
        if (v != null) {
          mx = v[0];
          my = v[1];
          mz = v[2];
          hasMagVec = true;
          hasTelemetry = true;
        }
      } else if (token.startsWith('9AX=')) {
        final parsed = int.tryParse(token.substring(4).trim());
        if (parsed != null) {
          fusedAxes = parsed;
          hasTelemetry = true;
        }
      } else if (token.startsWith('RPY=')) {
        final v = parseTriplet(token.substring(4));
        if (v != null) {
          rollDeg = v[0].toDouble();
          pitchDeg = v[1].toDouble();
          headingDeg = ((v[2] % 360) + 360) % 360.0;
          hasTelemetry = true;
        }
      } else if (token.startsWith('BAT=')) {
        final mv = int.tryParse(token.substring(4).trim());
        if (mv != null) batteryMv = mv;
      } else if (token.startsWith('CAL=')) {
        final parts = token.substring(4).split(',');
        if (parts.isNotEmpty) magCalState = int.tryParse(parts[0].trim());
        if (parts.length > 1) gyroCalState = int.tryParse(parts[1].trim());
      }
    }

    if (!hasTelemetry) return null;
    if (fusedAxes == 9 || hasMagVec) magOk = true;
    return WatchSensorData(
      rawLine: trimmed,
      mpuOk: mpuOk || fusedAxes >= 6,
      magOk: magOk,
      magChip: magChip,
      ax: ax,
      ay: ay,
      az: az,
      mx: mx,
      my: my,
      mz: mz,
      fusedAxes: fusedAxes,
      rollDeg: rollDeg,
      pitchDeg: pitchDeg,
      headingDeg: headingDeg,
      batteryMv: batteryMv,
      timestamp: timestamp ?? DateTime.now(),
      magCalState: magCalState,
      gyroCalState: gyroCalState,
    );
  }
}

int argbToRgb565(int argb) {
  final r = (argb >> 16) & 0xFF;
  final g = (argb >> 8) & 0xFF;
  final b = argb & 0xFF;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

int rgb565ToArgb(int c565) {
  final r5 = (c565 >> 11) & 0x1F;
  final g6 = (c565 >> 5) & 0x3F;
  final b5 = c565 & 0x1F;
  final r = (r5 << 3) | (r5 >> 2);
  final g = (g6 << 2) | (g6 >> 4);
  final b = (b5 << 3) | (b5 >> 2);
  return 0xFF000000 | (r << 16) | (g << 8) | b;
}

/// 手表 0x2A/0x2C 完整持久化配置 + 表盘样式 + AT24C256 I2C Flash 状态。
class WatchConfigBundle {
  const WatchConfigBundle({
    required this.saveToI2c,
    required this.appOverride,
    required this.use24h,
    required this.debug,
    required this.raiseWrist,
    required this.keyBeep,
    required this.brightness,
    required this.ledMode,
    required this.screenOff,
    required this.stepGoal,
    required this.dialId,
    required this.dialTop,
    required this.dialBot,
    required this.dialFlags,
    required this.dialTextColor,
    required this.dialAccentColor,
    required this.dialBgColor,
    this.autoClearMessages = false,
    this.extFlashPresent = false,
    this.cfgLoadedFromI2c = false,
    this.imgReady = false,
    this.magCalSaved = false,
    this.cfgSource = 0,
    this.imgBytes = 0,
    this.imgFmt = 0,
    this.extI2cAddr = 0,
  });

  final bool saveToI2c;
  final bool appOverride;
  final bool use24h;
  final bool debug;
  final bool autoClearMessages;
  final bool raiseWrist;
  final bool keyBeep;
  final int brightness;
  final int ledMode;
  final int screenOff;
  final int stepGoal;
  final int dialId;
  final int dialTop;
  final int dialBot;
  final int dialFlags;
  final int dialTextColor;
  final int dialAccentColor;
  final int dialBgColor;
  final bool extFlashPresent;
  final bool cfgLoadedFromI2c;
  final bool imgReady;
  final bool magCalSaved;
  final int cfgSource;
  final int imgBytes;
  final int imgFmt;
  final int extI2cAddr;

  /// 编码为 `0x2A` 下发给单片机的 20 字节载荷（与 C `settings_pack_wire` 一一对应）。
  Uint8List toSetConfigPayload() {
    final out = Uint8List(20);
    final goal = stepGoal.clamp(1000, 100000);
    out[0] = (saveToI2c ? 0x01 : 0) |
        (appOverride ? 0x02 : 0) |
        (use24h ? 0x04 : 0) |
        (debug ? 0x08 : 0) |
        (autoClearMessages ? 0x20 : 0);
    out[1] = raiseWrist ? 1 : 0;
    out[2] = keyBeep ? 1 : 0;
    out[3] = brightness.clamp(0, 2);
    out[4] = ledMode.clamp(0, 2);
    out[5] = screenOff.clamp(0, 4);
    out[6] = goal & 0xFF;
    out[7] = (goal >> 8) & 0xFF;
    out[8] = (goal >> 16) & 0xFF;
    out[9] = (goal >> 24) & 0xFF;
    out[10] = dialId.clamp(0, 3);
    out[11] = dialTop.clamp(0, 4);
    out[12] = dialBot.clamp(0, 4);
    out[13] = dialFlags & 0xFF;
    final fg = argbToRgb565(dialTextColor);
    final ac = argbToRgb565(dialAccentColor);
    final bg = argbToRgb565(dialBgColor);
    out[14] = fg & 0xFF;
    out[15] = (fg >> 8) & 0xFF;
    out[16] = ac & 0xFF;
    out[17] = (ac >> 8) & 0xFF;
    out[18] = bg & 0xFF;
    out[19] = (bg >> 8) & 0xFF;
    return out;
  }

  static WatchConfigBundle? fromFrame(WatchFrame frame) {
    final d = frame.data;
    if (frame.cmd != WatchCmd.configState || d.length < 26) return null;
    int u16(int off) => d[off] | (d[off + 1] << 8);
    int u32(int off) =>
        d[off] | (d[off + 1] << 8) | (d[off + 2] << 16) | (d[off + 3] << 24);
    final hw = d[20];
    return WatchConfigBundle(
      saveToI2c: (d[0] & 0x01) != 0,
      appOverride: (d[0] & 0x02) != 0,
      use24h: (d[0] & 0x04) != 0,
      debug: (d[0] & 0x08) != 0,
      autoClearMessages: (d[0] & 0x20) != 0,
      raiseWrist: d[1] != 0,
      keyBeep: d[2] != 0,
      brightness: d[3].clamp(0, 2),
      ledMode: d[4].clamp(0, 2),
      screenOff: d[5].clamp(0, 4),
      stepGoal: u32(6),
      dialId: d[10].clamp(0, 3),
      dialTop: d[11].clamp(0, 4),
      dialBot: d[12].clamp(0, 4),
      dialFlags: d[13],
      dialTextColor: rgb565ToArgb(u16(14)),
      dialAccentColor: rgb565ToArgb(u16(16)),
      dialBgColor: rgb565ToArgb(u16(18)),
      extFlashPresent: (hw & 0x01) != 0,
      cfgLoadedFromI2c: (hw & 0x02) != 0,
      imgReady: (hw & 0x04) != 0,
      magCalSaved: (hw & 0x08) != 0,
      cfgSource: d[21],
      imgBytes: u16(22),
      imgFmt: d[24],
      extI2cAddr: d[25],
    );
  }
}

/// AT24C256 统一外置动态 ROM Pool 状态（0x2E，12/13 字节）。
class WatchExtPoolState {
  const WatchExtPoolState({
    required this.poolTotal,
    required this.usedBytes,
    required this.freeContiguous,
    required this.msgBytes,
    required this.imgBytes,
    required this.msgCount,
    required this.msgMax,
    this.assetFlags = 0,
    this.hasAssetFlags = false,
  });

  final int poolTotal;
  final int usedBytes;
  final int freeContiguous;
  final int msgBytes;
  final int imgBytes;
  final int msgCount;
  final int msgMax;
  final int assetFlags;
  final bool hasAssetFlags;

  static WatchExtPoolState? fromFrame(WatchFrame frame) {
    final d = frame.data;
    if (frame.cmd != WatchCmd.extPoolState || d.length < 12) return null;
    int u16(int off) => d[off] | (d[off + 1] << 8);
    return WatchExtPoolState(
      poolTotal: u16(0),
      usedBytes: u16(2),
      freeContiguous: u16(4),
      msgBytes: u16(6),
      imgBytes: u16(8),
      msgCount: d[10],
      msgMax: d[11],
      assetFlags: d.length >= 13 ? d[12] : 0,
      hasAssetFlags: d.length >= 13,
    );
  }
}
