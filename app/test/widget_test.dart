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

    expect(controller.themeMode, ThemeMode.light);
    await tester.tap(find.byTooltip('切换到墨黑主题'));
    await tester.pumpAndSettle();
    expect(controller.themeMode, ThemeMode.dark);
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

    await tester.tap(find.text('设置'));
    await tester.pumpAndSettle();
    expect(find.text('SPI 超频'), findsOneWidget);
    expect(find.text('约 100KB 系统'), findsOneWidget);
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

  testWidgets('light/dark theme fidelity and portrait/landscape ergonomics', (
    tester,
  ) async {
    final controller = WatchController(startTicker: false)
      ..setAnimations(false);
    addTearDown(controller.dispose);
    addTearDown(() => tester.binding.setSurfaceSize(null));

    // 1. 竖屏浅色模式：验证竖屏主页 Hero 卡与媒体页专辑封套使用浅色梯度（无暗色泄露）
    await tester.binding.setSurfaceSize(const Size(390, 840));
    await tester.pumpWidget(WatchCompanionApp(controller: controller));
    await tester.pumpAndSettle();

    final lightColors = AppThemeColors.fromBrightness(Brightness.light);
    final portraitHeroInk = tester.widget<Ink>(
      find.ancestor(
        of: find.text('实时姿态'),
        matching: find.byType(Ink),
      ).first,
    );
    final heroDec = portraitHeroInk.decoration! as BoxDecoration;
    expect(
      (heroDec.gradient! as LinearGradient).colors,
      equals(lightColors.heroGradient),
    );

    await tester.tap(find.text('媒体'));
    await tester.pumpAndSettle();
    final mediaContext = tester.element(find.text('同步到手表'));
    expect(Theme.of(mediaContext).brightness, Brightness.light);
    expect(tester.takeException(), isNull);

    // 切到暗色模式，验证竖屏主页与媒体页同步切换
    controller.setThemeMode(ThemeMode.dark);
    await tester.pumpAndSettle();
    final darkColors = AppThemeColors.fromBrightness(Brightness.dark);
    final mediaDarkCtx = tester.element(find.text('同步到手表'));
    expect(Theme.of(mediaDarkCtx).brightness, Brightness.dark);

    await tester.tap(find.text('主页'));
    await tester.pumpAndSettle();
    final darkHeroInk = tester.widget<Ink>(
      find.ancestor(
        of: find.text('实时姿态'),
        matching: find.byType(Ink),
      ).first,
    );
    final darkHeroDec = darkHeroInk.decoration! as BoxDecoration;
    expect(
      (darkHeroDec.gradient! as LinearGradient).colors,
      equals(darkColors.heroGradient),
    );
    expect(tester.takeException(), isNull);

    // 2. 手机横屏 720x360：使用左侧精简导航栏且五页零垂直/水平溢出
    await tester.binding.setSurfaceSize(const Size(720, 360));
    await tester.pumpAndSettle();
    expect(find.byType(NavigationBar), findsNothing);
    expect(find.byIcon(Icons.menu_rounded), findsOneWidget);
    for (final tip in const ['主页', '运动', '媒体', '表盘', '设置']) {
      await tester.tap(find.byTooltip(tip).first);
      await tester.pumpAndSettle();
      expect(tester.takeException(), isNull);
    }

    // 3. 桌面横屏 1280x800（恢复暖白主题）：验证运动页顶部双卡等高齐平，且五页无溢出
    controller.setThemeMode(ThemeMode.light);
    await tester.binding.setSurfaceSize(const Size(1280, 800));
    await tester.pumpAndSettle();
    await tester.tap(find.byTooltip('运动').first);
    await tester.pumpAndSettle();
    final todayCard = find.ancestor(
      of: find.text('今日步数'),
      matching: find.byType(Ink),
    ).first;
    final distCard = find.ancestor(
      of: find.text('今日活动分布'),
      matching: find.byType(Ink),
    ).first;
    expect(
      tester.getBottomRight(todayCard).dy,
      closeTo(tester.getBottomRight(distCard).dy, 0.5),
    );
    for (final tip in const ['主页', '媒体', '表盘', '设置']) {
      await tester.tap(find.byTooltip(tip).first);
      await tester.pumpAndSettle();
      expect(tester.takeException(), isNull);
    }
  });
}
