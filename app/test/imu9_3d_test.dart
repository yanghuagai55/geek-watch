import 'dart:async';
import 'dart:convert';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:watch_companion/main.dart';
import 'package:watch_companion/services/watch_glb_loader.dart';
import 'package:watch_companion/services/watch_protocol.dart';
import 'package:watch_companion/services/watch_session.dart';
import 'package:watch_companion/services/watch_transport.dart';
import 'package:watch_companion/state/watch_controller.dart';
import 'package:watch_companion/widgets/metric_card.dart';
import 'package:watch_companion/widgets/watch_3d_view.dart';
import 'package:watch_companion/widgets/watch_preview.dart';

class _MockTransport implements WatchTransport {
  final StreamController<Uint8List> _rx = StreamController<Uint8List>.broadcast();
  final StreamController<void> _closed = StreamController<void>.broadcast();
  final List<Uint8List> sent = <Uint8List>[];
  bool _connected = true;

  @override
  String get displayName => 'Mock SPP';

  @override
  bool get isConnected => _connected;

  @override
  Stream<Uint8List> get incoming => _rx.stream;

  @override
  Stream<void> get closed => _closed.stream;

  @override
  Future<void> connect() async {
    _connected = true;
  }

  @override
  Future<void> send(Uint8List frame) async {
    sent.add(Uint8List.fromList(frame));
  }

  void pushText(String text) {
    _rx.add(Uint8List.fromList(utf8.encode(text)));
  }

  @override
  Future<void> close() async {
    _connected = false;
    await _rx.close();
    await _closed.close();
  }

  @override
  Future<void> release() => close();
}

void main() {
  const windowThemeChannel = MethodChannel('watch_companion/window_theme');

  setUp(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(windowThemeChannel, (call) async => null);
  });

  tearDown(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(windowThemeChannel, null);
  });

  test('WatchSensorData 解析固件 8c6ccf9 DBG 九轴调试行', () {
    const raw =
        'DBG I2C=2C,68;MPU=1;QMC5883P@2C;ID=80;ST=19;C1=CD;C2=08;'
        'A=-12,35,998;M=140,260,-460;9AX=9;RPY=-8,24,332;BAT=4020;CAL=1,1';
    final parsed = WatchSensorData.tryParseDebugLine(raw);
    expect(parsed, isNotNull);
    expect(parsed!.mpuOk, isTrue);
    expect(parsed.magOk, isTrue);
    expect(parsed.magChip, 'QMC5883P@2C');
    expect(parsed.ax, -12);
    expect(parsed.ay, 35);
    expect(parsed.az, 998);
    expect(parsed.mx, 140);
    expect(parsed.my, 260);
    expect(parsed.mz, -460);
    expect(parsed.fusedAxes, 9);
    expect(parsed.dofLabel, '9-DOF');
    expect(parsed.rollDeg, -8.0);
    expect(parsed.pitchDeg, 24.0);
    expect(parsed.headingDeg, 332.0);
    expect(parsed.batteryMv, 4020);
    expect(parsed.magCalState, 1);
    expect(parsed.gyroCalState, 1);

    expect(WatchSensorData.tryParseDebugLine('ok'), isNull);
    expect(WatchSensorData.tryParseDebugLine('random log'), isNull);
  });

  test('assets/models/at32_watch.glb 能正常解析并返回屏幕包围盒', () async {
    TestWidgetsFlutterBinding.ensureInitialized();
    final model = await WatchGlbModel.loadDefault();
    expect(model.vertexCount, greaterThan(5000));
    expect(model.parts.any((p) => p.name == 'TFT_GLASS'), isTrue);
    expect(model.parts.any((p) => p.name == 'PCB'), isTrue);
    expect(model.radius, inInclusiveRange(0.03, 0.08));
    expect(model.glassBoundsBody[1], greaterThan(model.glassBoundsBody[0]));
    expect(model.glassBoundsBody[3], greaterThan(model.glassBoundsBody[2]));
  });

  test('WatchSession 支持 0x13 传感器轮询与 0x07 KEY2 按键脉冲', () async {
    final mock = _MockTransport();
    final session = WatchSession(
      transportFactory: (_, _, _) => mock,
      enableTelemetry: false,
    );
    await session.connect(type: TransportType.spp, address: 'TEST');
    mock.sent.clear();

    final future = session.requestSensor(timeout: const Duration(seconds: 2));
    await pumpEventQueue();
    expect(mock.sent.last, buildWatchFrame(WatchCmd.sensor));
    mock.pushText(
      'DBG I2C=2C,68;MPU=1;QMC5883P@2C;A=0,707,707;M=0,-141,-566;'
      '9AX=9;RPY=0,45,0;BAT=4010\r\n',
    );
    final sensor = await future;
    expect(sensor, isNotNull);
    expect(sensor!.pitchDeg, 45.0);
    expect(sensor.fusedAxes, 9);

    mock.sent.clear();
    final keyFuture = session.sendKeyTap(
      WatchKeyMask.key2,
      hold: const Duration(milliseconds: 5),
    );
    // 依次应答 setDebug(1) / press(key2) / release(0)
    await pumpEventQueue();
    mock.pushText('ok\r\n');
    await pumpEventQueue();
    mock.pushText('ok\r\n');
    await Future<void>.delayed(const Duration(milliseconds: 15));
    mock.pushText('ok\r\n');

    final ok = await keyFuture;
    expect(ok, isTrue);
    expect(mock.sent.length, 3);
    expect(mock.sent[0][1], WatchCmd.debug);
    expect(mock.sent[0][3], 1);
    expect(mock.sent[1][1], WatchCmd.key);
    expect(mock.sent[1][3], WatchKeyMask.key2);
    expect(mock.sent[2][1], WatchCmd.key);
    expect(mock.sent[2][3], 0);

    await session.dispose();
  });

  test('WatchController KEY2 校准跨度统计达 120mG 自动就绪', () async {
    final mock = _MockTransport();
    final session = WatchSession(
      transportFactory: (_, _, _) => mock,
      enableTelemetry: false,
    );
    final controller = WatchController(startTicker: false, session: session);
    addTearDown(controller.dispose);

    await session.connect(type: TransportType.spp, address: 'TEST');
    controller.beginMagCalTracking();
    expect(controller.magCalPhase, MagCalPhase.calibrating);

    mock.pushText(
      'DBG MPU=1;QMC5883P@2C;A=0,0,1000;M=-200,-180,-500;9AX=9;RPY=0,0,10;BAT=4000\r\n',
    );
    await pumpEventQueue();
    expect(controller.magCalPhase, MagCalPhase.calibrating);

    mock.pushText(
      'DBG MPU=1;QMC5883P@2C;A=0,0,1000;M=100,120,-320;9AX=9;RPY=0,0,190;BAT=4000\r\n',
    );
    await pumpEventQueue();

    expect(controller.magSpanX, 300);
    expect(controller.magSpanY, 300);
    expect(controller.magSpanZ, 180);
    expect(controller.magCalCoverageReady, isTrue);
    expect(controller.magCalPhase, MagCalPhase.readyToFinish);
    expect(controller.magOffsetX, -50);
    expect(controller.magOffsetY, -30);
    expect(controller.magOffsetZ, -410);
  });

  testWidgets('首页竖屏 3D 实时展示适配与运动页 3D 大视图 / KEY2 提示', (tester) async {
    await tester.binding.setSurfaceSize(const Size(390, 840));
    addTearDown(() => tester.binding.setSurfaceSize(null));
    final controller = WatchController(startTicker: false)
      ..setAnimations(false);
    addTearDown(controller.dispose);

    await tester.pumpWidget(WatchCompanionApp(controller: controller));
    await tester.pumpAndSettle();

    expect(find.byKey(const Key('home-watch-3d')), findsOneWidget);
    expect(find.byType(MiniMetricCard), findsNWidgets(3));
    final metricsBottom = tester
        .getBottomRight(find.byType(MiniMetricCard).last)
        .dy;
    final navTop = tester
        .getTopLeft(find.byKey(const Key('mobile-liquid-nav')))
        .dy;
    expect(metricsBottom, lessThan(navTop));

    // 点击底部导航切到运动页，确认步数卡位于 3D 大视图上方
    await tester.tap(find.text('运动'));
    await tester.pumpAndSettle();
    expect(find.byKey(const Key('sport-watch-3d-large')), findsOneWidget);
    expect(find.text('今日步数'), findsOneWidget);
    expect(
      tester.getTopLeft(find.text('今日步数')).dy,
      lessThan(
        tester.getTopLeft(find.byKey(const Key('sport-watch-3d-large'))).dy,
      ),
    );
    expect(find.text('KEY2 磁强计硬铁校准'), findsOneWidget);
    expect(find.text('X 轴跨度'), findsOneWidget);

    await tester.ensureVisible(find.text('体验 KEY2 校准'));
    await tester.tap(find.text('体验 KEY2 校准'));
    await tester.pumpAndSettle();
    expect(find.text('正在采集磁场包络 · 请 360° 旋转'), findsOneWidget);
    expect(tester.takeException(), isNull);

    // 验证桌面宽屏运动页步数居首，首页同时展示实时截屏与 3D 视图
    await tester.binding.setSurfaceSize(const Size(1280, 800));
    await tester.pumpAndSettle();
    expect(find.byKey(const Key('sport-watch-3d-large')), findsOneWidget);
    expect(
      tester.getTopLeft(find.text('今日步数')).dy,
      lessThan(
        tester.getTopLeft(find.byKey(const Key('sport-watch-3d-large'))).dy,
      ),
    );
    await tester.tap(find.byTooltip('主页').first);
    await tester.pumpAndSettle();
    expect(find.byType(WatchPreview), findsOneWidget);
    expect(find.byType(Watch3DView), findsOneWidget);
    expect(find.byKey(const Key('home-watch-3d')), findsOneWidget);
    expect(find.text('实时截屏 / 表盘'), findsOneWidget);
    expect(find.text('3D 实时姿态 · 点击大视图'), findsOneWidget);

    // 验证媒体与表盘页在桌面端切换零异常
    await tester.tap(find.byTooltip('媒体').first);
    await tester.pumpAndSettle();
    expect(find.text('同步到手表'), findsOneWidget);
    expect(find.text('手动发送测试消息'), findsOneWidget);
    await tester.tap(find.byTooltip('表盘').first);
    await tester.pumpAndSettle();
    expect(find.text('32KB 外置 AT24C256 动态 ROM (0x50)'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('极窄竖屏 320x720 首页与运动页零溢出', (tester) async {
    await tester.binding.setSurfaceSize(const Size(320, 720));
    addTearDown(() => tester.binding.setSurfaceSize(null));
    final controller = WatchController(startTicker: false)
      ..setAnimations(false);
    addTearDown(controller.dispose);

    await tester.pumpWidget(WatchCompanionApp(controller: controller));
    await tester.pumpAndSettle();
    expect(find.byKey(const Key('home-watch-3d')), findsOneWidget);
    expect(tester.takeException(), isNull);

    await tester.tap(find.text('截屏'));
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);

    await tester.tap(find.text('运动'));
    await tester.pumpAndSettle();
    expect(find.byKey(const Key('sport-watch-3d-large')), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}
