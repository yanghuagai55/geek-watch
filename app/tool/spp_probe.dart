// AT32 手表助手 · GPL-3.0-or-later
//
// 命令行探针：验证 Dart 侧裸 RFCOMM 传输、拆帧与 0x0B 点阵。
// 用法：
//   dart tool/spp_probe.dart [04:26:01:08:02:0C]
//   dart tool/spp_probe.dart bitmap [04:26:01:08:02:0C]
//   dart tool/spp_probe.dart snap [04:26:01:08:02:0C]   只抓屏，不发任何命令

// ignore_for_file: avoid_print

import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

import 'package:watch_companion/services/watch_protocol.dart';
import 'package:watch_companion/services/windows_rfcomm.dart';

Future<void> main(List<String> args) async {
  final bitmapMode = args.isNotEmpty && args.first == 'bitmap';
  final snapOnly = args.isNotEmpty && args.first == 'snap';
  final address = args.length > 1
      ? args[1]
      : (bitmapMode || snapOnly || args.isEmpty
            ? '04:26:01:08:02:0C'
            : args.first);
  final transport = WindowsRfcommTransport(address);
  final snapshot = Completer<WatchSnapshot?>();
  final parser = WatchFrameParser(
    onFrame: (frame) {
      print('RX 帧 cmd=0x${frame.cmd.toRadixString(16)} ${frame.data.length}B');
      final stats = WatchStats.fromFrame(frame);
      if (stats != null) {
        print(
          '  性能 fps=${stats.fps} ram=${stats.ramUsedKb.toStringAsFixed(1)}/'
          '${stats.ramTotalKb.toStringAsFixed(1)}KB cpu=${stats.cpu.toStringAsFixed(1)}%',
        );
      }
      if (frame.cmd == WatchCmd.image && !snapshot.isCompleted) {
        snapshot.complete(WatchSnapshot.fromFrame(frame));
      }
    },
    onLine: (line) => print('RX 文本 "$line"'),
  );
  final subscription = transport.incoming.listen(parser.add);

  try {
    print('连接 $address …');
    final started = DateTime.now();
    await transport.connect();
    print('已连接，耗时 ${DateTime.now().difference(started).inMilliseconds} ms');

    if (snapOnly) {
      await Future<void>.delayed(const Duration(milliseconds: 300));
    } else if (bitmapMode) {
      await transport.send(buildWatchFrame(WatchCmd.nop));
      await Future<void>.delayed(const Duration(milliseconds: 400));
      await _sendBitmap(transport, _testPattern());
      await transport.send(buildWatchFrame(WatchCmd.playState, <int>[1]));
      await Future<void>.delayed(const Duration(seconds: 1));
    } else {
      await transport.send(buildWatchFrame(WatchCmd.nop));
      await Future<void>.delayed(const Duration(seconds: 1));

      await transport.send(buildWatchFrame(WatchCmd.perf));
      await Future<void>.delayed(const Duration(seconds: 2));

      final now = DateTime.now();
      await transport.send(
        buildWatchFrame(WatchCmd.time, <int>[
          now.year & 0xFF,
          (now.year >> 8) & 0xFF,
          now.month,
          now.day,
          now.hour,
          now.minute,
          now.second,
        ]),
      );
      await Future<void>.delayed(const Duration(milliseconds: 600));

      await transport.send(
        buildWatchFrame(WatchCmd.message, <int>[
          0,
          ...fitUtf8('你好，手表', watchMessageMax),
        ]),
      );
      await Future<void>.delayed(const Duration(milliseconds: 600));

      await transport.send(
        buildWatchFrame(WatchCmd.lyric, fitUtf8('晚风手记 · 第一句', watchLyricMax)),
      );
      await Future<void>.delayed(const Duration(milliseconds: 600));

      await transport.send(buildWatchFrame(WatchCmd.playState, <int>[1]));
      await Future<void>.delayed(const Duration(milliseconds: 600));
    }

    print('抓屏中…');
    await transport.send(buildWatchFrame(WatchCmd.snapshot));
    final image = await snapshot.future.timeout(const Duration(seconds: 60));
    print('画面 ${image?.width}×${image?.height}，RGBA ${image?.rgba.length} 字节');
    if (image != null) {
      final path = _saveBmp(image);
      print('已保存 $path');
    }
  } catch (error) {
    print('失败：$error');
    print('链路原因：${transport.closeReason}');
  } finally {
    await subscription.cancel();
    await transport.close();
  }
}

Future<void> _sendBitmap(
  WindowsRfcommTransport transport,
  Uint8List bits,
) async {
  for (var chunk = 0; chunk < watchBitmapChunks; chunk++) {
    final payload = Uint8List(1 + watchBitmapChunk);
    payload[0] = chunk;
    payload.setRange(1, payload.length, bits, chunk * watchBitmapChunk);
    await transport.send(buildWatchFrame(WatchCmd.mediaBitmap, payload));
    await Future<void>.delayed(const Duration(milliseconds: 350));
  }
}

/// 边框 + 实心块 + 棋盘格 + 底部长条，用来肉眼确认 0x0B 贴图通路。
Uint8List _testPattern() {
  const w = watchBitmapWidth;
  const h = watchBitmapHeight;
  final bits = Uint8List(watchBitmapBytes);
  void set(int x, int y) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    bits[y * (w ~/ 8) + (x >> 3)] |= 0x80 >> (x & 7);
  }

  for (var x = 0; x < w; x++) {
    set(x, 0);
    set(x, h - 1);
  }
  for (var y = 0; y < h; y++) {
    set(0, y);
    set(w - 1, y);
  }
  for (var y = 7; y < 23; y++) {
    for (var x = 8; x < 40; x++) {
      set(x, y);
    }
  }
  for (var y = 7; y < 23; y++) {
    for (var x = 48; x < 120; x++) {
      if (((x ~/ 4) + (y ~/ 4)) % 2 == 0) set(x, y);
    }
  }
  for (var y = 26; y < 30; y++) {
    for (var x = 8; x < 120; x++) {
      if ((x ~/ 2) % 2 == 0) set(x, y);
    }
  }
  return bits;
}

String _saveBmp(WatchSnapshot snapshot) {
  final width = snapshot.width;
  final height = snapshot.height;
  final rowSize = (width * 3 + 3) & ~3;
  final data = BytesBuilder();
  void u16(int value) => data.add(<int>[value & 0xFF, (value >> 8) & 0xFF]);
  void u32(int value) => data.add(<int>[
    value & 0xFF,
    (value >> 8) & 0xFF,
    (value >> 16) & 0xFF,
    (value >> 24) & 0xFF,
  ]);

  data.add('BM'.codeUnits);
  u32(54 + rowSize * height);
  u16(0);
  u16(0);
  u32(54);
  u32(40);
  u32(width);
  u32(height);
  u16(1);
  u16(24);
  u32(0);
  u32(rowSize * height);
  u32(2835);
  u32(2835);
  u32(0);
  u32(0);
  for (var y = height - 1; y >= 0; y--) {
    for (var x = 0; x < width; x++) {
      final offset = (y * width + x) * 4;
      data.add(<int>[
        snapshot.rgba[offset + 2],
        snapshot.rgba[offset + 1],
        snapshot.rgba[offset],
      ]);
    }
    for (var pad = width * 3; pad < rowSize; pad++) {
      data.addByte(0);
    }
  }

  final path =
      '${Platform.environment['TEMP'] ?? '.'}${Platform.pathSeparator}watch_probe.bmp';
  File(path).writeAsBytesSync(data.takeBytes());
  return path;
}
