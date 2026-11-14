import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:material_ui/material_ui.dart';

import 'core/crash_log.dart';
import 'state/watch_controller.dart';
import 'theme/app_theme.dart';
import 'widgets/app_shell.dart';

void main() {
  runZonedGuarded(() {
    WidgetsFlutterBinding.ensureInitialized();
    installCrashLogging();
    runApp(const WatchCompanionApp());
    if (const bool.fromEnvironment('CRASH_TEST')) {
      Future<void>.delayed(const Duration(seconds: 2), () {
        throw StateError('crash-test: 验证 crash.log 写入');
      });
    }
  }, (error, stack) => CrashLog.instance.record('Zone', error, stack));
}

class WatchCompanionApp extends StatefulWidget {
  const WatchCompanionApp({super.key, this.controller});

  final WatchController? controller;

  @override
  State<WatchCompanionApp> createState() => _WatchCompanionAppState();
}

class _WatchCompanionAppState extends State<WatchCompanionApp> {
  late final WatchController _controller;
  late final bool _ownsController;
  late ThemeMode _themeMode;

  @override
  void initState() {
    super.initState();
    _ownsController = widget.controller == null;
    _controller = widget.controller ?? WatchController();
    _themeMode = _controller.themeMode;
    // 只关心主题变化：1 Hz 的演示数据刷新交给页面内部的局部监听，
    // 避免整棵树（含液态玻璃底栏）每秒重建一次。
    _controller.addListener(_syncThemeMode);
  }

  @override
  void dispose() {
    _controller.removeListener(_syncThemeMode);
    if (_ownsController) _controller.dispose();
    super.dispose();
  }

  void _syncThemeMode() {
    if (_controller.themeMode == _themeMode) return;
    setState(() => _themeMode = _controller.themeMode);
  }

  @override
  Widget build(BuildContext context) {
    final platformBrightness =
        WidgetsBinding.instance.platformDispatcher.platformBrightness;
    final isDark = switch (_themeMode) {
      ThemeMode.dark => true,
      ThemeMode.light => false,
      ThemeMode.system => platformBrightness == Brightness.dark,
    };
    return MaterialApp(
      title: 'AT32 手表助手',
      debugShowCheckedModeBanner: false,
      theme: AppTheme.light(),
      darkTheme: AppTheme.dark(),
      themeMode: _themeMode,
      home: _WindowThemeBridge(
        dark: isDark,
        child: AppShell(controller: _controller),
      ),
    );
  }
}

class _WindowThemeBridge extends StatefulWidget {
  const _WindowThemeBridge({required this.dark, required this.child});

  final bool dark;
  final Widget child;

  @override
  State<_WindowThemeBridge> createState() => _WindowThemeBridgeState();
}

class _WindowThemeBridgeState extends State<_WindowThemeBridge> {
  static const _channel = MethodChannel('watch_companion/window_theme');

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _syncWindowTheme());
  }

  @override
  void didUpdateWidget(covariant _WindowThemeBridge oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.dark != widget.dark) _syncWindowTheme();
  }

  void _syncWindowTheme() {
    if (kIsWeb || defaultTargetPlatform != TargetPlatform.windows) return;
    _channel
        .invokeMethod<void>('setBrightness', {'dark': widget.dark})
        .catchError((Object _) {});
  }

  @override
  Widget build(BuildContext context) => widget.child;
}
