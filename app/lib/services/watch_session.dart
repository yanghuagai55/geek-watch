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
  final StreamController<WatchSettings> _watchSettings =
      StreamController<WatchSettings>.broadcast();
  final StreamController<WatchConfigBundle> _watchConfigs =
      StreamController<WatchConfigBundle>.broadcast();
  final StreamController<WatchExtPoolState> _extPoolStates =
      StreamController<WatchExtPoolState>.broadcast();
  final StreamController<WatchSport> _sport =
      StreamController<WatchSport>.broadcast();
  final StreamController<WatchSensorData> _sensors =
      StreamController<WatchSensorData>.broadcast();
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
  Completer<WatchSensorData?>? _sensorWaiter;
  Future<void> _tail = Future<void>.value();
  LinkState _state = LinkState.idle;
  bool _quiet = false;
  bool _commandBusy = false;
  bool _snapshotBusy = false;
  bool _sensorBusy = false;
  bool _disposed = false;
  int _externalTransferRefs = 0;

  LinkState get state => _state;

  bool get isConnected => _transport?.isConnected == true;

  /// 抓屏期间会暂停遥测；媒体跟随等逻辑可用它避让。
  bool get isCapturing => _snapshotBusy;

  /// 原生服务正在下发通知/媒体点阵时为 true，Dart 侧自动避让。
  bool get isExternalBusy {
    final transport = _transport;
    return transport is AndroidSppTransport && transport.isExternalBusy;
  }

  /// 当前存在抓屏、写命令或跨端长点阵传输。
  bool get isBusy =>
      _snapshotBusy ||
      _commandBusy ||
      _externalTransferRefs > 0 ||
      isExternalBusy;

  /// 状态流：连接中 / 已连接 / 失败 / 空闲。
  Stream<LinkState> get states => _states.stream;

  /// 手表每秒回传的性能数据。
  Stream<WatchStats> get stats => _stats.stream;

  /// 手表按键产生的播放控制（1 播放 / 2 暂停 / 3 下一首 / 4 上一首）。
  Stream<int> get controls => _controls.stream;

  /// 手表当前设置（亮度、抬腕、1207 震动、状态灯、熄屏等）。
  Stream<WatchSettings> get watchSettings => _watchSettings.stream;

  /// 手表完整配置与表盘 / AT24C256 持久化状态（0x2C）。
  Stream<WatchConfigBundle> get watchConfigs => _watchConfigs.stream;

  /// 手表外置 AT24C256 动态 ROM Pool 实时分配状态（0x2E）。
  Stream<WatchExtPoolState> get extPoolStates => _extPoolStates.stream;

  /// 手表今日运动数据（步数、距离、卡路里、活跃分钟、12 个时段桶）。
  Stream<WatchSport> get sport => _sport.stream;

  /// 手表 9 轴姿态与传感器调试流（由 0x13 触发后从 `DBG ...` 解析）。
  Stream<WatchSensorData> get sensors => _sensors.stream;

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
    if (ok) unawaited(querySettings());
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
    final sensorWaiter = _sensorWaiter;
    _sensorWaiter = null;
    if (sensorWaiter != null && !sensorWaiter.isCompleted) {
      sensorWaiter.complete(null);
    }
    _sensorBusy = false;
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
    return _serialize(() => _withExternalTransfer(() async {
      final transport = _transport;
      if (transport == null || !transport.isConnected) return false;
      for (var chunk = 0; chunk < watchBitmapChunks; chunk++) {
        final payload = Uint8List(1 + watchBitmapChunk);
        payload[0] = chunk;
        payload.setRange(1, payload.length, bits, chunk * watchBitmapChunk);
        if (!await _sendAckedFrame(transport, WatchCmd.mediaBitmap, payload)) {
          return false;
        }
      }
      return true;
    }));
  }

  Future<bool> setPlayState(bool playing) {
    return _command(WatchCmd.playState, <int>[playing ? 1 : 0]);
  }

  Future<bool> setBrightness(int level) {
    return _command(WatchCmd.brightness, <int>[level.clamp(0, 2)]);
  }

  /// 拉取手表当前设置；手表用 0x1A 回。
  Future<bool> querySettings() {
    return _command(WatchCmd.getSettings);
  }

  /// key：1 抬腕 2 震动 3 亮度 4 蓝牙 5 状态灯 6 熄屏。
  Future<bool> setSetting(int key, int value) {
    return _command(WatchCmd.setting, <int>[key, value]);
  }

  /// 查询完整配置、表盘状态与 AT24C256 存储信息（0x2B -> 0x2C）。
  Future<bool> requestConfig() {
    return _command(WatchCmd.getConfig);
  }

  /// 下发 20 字节完整配置与表盘样式，并按 flag 写入 AT24C256 持久化（0x2A）。
  Future<bool> sendConfigBundle(WatchConfigBundle bundle) {
    return _command(WatchCmd.setConfig, bundle.toSetConfigPayload());
  }

  /// 查询 AT24C256 动态 ROM 分配状态（0x2D [0x00] -> 0x2E）。
  Future<bool> requestExtPoolState() {
    return _command(WatchCmd.extOp, const <int>[0]);
  }

  /// 立即清空手表动态 ROM 与持久化目录里的全部消息（0x24）。
  Future<bool> clearWatchMessages() {
    return _command(WatchCmd.msgClear);
  }

  static int crc16Ccitt(Uint8List data) {
    var crc = 0xFFFF;
    for (final b in data) {
      crc ^= b << 8;
      for (var bit = 0; bit < 8; bit++) {
        crc = (crc & 0x8000) != 0 ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
      }
    }
    return crc;
  }

  /// 分块上传按需页面 1bpp 资源到手表 AT24C256 动态 ROM（0x2F，id=1）。
  Future<bool> sendExtAsset({
    required int assetId,
    required int width,
    required int height,
    required Uint8List bits,
  }) {
    const chunkSize = 128;
    return _serialize(() => _withExternalTransfer(() async {
      final transport = _transport;
      if (transport == null || !transport.isConnected) return false;
      for (var offset = 0, chunk = 0;
          offset < bits.length;
          offset += chunkSize, chunk++) {
        final end =
            (offset + chunkSize < bits.length) ? offset + chunkSize : bits.length;
        final pay = <int>[
          assetId & 0xFF,
          chunk & 0xFF,
          width & 0xFF,
          (width >> 8) & 0xFF,
          height & 0xFF,
          (height >> 8) & 0xFF,
          ...bits.sublist(offset, end),
        ];
        if (!await _sendAckedFrame(
          transport,
          WatchCmd.extAsset,
          pay,
          const Duration(seconds: 3),
        )) {
          return false;
        }
      }
      return true;
    }));
  }

  /// 分块上传可变长二进制模块/彩图包到手表 AT24C256（0x2F，id=3 3D指南针 / id=4 关于页）。
  Future<bool> sendExtBlob({
    required int assetId,
    required Uint8List blob,
  }) {
    const chunkSize = 128;
    final totalLen = blob.length;
    final crc = crc16Ccitt(blob);
    return _serialize(() => _withExternalTransfer(() async {
      final transport = _transport;
      if (transport == null || !transport.isConnected) return false;
      for (var offset = 0, chunk = 0;
          offset < totalLen;
          offset += chunkSize, chunk++) {
        final end =
            (offset + chunkSize < totalLen) ? offset + chunkSize : totalLen;
        final pay = <int>[
          assetId & 0xFF,
          chunk & 0xFF,
          totalLen & 0xFF,
          (totalLen >> 8) & 0xFF,
          crc & 0xFF,
          (crc >> 8) & 0xFF,
          ...blob.sublist(offset, end),
        ];
        if (!await _sendAckedFrame(
          transport,
          WatchCmd.extAsset,
          pay,
          const Duration(seconds: 3),
        )) {
          return false;
        }
      }
      return true;
    }));
  }

  /// 跨平台下发一条富文本通知点阵到手表 AT24C256 动态 ROM（0x2D + 0x26/0x23）。
  Future<bool> pushRichNotification({
    required Uint8List rowBytes,
    required Uint8List detailBytes,
    required int count,
    bool vibrate = true,
  }) {
    if (rowBytes.length != 352 || detailBytes.length != 2560) {
      throw ArgumentError('消息点阵大小需为行 352B + 详情 2560B');
    }
    return _serialize(() => _withExternalTransfer(() async {
      final transport = _transport;
      if (transport == null || !transport.isConnected) return false;
      const ackTimeout = Duration(seconds: 3);

      if (!await _sendAckedFrame(
        transport,
        WatchCmd.extOp,
        <int>[
          0x01,
          count.clamp(1, 20),
          if (!vibrate) 0x00,
        ],
        ackTimeout,
      )) {
        return false;
      }
      final rowChunks = (rowBytes.length + 127) ~/ 128;
      for (var sub = 0; sub < rowChunks; sub++) {
        final start = sub * 128;
        final end =
            (start + 128 < rowBytes.length) ? start + 128 : rowBytes.length;
        final payload = <int>[0x02, sub, ...rowBytes.sublist(start, end)];
        if (!await _sendAckedFrame(
          transport,
          WatchCmd.extOp,
          payload,
          ackTimeout,
        )) {
          return false;
        }
      }
      final detailChunks = detailBytes.length ~/ 128;
      for (var chunk = 0; chunk < detailChunks; chunk++) {
        final offset = chunk * 128;
        final rle = _rleEncode128(detailBytes, offset);
        final ok = rle.length <= 125
            ? await _sendAckedFrame(
                transport,
                WatchCmd.maskRle,
                <int>[5, 0, chunk, ...rle],
                ackTimeout,
              )
            : await _sendAckedFrame(
                transport,
                WatchCmd.msgDetailBmp,
                <int>[0, chunk, ...detailBytes.sublist(offset, offset + 128)],
                ackTimeout,
              );
        if (!ok) return false;
      }
      await _sendAckedFrame(
        transport,
        WatchCmd.extOp,
        const <int>[0x00],
        ackTimeout,
      );
      return true;
    }));
  }

  static List<int> _rleEncode128(Uint8List bits, int offset) {
    final out = <int>[];
    var i = 0;
    while (i < 128) {
      final value = bits[offset + i];
      var run = 1;
      while (i + run < 128 && bits[offset + i + run] == value && run < 255) {
        run++;
      }
      out.add(value);
      out.add(run);
      i += run;
    }
    return out;
  }

  /// 分块上传压缩表盘图像至手表外置 AT24C256 Flash（0x28 meta + 0x29 chunks）。
  Future<bool> sendExtImage({
    required int width,
    required int height,
    required int format,
    required Uint8List bytes,
    bool applyDial = true,
    void Function(int sentChunks, int totalChunks)? onProgress,
  }) {
    if (bytes.isEmpty || bytes.length > 0x5400) {
      throw ArgumentError.value(bytes.length, 'bytes', '大小需在 1..21504 字节内');
    }
    return _serialize(() => _withExternalTransfer(() async {
      final transport = _transport;
      if (transport == null || !transport.isConnected) return false;
      const ackTimeout = Duration(seconds: 3);

      final meta = Uint8List.fromList(<int>[
        width & 0xFF,
        (width >> 8) & 0xFF,
        height & 0xFF,
        (height >> 8) & 0xFF,
        format & 0xFF,
        bytes.length & 0xFF,
        (bytes.length >> 8) & 0xFF,
        applyDial ? 1 : 0,
      ]);
      if (!await _sendAckedFrame(
        transport,
        WatchCmd.extImgMeta,
        meta,
        ackTimeout,
      )) {
        return false;
      }

      final totalChunks = (bytes.length + 127) ~/ 128;
      for (var c = 0; c < totalChunks; c++) {
        final start = c * 128;
        final end = (start + 128 < bytes.length) ? start + 128 : bytes.length;
        final chunkLen = end - start;
        final payload = Uint8List(1 + chunkLen);
        payload[0] = c;
        payload.setRange(1, 1 + chunkLen, bytes, start);
        if (!await _sendAckedFrame(
          transport,
          WatchCmd.extImgData,
          payload,
          ackTimeout,
        )) {
          return false;
        }
        onProgress?.call(c + 1, totalChunks);
      }
      return true;
    }));
  }

  /// 拉取今日运动数据；手表用 0x27 回。
  Future<bool> requestSport() {
    return _command(WatchCmd.sport);
  }

  /// 请求一帧 9 轴姿态与传感器调试快照；固件回复 `DBG ...`（no_ack=1）。
  Future<WatchSensorData?> requestSensor({
    Duration timeout = const Duration(milliseconds: 550),
  }) async {
    if (_sensorBusy || _snapshotBusy || _commandBusy || _quiet || isExternalBusy) {
      return null;
    }
    final transport = _transport;
    if (transport == null || !transport.isConnected) return null;

    _sensorBusy = true;
    final completer = Completer<WatchSensorData?>();
    _sensorWaiter = completer;
    try {
      await transport.send(buildWatchFrame(WatchCmd.sensor));
      return await completer.future.timeout(timeout);
    } on TimeoutException {
      return null;
    } catch (_) {
      return null;
    } finally {
      if (identical(_sensorWaiter, completer)) _sensorWaiter = null;
      _sensorBusy = false;
    }
  }

  /// 开启/关闭手表链路按键调试权限（0x08，固件发送 0x07 按键注入前需开启）。
  Future<bool> setDebug(bool enabled) {
    return _command(WatchCmd.debug, <int>[enabled ? 1 : 0]);
  }

  /// 发送一次模拟物理按键脉冲（按下 -> 保持约 95ms -> 松开，匹配手表去抖边缘）。
  Future<bool> sendKeyTap(
    int keyMask, {
    Duration hold = const Duration(milliseconds: 95),
    bool ensureDebug = true,
  }) async {
    if (ensureDebug) {
      await setDebug(true);
    }
    final pressed = await _command(WatchCmd.key, <int>[keyMask & 0x07]);
    if (!pressed) return false;
    await Future<void>.delayed(hold);
    return _command(WatchCmd.key, const <int>[0]);
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
      return await _withExternalTransfer(() async {
        await transport.send(buildWatchFrame(WatchCmd.snapshot));
        return await completer.future.timeout(timeout);
      });
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
    await _watchSettings.close();
    await _watchConfigs.close();
    await _extPoolStates.close();
    await _sport.close();
    await _sensors.close();
    await _logs.close();
  }

  // ---- 内部 ---------------------------------------------------------------

  Future<void> _waitWhileExternalBusy() async {
    final transport = _transport;
    if (transport is! AndroidSppTransport ||
        !transport.isExternalBusy ||
        !transport.isConnected) {
      return;
    }
    try {
      await transport.externalBusy
          .firstWhere((busy) => !busy)
          .timeout(const Duration(seconds: 12));
    } catch (_) {
      // 超时或流关闭时继续尝试，由底层发送协调兜底。
    }
  }

  Future<T> _withExternalTransfer<T>(Future<T> Function() action) async {
    final transport = _transport;
    if (transport is! AndroidSppTransport) {
      return action();
    }
    if (transport.isExternalBusy) {
      await _waitWhileExternalBusy();
    }
    _externalTransferRefs++;
    if (_externalTransferRefs == 1) {
      await transport.setTransferActive(true);
    }
    try {
      return await action();
    } finally {
      _externalTransferRefs--;
      if (_externalTransferRefs == 0) {
        await transport.setTransferActive(false);
      }
    }
  }

  Future<bool> _sendAckedFrame(
    WatchTransport transport,
    int cmd, [
    List<int> payload = const [],
    Duration timeout = const Duration(seconds: 2),
  ]) async {
    if (!transport.isConnected) return false;
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
  }

  Future<bool> _command(
    int cmd, [
    List<int> payload = const [],
    Duration timeout = const Duration(seconds: 2),
  ]) {
    return _serialize(() async {
      final transport = _transport;
      if (transport == null || !transport.isConnected) return false;
      if (transport is AndroidSppTransport && transport.isExternalBusy) {
        await _waitWhileExternalBusy();
      }
      return _sendAckedFrame(transport, cmd, payload, timeout);
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
      case WatchCmd.settingsState:
        final settings = WatchSettings.fromFrame(frame);
        if (settings != null && !_watchSettings.isClosed) {
          _watchSettings.add(settings);
        }
      case WatchCmd.configState:
        final cfg = WatchConfigBundle.fromFrame(frame);
        if (cfg != null && !_watchConfigs.isClosed) {
          _watchConfigs.add(cfg);
        }
      case WatchCmd.extPoolState:
        final pool = WatchExtPoolState.fromFrame(frame);
        if (pool != null && !_extPoolStates.isClosed) {
          _extPoolStates.add(pool);
        }
      case WatchCmd.sport:
        final sport = WatchSport.fromFrame(frame);
        if (sport != null && !_sport.isClosed) _sport.add(sport);
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
    final sensor = WatchSensorData.tryParseDebugLine(line);
    if (sensor != null) {
      if (!_disposed && !_sensors.isClosed) _sensors.add(sensor);
      final waiter = _sensorWaiter;
      if (waiter != null && !waiter.isCompleted) waiter.complete(sensor);
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

    // 只释放 Dart 侧监听；Android 前台服务会自己重连，稍后由控制器挂回。
    unawaited(transport.release().catchError((Object _) {}));

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
    if (_quiet || _commandBusy || _snapshotBusy || isExternalBusy) return;
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
