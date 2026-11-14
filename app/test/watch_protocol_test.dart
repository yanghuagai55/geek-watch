import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:watch_companion/services/watch_protocol.dart';

void main() {
  test('组帧与固件示例一致', () {
    expect(
      buildWatchFrame(WatchCmd.nop),
      Uint8List.fromList([0xA5, 0x7F, 0x00, 0xDA]),
    );
    expect(
      buildWatchFrame(WatchCmd.snapshot),
      Uint8List.fromList([0xA5, 0x06, 0x00, 0xA3]),
    );
    expect(
      buildWatchFrame(WatchCmd.playState, [1]),
      Uint8List.fromList([0xA5, 0x04, 0x01, 0x01, 0xA1]),
    );
  });

  test('fitUtf8 按字符边界截断', () {
    expect(utf8.decode(fitUtf8('HELLO WATCH', 23)), 'HELLO WATCH');
    final trimmed = fitUtf8('你好世界你好世界你好世界', 23);
    expect(trimmed.length, 21);
    expect(utf8.decode(trimmed), '你好世界你好世');
    expect(utf8.decode(fitUtf8('abc你好', 4)), 'abc');
  });

  test('拆帧支持分片、文本行与校验', () {
    final frames = <WatchFrame>[];
    final lines = <String>[];
    final parser = WatchFrameParser(onFrame: frames.add, onLine: lines.add);
    final payload = List<int>.generate(32, (i) => i * 3 & 0xFF);
    final bytes = <int>[
      ...buildWatchFrame(WatchCmd.stats, payload),
      ...utf8.encode('ok\r\n'),
      ...buildWatchFrame(WatchCmd.control, [3]),
    ];

    for (var i = 0; i < bytes.length; i += 5) {
      final end = i + 5 < bytes.length ? i + 5 : bytes.length;
      parser.add(Uint8List.fromList(bytes.sublist(i, end)));
    }

    expect(frames.length, 2);
    expect(frames[0].cmd, WatchCmd.stats);
    expect(frames[0].data.length, 32);
    expect(frames[1].cmd, WatchCmd.control);
    expect(frames[1].data, [3]);
    expect(lines, ['ok']);
  });

  test('校验失败后能重新同步到下一帧', () {
    final frames = <WatchFrame>[];
    final parser = WatchFrameParser(onFrame: frames.add);
    final broken = buildWatchFrame(WatchCmd.control, [1]);
    broken[broken.length - 1] ^= 0xFF;
    parser.add(
      Uint8List.fromList([
        ...broken,
        ...buildWatchFrame(WatchCmd.control, [2]),
      ]),
    );
    expect(frames.single.data, [2]);
  });

  test('图像帧按三字节长度拆分并解码 RGB565', () {
    const width = 2;
    const height = 1;
    final total = 4 + width * height * 2;
    final payload = Uint8List(total);
    payload[0] = width & 0xFF;
    payload[1] = width >> 8;
    payload[2] = height & 0xFF;
    payload[3] = height >> 8;
    // 红 0xF800、绿 0x07E0，均为大端。
    payload[4] = 0xF8;
    payload[5] = 0x00;
    payload[6] = 0x07;
    payload[7] = 0xE0;

    final frameBytes = <int>[
      watchMark,
      WatchCmd.image,
      0xFF,
      total & 0xFF,
      (total >> 8) & 0xFF,
      (total >> 16) & 0xFF,
      ...payload,
    ];
    var check = 0;
    for (final byte in frameBytes) {
      check ^= byte;
    }
    frameBytes.add(check);

    final frames = <WatchFrame>[];
    final parser = WatchFrameParser(onFrame: frames.add);
    parser.add(Uint8List.fromList(frameBytes));

    expect(frames.single.cmd, WatchCmd.image);
    final snapshot = WatchSnapshot.fromFrame(frames.single);
    expect(snapshot, isNotNull);
    expect(snapshot!.width, width);
    expect(snapshot.height, height);
    expect(snapshot.rgba.sublist(0, 4), [255, 0, 0, 255]);
    expect(snapshot.rgba.sublist(4, 8), [0, 255, 0, 255]);
  });

  test('WatchStats 解析 RAM、CPU 与 FPS 压缩字段', () {
    final data = ByteData(32);
    for (var i = 0; i < 8; i++) {
      data.setUint32(i * 4, i, Endian.little);
    }
    data.setUint32(24, 0x1234 | (123 << 16), Endian.little);
    data.setUint32(28, 0x4000 | (60 << 16), Endian.little);

    final stats = WatchStats.fromFrame(
      WatchFrame(WatchCmd.stats, data.buffer.asUint8List()),
    );
    expect(stats, isNotNull);
    expect(stats!.ramUsedBytes, 0x1234);
    expect(stats.ramTotalBytes, 0x4000);
    expect(stats.cpu, 12.3);
    expect(stats.fps, 60);
    expect(stats.ramUsedKb, closeTo(5.05, 0.01));
    expect(stats.ramTotalKb, 16.5);
  });
}
