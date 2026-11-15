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

/// 通知栏里出现过的应用，供“只转发选中应用”勾选。
class NotificationApp {
  const NotificationApp({required this.package, required this.label});

  final String package;
  final String label;
}

/// KEY2 磁强计 360° 硬铁校准阶段（与手表 `传感器数值` 页面同步）。
enum MagCalPhase { idle, calibrating, readyToFinish, done }

class WatchController extends ChangeNotifier {
  WatchController({bool startTicker = true, this.session})
    : _autoTick = startTicker {
    _stateSub = session?.states.listen(_onLinkState);
    _statsSub = session?.stats.listen(_applyStats);
    _controlsSub = session?.controls.listen(_applyControl);
    _settingsSub = session?.watchSettings.listen(_applyWatchSettings);
    _sportSub = session?.sport.listen(_applySport);
    _sensorSub = session?.sensors.listen(_applySensor);
    if (startTicker) {
      _ticker = Timer.periodic(const Duration(seconds: 1), (_) => _tick());
    }
  }

  /// 为空时保持纯界面预览（测试与无硬件环境）。
  final WatchSession? session;
  final bool _autoTick;

  /// 当由单元测试以 `startTicker: false` 构造时为 false，关闭后台空闲循环动画。
  bool get ambientTickEnabled => _autoTick;

  Timer? _ticker;
  Timer? _sensorTimer;
  int _sensorBoostRefs = 0;
  int _ticks = 0;
  StreamSubscription<LinkState>? _stateSub;
  StreamSubscription<WatchStats>? _statsSub;
  StreamSubscription<int>? _controlsSub;
  StreamSubscription<WatchSettings>? _settingsSub;
  StreamSubscription<WatchSport>? _sportSub;
  StreamSubscription<WatchSensorData>? _sensorSub;

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

  /// 手表实际设置（0x1A 回传）。
  bool raiseWrist = true;
  bool keyBeep = true;
  int watchBrightness = 1;
  bool watchBluetooth = true;
  int ledMode = 1;
  int screenOff = 0;
  bool watchSettingsLoaded = false;

  /// 今日运动（0x27）。断开后保留最后一次读数，界面会标明“上次同步”。
  int steps = 0;
  int stepGoal = 8000;
  int distanceM = 0;
  int calories = 0;
  int activeMinutes = 0;
  List<int> sportBuckets = List<int>.filled(12, 0);
  DateTime? sportSyncedAt;
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
  double mediaPosition = 0;
  double mediaDuration = 0;

  /// Android：服务缓存的通知条数（最多 5 条）。
  int cachedNotifications = 0;

  bool get hasMedia => mediaTitle.isNotEmpty;

  /// Android 系统集成：通知转发、通知读取权限、电池优化白名单。
  bool notificationForward = true;
  bool notificationAccess = false;
  bool batteryExempt = false;

  /// 通知路由规则（原生服务持久化，App 重启后回读）。
  /// all = 全部通知；selected = 只转发勾选的应用。
  String routeMode = 'all';
  Set<String> routePackages = <String>{};
  int maxMessages = 5;
  bool mergeNotifications = true;
  List<NotificationApp> knownApps = const <NotificationApp>[];
  bool messageRulesLoaded = false;

  /// 主页预览用的真机画面（连接后自动抓一帧，点击可刷新）。
  ui.Image? deviceScreen;
  bool capturingPreview = false;

  /// 9 轴姿态融合实时遥测与 KEY2 硬铁校准状态。
  static const int magSpanTarget = 120;
  WatchSensorData? sensor;
  bool liveSensor = false;
  bool mpuOk = false;
  bool magOk = false;
  double rollDeg = 0;
  double pitchDeg = 0;
  double headingDeg = 0;
  int ax = 0;
  int ay = 0;
  int az = 1000;
  int mx = 0;
  int my = 0;
  int mz = 0;
  int fusedAxes = 0;
  int batteryMv = 0;
  bool sensorStreaming = true;
  MagCalPhase magCalPhase = MagCalPhase.idle;
  bool _magBoundsInit = false;
  int magMinX = 0;
  int magMaxX = 0;
  int magMinY = 0;
  int magMaxY = 0;
  int magMinZ = 0;
  int magMaxZ = 0;
  DateTime? magCalCompletedAt;
  String? calStatusNote;
  bool keyBusy = false;

  int get magSpanX => _magBoundsInit ? (magMaxX - magMinX) : 0;
  int get magSpanY => _magBoundsInit ? (magMaxY - magMinY) : 0;
  int get magSpanZ => _magBoundsInit ? (magMaxZ - magMinZ) : 0;
  bool get magCalCoverageReady =>
      _magBoundsInit &&
      magSpanX >= magSpanTarget &&
      magSpanY >= magSpanTarget &&
      magSpanZ >= magSpanTarget;
  int get magOffsetX => _magBoundsInit ? ((magMinX + magMaxX) ~/ 2) : 0;
  int get magOffsetY => _magBoundsInit ? ((magMinY + magMaxY) ~/ 2) : 0;
  int get magOffsetZ => _magBoundsInit ? ((magMinZ + magMaxZ) ~/ 2) : 0;

  String get dofBadge {
    if (liveSensor) {
      return sensor?.dofLabel ?? '--';
    }
    if (!connected && demoStreaming) return '3D 预览';
    if (connected && hasHardware) return '9-DOF 待测';
    return '9-DOF 演示';
  }

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
    if (!connected) return '未连接';
    if (_liveTelemetry) return '实时';
    return '等待数据';
  }

  double ramUsed = 0;
  double ramTotal = 16.0;
  double cpu = 0;
  int fps = 0;

  // 必须是可增长列表：_append 会 removeAt(0) + add，定长列表会抛异常，
  // 表现为数值在变、曲线却一直是平线（历史数组永远停在初始 0）。
  final List<double> ramHistory = List<double>.filled(28, 0, growable: true);
  final List<double> cpuHistory = List<double>.filled(28, 0, growable: true);
  final List<double> fpsHistory = List<double>.filled(28, 0, growable: true);

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
      unawaited(active.requestSport().catchError((Object _) => false));
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

  /// 让手表回一帧 0x1A，刷新设置页显示的档位。
  Future<bool> refreshWatchSettings() {
    final active = session;
    if (active == null || !connected) return Future<bool>.value(false);
    return active.querySettings();
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
      liveSensor = false;
      sensor = null;
      fusedAxes = 0;
      mpuOk = false;
      magOk = false;
      ramUsed = 0;
      cpu = 0;
      fps = 0;
      _fillZero(ramHistory);
      _fillZero(cpuHistory);
      _fillZero(fpsHistory);
      _stopMediaPager();
      deviceScreen?.dispose();
      deviceScreen = null;
    } else {
      unawaited(_syncAndroidIntegration());
    }
    _syncMediaPolling();
    _syncSensorPolling();
    notifyListeners();
  }

  /// 把设置同步给 Android 前台服务，并刷新系统权限状态。
  Future<void> _syncAndroidIntegration() async {
    if (!Platform.isAndroid) return;
    await AndroidWatchBridge.setMediaFollow(mediaFollow);
    await AndroidWatchBridge.setNotificationForward(notificationForward);
    await refreshAndroidStatus();
    await refreshMessageRules();
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

  void _applyWatchSettings(WatchSettings settings) {
    raiseWrist = settings.raiseWrist;
    keyBeep = settings.keyBeep;
    watchBrightness = settings.brightness;
    watchBluetooth = settings.bluetooth;
    ledMode = settings.ledMode;
    screenOff = settings.screenOff;
    watchSettingsLoaded = true;
    notifyListeners();
  }

  void _applySport(WatchSport sport) {
    steps = sport.steps;
    stepGoal = sport.goal > 0 ? sport.goal : 8000;
    distanceM = sport.distanceM;
    calories = sport.calories;
    activeMinutes = sport.activeMin;
    sportBuckets = List<int>.unmodifiable(sport.buckets);
    sportSyncedAt = DateTime.now();
    notifyListeners();
  }

  /// 主动拉一次今日运动数据。
  Future<bool> refreshSport() {
    final active = session;
    if (active == null || !connected) return Future<bool>.value(false);
    return active.requestSport();
  }

  /// 视图挂载 3D 大屏展示或校准面板时提高 9 轴采样率，退出时自动恢复。
  void acquireSensorBoost() {
    _sensorBoostRefs++;
    _syncSensorPolling();
  }

  void releaseSensorBoost() {
    if (_sensorBoostRefs > 0) _sensorBoostRefs--;
    _syncSensorPolling();
  }

  void setSensorStreaming(bool enabled) {
    if (sensorStreaming == enabled) return;
    sensorStreaming = enabled;
    _syncSensorPolling();
    notifyListeners();
  }

  void _syncSensorPolling() {
    _sensorTimer?.cancel();
    _sensorTimer = null;
    final active = session;
    if (!_autoTick ||
        !sensorStreaming ||
        !connected ||
        active == null) {
      return;
    }
    final intervalMs = _sensorBoostRefs > 0 ? 115 : 220;
    _sensorTimer = Timer.periodic(Duration(milliseconds: intervalMs), (_) {
      if (!connected || active.isCapturing || capturingPreview) return;
      unawaited(active.requestSensor().catchError((Object _) => null));
    });
  }

  Future<bool> refreshSensor() async {
    final active = session;
    if (active == null || !connected) return false;
    final data = await active.requestSensor();
    if (data != null) _applySensor(data);
    return data != null;
  }

  void _applySensor(WatchSensorData data) {
    sensor = data;
    liveSensor = true;
    mpuOk = data.mpuOk;
    magOk = data.magOk;
    ax = data.ax;
    ay = data.ay;
    az = data.az;
    mx = data.mx;
    my = data.my;
    mz = data.mz;
    fusedAxes = data.fusedAxes;
    rollDeg = data.rollDeg;
    pitchDeg = data.pitchDeg;
    headingDeg = data.headingDeg;
    if (data.batteryMv > 0) batteryMv = data.batteryMv;

    final fwCal = data.magCalState;
    if (fwCal == 1 &&
        magCalPhase != MagCalPhase.calibrating &&
        magCalPhase != MagCalPhase.readyToFinish) {
      beginMagCalTracking(note: '手表正在采集 360° 磁场样本，请多方向旋转手表');
    } else if (fwCal == 2 && magCalPhase != MagCalPhase.done) {
      magCalPhase = MagCalPhase.done;
      magCalCompletedAt = DateTime.now();
      calStatusNote = '固件已完成硬铁校准（9-DOF 磁力计偏置已锁存）';
    } else if (fwCal == 0 && magCalPhase == MagCalPhase.calibrating) {
      magCalPhase = MagCalPhase.idle;
      calStatusNote = null;
    }

    if (magCalPhase == MagCalPhase.calibrating ||
        magCalPhase == MagCalPhase.readyToFinish) {
      _updateMagCalibrationBounds(mx, my, mz);
    }
    notifyListeners();
  }

  void _updateMagCalibrationBounds(int x, int y, int z) {
    if (!_magBoundsInit) {
      _magBoundsInit = true;
      magMinX = magMaxX = x;
      magMinY = magMaxY = y;
      magMinZ = magMaxZ = z;
    } else {
      if (x < magMinX) magMinX = x;
      if (x > magMaxX) magMaxX = x;
      if (y < magMinY) magMinY = y;
      if (y > magMaxY) magMaxY = y;
      if (z < magMinZ) magMinZ = z;
      if (z > magMaxZ) magMaxZ = z;
    }
    if (magCalCoverageReady && magCalPhase == MagCalPhase.calibrating) {
      magCalPhase = MagCalPhase.readyToFinish;
      calStatusNote = '三轴跨度均已达到 ≥ 120 mG，请再次按下手表 KEY2 保存完成';
    }
  }

  void beginMagCalTracking({String? note}) {
    magCalPhase = MagCalPhase.calibrating;
    _magBoundsInit = false;
    magMinX = magMaxX = mx;
    magMinY = magMaxY = my;
    magMinZ = magMaxZ = mz;
    if (liveSensor) _magBoundsInit = true;
    calStatusNote = note ?? '请绕 Pitch / Roll / Yaw 三轴做 360° 旋转，使三轴跨度均 ≥ 120 mG';
    notifyListeners();
  }

  void completeMagCalTracking({String? note}) {
    magCalPhase = MagCalPhase.done;
    magCalCompletedAt = DateTime.now();
    calStatusNote = note ?? '已完成 KEY2 硬铁偏置校准，现在可查看精确朝向角';
    notifyListeners();
  }

  void cancelMagCalTracking() {
    magCalPhase = MagCalPhase.idle;
    _magBoundsInit = false;
    calStatusNote = null;
    notifyListeners();
  }

  /// 协同或远程触发手表 KEY2（启动/结束 360° 磁力计硬铁校准）。
  Future<bool> triggerKey2Calibration() async {
    if (keyBusy) return false;
    final active = session;
    if (active == null || !connected) {
      if (magCalPhase == MagCalPhase.idle || magCalPhase == MagCalPhase.done) {
        beginMagCalTracking(note: '预览模式：模拟 360° 采集中，再点一次完成校准（实机连接后将发送真机 KEY2）');
        _updateMagCalibrationBounds(-45, -30, -480);
        _updateMagCalibrationBounds(45, 55, -420);
      } else {
        completeMagCalTracking(note: '预览模式：已完成模拟 KEY2 校准');
      }
      return true;
    }

    keyBusy = true;
    notifyListeners();
    try {
      final ok = await active.sendKeyTap(WatchKeyMask.key2);
      if (!ok) {
        calStatusNote = '未能向手表发送 KEY2，请直接在手表按键操作';
        return false;
      }
      if (magCalPhase == MagCalPhase.idle || magCalPhase == MagCalPhase.done) {
        beginMagCalTracking(note: '已远程按下 KEY2，请保证手表处于「传感器数值」页并旋转 360°');
      } else {
        completeMagCalTracking(note: '已远程按下 KEY2 完成硬铁校准保存');
      }
      return true;
    } finally {
      keyBusy = false;
      notifyListeners();
    }
  }

  /// 远程触发手表 KEY3（复位姿态角与陀螺仪零偏）。
  Future<bool> triggerKey3Reset() async {
    if (keyBusy) return false;
    final active = session;
    if (active == null || !connected) {
      rollDeg = 0;
      pitchDeg = 0;
      headingDeg = 0;
      calStatusNote = '预览模式：已将姿态归零（实机下对应 KEY3 zero/reset）';
      notifyListeners();
      return true;
    }

    keyBusy = true;
    notifyListeners();
    try {
      final ok = await active.sendKeyTap(WatchKeyMask.key3);
      calStatusNote = ok
          ? '已发送 KEY3 姿态与陀螺仪零偏复位脉冲'
          : '发送 KEY3 失败，请在手表「传感器数值」页直接按 KEY3';
      if (ok) unawaited(refreshSensor());
      return ok;
    } finally {
      keyBusy = false;
      notifyListeners();
    }
  }

  void _sendSetting(int key, int value) {
    final active = session;
    if (active == null) return;
    unawaited(active.setSetting(key, value));
  }

  void setRaiseWrist(bool value) {
    raiseWrist = value;
    _sendSetting(1, value ? 1 : 0);
    notifyListeners();
    onSettingsChanged?.call();
  }

  void setKeyBeep(bool value) {
    keyBeep = value;
    _sendSetting(2, value ? 1 : 0);
    notifyListeners();
    onSettingsChanged?.call();
  }

  void setWatchBrightness(int value) {
    watchBrightness = value.clamp(0, 2);
    _sendSetting(3, watchBrightness);
    notifyListeners();
    onSettingsChanged?.call();
  }

  void setLedMode(int value) {
    ledMode = value.clamp(0, 2);
    _sendSetting(5, ledMode);
    notifyListeners();
    onSettingsChanged?.call();
  }

  void setScreenOff(int value) {
    screenOff = value.clamp(0, 4);
    _sendSetting(6, screenOff);
    notifyListeners();
    onSettingsChanged?.call();
  }

  /// 回读原生服务持久化的通知路由规则。
  Future<void> refreshMessageRules() async {
    if (!Platform.isAndroid) return;
    final rules = await AndroidWatchBridge.messageRules();
    final mode = '${rules['mode'] ?? 'all'}';
    routeMode = mode == 'selected' ? 'selected' : 'all';
    routePackages = <String>{
      for (final item in (rules['packages'] as List<Object?>? ?? const []))
        '$item',
    };
    maxMessages = ((rules['maxMessages'] as num?)?.toInt() ?? 5).clamp(1, 5);
    mergeNotifications = rules['merge'] != false;
    final apps = await AndroidWatchBridge.notificationApps();
    knownApps = <NotificationApp>[
      for (final item in apps)
        NotificationApp(
          package: '${item['package'] ?? ''}',
          label: '${item['label'] ?? item['package'] ?? ''}',
        ),
    ];
    messageRulesLoaded = true;
    notifyListeners();
  }

  void setRouteMode(String value) {
    routeMode = value == 'selected' ? 'selected' : 'all';
    unawaited(_pushMessageRules());
    notifyListeners();
  }

  void toggleRoutePackage(String package, bool selected) {
    if (selected) {
      routePackages = <String>{...routePackages, package};
    } else {
      routePackages = <String>{
        for (final item in routePackages)
          if (item != package) item,
      };
    }
    unawaited(_pushMessageRules());
    notifyListeners();
  }

  void setMaxMessages(int value) {
    maxMessages = value.clamp(1, 5);
    unawaited(_pushMessageRules());
    notifyListeners();
  }

  void setMergeNotifications(bool value) {
    mergeNotifications = value;
    unawaited(_pushMessageRules());
    notifyListeners();
  }

  Future<void> _pushMessageRules() {
    if (!Platform.isAndroid) return Future<void>.value();
    return AndroidWatchBridge.setMessageRules(
      mode: routeMode,
      packages: routePackages,
      maxMessages: maxMessages,
      merge: mergeNotifications,
    );
  }

  void _applyControl(int action) {
    lastControlAction = action;
    controlSeq++;
    // Android 的 0x10 已由前台服务直接操作系统媒体，这里只同步界面。
    if (Platform.isWindows) {
      if (action == WatchControl.play) _controlMedia('play');
      if (action == WatchControl.pause) _controlMedia('pause');
      if (action == WatchControl.next) _controlMedia('next');
      if (action == WatchControl.previous) _controlMedia('previous');
    }
    if (action == WatchControl.play) playing = true;
    if (action == WatchControl.pause) playing = false;
    notifyListeners();
  }

  void _controlMedia(String action) {
    if (Platform.isWindows) {
      unawaited(_mediaMonitor.control(action));
      return;
    }
    if (Platform.isAndroid) {
      unawaited(AndroidWatchBridge.mediaControl(action));
    }
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
    if (!connected) {
      // 没连接就老实显示 0，不再跑演示数据。
      ramUsed = 0;
      cpu = 0;
      fps = 0;
    } else if (session == null && demoStreaming && !_liveTelemetry) {
      // 仅用于没有硬件会话的纯界面预览。
      final phase = _ticks / 3;
      ramUsed = 10.0 + 0.16 * math.sin(phase * 0.7);
      cpu = 18.4 + 4.2 * math.sin(phase) + 1.4 * math.cos(phase * 0.43);
      fps = _ticks % 9 == 0 ? 61 : 60;
      _append(ramHistory, ramUsed / ramTotal);
      _append(cpuHistory, cpu / 100);
      _append(fpsHistory, fps / 61);
    }
    // 运动数据 5 秒一次；抓屏期间让路，避免把帧插进图像流。
    final activeSession = session;
    if (connected &&
        activeSession != null &&
        _ticks % 5 == 0 &&
        !activeSession.isCapturing) {
      unawaited(
        activeSession.requestSport().catchError((Object _) => false),
      );
    }
    if (Platform.isAndroid) {
      unawaited(_pollAndroidMedia());
      if (_ticks % 2 == 0) {
        unawaited(_pollMessageCount());
        unawaited(_maybeReattach());
      }
    }
    notifyListeners();
  }

  bool _reattaching = false;
  DateTime? _lastReattachAttempt;

  /// 前台服务自己重连成功后，Flutter 侧自动挂回，不必手点“连接”。
  Future<void> _maybeReattach() async {
    if (!Platform.isAndroid || session == null || _reattaching) return;
    if (connected || connecting) return;
    final now = DateTime.now();
    final last = _lastReattachAttempt;
    if (last != null && now.difference(last) < const Duration(seconds: 5)) {
      return;
    }
    _lastReattachAttempt = now;
    _reattaching = true;
    try {
      if (!await AndroidWatchBridge.isConnected()) return;
      if (connected || connecting) return;
      await connect();
    } finally {
      _reattaching = false;
    }
  }

  bool _pollingAndroidMedia = false;

  /// Android 没有 SMTC，改为每秒向原生服务要一次当前媒体与进度。
  Future<void> _pollAndroidMedia() async {
    if (!Platform.isAndroid || _pollingAndroidMedia) return;
    _pollingAndroidMedia = true;
    try {
      final info = await AndroidWatchBridge.nowPlaying();
      if (info == null) return;
      final title = (info['title'] as String?)?.trim() ?? '';
      final artist = (info['artist'] as String?)?.trim() ?? '';
      final isPlaying = info['playing'] == true;
      final position = (info['positionMs'] as num?)?.toDouble() ?? 0;
      final duration = (info['durationMs'] as num?)?.toDouble() ?? 0;
      mediaTitle = title;
      mediaArtist = artist;
      playing = isPlaying;
      mediaPosition = position;
      mediaDuration = duration;
      notifyListeners();
    } finally {
      _pollingAndroidMedia = false;
    }
  }

  Future<void> _pollMessageCount() async {
    if (!Platform.isAndroid) return;
    final count = await AndroidWatchBridge.messageCount();
    if (count != cachedNotifications) {
      cachedNotifications = count;
      notifyListeners();
    }
  }

  /// 手机端切歌；手表按键走原生 0x10，不会重复触发。
  void skipMedia({required bool next}) {
    _controlMedia(next ? 'next' : 'previous');
  }

  void seekMedia(double seconds) {
    if (Platform.isAndroid) {
      unawaited(AndroidWatchBridge.seekTo((seconds * 1000).round()));
    }
  }

  static void _fillZero(List<double> values) {
    for (var i = 0; i < values.length; i++) {
      values[i] = 0;
    }
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
    if (Platform.isAndroid) {
      unawaited(refreshAndroidStatus());
      unawaited(refreshMessageRules());
    }
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
  };

  void _update(VoidCallback change) {
    change();
    notifyListeners();
  }

  @override
  void dispose() {
    _ticker?.cancel();
    _sensorTimer?.cancel();
    _mediaTimer?.cancel();
    _mediaPageTimer?.cancel();
    _stateSub?.cancel();
    _statsSub?.cancel();
    _controlsSub?.cancel();
    _settingsSub?.cancel();
    _sportSub?.cancel();
    _sensorSub?.cancel();
    deviceScreen?.dispose();
    unawaited(session?.dispose());
    super.dispose();
  }
}
