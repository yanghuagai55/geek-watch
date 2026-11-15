import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:watch_companion/services/lrc_library.dart';
import 'package:watch_companion/services/text_bitmap.dart';
import 'package:watch_companion/services/text_paginator.dart';
import 'package:watch_companion/state/watch_controller.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('长标题分页，每页不超过两行且内容不丢', (tester) async {
    const text = '这是一个非常长的哔哩哔哩视频标题，用来验证分页轮播是否能把完整标题逐页送到手表上。';
    final pages = paginateWatchText(text);
    expect(pages.length, greaterThan(1));
    for (final page in pages) {
      expect(page.length, inInclusiveRange(1, 2));
      expect(page.every((line) => line.trim().isNotEmpty), isTrue);
    }
    final joined = pages.expand((page) => page).join().replaceAll(' ', '');
    expect(joined, text.replaceAll(' ', ''));
  });

  testWidgets('短标题只有一页', (tester) async {
    expect(paginateWatchText('晚风手记').length, 1);
    expect(paginateWatchText('   '), isEmpty);
  });

  testWidgets('点阵渲染返回 512 字节且有内容', (tester) async {
    final bits = await tester.runAsync(
      () => renderWatchBitmap([const WatchBitmapLine('测试标题', fontSize: 16)]),
    );
    expect(bits, isNotNull);
    expect(bits!.length, 512);
    expect(bits.any((byte) => byte != 0), isTrue);
  });

  testWidgets('PC 播放按钮会调用 Windows 媒体控制通道', (tester) async {
    if (!Platform.isWindows) return;
    const channel = MethodChannel('watch_companion/media');
    final actions = <String>[];
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          if (call.method == 'control') {
            final args = call.arguments;
            if (args is Map) actions.add('${args['action']}');
          }
          return null;
        });
    addTearDown(
      () => TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, null),
    );

    final controller = WatchController(startTicker: false);
    addTearDown(controller.dispose);
    await controller.setPlayState(true);
    await controller.setPlayState(false);
    await tester.pump();

    expect(actions, containsAllInOrder(<String>['play', 'pause']));
  });

  test('LRC 解析时间戳', () {
    final lines = LrcLibrary.parse(
      '[00:01.00]第一句\n[00:05.50]第二句\n[00:09.2]第三句',
    );
    expect(lines.length, 3);
    expect(lines[0].timeMs, 1000);
    expect(lines[1].timeMs, 5500);
    expect(lines[2].timeMs, 9200);
  });

  test('LRC cue 带下一行时间窗口', () async {
    final folder = await Directory.systemTemp.createTemp('at32_lrc_test');
    addTearDown(() => folder.delete(recursive: true));
    await File('${folder.path}${Platform.pathSeparator}晚风手记.lrc')
        .writeAsString('[00:01.00]第一句\n[00:05.50]第二句\n[00:09.20]第三句');
    final library = LrcLibrary(directory: folder.path);

    final cue = await library.cueAt('晚风手记', 6000);
    expect(cue, isNotNull);
    expect(cue!.text, '第二句');
    expect(cue.startMs, 5500);
    expect(cue.endMs, 9200);
    expect(cue.durationMs, 3700);

    expect(await library.cueAt('不存在的标题', 6000), isNull);
  });
}
