import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:prism_ui/icons/glyph.dart';
import 'package:prism_ui/icons/glyph_painter.dart';
import 'package:prism_ui/liquid_glass.dart';
import 'package:watch_companion/main.dart';
import 'package:watch_companion/services/watch_session.dart';
import 'package:watch_companion/services/watch_transport.dart';
import 'package:watch_companion/state/watch_controller.dart';
import 'package:watch_companion/theme/app_theme.dart';
import 'package:watch_companion/widgets/companion_glyphs.dart';

class _SensorMockTransport implements WatchTransport {
  final StreamController<Uint8List> _rx = StreamController<Uint8List>.broadcast();
  final StreamController<void> _closed = StreamController<void>.broadcast();
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
  Future<void> connect() async => _connected = true;

  @override
  Future<void> send(Uint8List frame) async {}

  void pushText(String text) => _rx.add(Uint8List.fromList(utf8.encode(text)));

  @override
  Future<void> close() async {
    _connected = false;
    await _rx.close();
    await _closed.close();
  }

  @override
  Future<void> release() => close();
}

const _previewOut = String.fromEnvironment('PREVIEW_OUT');

Future<ui.Image> _renderGlyphSheet() async {
  const glyphs = <(String, Glyph)>[
    ('主页', CompanionGlyphs.dashboard),
    ('运动', CompanionGlyphs.sport),
    ('媒体', CompanionGlyphs.media),
    ('表盘', CompanionGlyphs.watchFace),
    ('设置', CompanionGlyphs.settings),
  ];

  const width = 900.0;
  const height = 460.0;
  final recorder = ui.PictureRecorder();
  final canvas = Canvas(recorder, const Rect.fromLTWH(0, 0, width, height));

  // Row 1: Light dock simulation (actual 28dp * 2 = 56px) + 96px zoomed
  canvas.drawRect(
    const Rect.fromLTWH(0, 0, width, 230),
    Paint()..color = const Color(0xFFF3F5EE),
  );
  // Row 2: Dark dock simulation
  canvas.drawRect(
    const Rect.fromLTWH(0, 230, width, 230),
    Paint()..color = const Color(0xFF0A0F10),
  );

  void drawDockBar(double top, bool dark) {
    final pillRect = Rect.fromLTWH(40, top + 16, width - 80, 108);
    final pillRRect = RRect.fromRectAndRadius(
      pillRect,
      const Radius.circular(54),
    );
    canvas.drawRRect(
      pillRRect,
      Paint()..color = dark ? const Color(0xFF182120) : const Color(0xFFFCFCF6),
    );
    canvas.drawRRect(
      pillRRect,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.5
        ..color = dark ? const Color(0xFF31413C) : const Color(0xFFC7D5C8),
    );

    // Selected mint pill on first item
    final selRect = Rect.fromLTWH(48, top + 24, (width - 96) / 5, 92);
    canvas.drawRRect(
      RRect.fromRectAndRadius(selRect, const Radius.circular(46)),
      Paint()..color = dark ? const Color(0xFF19352B) : const Color(0xFFB7F5DA),
    );

    final colW = (width - 96) / 5;
    for (var i = 0; i < glyphs.length; i++) {
      final cx = 48 + colW * (i + 0.5);
      final iconColor = i == 0
          ? (dark ? AppPalette.mint : AppPalette.mintDeep)
          : (dark ? AppPalette.muted : const Color(0xFF56675D));
      // 56px (2x of 28dp) dock icon at fill=1
      canvas.save();
      canvas.translate(cx - 28, top + 30);
      GlyphPainter(
        glyph: glyphs[i].$2,
        fill: 1,
        color: iconColor,
      ).paint(canvas, const Size.square(56));
      canvas.restore();

      // Zoomed 76px preview right below the bar (fill=1 left, fill=0 right)
      canvas.save();
      canvas.translate(cx - 42, top + 138);
      GlyphPainter(
        glyph: glyphs[i].$2,
        fill: 1,
        color: dark ? AppPalette.warmWhite : const Color(0xFF192520),
      ).paint(canvas, const Size.square(72));
      canvas.restore();

      canvas.save();
      canvas.translate(cx + 34, top + 152);
      GlyphPainter(
        glyph: glyphs[i].$2,
        fill: 0,
        color: dark ? AppPalette.mint : AppPalette.mintDeep,
      ).paint(canvas, const Size.square(44));
      canvas.restore();
    }
  }

  drawDockBar(0, false);
  drawDockBar(230, true);

  final picture = recorder.endRecording();
  final image = await picture.toImage(width.toInt(), height.toInt());
  picture.dispose();
  return image;
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

  testWidgets('redesigned portrait glyphs have crisp cutouts and balanced coverage', (
    tester,
  ) async {
    const glyphs = <(String, Glyph)>[
      ('dashboard', CompanionGlyphs.dashboard),
      ('sport', CompanionGlyphs.sport),
      ('media', CompanionGlyphs.media),
      ('watchFace', CompanionGlyphs.watchFace),
      ('settings', CompanionGlyphs.settings),
    ];

    await tester.runAsync(() async {
      for (final (name, glyph) in glyphs) {
        // Every portrait dock glyph should combine body + negative-space detail.
        expect(
          glyph.shapes.any((s) => s.role == GlyphRole.body),
          isTrue,
          reason: '$name missing body',
        );
        expect(
          glyph.shapes.any((s) => s.role == GlyphRole.detail),
          isTrue,
          reason: '$name missing detail cutout',
        );

        // Render at 84x84 (28dp @ 3x DPR) and measure filled vs cutout interior.
        const side = 84;
        final rec = ui.PictureRecorder();
        final canvas = Canvas(
          rec,
          Rect.fromLTWH(0, 0, side.toDouble(), side.toDouble()),
        );
        GlyphPainter(
          glyph: glyph,
          fill: 1,
          color: const Color(0xFF192520),
        ).paint(canvas, Size.square(side.toDouble()));
        final pic = rec.endRecording();
        final img = await pic.toImage(side, side);
        pic.dispose();
        final bd = await img.toByteData(format: ui.ImageByteFormat.rawRgba);
        img.dispose();
        final rgba = bd!.buffer.asUint8List();

        var opaquePixels = 0;
        var centerCutoutPixels = 0;
        for (var y = 0; y < side; y++) {
          for (var x = 0; x < side; x++) {
            final a = rgba[(y * side + x) * 4 + 3];
            if (a > 128) opaquePixels++;
            if (x >= 26 && x <= 58 && y >= 26 && y <= 62 && a < 40) {
              centerCutoutPixels++;
            }
          }
        }
        final coverage = opaquePixels / (side * side);
        expect(
          coverage,
          inInclusiveRange(0.26, 0.62),
          reason: '$name optical fill coverage=$coverage out of balanced range',
        );
        expect(
          centerCutoutPixels,
          greaterThan(70),
          reason: '$name interior negative-space cutout too small ($centerCutoutPixels px)',
        );
      }

      if (_previewOut.isNotEmpty) {
        final sheet = await _renderGlyphSheet();
        final bytes = await sheet.toByteData(format: ui.ImageByteFormat.png);
        sheet.dispose();
        final outFile = File(_previewOut);
        await outFile.parent.create(recursive: true);
        await outFile.writeAsBytes(bytes!.buffer.asUint8List());
      }
    });
  });

  testWidgets('60fps 3D watch animation does not re-capture Liquid Glass dock', (
    tester,
  ) async {
    await tester.binding.setSurfaceSize(const Size(390, 840));
    addTearDown(() => tester.binding.setSurfaceSize(null));

    // 默认挡位是 ultra；这里显式降到 standard，验证省电档的抓帧路径。
    final controller = WatchController(startTicker: true)
      ..setLiquidGlassLevel(LiquidGlassLevel.standard);
    addTearDown(controller.dispose);

    await tester.pumpWidget(WatchCompanionApp(controller: controller));
    // Let initial async font/shader/GLB setup settle over a few frames.
    for (var i = 0; i < 6; i++) {
      await tester.pump(const Duration(milliseconds: 30));
    }

    final initialCapture = LiquidGlassHost.debugLastCapture;
    expect(initialCapture, isNotNull);
    // Standard quality uses a single downscaled blurred slot (not 2 slots / full-res crisp).
    expect(initialCapture!.panes.length, 1);
    expect(initialCapture.panes.single.scale, lessThan(1.0));

    // Pump 30 frames (~480ms) while Watch3DView animates continuously at 60 FPS.
    // Because Watch3DView is isolated in a RepaintBoundary and _snapshot dry-runs
    // before recording, zero new backdrop snapshots should occur.
    var prevCaptureCount = LiquidGlassHost.debugCaptures;
    var extraSnapshots = 0;
    for (var i = 0; i < 30; i++) {
      await tester.pump(const Duration(milliseconds: 16));
      final nowCount = LiquidGlassHost.debugCaptures;
      if (nowCount > prevCaptureCount) {
        extraSnapshots += LiquidGlassHost.debugLastCapture?.snapshots ?? 0;
        prevCaptureCount = nowCount;
      }
    }
    expect(extraSnapshots, 0);

    // Pressing the dock triggers elastic swell, but steadyFrost + holdInsets
    // keeps the Level-0 dock backdrop cached with zero Level-0 re-captures.
    final dockCenter = tester.getCenter(find.byKey(const Key('mobile-liquid-nav')));
    final gesture = await tester.startGesture(dockCenter);
    await tester.pump(const Duration(milliseconds: 16));
    final level0Before = LiquidGlassHost.debugLastCapture!.panes
        .firstWhere((p) => p.level == 0)
        .atlas;
    var pressChanges = 0;
    var lastAtlas = level0Before;
    for (var i = 0; i < 12; i++) {
      await tester.pump(const Duration(milliseconds: 16));
      final level0Now = LiquidGlassHost.debugLastCapture!.panes
          .firstWhere((p) => p.level == 0);
      if (!identical(level0Now.atlas, lastAtlas)) {
        pressChanges++;
        lastAtlas = level0Now.atlas;
      }
    }
    expect(pressChanges, 0);
    await gesture.up();
    await tester.pump(const Duration(milliseconds: 400));
    expect(tester.takeException(), isNull);
  });

  test('WatchController routes routine 9-DOF samples to sensorListenable without full rebuild', () async {
    final mock = _SensorMockTransport();
    final session = WatchSession(
      transportFactory: (_, _, _) => mock,
      enableTelemetry: false,
    );
    final controller = WatchController(startTicker: false, session: session);
    addTearDown(controller.dispose);

    await session.connect(type: TransportType.spp, address: 'TEST');
    await pumpEventQueue();

    var mainNotifies = 0;
    var sensorNotifies = 0;
    controller.addListener(() => mainNotifies++);
    controller.sensorListenable.addListener(() => sensorNotifies++);

    mock.pushText(
      'DBG MPU=1;QMC5883P@2C;A=0,0,1000;M=10,20,-400;9AX=9;RPY=1,2,30;BAT=4000\r\n',
    );
    await pumpEventQueue();
    expect(sensorNotifies, 1);
    expect(mainNotifies, 1); // first sample flips liveSensor false -> true

    mock.pushText(
      'DBG MPU=1;QMC5883P@2C;A=1,2,999;M=11,19,-401;9AX=9;RPY=4,5,35;BAT=4000\r\n',
    );
    mock.pushText(
      'DBG MPU=1;QMC5883P@2C;A=2,3,998;M=12,18,-402;9AX=9;RPY=7,8,40;BAT=4005\r\n',
    );
    await pumpEventQueue();

    // Routine angle samples notify only sensorListenable, avoiding full-page rebuilds.
    expect(sensorNotifies, 3);
    expect(mainNotifies, 1);
    expect(controller.headingDeg, 40);
  });

  test('液态玻璃挡位随 settings.json 往返，旧配置回落最高挡', () {
    final controller = WatchController(startTicker: false);
    addTearDown(controller.dispose);
    expect(controller.liquidGlassLevel, LiquidGlassLevel.ultra);
    expect(controller.liquidGlass, isTrue);

    controller.setLiquidGlassLevel(LiquidGlassLevel.standard);
    expect(controller.persistedSettings['liquidGlassLevel'], 'standard');

    final restored = WatchController(startTicker: false)
      ..applyPersisted(controller.persistedSettings);
    addTearDown(restored.dispose);
    expect(restored.liquidGlassLevel, LiquidGlassLevel.standard);

    restored.setLiquidGlassLevel(LiquidGlassLevel.off);
    expect(restored.liquidGlass, isFalse);

    // 旧版本 settings.json 没有该键：回到被临时移除的 ultra 挡。
    final legacy = WatchController(startTicker: false)
      ..applyPersisted(<String, dynamic>{'uiRevision': 2});
    addTearDown(legacy.dispose);
    expect(legacy.liquidGlassLevel, LiquidGlassLevel.ultra);
  });

  testWidgets('挡位切换立刻改变 LiquidGlassScope，关闭后回到 Material 底栏', (
    tester,
  ) async {
    await tester.binding.setSurfaceSize(const Size(390, 840));
    addTearDown(() => tester.binding.setSurfaceSize(null));

    final controller = WatchController(startTicker: true);
    addTearDown(controller.dispose);

    await tester.pumpWidget(WatchCompanionApp(controller: controller));
    for (var i = 0; i < 6; i++) {
      await tester.pump(const Duration(milliseconds: 30));
    }

    final dock = find.byKey(const Key('mobile-liquid-nav'));
    expect(dock, findsOneWidget);
    expect(
      LiquidGlassScope.qualityOf(tester.element(dock)),
      LiquidGlassQuality.ultra,
    );

    controller.setLiquidGlassLevel(LiquidGlassLevel.standard);
    await tester.pump();
    expect(
      LiquidGlassScope.qualityOf(tester.element(dock)),
      LiquidGlassQuality.standard,
    );

    controller.setLiquidGlassLevel(LiquidGlassLevel.off);
    await tester.pump(const Duration(milliseconds: 50));
    expect(find.byKey(const Key('mobile-liquid-nav')), findsNothing);
    expect(find.byType(NavigationBar), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}
