// AT32 手表助手 · GPL-3.0-or-later
//
// 会话层：持有传输、拆帧、命令串行化、等待 ok、遥测轮询与抓屏重组。
// UI 只与这里打交道，不直接解析字节。

import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

import 'android_spp.dart';
import 'watch_protocol.dart';
import 'watch_transport.dart';
import 'windows_rfcomm.dart';

typedef WatchTransportFactory = WatchTransport Function(
  TransportType type,
  String address,
  int baud,
);

/// 手表链路会话。
class WatchSession {
  WatchSession({
    WatchTransportFactory? transportFactory,
    this.telemetryInterval = const Duration(seconds: 1),
    this.enableTelemetry = true,
  }) : _factory = transportFactory ?? _defaultTransport;

  final WatchTransportFactory _factory;
  final Duration telemetryInterval;
  final bool enableTelemetry;

  final StreamController<LinkState> _states =
      StreamController<LinkState>.broadcast();
  final StreamController<WatchStats> _stats =
      StreamController<WatchStats>.broadcast();
  final StreamController<int> _controls = StreamController<int>.broadcast();
  final StreamController<String> _logs = StreamController<String>.broadcast();

  late final WatchFrameParser _parser = WatchFrameParser(
    onFrame: _onFrame,
    onLine: _onLine,
  );

  WatchTransport? _transport;
  StreamSubscription? _dataSub;
  StreamSubscription? _closedSub;
  Timer? _telemetry;
  Completer<bool>? _ack;
  Completer<WatchSnapshot?>? _snapshot;
  Future<void> _tail = Future<void>.value();
  LinkState _state = LinkState.idle;
  bool _quiet = false;
  bool _commandBusy = false;
  bool _snapshotBusy = false;
  bool _disposed = false;

  LinkState get state => _state;

  bool get isConnected => _transport?.isConnected == true;

  /// 抓屏期间会暂停遥测；媒体跟随等逻辑可用它避让。
  bool get isCapturing => _snapshotBusy;

  /// 状态流：连接中 / 已连接 / 失败 / 空闲。
  Stream<LinkState> get states => _states.stream;

  /// 手表每秒回传的性能数据。
  Stream<WatchStats> get stats => _stats.stream;

  /// 手表按键产生的播放控制（1 播放 / 2 暂停 / 3 下一首 / 4 上一首）。
  Stream<int> get controls => _controls.stream;

  /// 链路日志（已连接、握手无回应、断开原因等）。
  Stream<String> get logs => _logs.stream;

  Future<void> connect({
    required TransportType type,
    required String address,
    int baud = 115200,
  }) async {
    if (_disposed) throw const TransportException('会话已释放');
    await disconnect();
    _setState(LinkState.connecting);
    _log('连接 $address（${_typeName(type)}）…');

    WatchTransport? transport;
    try {
      transport = _factory(type, address, baud);
      await transport.connect();
    } catch (error) {
      try {
        await transport?.close();
      } catch (_) {}
      _setState(LinkState.failed);
      _log('连接失败：$error');
      rethrow;
    }

    final active = transport;
    _transport = active;
    _parser.reset();
    _dataSub = active.incoming.listen(_parser.add);
    _closedSub = active.closed.listen((_) => _onLinkClosed(active));
    _setState(LinkState.connected);
    _log('已连接 ${active.displayName}');

    final ok = await _command(
      WatchCmd.nop,
      const [],
      const Duration(seconds: 3),
    );
    _log(ok ? '握手成功' : '握手无回应');
    _startTelemetry();
  }

  Future<void> disconnect() async {
    await _teardown(releaseOnly: false);
  }

  Future<void> _teardown({required bool releaseOnly}) async {
    _telemetry?.cancel();
    _telemetry = null;

    final transport = _transport;
    _transport = null;
    final dataSub = _dataSub;
    _dataSub = null;
    final closedSub = _closedSub;
    _closedSub = null;
    await dataSub?.cancel();
    await closedSub?.cancel();

    if (transport != null) {
      try {
        if (releaseOnly) {
          await transport.release();
        } else {
          await transport.close();
        }
      } catch (error) {
        _log('断开出错：$error');
      }
    }

    _parser.reset();
    _ack = null;
    final snapshot = _snapshot;
    _snapshot = null;
    if (snapshot != null && !snapshot.isCompleted) snapshot.complete(null);
    if (_state != LinkState.idle) _setState(LinkState.idle);
  }

  Future<bool> syncTime(DateTime now) {
    return _command(WatchCmd.time, <int>[
      now.year & 0xFF,
      (now.year >> 8) & 0xFF,
      now.month,
      now.day,
      now.hour,
      now.minute,
      now.second,
    ]);
  }

  Future<bool> sendMessage(int slot, String text) {
    return _command(WatchCmd.message, <int>[
      slot & 0x03,
      ...fitUtf8(text, watchMessageMax),
    ]);
  }

  Future<bool> sendLyric(String text) {
    return _command(WatchCmd.lyric, fitUtf8(text, watchLyricMax));
  }

  /// 下发 128×32 单色点阵（512 字节，MSB 在前），按 4 块发送。
  /// 手表会用 PC 栅格化的字模显示，绕开固件内置的 500 字子集。
  Future<bool> sendMediaBitmap(Uint8List bits) {
    if (bits.length != watchBitmapBytes) {
      throw ArgumentError.value(
        bits.length,
        'bits',
        '点阵必须是 $watchBitmapBytes 字节',
      );
    }
    return _serialize(() async {
      final transport = _transport;
      if (transport == null || !transport.isConnected) return false;
      for (var chunk = 0; chunk < watchBitmapChunks; chunk++) {
        final payload = Uint8List(1 + watchBitmapChunk);
        payload[0] = chunk;
        payload.setRange(1, payload.length, bits, chunk * watchBitmapChunk);
        final ack = Completer<bool>();
        _ack = ack;
        _commandBusy = true;
        try {
          await transport.send(buildWatchFrame(WatchCmd.mediaBitmap, payload));
          final ok = await ack.future.timeout(
            const Duration(seconds: 2),
            onTimeout: () => false,
          );
          if (!ok) return false;
        } on TransportException {
          return false;
        } finally {
          _commandBusy = false;
          if (identical(_ack, ack)) _ack = null;
        }
      }
      return true;
    });
  }

  Future<bool> setPlayState(bool playing) {
    return _command(WatchCmd.playState, <int>[playing ? 1 : 0]);
  }

  Future<bool> setBrightness(int level) {
    return _command(WatchCmd.brightness, <int>[level.clamp(0, 2)]);
  }

  /// 请求一帧 128×160 画面；超时或链路断开返回 null。
  Future<WatchSnapshot?> requestSnapshot({
    Duration timeout = const Duration(seconds: 60),
  }) async {
    if (_snapshotBusy) return null;
    final transport = _transport;
    if (transport == null || !transport.isConnected) return null;

    _snapshotBusy = true;
    _quiet = true;
    final completer = Completer<WatchSnapshot?>();
    _snapshot = completer;
    try {
      await transport.send(buildWatchFrame(WatchCmd.snapshot));
      return await completer.future.timeout(timeout);
    } on TimeoutException {
      _log('抓屏超时');
      return null;
    } catch (error) {
      _log('抓屏失败：$error');
      return null;
    } finally {
      if (identical(_snapshot, completer)) _snapshot = null;
      _quiet = false;
      _snapshotBusy = false;
    }
  }

  Future<void> dispose() async {
    if (_disposed) return;
    _disposed = true;
    await _teardown(releaseOnly: true);
    await _states.close();
    await _stats.close();
    await _controls.close();
    await _logs.close();
  }

  // ---- 内部 ---------------------------------------------------------------

  Future<bool> _command(
    int cmd, [
    List<int> payload = const [],
    Duration timeout = const Duration(seconds: 2),
  ]) {
    return _serialize(() async {
      final transport = _transport;
      if (transport == null || !transport.isConnected) return false;

      final ack = Completer<bool>();
      _ack = ack;
      _commandBusy = true;
      try {
        await transport.send(buildWatchFrame(cmd, payload));
        return await ack.future.timeout(timeout, onTimeout: () => false);
      } on TransportException {
        return false;
      } finally {
        _commandBusy = false;
        if (identical(_ack, ack)) _ack = null;
      }
    });
  }

  Future<T> _serialize<T>(Future<T> Function() action) {
    final result = Completer<T>();
    _tail = _tail.then((_) async {
      if (_disposed) {
        result.completeError(const TransportException('会话已释放'));
        return;
      }
      try {
        result.complete(await action());
      } catch (error, stack) {
        result.completeError(error, stack);
      }
    });
    return result.future;
  }

  void _onFrame(WatchFrame frame) {
    switch (frame.cmd) {
      case WatchCmd.stats:
        final stats = WatchStats.fromFrame(frame);
        if (stats != null && !_stats.isClosed) _stats.add(stats);
      case WatchCmd.control:
        if (frame.data.isNotEmpty && !_controls.isClosed) {
          _controls.add(frame.data[0]);
        }
      case WatchCmd.image:
        final snapshot = WatchSnapshot.fromFrame(frame);
        final waiter = _snapshot;
        if (snapshot != null && waiter != null && !waiter.isCompleted) {
          waiter.complete(snapshot);
        }
    }
  }

  void _onLine(String line) {
    final text = line.trim().toLowerCase();
    if (text == 'ok' || text == 'bad') {
      final ack = _ack;
      if (ack != null && !ack.isCompleted) ack.complete(text == 'ok');
      return;
    }
    _log(line);
  }

  void _onLinkClosed(WatchTransport transport) {
    if (!identical(_transport, transport)) return;
    _transport = null;
    _telemetry?.cancel();
    _telemetry = null;
    _dataSub?.cancel();
    _dataSub = null;
    _closedSub?.cancel();
    _closedSub = null;
    _parser.reset();

    final ack = _ack;
    if (ack != null && !ack.isCompleted) ack.complete(false);
    _ack = null;
    final snapshot = _snapshot;
    _snapshot = null;
    if (snapshot != null && !snapshot.isCompleted) snapshot.complete(null);

    _setState(LinkState.failed);
    _log('链路已断开');
  }

  void _startTelemetry() {
    if (!enableTelemetry) return;
    _telemetry?.cancel();
    _pollPerf();
    _telemetry = Timer.periodic(telemetryInterval, (_) => _pollPerf());
  }

  void _pollPerf() {
    final transport = _transport;
    if (_quiet || _commandBusy || _snapshotBusy) return;
    if (transport == null || !transport.isConnected) return;
    unawaited(
      transport.send(buildWatchFrame(WatchCmd.perf)).catchError((Object _) {}),
    );
  }

  void _setState(LinkState value) {
    if (_state == value) return;
    _state = value;
    if (!_states.isClosed) _states.add(value);
  }

  void _log(String line) {
    if (!_logs.isClosed) _logs.add(line);
  }

  static String _typeName(TransportType type) => switch (type) {
    TransportType.spp => '经典 SPP',
    TransportType.ble => '蓝牙 BLE',
    TransportType.serial => '串口',
  };

  static WatchTransport _defaultTransport(
    TransportType type,
    String address,
    int baud,
  ) {
    switch (type) {
      case TransportType.spp:
        if (Platform.isWindows) return WindowsRfcommTransport(address);
        if (Platform.isAndroid) return AndroidSppTransport(address);
        throw const TransportException('经典 SPP 目前支持 Windows / Android');
      case TransportType.ble:
        throw const TransportException('BLE 链路尚未接入 Flutter 端，请先使用经典 SPP');
      case TransportType.serial:
        throw const TransportException('串口链路尚未接入 Flutter 端，请先使用经典 SPP');
    }
  }
}
