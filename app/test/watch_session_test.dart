import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:watch_companion/services/watch_protocol.dart';
import 'package:watch_companion/services/watch_session.dart';
import 'package:watch_companion/services/watch_transport.dart';

class FakeTransport implements WatchTransport {
  FakeTransport({this.autoAck = true});

  final bool autoAck;
  final StreamController<Uint8List> _data =
      StreamController<Uint8List>.broadcast();
  final StreamController<void> _closed = StreamController<void>.broadcast();
  final List<Uint8List> sent = <Uint8List>[];
  bool _connected = false;

  @override
  String get displayName => '假链路';

  @override
  bool get isConnected => _connected;

  @override
  Stream<Uint8List> get incoming => _data.stream;

  @override
  Stream<void> get closed => _closed.stream;

  @override
  Future<void> connect() async {
    _connected = true;
  }

  @override
  Future<void> send(Uint8List bytes) async {
    sent.add(bytes);
    if (autoAck) {
      scheduleMicrotask(() {
        if (!_data.isClosed) {
          _data.add(Uint8List.fromList(utf8.encode('ok\r\n')));
        }
      });
    }
  }

  @override
  Future<void> close() async {
    _connected = false;
    if (!_data.isClosed) await _data.close();
    if (!_closed.isClosed) await _closed.close();
  }

  @override
  Future<void> release() => close();

  void push(Uint8List bytes) {
    if (!_data.isClosed) _data.add(bytes);
  }

  void drop() {
    _connected = false;
    if (!_closed.isClosed) _closed.add(null);
  }
}

Uint8List imageFrame(int width, int height) {
  final total = 4 + width * height * 2;
  final bytes = <int>[
    watchMark,
    WatchCmd.image,
    0xFF,
    total & 0xFF,
    (total >> 8) & 0xFF,
    (total >> 16) & 0xFF,
    width & 0xFF,
    width >> 8,
    height & 0xFF,
    height >> 8,
  ];
  for (var i = 0; i < width * height; i++) {
    bytes.add(0xF8);
    bytes.add(0x00);
  }
  var check = 0;
  for (final byte in bytes) {
    check ^= byte;
  }
  bytes.add(check);
  return Uint8List.fromList(bytes);
}

void main() {
  test('连接、校时、遥测与控制帧', () async {
    final fake = FakeTransport();
    final session = WatchSession(
      transportFactory: (_, _, _) => fake,
      enableTelemetry: false,
    );
    final stats = <WatchStats>[];
    final controls = <int>[];
    session.stats.listen(stats.add);
    session.controls.listen(controls.add);

    await session.connect(type: TransportType.spp, address: 'TEST');
    expect(session.state, LinkState.connected);
    expect(fake.sent.first[1], WatchCmd.nop);

    final timeOk = await session.syncTime(DateTime(2026, 11, 14, 10, 20, 30));
    expect(timeOk, isTrue);
    final timeFrame = fake.sent.last;
    expect(timeFrame[1], WatchCmd.time);
    expect(timeFrame[2], 7);
    expect(timeFrame.sublist(3, 10), [0xEA, 0x07, 11, 14, 10, 20, 30]);

    final data = ByteData(32);
    data.setUint32(24, 0x2000 | (55 << 16), Endian.little);
    data.setUint32(28, 0x4000 | (59 << 16), Endian.little);
    fake.push(buildWatchFrame(WatchCmd.stats, data.buffer.asUint8List()));
    await pumpEventQueue();
    expect(stats, hasLength(1));
    expect(stats.single.fps, 59);
    expect(stats.single.cpu, 5.5);

    fake.push(buildWatchFrame(WatchCmd.control, <int>[2]));
    await pumpEventQueue();
    expect(controls, <int>[2]);

    await session.dispose();
    expect(session.state, LinkState.idle);
  });

  test('消息按 UTF-8 字符边界截断', () async {
    final fake = FakeTransport();
    final session = WatchSession(
      transportFactory: (_, _, _) => fake,
      enableTelemetry: false,
    );
    await session.connect(type: TransportType.spp, address: 'TEST');
    fake.sent.clear();

    final ok = await session.sendMessage(1, '你好世界你好世界你好世界');
    expect(ok, isTrue);
    final frame = fake.sent.single;
    expect(frame[1], WatchCmd.message);
    expect(frame[2], 1 + 21);
    final payload = frame.sublist(3, 3 + frame[2]);
    expect(payload.first, 1);
    expect(utf8.decode(payload.sublist(1)), '你好世界你好世');

    await session.dispose();
  });

  test('点阵按 4 块 129 字节下发', () async {
    final fake = FakeTransport();
    final session = WatchSession(
      transportFactory: (_, _, _) => fake,
      enableTelemetry: false,
    );
    await session.connect(type: TransportType.spp, address: 'TEST');
    fake.sent.clear();

    final bits = Uint8List(512);
    for (var i = 0; i < bits.length; i++) {
      bits[i] = i & 0xFF;
    }
    final ok = await session.sendMediaBitmap(bits);
    expect(ok, isTrue);
    expect(fake.sent.length, 4);
    for (var chunk = 0; chunk < 4; chunk++) {
      final frame = fake.sent[chunk];
      expect(frame[1], WatchCmd.mediaBitmap);
      expect(frame[2], 129);
      expect(frame[3], chunk);
      expect(
        frame.sublist(4, 4 + 128),
        bits.sublist(chunk * 128, (chunk + 1) * 128),
      );
    }

    await session.dispose();
  });

  test('抓屏帧组装成快照', () async {
    final fake = FakeTransport();
    final session = WatchSession(
      transportFactory: (_, _, _) => fake,
      enableTelemetry: false,
    );
    await session.connect(type: TransportType.spp, address: 'TEST');

    final future = session.requestSnapshot(timeout: const Duration(seconds: 2));
    await pumpEventQueue();
    fake.push(imageFrame(2, 1));
    final snapshot = await future;

    expect(snapshot, isNotNull);
    expect(snapshot!.width, 2);
    expect(snapshot.height, 1);
    expect(snapshot.rgba.sublist(0, 4), [255, 0, 0, 255]);

    await session.dispose();
  });

  test('对端断开后状态变为 failed', () async {
    final fake = FakeTransport();
    final session = WatchSession(
      transportFactory: (_, _, _) => fake,
      enableTelemetry: false,
    );
    await session.connect(type: TransportType.spp, address: 'TEST');
    fake.drop();
    await pumpEventQueue();

    expect(session.state, LinkState.failed);
    expect(session.isConnected, isFalse);

    await session.dispose();
    fake.close();
  });
}
