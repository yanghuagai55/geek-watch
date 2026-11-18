// AT32 手表助手 · GPL-3.0-or-later
//
// Android 经典 SPP 传输。蓝牙连接/读写由 MainActivity.kt 的原生 RFCOMM
// socket 完成，这里只做 MethodChannel/EventChannel 包装。

import 'dart:async';

import 'package:flutter/services.dart';

import 'watch_transport.dart';

class AndroidSppTransport implements WatchTransport {
  AndroidSppTransport(this.address);

  final String address;

  static const MethodChannel _channel = MethodChannel(
    'watch_companion/bluetooth',
  );
  static const EventChannel _events = EventChannel(
    'watch_companion/bluetooth/events',
  );

  final StreamController<Uint8List> _incoming =
      StreamController<Uint8List>.broadcast();
  final StreamController<void> _closed = StreamController<void>.broadcast();
  final StreamController<bool> _externalBusyCtrl =
      StreamController<bool>.broadcast();

  StreamSubscription? _eventSub;
  bool _connected = false;
  bool _externalBusy = false;
  String? _lastError;

  @override
  String get displayName => '经典 SPP';

  @override
  bool get isConnected => _connected;

  bool get isExternalBusy => _externalBusy;

  Stream<bool> get externalBusy => _externalBusyCtrl.stream;

  @override
  Stream<Uint8List> get incoming => _incoming.stream;

  @override
  Stream<void> get closed => _closed.stream;

  @override
  Future<void> connect() async {
    if (_connected) return;
    _lastError = null;

    final granted = await _channel.invokeMethod<bool>('requestPermissions');
    if (granted != true) {
      throw const TransportException('需要蓝牙权限，请在系统弹窗里允许。');
    }

    await _eventSub?.cancel();
    _eventSub = _events.receiveBroadcastStream().listen(_onEvent);

    try {
      final ok = await _channel.invokeMethod<bool>('connect', {
        'address': address,
      });
      if (ok != true) {
        await _eventSub?.cancel();
        _eventSub = null;
        throw TransportException(
          _lastError ??
              '连接 $address 失败：请先在系统蓝牙里配对 HC-04（PIN 1234），'
                  '并确认手表上电。',
        );
      }
      _connected = true;
    } on PlatformException catch (error) {
      await _eventSub?.cancel();
      _eventSub = null;
      throw TransportException(error.message ?? '连接失败：${error.code}');
    }
  }

  void _onEvent(Object? event) {
    if (event is! Map) return;
    final type = event['type'];
    if (type == 'data') {
      final bytes = event['bytes'];
      if (bytes is Uint8List && !_incoming.isClosed) {
        _incoming.add(bytes);
      }
      return;
    }
    if (type == 'busy') {
      final busy = event['busy'] == true;
      if (_externalBusy != busy) {
        _externalBusy = busy;
        if (!_externalBusyCtrl.isClosed) _externalBusyCtrl.add(busy);
      }
      return;
    }
    if (type == 'state' && event['connected'] != true) {
      final wasConnected = _connected;
      _connected = false;
      _lastError = event['message'] as String?;
      if (_externalBusy) {
        _externalBusy = false;
        if (!_externalBusyCtrl.isClosed) _externalBusyCtrl.add(false);
      }
      if (wasConnected && !_closed.isClosed) _closed.add(null);
    }
  }

  Future<void> setTransferActive(bool active) async {
    try {
      await _channel.invokeMethod<void>('setDartTransfer', {
        'active': active,
      });
    } on MissingPluginException {
      // 非 Android 或测试环境忽略。
    } on PlatformException {
      // 服务未启动或已断开时忽略。
    }
  }

  @override
  Future<void> send(Uint8List bytes) async {
    final ok = await _channel.invokeMethod<bool>('send', {'bytes': bytes});
    if (ok != true) {
      throw const TransportException('蓝牙发送失败');
    }
  }

  @override
  Future<void> close() async {
    _connected = false;
    if (_externalBusy) {
      _externalBusy = false;
      if (!_externalBusyCtrl.isClosed) _externalBusyCtrl.add(false);
    }
    await _eventSub?.cancel();
    _eventSub = null;
    try {
      await _channel.invokeMethod<void>('close');
    } on PlatformException {
      // 已经断开时忽略。
    }
    if (!_incoming.isClosed) await _incoming.close();
    if (!_closed.isClosed) await _closed.close();
    if (!_externalBusyCtrl.isClosed) await _externalBusyCtrl.close();
  }

  /// App 退出时只摘掉本端监听；前台服务继续持有 socket。
  @override
  Future<void> release() async {
    _connected = false;
    if (_externalBusy) {
      _externalBusy = false;
      if (!_externalBusyCtrl.isClosed) _externalBusyCtrl.add(false);
    }
    await _eventSub?.cancel();
    _eventSub = null;
    if (!_incoming.isClosed) await _incoming.close();
    if (!_closed.isClosed) await _closed.close();
    if (!_externalBusyCtrl.isClosed) await _externalBusyCtrl.close();
  }
}

/// Android 系统集成：媒体跟随、通知转发、通知读取权限与电池白名单。
class AndroidWatchBridge {
  AndroidWatchBridge._();

  static const MethodChannel _channel = MethodChannel(
    'watch_companion/bluetooth',
  );

  /// 删除系统里这块手表的配对。成功后手机不会再自动连上 HC-04。
  static Future<String> forgetBluetooth(String address) async {
    try {
      final message = await _channel.invokeMethod<String>('forgetBluetooth', {
        'address': address,
      });
      return message ?? '已请求清除配对';
    } on PlatformException catch (error) {
      return error.message ?? '清除配对失败';
    }
  }

  static Future<void> setMediaFollow(bool enabled) async {
    try {
      await _channel.invokeMethod<void>('setMediaFollow', {'enabled': enabled});
    } on PlatformException {
      // 服务未启动时忽略，连接后控制器会再同步一次。
    }
  }

  /// 原生前台服务是否仍持有链路（用于断线后自动挂回）。
  static Future<bool> isConnected() async {
    try {
      return await _channel.invokeMethod<bool>('isConnected') ?? false;
    } on PlatformException {
      return false;
    }
  }

  static Future<void> setNotificationForward(bool enabled) async {
    try {
      await _channel.invokeMethod<void>('setNotificationForward', {
        'enabled': enabled,
      });
    } on PlatformException {
      // 同上。
    }
  }

  static Future<bool> hasNotificationAccess() async {
    try {
      return await _channel.invokeMethod<bool>('notificationAccess') ?? false;
    } on PlatformException {
      return false;
    }
  }

  static Future<void> openNotificationAccess() async {
    try {
      await _channel.invokeMethod<void>('openNotificationAccess');
    } on PlatformException {
      // 系统设置页打不开时保持原状态。
    }
  }

  static Future<bool> isBatteryExempt() async {
    try {
      return await _channel.invokeMethod<bool>('batteryExempt') ?? false;
    } on PlatformException {
      return false;
    }
  }

  static Future<void> openBatterySettings() async {
    try {
      await _channel.invokeMethod<void>('openBatterySettings');
    } on PlatformException {
      // 部分 ROM 没有这个入口。
    }
  }

  /// 当前手机媒体：标题、歌手、播放态、进度（毫秒）。
  static Future<Map<String, Object?>?> nowPlaying() async {
    try {
      final result = await _channel.invokeMethod<Map<dynamic, dynamic>>(
        'nowPlaying',
      );
      if (result == null) return null;
      return result.map((key, value) => MapEntry('$key', value));
    } on PlatformException {
      return null;
    }
  }

  /// play / pause / next / previous。
  static Future<void> mediaControl(String action) async {
    try {
      await _channel.invokeMethod<void>('mediaControl', {'action': action});
    } on PlatformException {
      // 没有媒体会话时静默失败，界面会显示权限提示。
    }
  }

  static Future<void> seekTo(int positionMs) async {
    try {
      await _channel.invokeMethod<void>('seekTo', {'positionMs': positionMs});
    } on PlatformException {
      // 同上。
    }
  }

  /// 服务里缓存的通知条数（最多 20 条）。
  static Future<int> messageCount() async {
    try {
      return await _channel.invokeMethod<int>('messageCount') ?? 0;
    } on PlatformException {
      return 0;
    }
  }

  /// 清空原生服务缓存与手表 AT24C256 中的消息记录。
  static Future<void> clearMessages() async {
    try {
      await _channel.invokeMethod<void>('clearMessages');
    } on PlatformException {
      // 服务未启动时忽略。
    }
  }

  /// 将 Flutter 界面手动推送的消息同步写入原生缓存，防止重扫通知栏时覆盖。
  static Future<void> recordManualMessage({
    required String app,
    required String title,
    required String text,
  }) async {
    try {
      await _channel.invokeMethod<void>('recordManualMessage', {
        'app': app,
        'title': title,
        'text': text,
      });
    } on PlatformException {
      // 服务未启动时忽略。
    }
  }

  /// 原生服务持久化的通知路由规则。
  static Future<Map<String, Object?>> messageRules() async {
    try {
      final result = await _channel.invokeMethod<Map<dynamic, dynamic>>(
        'messageRules',
      );
      if (result == null) return const <String, Object?>{};
      return result.map((key, value) => MapEntry('$key', value));
    } on PlatformException {
      return const <String, Object?>{};
    }
  }

  static Future<void> setMessageRules({
    required String mode,
    required Set<String> packages,
    required int maxMessages,
    required bool merge,
  }) async {
    try {
      await _channel.invokeMethod<void>('setMessageRules', {
        'mode': mode,
        'packages': packages.toList(),
        'maxMessages': maxMessages,
        'merge': merge,
      });
    } on PlatformException {
      // 服务未启动时忽略，下次连接会重新下发。
    }
  }

  /// 通知栏里出现过的应用（package + 显示名）。
  static Future<List<Map<String, Object?>>> notificationApps() async {
    try {
      final result = await _channel.invokeMethod<List<dynamic>>(
        'notificationApps',
      );
      if (result == null) return const <Map<String, Object?>>[];
      return <Map<String, Object?>>[
        for (final item in result)
          if (item is Map)
            item.map((key, value) => MapEntry('$key', value)),
      ];
    } on PlatformException {
      return const <Map<String, Object?>>[];
    }
  }

  /// 触发原生通知监听服务重新绑定并扫描当前状态栏活动通知。
  static Future<bool> rescanNotifications() async {
    try {
      final bound = await _channel.invokeMethod<bool>('rescanNotifications');
      return bound == true;
    } on MissingPluginException {
      return false;
    } on PlatformException {
      return false;
    }
  }
}
