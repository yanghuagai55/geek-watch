import 'package:flutter/services.dart';
import 'package:material_ui/material_ui.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:watch_companion/main.dart';
import 'package:watch_companion/state/watch_controller.dart';
import 'package:watch_companion/theme/app_theme.dart';
import 'package:watch_companion/widgets/metric_card.dart';

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

  testWidgets('desktop drawer, connection preview and light theme', (
    tester,
  ) async {
    await tester.binding.setSurfaceSize(const Size(1280, 800));
    addTearDown(() => tester.binding.setSurfaceSize(null));
    final controller = WatchController(startTicker: false);
    addTearDown(controller.dispose);

    await tester.pumpWidget(WatchCompanionApp(controller: controller));
    await tester.pumpAndSettle();

    expect(find.text('手表工作台'), findsOneWidget);
    expect(find.text('AT32 Watch'), findsOneWidget);
    expect(find.text('实时性能'), findsOneWidget);
    expect(find.byIcon(Icons.menu_rounded), findsOneWidget);
    expect(find.text('SPP'), findsOneWidget);
    expect(find.byType(AnimatedIcon), findsOneWidget);

    await tester.tap(find.byIcon(Icons.menu_rounded));
    await tester.pump(const Duration(milliseconds: 120));
    expect(find.byType(BackdropFilter), findsWidgets);
    await tester.pumpAndSettle();
    expect(find.text('连接'), findsOneWidget);
    expect(find.text('断开'), findsOneWidget);
    expect(find.byType(BackdropFilter), findsWidgets);

    await tester.tap(find.text('连接'));
    await tester.pump(const Duration(milliseconds: 50));
    expect(controller.previewConnected, isTrue);
    expect(find.text('SPP · 演示连接'), findsOneWidget);

    await tester.tap(find.text('设置'));
    await tester.pumpAndSettle();
    expect(find.text('连接偏好'), findsOneWidget);
    expect(find.text('外观与动效'), findsOneWidget);
    expect(find.text('手表显示'), findsOneWidget);

    await tester.tap(find.byTooltip('切换到暖白主题'));
    await tester.pumpAndSettle();
    expect(controller.themeMode, ThemeMode.light);
    final context = tester.element(find.text('连接偏好'));
    expect(Theme.of(context).brightness, Brightness.light);
    expect(Theme.of(context).colorScheme.primary, AppPalette.mintDeep);
    expect(tester.takeException(), isNull);
  });

  testWidgets('collapsed icon navigation and mobile pages', (tester) async {
    await tester.binding.setSurfaceSize(const Size(1280, 800));
    addTearDown(() => tester.binding.setSurfaceSize(null));
    final controller = WatchController(startTicker: false);
    addTearDown(controller.dispose);

    await tester.pumpWidget(WatchCompanionApp(controller: controller));
    await tester.pumpAndSettle();
    await tester.tap(find.byIcon(Icons.settings_outlined).first);
    await tester.pumpAndSettle();
    expect(find.text('连接偏好'), findsOneWidget);

    await tester.binding.setSurfaceSize(const Size(400, 840));
    await tester.pumpAndSettle();
    expect(find.byKey(const Key('mobile-liquid-nav')), findsOneWidget);
    expect(find.byType(NavigationBar), findsNothing);
    expect(find.text('手表工作台'), findsNothing);

    await tester.tap(find.text('音乐'));
    await tester.pumpAndSettle();
    expect(find.text('播放与歌词'), findsOneWidget);
    expect(find.text('晚风手记'), findsOneWidget);

    await tester.tap(find.text('消息'));
    await tester.pumpAndSettle();
    expect(find.text('设备消息'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('portrait home shows screen and metrics at once', (tester) async {
    await tester.binding.setSurfaceSize(const Size(400, 840));
    addTearDown(() => tester.binding.setSurfaceSize(null));
    final controller = WatchController(startTicker: false);
    addTearDown(controller.dispose);

    await tester.pumpWidget(WatchCompanionApp(controller: controller));
    await tester.pumpAndSettle();

    expect(find.text('AT32 Watch'), findsOneWidget);
    expect(find.text('AT32F421K8T7  ·  128 × 160'), findsOneWidget);
    expect(find.byType(MiniMetricCard), findsNWidgets(3));
    expect(find.text('主循环占用'), findsOneWidget);
    final metricsBottom = tester
        .getBottomRight(find.byType(MiniMetricCard).last)
        .dy;
    final navTop = tester
        .getTopLeft(find.byKey(const Key('mobile-liquid-nav')))
        .dy;
    expect(metricsBottom, lessThan(navTop));
    expect(tester.takeException(), isNull);
  });
}
