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
    expect(find.text('手表设置'), findsOneWidget);

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

    await tester.tap(find.text('媒体'));
    await tester.pumpAndSettle();
    expect(find.text('同步到手表'), findsOneWidget);
    expect(find.text('歌词演示 · 晚风手记'), findsOneWidget);
    expect(find.text('手动发送测试消息'), findsOneWidget);

    await tester.tap(find.text('表盘'));
    await tester.pumpAndSettle();
    expect(find.text('表盘方案切换'), findsOneWidget);
    expect(find.text('32KB 外置 AT24C256 动态 ROM (0x50)'), findsOneWidget);

    await tester.ensureVisible(find.text('墨竹极简指针'));
    await tester.tap(find.text('墨竹极简指针'));
    await tester.pumpAndSettle();
    await tester.ensureVisible(find.text('保存本地（连表覆盖）'));
    await tester.tap(find.text('保存本地（连表覆盖）'));
    await tester.pumpAndSettle();
    expect(find.textContaining('AT24C256'), findsWidgets);
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
