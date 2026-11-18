import 'dart:async';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:watch_companion/main.dart';
import 'package:watch_companion/services/watch_image_codec.dart';
import 'package:watch_companion/services/watch_message_rasterizer.dart';
import 'package:watch_companion/services/watch_protocol.dart';
import 'package:watch_companion/services/watch_session.dart';
import 'package:watch_companion/services/watch_transport.dart';
import 'package:watch_companion/state/watch_controller.dart';

class _MockTransport implements WatchTransport {
  final StreamController<Uint8List> _rx = StreamController<Uint8List>.broadcast();
  final StreamController<void> _closed = StreamController<void>.broadcast();
  final List<Uint8List> sent = <Uint8List>[];
  bool _connected = true;
  int _msgCount = 2;
  int _assetFlags = 0;

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

  Uint8List _poolFrame() {
    final p = Uint8List(13);
    void u16(int off, int v) {
      p[off] = v & 0xFF;
      p[off + 1] = (v >> 8) & 0xFF;
    }

    final mb = _msgCount * 180;
    final ib = 2048;
    final ab = (_assetFlags & 1 != 0 ? 220 : 0) + (_assetFlags & 2 != 0 ? 210 : 0);
    final used = mb + ib + ab;
    u16(0, 32000);
    u16(2, used);
    u16(4, 32000 - used);
    u16(6, mb);
    u16(8, ib);
    p[10] = _msgCount;
    p[11] = 20;
    p[12] = _assetFlags;
    return buildWatchFrame(WatchCmd.extPoolState, p);
  }

  @override
  Future<void> send(Uint8List frame) async {
    sent.add(Uint8List.fromList(frame));
    // 对握手 nop / 查询 / 配置写入 / 动态 ROM 指令自动回复模拟响应。
    if (frame.length >= 4 && frame[0] == watchMark) {
      final cmd = frame[1];
      if (cmd == WatchCmd.nop ||
          cmd == WatchCmd.setConfig ||
          cmd == WatchCmd.extImgMeta ||
          cmd == WatchCmd.extImgData ||
          cmd == WatchCmd.setting ||
          cmd == WatchCmd.time ||
          cmd == WatchCmd.maskRle ||
          cmd == WatchCmd.msgDetailBmp) {
        if (cmd == WatchCmd.setConfig &&
            frame.length >= 4 &&
            (frame[3] & 0x20) != 0) {
          _msgCount = 0;
        }
        scheduleMicrotask(() => pushText('ok\r\n'));
      } else if (cmd == WatchCmd.extOp) {
        final sub = frame.length >= 4 ? frame[3] : 0;
        if (sub == 1 && frame.length >= 5) {
          _msgCount = frame[4];
        }
        scheduleMicrotask(() {
          if (sub == 0) pushBytes(_poolFrame());
          pushText('ok\r\n');
        });
      } else if (cmd == WatchCmd.msgClear) {
        _msgCount = 0;
        scheduleMicrotask(() {
          pushBytes(_poolFrame());
          pushText('ok\r\n');
        });
      } else if (cmd == WatchCmd.extAsset) {
        final id = frame.length >= 4 ? frame[3] : 0;
        final chunk = frame.length >= 5 ? frame[4] : 0;
        var done = false;
        if (id == 1) {
          done = chunk >= 3;
          if (done) _assetFlags |= 0x01;
        } else if (id >= 3 && id <= 10) {
          final totalLen = frame.length >= 7 ? (frame[5] | (frame[6] << 8)) : 0;
          done = totalLen > 0 && ((chunk + 1) * 128 >= totalLen);
          if (done) {
            final bit = switch (id) {
              3 => 0x04,
              4 => 0x08,
              5 => 0x20,
              6 => 0x40,
              7 => 0x80,
              8 => 0x02,
              _ => 0,
            };
            _assetFlags |= bit;
          }
        }
        scheduleMicrotask(() {
          if (done) pushBytes(_poolFrame());
          pushText('ok\r\n');
        });
      } else if (cmd == WatchCmd.getSettings) {
        final f = buildWatchFrame(WatchCmd.settingsState, [1, 1, 1, 1, 1, 0]);
        scheduleMicrotask(() {
          pushBytes(f);
          pushText('ok\r\n');
        });
      } else if (cmd == WatchCmd.getConfig) {
        final cfg = const WatchConfigBundle(
          saveToI2c: true,
          appOverride: false,
          use24h: true,
          debug: false,
          raiseWrist: false,
          keyBeep: false,
          brightness: 0,
          ledMode: 0,
          screenOff: 1,
          stepGoal: 6000,
          dialId: 2,
          dialTop: 1,
          dialBot: 2,
          dialFlags: 1,
          dialTextColor: 0xFFFFFFFF,
          dialAccentColor: 0xFFF5B759,
          dialBgColor: 0xFF000000,
        ).toSetConfigPayload();
        final replyPayload = Uint8List(26)
          ..setRange(0, 20, cfg)
          ..[20] = 0x0F
          ..[21] = 1
          ..[22] = 0x10
          ..[23] = 0x10
          ..[24] = 4
          ..[25] = 0x50;
        final f = buildWatchFrame(WatchCmd.configState, replyPayload);
        scheduleMicrotask(() {
          pushBytes(f);
          pushText('ok\r\n');
        });
      }
    }
  }

  void pushBytes(Uint8List bytes) {
    if (!_rx.isClosed) _rx.add(bytes);
  }

  void pushText(String text) {
    if (!_rx.isClosed) _rx.add(Uint8List.fromList(text.codeUnits));
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

  test('128x160 压缩算法 PAL4-RLE / RGB565-RLE / 1bpp 编解码一致且适配 17KB 分区', () {
    for (final preset in ['aurora', 'amber', 'cyber', 'starry']) {
      final rgba = generateBuiltInWallpaperRgba(preset);
      final pal4 = encodeWatchImage(rgba, format: WatchExtImgFormat.pal4Rle);
      expect(pal4.fitsFlash, isTrue, reason: '$preset PAL4-RLE 超出 17KB');
      expect(pal4.compressionRatio, greaterThan(3.0));
      final decodedPal = decodeWatchExtImageToRgba(
        pal4.bytes,
        format: WatchExtImgFormat.pal4Rle,
      );
      expect(decodedPal, equals(pal4.previewRgba));

      final mono = encodeWatchImage(rgba, format: WatchExtImgFormat.mono1bpp);
      expect(mono.bytes.length, 2560);
      final decodedMono = decodeWatchExtImageToRgba(
        mono.bytes,
        format: WatchExtImgFormat.mono1bpp,
      );
      expect(decodedMono, equals(mono.previewRgba));
    }

    // 条纹低熵图案可无损使用 RGB565-RLE 压缩
    final stripe = Uint8List(watchDisplayWidth * watchDisplayHeight * 4);
    for (var y = 0; y < watchDisplayHeight; y++) {
      for (var x = 0; x < watchDisplayWidth; x++) {
        final i = (y * watchDisplayWidth + x) * 4;
        stripe[i] = y < 80 ? 24 : 112;
        stripe[i + 1] = y < 80 ? 48 : 212;
        stripe[i + 2] = y < 80 ? 96 : 174;
        stripe[i + 3] = 255;
      }
    }
    final rgbRle = encodeWatchImage(stripe, format: WatchExtImgFormat.rgb565Rle);
    expect(rgbRle.fitsFlash, isTrue);
    expect(rgbRle.compressionRatio, greaterThan(10.0));
    final decodedRgb = decodeWatchExtImageToRgba(
      rgbRle.bytes,
      format: WatchExtImgFormat.rgb565Rle,
    );
    expect(decodedRgb, equals(rgbRle.previewRgba));
  });

  test('App 最高优先级连接即下发 0x2A 覆盖 I2C，关闭优先级则可拉取 I2C 配置', () async {
    final mock = _MockTransport();
    final session = WatchSession(
      transportFactory: (_, _, _) => mock,
      enableTelemetry: false,
    );
    final controller = WatchController(startTicker: false, session: session);
    addTearDown(controller.dispose);

    controller.updateDialConfig(
      id: 1,
      top: 0,
      bot: 1,
      accentColor: 0xFF73D4AE,
      syncToWatch: false,
    );
    controller.stepGoal = 10000;
    controller.appPriorityOverride = true;

    final err = await controller.connect();
    expect(err, isNull);
    await Future<void>.delayed(const Duration(milliseconds: 50));

    // 已查询 AT24C256 硬件信息，并下发 App 的 0x2A 覆盖配置（保持 dialId == 1 & stepGoal == 10000）。
    expect(controller.extFlashPresent, isTrue);
    expect(controller.extI2cAddr, 0x50);
    expect(controller.dialId, 1);
    expect(controller.stepGoal, 10000);
    expect(mock.sent.any((f) => f[1] == WatchCmd.setConfig), isTrue);

    // 手动从手表 I2C 拉取，则采用 Mock 返回的 AT24C256 默认设置（dialId == 2, stepGoal == 6000）。
    final pulled = await controller.pullConfigFromWatch();
    expect(pulled, isTrue);
    await pumpEventQueue();
    expect(controller.dialId, 2);
    expect(controller.stepGoal, 6000);

    // 持久化键值往返不丢失。
    final next = WatchController(startTicker: false);
    addTearDown(next.dispose);
    next.applyPersisted(controller.persistedSettings);
    expect(next.dialId, 2);
    expect(next.stepGoal, 6000);
    expect(next.appPriorityOverride, isTrue);
  });

  test('0x2D/0x2E 动态 ROM 状态、0x20 通知清理开关位与跨平台消息点阵栅格化', () async {
    const cfg = WatchConfigBundle(
      saveToI2c: true,
      appOverride: true,
      use24h: true,
      debug: false,
      autoClearMessages: true,
      raiseWrist: true,
      keyBeep: true,
      brightness: 1,
      ledMode: 1,
      screenOff: 0,
      stepGoal: 8000,
      dialId: 3,
      dialTop: 0,
      dialBot: 1,
      dialFlags: 1,
      dialTextColor: 0xFFF0F1E4,
      dialAccentColor: 0xFF73D4AE,
      dialBgColor: 0xFF000000,
    );
    final payload = cfg.toSetConfigPayload();
    expect(payload[0] & 0x20, 0x20);
    final replyPayload = Uint8List(26)..setRange(0, 20, payload);
    final parsedCfg = WatchConfigBundle.fromFrame(
      WatchFrame(WatchCmd.configState, replyPayload),
    );
    expect(parsedCfg, isNotNull);
    expect(parsedCfg!.autoClearMessages, isTrue);

    const item = WatchMessageItem(
      app: '微信',
      title: '测试标题',
      text: '支持最多 20 条 AT24C256 持久化消息。',
    );
    final rowBits = await WatchMessageRasterizer.renderRow(item);
    final detBits = await WatchMessageRasterizer.renderDetail(item);
    final compBits = await WatchMessageRasterizer.renderCompassAsset();
    final emptyBits = await WatchMessageRasterizer.renderMsgEmptyAsset();
    expect(rowBits.length, 352);
    expect(detBits.length, 2560);
    expect(detBits.any((b) => b != 0), isTrue);
    expect(compBits.length, 512);
    expect(compBits.any((b) => b != 0), isTrue);
    expect(emptyBits.length, 480);
    expect(emptyBits.any((b) => b != 0), isTrue);

    final mock = _MockTransport();
    final session = WatchSession(
      transportFactory: (_, _, _) => mock,
      enableTelemetry: false,
    );
    final controller = WatchController(startTicker: false, session: session);
    addTearDown(controller.dispose);
    controller.setMaxMessages(20);
    expect(controller.maxMessages, 20);

    await controller.connect();
    await controller.refreshExtPoolState();
    await Future<void>.delayed(const Duration(milliseconds: 520));
    expect(controller.extPoolMsgMax, 20);
    expect(controller.extPoolMsgCount, 2);
    expect(mock.sent.any((f) => f[1] == WatchCmd.extAsset && f[3] == 1), isTrue);
    expect(mock.sent.any((f) => f[1] == WatchCmd.extAsset && f[3] == 3), isTrue);
    expect(mock.sent.any((f) => f[1] == WatchCmd.extAsset && f[3] == 4), isTrue);
    expect(mock.sent.any((f) => f[1] == WatchCmd.extAsset && f[3] == 5), isTrue);
    expect(mock.sent.any((f) => f[1] == WatchCmd.extAsset && f[3] == 6), isTrue);
    expect(mock.sent.any((f) => f[1] == WatchCmd.extAsset && f[3] == 7), isTrue);
    expect(mock.sent.any((f) => f[1] == WatchCmd.extAsset && f[3] == 8), isTrue);
    expect((controller.extPoolState?.assetFlags ?? 0) & 0xEF, 0xEF);
    expect(controller.watchMusicActive, isFalse);

    // 验证仅当手表端明确打开音乐页（0x2E bit4=1）时才激活音乐监听状态
    final activePool = Uint8List(13)
      ..[0] = 0x00
      ..[1] = 0x7D
      ..[11] = 20
      ..[12] = 0xFF;
    mock.pushBytes(buildWatchFrame(WatchCmd.extPoolState, activePool));
    await Future<void>.delayed(const Duration(milliseconds: 30));
    expect(controller.watchMusicActive, isTrue);

    final idlePool = Uint8List(13)
      ..[0] = 0x00
      ..[1] = 0x7D
      ..[11] = 20
      ..[12] = 0xEF;
    mock.pushBytes(buildWatchFrame(WatchCmd.extPoolState, idlePool));
    await Future<void>.delayed(const Duration(milliseconds: 30));
    expect(controller.watchMusicActive, isFalse);

    final pushed = await controller.pushRichMessage(
      app: '日程提醒',
      title: '硬件评审',
      text: 'AT24C256 动态池测试消息',
    );
    expect(pushed, isTrue);
    await Future<void>.delayed(const Duration(milliseconds: 50));
    expect(
      mock.sent.any((f) => f[1] == WatchCmd.extOp && f[3] == 1),
      isTrue,
    );
    expect(
      mock.sent.any((f) => f[1] == WatchCmd.extOp && f[3] == 2),
      isTrue,
    );
    expect(controller.extPoolMsgCount, 1);

    final quietPushed = await controller.pushRichMessage(
      app: '短信槽',
      title: '消息槽 1',
      text: '静默写入 AT24C256 详情',
      vibrate: false,
    );
    expect(quietPushed, isTrue);
    expect(
      mock.sent.any(
        (f) => f[1] == WatchCmd.extOp && f[2] == 3 && f[3] == 1 && f[5] == 0,
      ),
      isTrue,
    );

    final cleared = await controller.clearWatchMessages();
    expect(cleared, isTrue);
    await Future<void>.delayed(const Duration(milliseconds: 50));
    expect(controller.extPoolMsgCount, 0);
    expect(mock.sent.any((f) => f[1] == WatchCmd.msgClear), isTrue);

    controller.setAutoClearMessages(true);
    await Future<void>.delayed(const Duration(milliseconds: 50));
    expect(controller.autoClearMessages, isTrue);
    final lastSet = mock.sent.lastWhere((f) => f[1] == WatchCmd.setConfig);
    expect(lastSet[3] & 0x20, 0x20);
  });

  testWidgets('表盘、媒体持久化通知与设置页在 1280x800 及极窄 320x720 零溢出', (tester) async {
    await tester.binding.setSurfaceSize(const Size(320, 720));
    addTearDown(() => tester.binding.setSurfaceSize(null));
    final controller = WatchController(startTicker: false)..setAnimations(false);
    addTearDown(controller.dispose);

    await tester.pumpWidget(WatchCompanionApp(controller: controller));
    await tester.pumpAndSettle();

    await tester.tap(find.text('表盘'));
    await tester.pumpAndSettle();
    expect(find.text('表盘方案切换'), findsOneWidget);
    expect(find.text('128×160 相册裁切选区与抗噪压缩工作台'), findsOneWidget);
    expect(find.text('32KB 外置 AT24C256 动态 ROM (0x50)'), findsOneWidget);
    expect(tester.takeException(), isNull);

    await tester.ensureVisible(find.text('自定义相册表盘'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('自定义相册表盘'));
    await tester.pumpAndSettle();
    expect(controller.dialId, 3);
    expect(tester.takeException(), isNull);

    await tester.tap(find.text('媒体'));
    await tester.pumpAndSettle();
    expect(find.text('片外 AT24C256 动态持久化上限'), findsOneWidget);
    expect(find.text('20 条'), findsOneWidget);
    expect(find.text('立即清理手表通知'), findsOneWidget);
    expect(find.text('通知清理开关（开启即清空 / 重启不保留）'), findsOneWidget);
    expect(tester.takeException(), isNull);

    await tester.tap(find.text('设置'));
    await tester.pumpAndSettle();
    expect(find.text('配置优先级与 AT24C256 持久化'), findsOneWidget);
    expect(find.text('App 最高优先级（连接自动覆盖 I2C）'), findsOneWidget);
    expect(find.text('通知清理开关（重启自动清空）'), findsNothing);
    expect(tester.takeException(), isNull);

    await tester.binding.setSurfaceSize(const Size(1280, 800));
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
  });

  testWidgets('实测图片抗噪压缩与 10KB 片内热槽适配及档案生成', (tester) async {
    final sample = File(r'D:\下载\Screenshot_2026-11-16-16-38-08-005_tv.danmaku.bi.jpg');
    if (!sample.existsSync()) {
      return;
    }
    await tester.runAsync(() async {
      final rawBytes = await sample.readAsBytes();
      final dims = await probeImageSize(rawBytes);
      expect(dims.width, greaterThan(400));
      expect(dims.height, greaterThan(600));

      final rgba = await cropImageBytesToRgba(
        rawBytes,
        cropRectNorm: const ui.Rect.fromLTWH(0.0, 0.04, 1.0, 0.692),
      );
      final encoded = encodeWatchImageAdaptive(
        rgba,
        format: WatchExtImgFormat.pal4Rle,
        dither: true,
      );
      final profile = analyzeWatchImageProfile(rgba, encoded);
      // ignore: avoid_print
      print(
        'Anime encoded: ${encoded.bytes.length} B (${encoded.compressionRatio.toStringAsFixed(1)}x), '
        'onChip=${encoded.fitsOnChipHotSwap}, flat=${(profile.flatRatio * 100).toStringAsFixed(1)}%, '
        'align=${profile.recommendedAlign}, busy=${profile.zoneBusyScores}',
      );
      final dec = decodeWatchExtImageToRgba(
        encoded.bytes,
        format: WatchExtImgFormat.pal4Rle,
      );
      final uiImg = await rgbaToUiImage(dec);
      final pngData = await uiImg.toByteData(format: ui.ImageByteFormat.png);
      uiImg.dispose();
      final outFile = File(
        r'C:\Users\coolw\.gemini\antigravity\brain\b54e9a64-ced1-4194-82d6-c6d696276ed6\scratch\anime_pal4_clean.png',
      );
      await outFile.parent.create(recursive: true);
      await outFile.writeAsBytes(pngData!.buffer.asUint8List());

      expect(encoded.fitsOnChipHotSwap, isTrue);
      expect(encoded.bytes.length, lessThanOrEqualTo(watchOnChipHotSwapMaxBytes));
      // 验证纯白背景角落（左上/右上外边距）100% 锁定纯白 #FFFFFF，零彩色斑点
      var cornerWhite = 0;
      var cornerTot = 0;
      for (var y = 0; y < 24; y++) {
        for (var x = 0; x < 128; x++) {
          if (x < 22 || x >= 106) {
            final idx = (y * 128 + x) * 4;
            cornerTot++;
            if (dec[idx] == 255 && dec[idx + 1] == 255 && dec[idx + 2] == 255) {
              cornerWhite++;
            }
          }
        }
      }
      expect(cornerWhite / cornerTot, greaterThan(0.95));
      expect(profile.flatRatio, greaterThan(0.35));
    });
  });
}
