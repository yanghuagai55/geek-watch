import 'dart:async';
import 'dart:math' as math;

import 'package:material_ui/material_ui.dart';

enum TransportType { spp, ble, serial }

class WatchController extends ChangeNotifier {
  WatchController({bool startTicker = true}) {
    if (startTicker) {
      _ticker = Timer.periodic(const Duration(seconds: 1), (_) => _tick());
    }
  }

  Timer? _ticker;
  int _ticks = 0;

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

  void _tick() {
    _ticks += 1;
    now = DateTime.now();
    if (demoStreaming) {
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
  }

  void setPreviewConnected(bool value) =>
      _update(() => previewConnected = value);
  void setAutoConnect(bool value) => _update(() => autoConnect = value);
  void setAutoSyncTime(bool value) => _update(() => autoSyncTime = value);
  void setAnimations(bool value) => _update(() => animations = value);
  void setDemoStreaming(bool value) => _update(() => demoStreaming = value);
  void setLiquidGlass(bool value) => _update(() => liquidGlass = value);
  void setUse24Hour(bool value) => _update(() => use24Hour = value);
  void setBrightness(double value) => _update(() => brightness = value);
  void setScreenTimeout(String value) => _update(() => screenTimeout = value);
  void setAddress(String value) => deviceAddress = value;
  void setSerialPort(String value) => serialPort = value;
  void setBaudRate(String? value) {
    if (value != null) _update(() => baudRate = value);
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
    notifyListeners();
  }

  void _update(VoidCallback change) {
    change();
    notifyListeners();
  }

  @override
  void dispose() {
    _ticker?.cancel();
    super.dispose();
  }
}
