import 'dart:async';
import 'dart:io';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:material_ui/material_ui.dart';

import '../services/android_spp.dart';
import '../services/lrc_library.dart';
import '../services/media_monitor.dart';
import '../services/snapshot_image.dart';
import '../services/system_tray.dart';
import '../services/text_bitmap.dart';
import '../services/text_paginator.dart';
import '../services/watch_protocol.dart';
import '../services/watch_session.dart';
import '../services/watch_transport.dart';

export '../services/watch_transport.dart' show TransportType;

class WatchController extends ChangeNotifier {
  WatchController({bool startTicker = true, this.session}) {
    _stateSub = session?.states.listen(_onLinkState);
    _statsSub = session?.stats.listen(_applyStats);
    _controlsSub = session?.controls.listen(_applyControl);
    if (startTicker) {
      _ticker = Timer.periodic(const Duration(seconds: 1), (_) => _tick());
    }
  }

  /// 为空时保持纯界面预览（测试与无硬件环境）。
  final WatchSession? session;

  Timer? _ticker;
  int _ticks = 0;
  StreamSubscription<LinkState>? _stateSub;
  StreamSubscription<WatchStats>? _statsSub;
  StreamSubscription<int>? _controlsSub;

  DateTime now = DateTime.now();
  ThemeMode themeMode = ThemeMode.dark;
  TransportType transport = TransportType.spp;
  String deviceAddress = '04:26:01:08:02:0C';
  String serialPort = 'COM7';
  String baudRate = '115200';
  bool autoConnect = true;
  bool autoSyncTime = true;
  bool animations = true;
  bool demoStreaming = true;
  bool liquidGlass = true;
  bool use24Hour = true;
  double brightness = 0.65;
  String screenTimeout = '30 秒';
  bool previewConnected = false;
  LinkState linkState = LinkState.idle;
  String? lastError;
  bool _liveTelemetry = false;

  bool playing = false;
  int lastControlAction = 0;
  int controlSeq = 0;

  /// Windows 媒体跟随：读 SMTC 的标题/艺人/播放状态；歌词优先同名 .lrc。
  bool mediaFollow = true;
  bool backgroundEnabled = false;
  String mediaTitle = '';
  String mediaArtist = '';

  /// Android 系统集成：通知转发、通知读取权限、电池优化白名单。
  bool notificationForward = true;
  bool notificationAccess = false;
  bool batteryExempt = false;

  /// 主页预览用的真机画面（连接后自动抓一帧，点击可刷新）。
  ui.Image? deviceScreen;
  bool capturingPreview = false;

  /// 设置变更回调（main 里做防抖落盘）。
  VoidCallback? onSettingsChanged;

  final MediaMonitor _mediaMonitor = const MediaMonitor();
  final LrcLibrary _lrc = LrcLibrary();
  Timer? _mediaTimer;
  bool _pollingMedia = false;
  Timer? _mediaPageTimer;
  List<List<String>> _mediaPages = const <List<String>>[];
  int _mediaPageIndex = 0;
  String? _mediaCueKey;

  final List<String> messageSlots = [
    '晚上记得给手表充电。',
    '明天 09:30 硬件评审',
    'HELLO AT32',
  ];

  bool get hasHardware => session != null;

  /// 有会话时以真实链路为准，否则沿用界面预览开关。
  bool get connected =>
      session != null ? linkState == LinkState.connected : previewConnected;

  bool get connecting => linkState == LinkState.connecting;

  /// 是否正在展示真机回传的性能数据。
  bool get liveTelemetry => _liveTelemetry;

  String get transportLabel => switch (transport) {
    TransportType.spp => 'SPP',
    TransportType.ble => 'BLE',
    TransportType.serial => 'COM',
  };

  String get transportName => switch (transport) {
    TransportType.spp => '经典 SPP',
    TransportType.ble => '蓝牙 BLE',
    TransportType.serial => '串口',
  };

  String get connectionLabel {
    if (connected) return hasHardware ? '已连接' : '演示连接';
    if (connecting) return '连接中';
    return '未连接';
  }

  String get telemetryLabel {
    if (_liveTelemetry) return '实时';
    return demoStreaming ? 'LIVE · 演示' : '已暂停';
  }

  double ramUsed = 10.0;
  double ramTotal = 16.0;
  double cpu = 18.4;
  int fps = 60;

  final List<double> ramHistory = List<double>.generate(
    28,
    (i) => 0.58 + 0.035 * math.sin(i * 0.55),
  );
  final List<double> cpuHistory = List<double>.generate(
    28,
    (i) => 0.18 + 0.045 * math.sin(i * 0.7),
  );
  final List<double> fpsHistory = List<double>.generate(
    28,
    (i) => i % 7 == 0 ? 1.0 : 60 / 61,
  );

  Future<String?> connect() async {
    final active = session;
    if (active == null) {
      setPreviewConnected(true);
      return null;
    }
    if (linkState == LinkState.connecting) return '正在连接中';
    lastError = null;
    notifyListeners();
    try {
      await active.connect(
        type: transport,
        address: transport == TransportType.serial ? serialPort : deviceAddress,
        baud: int.tryParse(baudRate) ?? 115200,
      );
      if (autoSyncTime) {
        // 校时不阻塞「已连接」提示；失败也不影响链路可用。
        unawaited(
          active.syncTime(DateTime.now()).catchError((Object _) => false),
        );
      }
      unawaited(capturePreview());
      return null;
    } catch (error) {
      lastError = error is TransportException ? error.message : '$error';
      notifyListeners();
      return lastError;
    }
  }

  Future<void> disconnect() async {
    final active = session;
    if (active == null) {
      setPreviewConnected(false);
      return;
    }
    await active.disconnect();
    _liveTelemetry = false;
    notifyListeners();
  }

  Future<bool> sendMessage(int slot, String text) {
    final active = session;
    if (active == null || !connected) return Future<bool>.value(false);
    return active.sendMessage(slot, text);
  }

  /// 逐行发送歌词（文字帧 + 点阵帧），返回成功发送的行数。
  /// 固件字库只有约 500 字，点阵是避免方框的关键；文字帧只做旧固件回退。
  Future<int> syncLyrics(String text) async {
    final active = session;
    if (active == null || !connected) return 0;
    final lines = text
        .split('\n')
        .map((line) => line.trim())
        .where((line) => line.isNotEmpty)
        .toList();
    var sent = 0;
    for (var i = 0; i < lines.length; i++) {
      final textOk = await active.sendLyric(lines[i]);
      final bits = await renderWatchBitmap([
        WatchBitmapLine(lines[i], fontSize: 16),
      ]);
      final bitmapOk = await active.sendMediaBitmap(bits);
      if (textOk || bitmapOk) sent++;
      if (i + 1 < lines.length) {
        await Future<void>.delayed(const Duration(milliseconds: 250));
      }
    }
    return sent;
  }

  /// 给媒体跟随使用：把标题/艺人或歌词行栅格化后下发。
  Future<bool> sendWatchLines(List<WatchBitmapLine> lines) async {
    final active = session;
    if (active == null || !connected) return false;
    final bits = await renderWatchBitmap(lines);
    return active.sendMediaBitmap(bits);
  }

  Future<bool> setPlayState(bool value) async {
    playing = value;
    notifyListeners();
    // PC 端按钮同时控制系统媒体；手表端收到 04 后同步图标。
    _controlMedia(value ? 'play' : 'pause');
    final active = session;
    if (active == null || !connected) return false;
    return active.setPlayState(value);
  }

  /// 把 0..1 的滑杆映射成 0/1/2 三档亮度。
  Future<bool> applyBrightness() {
    final active = session;
    if (active == null || !connected) return Future<bool>.value(false);
    final level = brightness < 0.34 ? 0 : (brightness < 0.67 ? 1 : 2);
    return active.setBrightness(level);
  }

  Future<bool> syncTime() {
    final active = session;
    if (active == null || !connected) return Future<bool>.value(false);
    return active.syncTime(DateTime.now());
  }

  Future<WatchSnapshot?> requestSnapshot() {
    final active = session;
    if (active == null || !connected) return Future<WatchSnapshot?>.value(null);
    return active.requestSnapshot();
  }

  /// 抓一帧真机画面给主页预览用；失败时保留旧画面。
  Future<void> capturePreview() async {
    final active = session;
    if (active == null || !connected || capturingPreview) return;
    capturingPreview = true;
    notifyListeners();
    try {
      final snapshot = await active.requestSnapshot();
      if (snapshot == null) return;
      final image = await decodeWatchSnapshot(snapshot);
      if (!connected) {
        image.dispose();
        return;
      }
      deviceScreen?.dispose();
      deviceScreen = image;
    } finally {
      capturingPreview = false;
      notifyListeners();
    }
  }

  void setMessageSlot(int index, String text) {
    if (index < 0 || index >= messageSlots.length) return;
    messageSlots[index] = text;
    notifyListeners();
  }

  void _onLinkState(LinkState state) {
    linkState = state;
    if (state != LinkState.connected) {
      _liveTelemetry = false;
      _stopMediaPager();
      deviceScreen?.dispose();
      deviceScreen = null;
    } else {
      unawaited(_syncAndroidIntegration());
    }
    _syncMediaPolling();
    notifyListeners();
  }

  /// 把设置同步给 Android 前台服务，并刷新系统权限状态。
  Future<void> _syncAndroidIntegration() async {
    if (!Platform.isAndroid) return;
    await AndroidWatchBridge.setMediaFollow(mediaFollow);
    await AndroidWatchBridge.setNotificationForward(notificationForward);
    await refreshAndroidStatus();
  }

  Future<void> refreshAndroidStatus() async {
    if (!Platform.isAndroid) return;
    final access = await AndroidWatchBridge.hasNotificationAccess();
    final exempt = await AndroidWatchBridge.isBatteryExempt();
    if (access == notificationAccess && exempt == batteryExempt) return;
    notificationAccess = access;
    batteryExempt = exempt;
    notifyListeners();
  }

  Future<void> openNotificationAccess() async {
    if (!Platform.isAndroid) return;
    await AndroidWatchBridge.openNotificationAccess();
  }

  Future<void> openBatterySettings() async {
    if (!Platform.isAndroid) return;
    await AndroidWatchBridge.openBatterySettings();
  }

  void _applyStats(WatchStats stats) {
    _liveTelemetry = true;
    if (stats.ramTotalKb > 0) ramTotal = stats.ramTotalKb;
    ramUsed = stats.ramUsedKb;
    cpu = stats.cpu;
    fps = stats.fps;
    _append(ramHistory, ramTotal > 0 ? ramUsed / ramTotal : 0);
    _append(cpuHistory, cpu / 100);
    _append(fpsHistory, fps / 61);
    notifyListeners();
  }

  void _applyControl(int action) {
    lastControlAction = action;
    controlSeq++;
    if (action == WatchControl.play) {
      playing = true;
      _controlMedia('play');
    }
    if (action == WatchControl.pause) {
      playing = false;
      _controlMedia('pause');
    }
    if (action == WatchControl.next) _controlMedia('next');
    if (action == WatchControl.previous) _controlMedia('previous');
    notifyListeners();
  }

  void _controlMedia(String action) {
    if (!Platform.isWindows) return;
    unawaited(_mediaMonitor.control(action));
  }

  void _syncMediaPolling() {
    final should =
        mediaFollow && connected && session != null && Platform.isWindows;
    if (should) {
      if (_mediaTimer == null) {
        _mediaTimer = Timer.periodic(
          const Duration(seconds: 1),
          (_) => unawaited(_pollMedia()),
        );
        unawaited(_pollMedia());
      }
      return;
    }
    _mediaTimer?.cancel();
    _mediaTimer = null;
    _stopMediaPager();
  }

  Future<void> _pollMedia() async {
    if (_pollingMedia ||
        !connected ||
        !mediaFollow ||
        session?.isCapturing == true) {
      return;
    }
    _pollingMedia = true;
    try {
      final media = await _mediaMonitor.nowPlaying();
      if (media == null || !media.hasSession || media.title.trim().isEmpty) {
        return;
      }
      if (media.title != mediaTitle || media.artist != mediaArtist) {
        mediaTitle = media.title;
        mediaArtist = media.artist;
        notifyListeners();
      }
      if (playing != media.playing) {
        playing = media.playing;
        await session?.setPlayState(media.playing);
        notifyListeners();
      }
      await _updateMediaCue(media);
    } finally {
      _pollingMedia = false;
    }
  }

  Future<void> _updateMediaCue(NowPlaying media) async {
    final cue = await _lrc.cueAt(media.title, media.positionMs);
    final String key;
    final String text;
    final Duration? window;
    final bool loop;
    if (cue != null) {
      key = 'lrc:${cue.startMs}:${cue.text}';
      text = cue.text;
      window = Duration(milliseconds: cue.durationMs);
      loop = false;
    } else {
      text = media.artist.isEmpty
          ? media.title
          : '${media.title} · ${media.artist}';
      key = 'title:$text';
      window = null;
      loop = true;
    }
    if (key == _mediaCueKey) return;
    _mediaCueKey = key;

    final pages = paginateWatchText(text);
    if (pages.isEmpty) return;
    _mediaPageTimer?.cancel();
    _mediaPageTimer = null;
    _mediaPages = pages;
    _mediaPageIndex = 0;
    unawaited(_sendMediaPage());
    if (pages.length < 2) return;

    var interval = const Duration(seconds: 3);
    if (window != null) {
      final raw = window.inMilliseconds ~/ pages.length;
      final clamped = raw.clamp(1200, 8000).toInt();
      interval = Duration(milliseconds: clamped);
    }
    _mediaPageTimer = Timer.periodic(interval, (_) {
      if (_mediaPageIndex + 1 >= _mediaPages.length) {
        if (!loop) {
          _mediaPageTimer?.cancel();
          _mediaPageTimer = null;
          return;
        }
        _mediaPageIndex = 0;
      } else {
        _mediaPageIndex++;
      }
      unawaited(_sendMediaPage());
    });
  }

  Future<void> _sendMediaPage() async {
    if (_mediaPageIndex < 0 || _mediaPageIndex >= _mediaPages.length) return;
    final page = _mediaPages[_mediaPageIndex];
    final ok = await sendWatchLines([
      for (final line in page)
        WatchBitmapLine(line, fontSize: watchMediaFontSize),
    ]);
    if (!ok) {
      _mediaPageTimer?.cancel();
      _mediaPageTimer = null;
    }
  }

  void _stopMediaPager() {
    _mediaPageTimer?.cancel();
    _mediaPageTimer = null;
    _mediaPages = const <List<String>>[];
    _mediaPageIndex = 0;
    _mediaCueKey = null;
  }

  void _tick() {
    _ticks += 1;
    now = DateTime.now();
    if (demoStreaming && !_liveTelemetry) {
      final phase = _ticks / 3;
      ramUsed = 10.0 + 0.16 * math.sin(phase * 0.7);
      cpu = 18.4 + 4.2 * math.sin(phase) + 1.4 * math.cos(phase * 0.43);
      fps = _ticks % 9 == 0 ? 61 : 60;
      _append(ramHistory, ramUsed / ramTotal);
      _append(cpuHistory, cpu / 100);
      _append(fpsHistory, fps / 61);
    }
    notifyListeners();
  }

  static void _append(List<double> values, double value) {
    values.removeAt(0);
    values.add(value.clamp(0.0, 1.0));
  }

  void setThemeMode(ThemeMode value) {
    themeMode = value;
    notifyListeners();
    onSettingsChanged?.call();
  }

  void setTransport(TransportType value) {
    transport = value;
    switch (value) {
      case TransportType.spp:
        deviceAddress = '04:26:01:08:02:0C';
      case TransportType.ble:
        deviceAddress = 'C4:26:01:08:02:0C';
      case TransportType.serial:
        deviceAddress = serialPort;
    }
    notifyListeners();
    onSettingsChanged?.call();
  }

  void setMediaFollow(bool value) {
    mediaFollow = value;
    _stopMediaPager();
    _syncMediaPolling();
    if (Platform.isAndroid) {
      unawaited(AndroidWatchBridge.setMediaFollow(value));
    }
    notifyListeners();
    onSettingsChanged?.call();
  }

  void setNotificationForward(bool value) {
    notificationForward = value;
    if (Platform.isAndroid) {
      unawaited(AndroidWatchBridge.setNotificationForward(value));
    }
    notifyListeners();
    onSettingsChanged?.call();
  }

  void setBackgroundEnabled(bool value) {
    backgroundEnabled = value;
    unawaited(SystemTray.setEnabled(value));
    notifyListeners();
    onSettingsChanged?.call();
  }

  void setPreviewConnected(bool value) =>
      _update(() => previewConnected = value);
  void setAutoConnect(bool value) {
    autoConnect = value;
    notifyListeners();
    onSettingsChanged?.call();
  }

  void setAutoSyncTime(bool value) {
    autoSyncTime = value;
    notifyListeners();
    onSettingsChanged?.call();
  }

  void setAnimations(bool value) => _update(() => animations = value);
  void setDemoStreaming(bool value) => _update(() => demoStreaming = value);
  void setLiquidGlass(bool value) => _update(() => liquidGlass = value);
  void setUse24Hour(bool value) => _update(() => use24Hour = value);
  void setBrightness(double value) {
    brightness = value;
    notifyListeners();
    onSettingsChanged?.call();
  }

  void setScreenTimeout(String value) => _update(() => screenTimeout = value);
  void setAddress(String value) {
    deviceAddress = value;
    onSettingsChanged?.call();
  }

  void setSerialPort(String value) {
    serialPort = value;
    onSettingsChanged?.call();
  }

  void setBaudRate(String? value) {
    if (value != null) {
      baudRate = value;
      notifyListeners();
      onSettingsChanged?.call();
    }
  }

  void resetSettings() {
    themeMode = ThemeMode.dark;
    transport = TransportType.spp;
    deviceAddress = '04:26:01:08:02:0C';
    serialPort = 'COM7';
    baudRate = '115200';
    autoConnect = true;
    autoSyncTime = true;
    animations = true;
    demoStreaming = true;
    liquidGlass = true;
    use24Hour = true;
    brightness = 0.65;
    screenTimeout = '30 秒';
    previewConnected = false;
    mediaFollow = true;
    notificationForward = true;
    backgroundEnabled = false;
    _stopMediaPager();
    _syncMediaPolling();
    unawaited(SystemTray.setEnabled(false));
    notifyListeners();
    onSettingsChanged?.call();
  }

  /// 启动时从 settings.json 恢复；不触发托盘与落盘。
  void applyPersisted(Map<String, dynamic> values) {
    bool readBool(String key, bool fallback) =>
        values[key] is bool ? values[key] as bool : fallback;
    String readString(String key, String fallback) =>
        values[key] is String ? values[key] as String : fallback;

    mediaFollow = readBool('mediaFollow', mediaFollow);
    notificationForward = readBool('notificationForward', notificationForward);
    backgroundEnabled = readBool('backgroundEnabled', backgroundEnabled);
    autoConnect = readBool('autoConnect', autoConnect);
    autoSyncTime = readBool('autoSyncTime', autoSyncTime);
    final theme = readString('themeMode', themeMode.name);
    themeMode = ThemeMode.values.firstWhere(
      (mode) => mode.name == theme,
      orElse: () => ThemeMode.dark,
    );
    final transportName = readString('transport', transport.name);
    transport = TransportType.values.firstWhere(
      (item) => item.name == transportName,
      orElse: () => TransportType.spp,
    );
    if (Platform.isAndroid && transport != TransportType.spp) {
      transport = TransportType.spp;
    }
    deviceAddress = readString('deviceAddress', deviceAddress);
    serialPort = readString('serialPort', serialPort);
    baudRate = readString('baudRate', baudRate);
    final level = values['brightness'];
    if (level is num) brightness = level.toDouble().clamp(0.0, 1.0);
    if (Platform.isAndroid) unawaited(refreshAndroidStatus());
  }

  Map<String, Object?> get persistedSettings => <String, Object?>{
    'mediaFollow': mediaFollow,
    'notificationForward': notificationForward,
    'backgroundEnabled': backgroundEnabled,
    'autoConnect': autoConnect,
    'autoSyncTime': autoSyncTime,
    'themeMode': themeMode.name,
    'transport': transport.name,
    'deviceAddress': deviceAddress,
    'serialPort': serialPort,
    'baudRate': baudRate,
    'brightness': brightness,
  };

  void _update(VoidCallback change) {
    change();
    notifyListeners();
  }

  @override
  void dispose() {
    _ticker?.cancel();
    _mediaTimer?.cancel();
    _mediaPageTimer?.cancel();
    _stateSub?.cancel();
    _statsSub?.cancel();
    _controlsSub?.cancel();
    deviceScreen?.dispose();
    unawaited(session?.dispose());
    super.dispose();
  }
}
