import 'dart:ui' show ImageFilter, lerpDouble;

import 'package:material_ui/material_ui.dart';
import 'package:prism_ui/icons/glyph.dart';
import 'package:prism_ui/prism_ui.dart' as prism;

import '../pages/dials_page.dart';
import '../pages/home_page.dart';
import '../pages/media_page.dart';
import '../pages/settings_page.dart';
import '../pages/sport_page.dart';
import '../state/watch_controller.dart';
import '../theme/app_theme.dart';
import 'common.dart';
import 'companion_glyphs.dart';

class AppShell extends StatefulWidget {
  const AppShell({super.key, required this.controller});

  final WatchController controller;

  @override
  State<AppShell> createState() => _AppShellState();
}

class _AppShellState extends State<AppShell>
    with SingleTickerProviderStateMixin {
  int _index = 0;
  late final AnimationController _drawerController = AnimationController(
    vsync: this,
    duration: const Duration(milliseconds: 260),
  );
  late final Animation<double> _drawerAnimation = CurvedAnimation(
    parent: _drawerController,
    curve: Curves.easeOutCubic,
    reverseCurve: Curves.easeInCubic,
  );

  /// 液态玻璃开关需要立刻生效，但又不能让 1 Hz 演示数据把它整棵子树刷一遍，
  /// 所以单独记一份值，只在真的变化时 setState。
  late bool _liquidGlass = widget.controller.liquidGlass;

  /// 缓存给 prism_ui 的 ThemeData：prism.ThemeData 未覆写 ==，
  /// 每次 build 新建会让 InheritedTheme 误触发整个液态玻璃底栏重建。
  ColorScheme? _cachedMaterialScheme;
  TargetPlatform? _cachedPlatform;
  prism.ThemeData? _cachedPrismTheme;

  static const _items = [
    _NavItem(
      Icons.home_outlined,
      Icons.home_rounded,
      '主页',
      CompanionGlyphs.dashboard,
    ),
    _NavItem(
      Icons.directions_run_outlined,
      Icons.directions_run_rounded,
      '运动',
      CompanionGlyphs.sport,
    ),
    _NavItem(
      Icons.perm_media_outlined,
      Icons.perm_media_rounded,
      '媒体',
      CompanionGlyphs.media,
    ),
    _NavItem(
      Icons.watch_outlined,
      Icons.watch_rounded,
      '表盘',
      CompanionGlyphs.watchFace,
    ),
    _NavItem(
      Icons.settings_outlined,
      Icons.settings_rounded,
      '设置',
      CompanionGlyphs.settings,
    ),
  ];

  static final List<prism.NavigationDockDestination> _dockDestinations =
      List.unmodifiable([
        for (final item in _items)
          prism.NavigationDockDestination(glyph: item.glyph, label: item.label),
      ]);

  prism.ThemeData _resolvePrismTheme(ThemeData materialTheme) {
    final materialScheme = materialTheme.colorScheme;
    final platform = materialTheme.platform;
    if (_cachedPrismTheme != null &&
        _cachedMaterialScheme == materialScheme &&
        _cachedPlatform == platform) {
      return _cachedPrismTheme!;
    }
    _cachedMaterialScheme = materialScheme;
    _cachedPlatform = platform;
    return _cachedPrismTheme = prism.ThemeData(
      colorScheme: prism.ColorScheme(
        brightness: materialScheme.brightness,
        primary: materialScheme.primary,
        onPrimary: materialScheme.onPrimary,
        secondary: materialScheme.secondary,
        onSecondary: materialScheme.onSecondary,
        error: materialScheme.error,
        onError: materialScheme.onError,
        surface: materialScheme.surface,
        onSurface: materialScheme.onSurface,
        onSurfaceVariant: materialScheme.onSurfaceVariant,
        surfaceContainerLow: materialScheme.surfaceContainerLow,
        surfaceContainer: materialScheme.surfaceContainer,
        surfaceContainerHigh: materialScheme.surfaceContainerHigh,
        surfaceContainerHighest: materialScheme.surfaceContainerHighest,
        outline: materialScheme.outline,
        outlineVariant: materialScheme.outlineVariant,
        shadow: materialScheme.shadow,
        scrim: materialScheme.scrim,
        inverseSurface: materialScheme.inverseSurface,
        onInverseSurface: materialScheme.onInverseSurface,
        inversePrimary: materialScheme.inversePrimary,
        surfaceTint: materialScheme.surfaceTint,
      ),
      platform: platform,
      fontFamily: AppFonts.han,
      fontFamilyFallback: AppFonts.systemFallback,
    );
  }

  @override
  void initState() {
    super.initState();
    widget.controller.addListener(_syncGlassSetting);
  }

  @override
  void dispose() {
    widget.controller.removeListener(_syncGlassSetting);
    _drawerController.dispose();
    super.dispose();
  }

  void _syncGlassSetting() {
    if (widget.controller.liquidGlass == _liquidGlass) return;
    setState(() => _liquidGlass = widget.controller.liquidGlass);
  }

  void _setExpanded(bool value) {
    if (!widget.controller.animations) {
      _drawerController.value = value ? 1 : 0;
      return;
    }
    if (value) {
      _drawerController.forward();
    } else {
      _drawerController.reverse();
    }
  }

  void _select(int index) {
    setState(() => _index = index);
    _setExpanded(false);
  }

  void _setConnection(BuildContext context, bool connected) {
    setWatchConnection(context, widget.controller, connect: connected);
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final content = _buildContent();
        final portrait =
            constraints.maxWidth < 760 &&
            constraints.maxHeight > constraints.maxWidth;
        if (portrait) {
          if (!_liquidGlass) {
            return Scaffold(
              body: SafeArea(child: content),
              bottomNavigationBar: NavigationBar(
                selectedIndex: _index,
                onDestinationSelected: _select,
                destinations: [
                  for (final item in _items)
                    NavigationDestination(
                      icon: Icon(item.icon),
                      selectedIcon: Icon(
                        item.selectedIcon,
                        color: Theme.of(context).colorScheme.primary,
                      ),
                      label: item.label,
                    ),
                ],
              ),
            );
          }
          final materialTheme = Theme.of(context);
          return prism.Theme(
            data: _resolvePrismTheme(materialTheme),
            child: prism.LiquidGlassScope(
              enabled: true,
              // Android 移动端使用 standard 档：单层 1/8 降采样模糊图集（较 ultra 减少约 64× 像素抓帧与 3× 片元采样），
              // 并去掉按压抬起时的第三级 Clip.antiAliasWithSaveLayer + 重复折射捕获，同时保持底栏文字更清晰。
              quality: prism.LiquidGlassQuality.standard,
              child: prism.LiquidGlassHost(
                enabled: true,
                child: Scaffold(
                  extendBody: true,
                  body: SafeArea(bottom: false, child: content),
                  bottomNavigationBar: prism.NavigationDock(
                    key: const Key('mobile-liquid-nav'),
                    destinations: _dockDestinations,
                    selectedIndex: _index,
                    onSelected: _select,
                  ),
                ),
              ),
            ),
          );
        }

        final scheme = Theme.of(context).colorScheme;
        final railWidth = constraints.maxWidth < 760 ? 64.0 : 76.0;
        return Scaffold(
          // 窗口栏颜色与侧边栏一致：整块外壳用 surfaceContainerLow，
          // 内容区再以圆角面板嵌进去，形成明暗自适应的「一层壳 + 一张卡」。
          backgroundColor: scheme.surfaceContainerLow,
          body: SafeArea(
            child: AnimatedBuilder(
              animation: _drawerAnimation,
              builder: (context, _) {
                final progress = _drawerAnimation.value;
                return Stack(
                  children: [
                    Row(
                      children: [
                        ListenableBuilder(
                          listenable: widget.controller,
                          builder: (context, _) => _CollapsedRail(
                            controller: widget.controller,
                            railWidth: railWidth,
                            progress: progress,
                            selected: _index,
                            items: _items,
                            onSelected: _select,
                            onExpand: () => _setExpanded(true),
                          ),
                        ),
                        Expanded(
                          child: Padding(
                            padding: const EdgeInsets.fromLTRB(0, 8, 8, 8),
                            child: Container(
                              decoration: BoxDecoration(
                                color: Theme.of(context).scaffoldBackgroundColor,
                                borderRadius: BorderRadius.circular(16),
                                border: Border.all(
                                  color: scheme.outlineVariant.withValues(
                                    alpha: 0.55,
                                  ),
                                ),
                              ),
                              clipBehavior: Clip.antiAlias,
                              child: content,
                            ),
                          ),
                        ),
                      ],
                    ),
                    Positioned.fill(
                      child: ListenableBuilder(
                        listenable: widget.controller,
                        builder: (context, _) => _ExpandedOverlay(
                          animation: _drawerAnimation,
                          collapsedWidth: railWidth,
                          controller: widget.controller,
                          selected: _index,
                          items: _items,
                          onClose: () => _setExpanded(false),
                          onSelected: _select,
                          onConnectionChanged: (value) =>
                              _setConnection(context, value),
                        ),
                      ),
                    ),
                  ],
                );
              },
            ),
          ),
        );
      },
    );
  }

  Widget _buildPage(int index) => switch (index) {
    0 => HomePage(controller: widget.controller, onNavigate: _select),
    1 => SportPage(controller: widget.controller),
    2 => MediaPage(controller: widget.controller),
    3 => DialsPage(controller: widget.controller),
    _ => SettingsPage(controller: widget.controller),
  };

  /// 只有页面内容订阅状态变化；底栏、侧边栏、液态玻璃不随业务数据重建。
  /// 页面在 builder 内按当前索引按需构造并用 RepaintBoundary 隔离过渡绘制。
  Widget _buildContent() {
    return ListenableBuilder(
      listenable: widget.controller,
      builder: (context, _) {
        return AnimatedSwitcher(
          duration: widget.controller.animations
              ? const Duration(milliseconds: 200)
              : Duration.zero,
          layoutBuilder: (currentChild, previousChildren) => Stack(
            fit: StackFit.expand,
            children: <Widget>[
              ...previousChildren,
              ?currentChild,
            ],
          ),
          transitionBuilder: (child, animation) => FadeTransition(
            opacity: animation,
            child: SlideTransition(
              position: Tween(
                begin: const Offset(0.012, 0),
                end: Offset.zero,
              ).animate(animation),
              child: child,
            ),
          ),
          child: KeyedSubtree(
            key: ValueKey(_index),
            child: RepaintBoundary(child: _buildPage(_index)),
          ),
        );
      },
    );
  }
}

class _CollapsedRail extends StatelessWidget {
  const _CollapsedRail({
    required this.controller,
    required this.railWidth,
    required this.progress,
    required this.selected,
    required this.items,
    required this.onSelected,
    required this.onExpand,
  });

  final WatchController controller;
  final double railWidth;
  final double progress;
  final int selected;
  final List<_NavItem> items;
  final ValueChanged<int> onSelected;
  final VoidCallback onExpand;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final isDark = scheme.brightness == Brightness.dark;
    final compactPad = railWidth < 70 ? 6.0 : 10.0;
    return Container(
      width: railWidth,
      color: scheme.surfaceContainerLow,
      child: Opacity(
        opacity: (1 - progress).clamp(0.0, 1.0).toDouble(),
        child: LayoutBuilder(
          builder: (context, constraints) {
            final short = constraints.maxHeight < 500;
            final children = <Widget>[
              IconButton(
                onPressed: onExpand,
                tooltip: '展开侧边栏',
                icon: const Icon(Icons.menu_rounded),
              ),
              SizedBox(height: short ? 6 : 10),
              const AppBrand(compact: true),
              SizedBox(height: short ? 14 : 26),
              for (var i = 0; i < items.length; i++) ...[
                _SideNavItem(
                  item: items[i],
                  extended: false,
                  progress: 0,
                  selected: selected == i,
                  onTap: () => onSelected(i),
                ),
                const SizedBox(height: 6),
              ],
              if (short) const SizedBox(height: 12) else const Spacer(),
              IconButton(
                onPressed: () => controller.setThemeMode(
                  isDark ? ThemeMode.light : ThemeMode.dark,
                ),
                tooltip: isDark ? '切换到暖白主题' : '切换到墨黑主题',
                icon: Icon(
                  isDark ? Icons.light_mode_outlined : Icons.dark_mode_outlined,
                ),
              ),
              const SizedBox(height: 6),
              _CompactConnectionBadge(
                controller: controller,
                onTap: () => toggleWatchConnection(context, controller),
              ),
              const SizedBox(height: 8),
            ];
            if (short) {
              return SingleChildScrollView(
                padding: EdgeInsets.symmetric(
                  horizontal: compactPad,
                  vertical: 12,
                ),
                child: Column(children: children),
              );
            }
            return Padding(
              padding: EdgeInsets.symmetric(
                horizontal: compactPad,
                vertical: 18,
              ),
              child: Column(children: children),
            );
          },
        ),
      ),
    );
  }
}

class _ExpandedOverlay extends StatelessWidget {
  const _ExpandedOverlay({
    required this.animation,
    required this.collapsedWidth,
    required this.controller,
    required this.selected,
    required this.items,
    required this.onClose,
    required this.onSelected,
    required this.onConnectionChanged,
  });

  final Animation<double> animation;
  final double collapsedWidth;
  final WatchController controller;
  final int selected;
  final List<_NavItem> items;
  final VoidCallback onClose;
  final ValueChanged<int> onSelected;
  final ValueChanged<bool> onConnectionChanged;

  @override
  Widget build(BuildContext context) {
    final progress = animation.value;
    final scheme = Theme.of(context).colorScheme;
    final isDark = scheme.brightness == Brightness.dark;
    final width = lerpDouble(collapsedWidth, 236, progress)!;
    final blur = 20 * progress;
    final scrimBlur = 2 * progress;
    double reveal(double start) {
      final value = ((progress - start) / (1 - start))
          .clamp(0.0, 1.0)
          .toDouble();
      return Curves.easeOut.transform(value);
    }

    final brandReveal = reveal(0.58);
    final panelReveal = reveal(0.48);
    final scrimChild = Container(
      color: const Color(
        0xFF0A0F10,
      ).withValues(alpha: (isDark ? 0.16 : 0.08) * progress),
    );
    final panelChild = OverflowBox(
      alignment: Alignment.centerLeft,
      minWidth: 236,
      maxWidth: 236,
      child: SizedBox(
        width: 236,
        child: Container(
          decoration: BoxDecoration(
            color: scheme.surfaceContainerLow.withValues(
              alpha: (isDark ? 0.92 : 0.9) * progress,
            ),
            border: Border(
              right: BorderSide(
                color: scheme.outlineVariant.withValues(alpha: 0.75 * progress),
              ),
            ),
            boxShadow: progress > 0.05
                ? [
                    BoxShadow(
                      color: Colors.black.withValues(
                        alpha: (isDark ? 0.28 : 0.12) * progress,
                      ),
                      blurRadius: 24,
                    ),
                  ]
                : null,
          ),
          padding: const EdgeInsets.fromLTRB(14, 18, 14, 18),
          child: Opacity(
            opacity: progress,
            child: Material(
              color: Colors.transparent,
              child: LayoutBuilder(
                builder: (context, constraints) {
                  final short = constraints.maxHeight < 520;
                  final column = Column(
                    children: [
                      SizedBox(
                        height: 44,
                        child: Row(
                          children: [
                            IconButton(
                              onPressed: onClose,
                              tooltip: '收起侧边栏',
                              icon: AnimatedIcon(
                                icon: AnimatedIcons.menu_close,
                                progress: animation,
                              ),
                            ),
                          ],
                        ),
                      ),
                      const SizedBox(height: 8),
                      Padding(
                        padding: const EdgeInsets.only(left: 3),
                        child: AppBrand(
                          compact: false,
                          textOpacity: brandReveal,
                        ),
                      ),
                      SizedBox(height: short ? 16 : 26),
                      for (var i = 0; i < items.length; i++) ...[
                        _SideNavItem(
                          item: items[i],
                          extended: true,
                          progress: progress,
                          selected: selected == i,
                          onTap: () => onSelected(i),
                        ),
                        const SizedBox(height: 6),
                      ],
                      if (short)
                        const SizedBox(height: 16)
                      else
                        const Spacer(),
                      Opacity(
                        opacity: panelReveal,
                        child: Transform.translate(
                          offset: Offset(0, 10 * (1 - panelReveal)),
                          child: _ConnectionPanel(
                            controller: controller,
                            onConnectionChanged: onConnectionChanged,
                          ),
                        ),
                      ),
                    ],
                  );
                  if (short) {
                    return SingleChildScrollView(child: column);
                  }
                  return column;
                },
              ),
            ),
          ),
        ),
      ),
    );
    final activeBlur = animation.status != AnimationStatus.dismissed || progress > 0;
    return IgnorePointer(
      ignoring: progress == 0,
      child: Stack(
        children: [
          Positioned.fill(
            child: GestureDetector(
              behavior: HitTestBehavior.opaque,
              onTap: onClose,
              child: activeBlur
                  ? BackdropFilter(
                      filter: ImageFilter.blur(
                        sigmaX: scrimBlur,
                        sigmaY: scrimBlur,
                      ),
                      child: scrimChild,
                    )
                  : scrimChild,
            ),
          ),
          Positioned(
            left: 0,
            top: 0,
            bottom: 0,
            width: width,
            child: ClipRect(
              child: activeBlur
                  ? BackdropFilter(
                      filter: ImageFilter.blur(sigmaX: blur, sigmaY: blur),
                      child: panelChild,
                    )
                  : panelChild,
            ),
          ),
        ],
      ),
    );
  }
}

class _CompactConnectionBadge extends StatelessWidget {
  const _CompactConnectionBadge({required this.controller, this.onTap});

  final WatchController controller;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final colors = context.appColors;
    final active = controller.connected;
    return Tooltip(
      message: '${controller.transportName} · ${controller.connectionLabel}',
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(14),
        child: Container(
          width: 48,
          height: 54,
          decoration: BoxDecoration(
            color: active
                ? colors.accentContainer.withValues(alpha: 0.72)
                : scheme.surfaceContainerHigh.withValues(alpha: 0.7),
            borderRadius: BorderRadius.circular(14),
            border: Border.all(
              color: active
                  ? colors.accent.withValues(alpha: 0.45)
                  : scheme.outlineVariant,
            ),
          ),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(
                controller.transport == TransportType.serial
                    ? Icons.cable_rounded
                    : Icons.bluetooth_rounded,
                size: 17,
                color: active ? colors.accent : scheme.onSurfaceVariant,
              ),
              const SizedBox(height: 3),
              Text(
                controller.transportLabel,
                style: TextStyle(
                  color: active ? colors.accent : scheme.onSurface,
                  fontSize: 9,
                  fontWeight: FontWeight.w800,
                  letterSpacing: 0.4,
                ),
              ),
              const SizedBox(height: 2),
              Container(
                width: 5,
                height: 5,
                decoration: BoxDecoration(
                  color: active ? colors.accent : scheme.outline,
                  shape: BoxShape.circle,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _ConnectionPanel extends StatelessWidget {
  const _ConnectionPanel({
    required this.controller,
    required this.onConnectionChanged,
  });

  final WatchController controller;
  final ValueChanged<bool> onConnectionChanged;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final colors = context.appColors;
    final active = controller.connected;
    return AppCard(
      radius: 16,
      padding: const EdgeInsets.all(14),
      color: scheme.surfaceContainerHigh.withValues(alpha: 0.72),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisSize: MainAxisSize.min,
        children: [
          StatusPill(
            label:
                '${controller.transportLabel} · ${controller.connectionLabel}',
            color: active ? colors.accent : scheme.onSurfaceVariant,
          ),
          const SizedBox(height: 9),
          Text(
            controller.connected
                ? '链路已就绪，可发送消息、歌词与抓屏'
                : controller.hasHardware
                ? '当前通道：${controller.transportName}\n点击连接建立真实链路'
                : '当前通道：${controller.transportName}\n界面预览模式，未连接硬件',
            style: TextStyle(
              color: scheme.onSurfaceVariant,
              fontSize: 10.5,
              height: 1.45,
            ),
          ),
          const SizedBox(height: 12),
          Row(
            children: [
              Expanded(
                child: FilledButton.icon(
                  onPressed: active || controller.connecting
                      ? null
                      : () => onConnectionChanged(true),
                  icon: const Icon(Icons.link_rounded, size: 16),
                  label: Text(controller.connecting ? '连接中' : '连接'),
                  style: FilledButton.styleFrom(
                    minimumSize: const Size(0, 36),
                    padding: const EdgeInsets.symmetric(vertical: 8),
                    visualDensity: VisualDensity.compact,
                  ),
                ),
              ),
              const SizedBox(width: 8),
              Expanded(
                child: OutlinedButton.icon(
                  onPressed: active && !controller.connecting
                      ? () => onConnectionChanged(false)
                      : null,
                  icon: const Icon(Icons.link_off_rounded, size: 16),
                  label: const Text('断开'),
                  style: OutlinedButton.styleFrom(
                    minimumSize: const Size(0, 36),
                    padding: const EdgeInsets.symmetric(vertical: 8),
                    visualDensity: VisualDensity.compact,
                  ),
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

class _SideNavItem extends StatelessWidget {
  const _SideNavItem({
    required this.item,
    required this.extended,
    required this.progress,
    required this.selected,
    required this.onTap,
  });

  final _NavItem item;
  final bool extended;
  final double progress;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final colors = context.appColors;
    final labelProgress = Curves.easeOut.transform(
      ((progress - 0.36) / 0.64).clamp(0.0, 1.0).toDouble(),
    );
    final content = Container(
      height: 46,
      padding: EdgeInsets.symmetric(horizontal: extended ? 14 : 0),
      decoration: BoxDecoration(
        color: selected
            ? colors.accentContainer.withValues(alpha: 0.85)
            : Colors.transparent,
        borderRadius: BorderRadius.circular(14),
        border: selected
            ? Border.all(color: colors.accent.withValues(alpha: 0.25))
            : null,
      ),
      child: Row(
        mainAxisAlignment: extended
            ? MainAxisAlignment.start
            : MainAxisAlignment.center,
        children: [
          Icon(
            selected ? item.selectedIcon : item.icon,
            size: 21,
            color: selected ? colors.accent : scheme.onSurfaceVariant,
          ),
          if (extended) ...[
            const SizedBox(width: 12),
            Expanded(
              child: Opacity(
                opacity: labelProgress,
                child: Transform.translate(
                  offset: Offset(12 * (1 - labelProgress), 0),
                  child: Text(
                    item.label,
                    style: TextStyle(
                      color: selected
                          ? colors.onAccentContainer
                          : scheme.onSurfaceVariant,
                      fontWeight: selected ? FontWeight.w700 : FontWeight.w500,
                    ),
                  ),
                ),
              ),
            ),
            if (selected)
              Opacity(
                opacity: labelProgress,
                child: Container(
                  width: 5,
                  height: 5,
                  decoration: BoxDecoration(
                    color: colors.accent,
                    shape: BoxShape.circle,
                  ),
                ),
              ),
          ],
        ],
      ),
    );
    return Tooltip(
      message: extended ? '' : item.label,
      waitDuration: const Duration(milliseconds: 350),
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(14),
        child: content,
      ),
    );
  }
}

class _NavItem {
  const _NavItem(this.icon, this.selectedIcon, this.label, this.glyph);

  final IconData icon;
  final IconData selectedIcon;
  final String label;
  final Glyph glyph;
}
