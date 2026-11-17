import 'package:material_ui/material_ui.dart';

import '../services/snapshot_image.dart';
import '../state/watch_controller.dart';
import '../theme/app_theme.dart';
import '../widgets/common.dart';
import '../widgets/metric_card.dart';
import '../widgets/watch_3d_view.dart';
import '../widgets/watch_preview.dart';

class HomePage extends StatelessWidget {
  const HomePage({
    super.key,
    required this.controller,
    required this.onNavigate,
  });

  final WatchController controller;
  final ValueChanged<int> onNavigate;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final portrait =
            constraints.maxWidth < 760 &&
            constraints.maxHeight > constraints.maxWidth;
        if (portrait) {
          return _PortraitHome(controller: controller, onNavigate: onNavigate);
        }
        final scheme = Theme.of(context).colorScheme;
        return PageScroll(
          children: [
            PageHeader(
              title: '手表工作台',
              subtitle: '实时表盘与设备状态，一眼掌握。',
              trailing: StatusPill(
                label: controller.connected ? '硬件已连接' : '界面预览',
                icon: Icons.tune_rounded,
              ),
            ),
            const SizedBox(height: 22),
            _HeroCard(controller: controller, onNavigate: onNavigate),
            const SizedBox(height: 26),
            SectionHeader(
              title: '实时性能',
              subtitle: 'RAM、主循环负载与完整帧传输率',
              trailing: Wrap(
                crossAxisAlignment: WrapCrossAlignment.center,
                spacing: 8,
                children: [
                  StatusPill(
                    label: controller.telemetryLabel,
                    color: controller.liveTelemetry || controller.demoStreaming
                        ? null
                        : scheme.onSurfaceVariant,
                  ),
                  if (!controller.liveTelemetry)
                    IconButton(
                      onPressed: () => controller.setDemoStreaming(
                        !controller.demoStreaming,
                      ),
                      tooltip: controller.demoStreaming ? '暂停演示数据' : '继续演示数据',
                      iconSize: 20,
                      icon: Icon(
                        controller.demoStreaming
                            ? Icons.pause_rounded
                            : Icons.play_arrow_rounded,
                      ),
                    ),
                ],
              ),
            ),
            const SizedBox(height: 12),
            _Metrics(controller: controller),
            const SizedBox(height: 26),
            const SectionHeader(title: '快捷功能', subtitle: '媒体、表盘、运动与画面镜像'),
            const SizedBox(height: 12),
            _QuickActions(controller: controller, onNavigate: onNavigate),
            const SizedBox(height: 18),
            AppCard(
              padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 14),
              child: Row(
                children: [
                  Icon(
                    controller.connected
                        ? Icons.link_rounded
                        : Icons.info_outline_rounded,
                    size: 18,
                    color: scheme.primary,
                  ),
                  const SizedBox(width: 10),
                  Expanded(
                    child: Text(
                      controller.connected
                          ? '性能数据来自手表实时回传，约每秒一帧。'
                          : '当前数据仅用于 UI 验收，不代表已连接真实手表。',
                      style: TextStyle(
                        color: scheme.onSurfaceVariant,
                        fontSize: 12,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ],
        );
      },
    );
  }
}

class _HeroCard extends StatelessWidget {
  const _HeroCard({required this.controller, required this.onNavigate});

  final WatchController controller;
  final ValueChanged<int> onNavigate;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final isDark = scheme.brightness == Brightness.dark;
    return AppCard(
      radius: 24,
      padding: const EdgeInsets.all(28),
      gradient: LinearGradient(
        begin: Alignment.topLeft,
        end: Alignment.bottomRight,
        colors: isDark
            ? const [Color(0xFF1C2B26), Color(0xFF141D1A), Color(0xFF101615)]
            : const [Color(0xFFDDEFE3), Color(0xFFEAF3E8), Color(0xFFF5F4E9)],
      ),
      child: LayoutBuilder(
        builder: (context, constraints) {
          final stacked = constraints.maxWidth < 650;
          final showDualInline = constraints.maxWidth >= 720;
          final showDualStacked = stacked && constraints.maxWidth >= 420;
          final intro = SizedBox(
            width: double.infinity,
            child: Column(
              crossAxisAlignment: stacked
                  ? CrossAxisAlignment.center
                  : CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                StatusPill(
                  label: controller.connected
                      ? '${controller.transportName} · 已连接'
                      : '本地演示 · 未连接硬件',
                  color: isDark ? AppPalette.mint : AppPalette.mintDeep,
                ),
                const SizedBox(height: 18),
                Text(
                  'AT32 Watch',
                  textAlign: stacked ? TextAlign.center : TextAlign.start,
                  style: Theme.of(context).textTheme.headlineLarge?.copyWith(
                    color: isDark
                        ? AppPalette.warmWhite
                        : const Color(0xFF18241F),
                    fontWeight: FontWeight.w700,
                    letterSpacing: -0.8,
                  ),
                ),
                const SizedBox(height: 8),
                Text(
                  'AT32F421K8T7  ·  128 × 160  ·  ST7735S',
                  textAlign: stacked ? TextAlign.center : TextAlign.start,
                  style: TextStyle(
                    color: isDark ? AppPalette.mintSoft : AppPalette.mintDeep,
                    fontSize: 12,
                    fontWeight: FontWeight.w600,
                    letterSpacing: 0.3,
                  ),
                ),
                const SizedBox(height: 12),
                Text(
                  '把表盘、运动姿态与媒体带到大屏。\n保留手表的安静，也保留每一处细节。',
                  textAlign: stacked ? TextAlign.center : TextAlign.start,
                  style: TextStyle(
                    color: isDark ? AppPalette.muted : scheme.onSurfaceVariant,
                    height: 1.6,
                    fontSize: 13,
                  ),
                ),
                const SizedBox(height: 22),
                Wrap(
                  alignment: stacked
                      ? WrapAlignment.center
                      : WrapAlignment.start,
                  spacing: 10,
                  runSpacing: 10,
                  children: [
                    FilledButton.icon(
                      onPressed: () =>
                          toggleWatchConnection(context, controller),
                      icon: Icon(
                        controller.connected
                            ? Icons.link_off_rounded
                            : Icons.bluetooth_rounded,
                        size: 18,
                      ),
                      label: Text(controller.connected ? '断开设备' : '连接设备'),
                    ),
                    OutlinedButton.icon(
                      onPressed: () => onNavigate(1),
                      icon: const Icon(
                        Icons.view_in_ar_rounded,
                        size: 17,
                      ),
                      label: const Text('3D 大视图 & 校准'),
                    ),
                    OutlinedButton.icon(
                      onPressed: () => onNavigate(2),
                      icon: const Icon(
                        Icons.perm_media_outlined,
                        size: 17,
                      ),
                      label: const Text('媒体与通知'),
                    ),
                  ],
                ),
              ],
            ),
          );
          if (showDualInline) {
            final compactDual = constraints.maxWidth < 860;
            return Row(
              crossAxisAlignment: CrossAxisAlignment.center,
              children: [
                Expanded(
                  child: Align(
                    alignment: Alignment.centerLeft,
                    child: ConstrainedBox(
                      constraints: const BoxConstraints(maxWidth: 460),
                      child: intro,
                    ),
                  ),
                ),
                const SizedBox(width: 20),
                _DualWatchShowcase(
                  controller: controller,
                  onNavigate: onNavigate,
                  previewWidth: compactDual ? 132 : 154,
                  threeDWidth: compactDual ? 186 : 220,
                  stageHeight: compactDual ? 196 : 220,
                ),
              ],
            );
          }
          if (showDualStacked) {
            return Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                intro,
                const SizedBox(height: 22),
                Center(
                  child: _DualWatchShowcase(
                    controller: controller,
                    onNavigate: onNavigate,
                    previewWidth: 144,
                    threeDWidth: 204,
                    stageHeight: 206,
                  ),
                ),
              ],
            );
          }
          final watch = _WatchStageSlot(
            controller: controller,
            onNavigate: onNavigate,
            width: stacked ? 216 : 248,
            stageHeight: stacked ? 196 : 226,
          );
          if (stacked) {
            return Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                intro,
                const SizedBox(height: 22),
                Center(child: watch),
              ],
            );
          }
          return Row(
            children: [
              Expanded(child: intro),
              const SizedBox(width: 28),
              watch,
            ],
          );
        },
      ),
    );
  }
}

class _PortraitHome extends StatelessWidget {
  const _PortraitHome({required this.controller, required this.onNavigate});

  final WatchController controller;
  final ValueChanged<int> onNavigate;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return SingleChildScrollView(
      padding: const EdgeInsets.fromLTRB(16, 18, 16, 112),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _PortraitHeader(controller: controller),
          const SizedBox(height: 16),
          _PortraitWatchCard(controller: controller, onNavigate: onNavigate),
          const SizedBox(height: 20),
          SectionHeader(
            title: '实时性能',
            trailing: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                StatusPill(
                  label: controller.telemetryLabel,
                  color: controller.liveTelemetry || controller.demoStreaming
                      ? null
                      : scheme.onSurfaceVariant,
                ),
                if (!controller.liveTelemetry)
                  IconButton(
                    visualDensity: VisualDensity.compact,
                    onPressed: () =>
                        controller.setDemoStreaming(!controller.demoStreaming),
                    tooltip: controller.demoStreaming ? '暂停演示数据' : '继续演示数据',
                    iconSize: 18,
                    icon: Icon(
                      controller.demoStreaming
                          ? Icons.pause_rounded
                          : Icons.play_arrow_rounded,
                    ),
                  ),
              ],
            ),
          ),
          const SizedBox(height: 10),
          _PortraitMetrics(controller: controller),
          const SizedBox(height: 20),
          const SectionHeader(title: '快捷功能'),
          const SizedBox(height: 10),
          _QuickActions(controller: controller, onNavigate: onNavigate),
          const SizedBox(height: 18),
          const _IntroFooter(),
        ],
      ),
    );
  }
}

class _PortraitHeader extends StatelessWidget {
  const _PortraitHeader({required this.controller});

  final WatchController controller;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                'AT32 Watch',
                style: Theme.of(context).textTheme.headlineMedium?.copyWith(
                  fontSize: 30,
                  fontWeight: FontWeight.w800,
                  letterSpacing: -1,
                ),
              ),
              const SizedBox(height: 4),
              Text(
                'AT32F421K8T7  ·  128 × 160',
                style: TextStyle(
                  color: scheme.primary,
                  fontSize: 10.5,
                  fontWeight: FontWeight.w600,
                  letterSpacing: 0.2,
                ),
              ),
            ],
          ),
        ),
        const SizedBox(width: 8),
        StatusPill(label: controller.connectionLabel, icon: Icons.tune_rounded),
      ],
    );
  }
}

class _DualWatchShowcase extends StatelessWidget {
  const _DualWatchShowcase({
    required this.controller,
    required this.onNavigate,
    required this.previewWidth,
    required this.threeDWidth,
    required this.stageHeight,
  });

  final WatchController controller;
  final ValueChanged<int> onNavigate;
  final double previewWidth;
  final double threeDWidth;
  final double stageHeight;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final dark = scheme.brightness == Brightness.dark;
    final frameBg = (dark ? const Color(0xFF0E1615) : const Color(0xFFF6F8F1))
        .withValues(alpha: 0.78);
    final accent = dark ? AppPalette.mint : AppPalette.mintDeep;

    return Row(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Container(
          width: previewWidth + 24,
          padding: const EdgeInsets.fromLTRB(12, 12, 12, 10),
          decoration: BoxDecoration(
            color: frameBg,
            borderRadius: BorderRadius.circular(20),
            border: Border.all(color: scheme.outlineVariant),
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              SizedBox(
                height: stageHeight,
                child: Center(
                  child: WatchPreview(
                    controller: controller,
                    width: previewWidth,
                  ),
                ),
              ),
              const SizedBox(height: 8),
              Row(
                mainAxisAlignment: MainAxisAlignment.center,
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(
                    Icons.screenshot_monitor_rounded,
                    size: 13,
                    color: accent,
                  ),
                  const SizedBox(width: 5),
                  Flexible(
                    child: Text(
                      controller.deviceScreen != null
                          ? '实时截屏 · 点击刷新'
                          : '实时截屏 / 表盘',
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                        fontSize: 11,
                        fontWeight: FontWeight.w600,
                        color: scheme.onSurfaceVariant,
                      ),
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
        const SizedBox(width: 14),
        Container(
          width: threeDWidth + 20,
          padding: const EdgeInsets.fromLTRB(10, 12, 10, 10),
          decoration: BoxDecoration(
            color: frameBg,
            borderRadius: BorderRadius.circular(20),
            border: Border.all(color: scheme.outlineVariant),
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Watch3DView(
                key: const Key('home-watch-3d'),
                controller: controller,
                mode: Watch3DMode.heroCompact,
                width: threeDWidth,
                height: stageHeight,
                onTapExpand: () => onNavigate(1),
              ),
              const SizedBox(height: 8),
              Row(
                mainAxisAlignment: MainAxisAlignment.center,
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(Icons.view_in_ar_rounded, size: 13, color: accent),
                  const SizedBox(width: 5),
                  Flexible(
                    child: Text(
                      '3D 实时姿态 · 点击大视图',
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                        fontSize: 11,
                        fontWeight: FontWeight.w600,
                        color: scheme.onSurfaceVariant,
                      ),
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
      ],
    );
  }
}

class _WatchStageSlot extends StatefulWidget {
  const _WatchStageSlot({
    required this.controller,
    required this.onNavigate,
    required this.width,
    required this.stageHeight,
  });

  final WatchController controller;
  final ValueChanged<int> onNavigate;
  final double width;
  final double stageHeight;

  @override
  State<_WatchStageSlot> createState() => _WatchStageSlotState();
}

class _WatchStageSlotState extends State<_WatchStageSlot> {
  bool _showSnapshot = false;

  @override
  Widget build(BuildContext context) {
    final dark = Theme.of(context).brightness == Brightness.dark;
    final accent = dark ? AppPalette.mint : AppPalette.mintDeep;
    final muted = dark
        ? AppPalette.muted
        : Theme.of(context).colorScheme.onSurfaceVariant;
    final pillBg = (dark ? const Color(0xFF0E1615) : const Color(0xFFF3F5EA))
        .withValues(alpha: 0.9);

    return SizedBox(
      width: widget.width,
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          SizedBox(
            width: widget.width,
            height: widget.stageHeight,
            child: _showSnapshot
                ? Center(
                    child: WatchPreview(
                      controller: widget.controller,
                      width: (widget.width * 0.78).clamp(96.0, 160.0),
                    ),
                  )
                : Watch3DView(
                    key: const Key('home-watch-3d'),
                    controller: widget.controller,
                    mode: Watch3DMode.heroCompact,
                    width: widget.width,
                    height: widget.stageHeight,
                    onTapExpand: () => widget.onNavigate(1),
                  ),
          ),
          const SizedBox(height: 6),
          Container(
            padding: const EdgeInsets.all(2),
            decoration: BoxDecoration(
              color: pillBg,
              borderRadius: BorderRadius.circular(999),
              border: Border.all(
                color: Theme.of(context).colorScheme.outlineVariant,
              ),
            ),
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                _ModeChip(
                  label: '3D 姿态',
                  selected: !_showSnapshot,
                  accent: accent,
                  muted: muted,
                  onTap: () => setState(() => _showSnapshot = false),
                ),
                _ModeChip(
                  label: '截屏',
                  selected: _showSnapshot,
                  accent: accent,
                  muted: muted,
                  onTap: () => setState(() => _showSnapshot = true),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _ModeChip extends StatelessWidget {
  const _ModeChip({
    required this.label,
    required this.selected,
    required this.accent,
    required this.muted,
    required this.onTap,
  });

  final String label;
  final bool selected;
  final Color accent;
  final Color muted;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 160),
        padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 3),
        decoration: BoxDecoration(
          color: selected ? accent.withValues(alpha: 0.18) : Colors.transparent,
          borderRadius: BorderRadius.circular(999),
        ),
        child: Text(
          label,
          style: TextStyle(
            fontSize: 9.5,
            fontWeight: selected ? FontWeight.w700 : FontWeight.w500,
            color: selected ? accent : muted,
          ),
        ),
      ),
    );
  }
}

class _PortraitWatchCard extends StatelessWidget {
  const _PortraitWatchCard({
    required this.controller,
    required this.onNavigate,
  });

  final WatchController controller;
  final ValueChanged<int> onNavigate;

  @override
  Widget build(BuildContext context) {
    const weekdays = ['一', '二', '三', '四', '五', '六', '日'];
    final calActive = controller.magCalPhase == MagCalPhase.calibrating ||
        controller.magCalPhase == MagCalPhase.readyToFinish;
    return LayoutBuilder(
      builder: (context, constraints) {
        final narrow = constraints.maxWidth < 330;
        final watchWidth = narrow ? 124.0 : 148.0;
        final stageHeight = narrow ? 148.0 : 170.0;
        return AppCard(
          radius: 22,
          padding: const EdgeInsets.all(15),
          gradient: const LinearGradient(
            begin: Alignment.topLeft,
            end: Alignment.bottomRight,
            colors: [Color(0xFF1C2B26), Color(0xFF141D1A), Color(0xFF101615)],
          ),
          child: Row(
            children: [
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    StatusPill(
                      label: calActive
                          ? 'KEY2 校准中'
                          : (controller.liveSensor ? '9 轴实时' : '3D 实时姿态'),
                      color: calActive
                          ? const Color(0xFFF5B759)
                          : AppPalette.mint,
                    ),
                    const SizedBox(height: 10),
                    const Text(
                      '实时姿态',
                      style: TextStyle(
                        color: AppPalette.warmWhite,
                        fontSize: 19,
                        fontWeight: FontWeight.w700,
                      ),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      '${controller.now.month}月${controller.now.day}日 · 周${weekdays[controller.now.weekday - 1]}',
                      style: const TextStyle(
                        color: AppPalette.muted,
                        fontSize: 10.5,
                      ),
                    ),
                    const SizedBox(height: 6),
                    Text(
                      controller.liveSensor
                          ? 'R ${controller.rollDeg.round()}° · P ${controller.pitchDeg.round()}° · H ${controller.headingDeg.round()}°'
                          : '${controller.transportLabel} · ${controller.connectionLabel}',
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                        color: controller.connected || controller.liveSensor
                            ? AppPalette.mint
                            : AppPalette.muted,
                        fontSize: 10,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                    const SizedBox(height: 12),
                    Wrap(
                      spacing: 6,
                      runSpacing: 6,
                      children: [
                        FilledButton.icon(
                          onPressed: () =>
                              toggleWatchConnection(context, controller),
                          icon: Icon(
                            controller.connected
                                ? Icons.link_off_rounded
                                : Icons.bluetooth_rounded,
                            size: 15,
                          ),
                          label: Text(controller.connected ? '断开' : '连接'),
                          style: FilledButton.styleFrom(
                            minimumSize: const Size(0, 34),
                            padding: const EdgeInsets.symmetric(
                              horizontal: 11,
                              vertical: 6,
                            ),
                            visualDensity: VisualDensity.compact,
                            textStyle: const TextStyle(
                              fontSize: 12,
                              fontWeight: FontWeight.w700,
                            ),
                          ),
                        ),
                        OutlinedButton(
                          onPressed: () => onNavigate(1),
                          style: OutlinedButton.styleFrom(
                            minimumSize: const Size(0, 34),
                            padding: const EdgeInsets.symmetric(
                              horizontal: 10,
                              vertical: 6,
                            ),
                            visualDensity: VisualDensity.compact,
                            foregroundColor: AppPalette.mintSoft,
                            side: BorderSide(
                              color: AppPalette.mint.withValues(alpha: 0.45),
                            ),
                            textStyle: const TextStyle(
                              fontSize: 11.5,
                              fontWeight: FontWeight.w600,
                            ),
                          ),
                          child: const Text('大视图'),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 10),
              _WatchStageSlot(
                controller: controller,
                onNavigate: onNavigate,
                width: watchWidth,
                stageHeight: stageHeight,
              ),
            ],
          ),
        );
      },
    );
  }
}

class _PortraitMetrics extends StatelessWidget {
  const _PortraitMetrics({required this.controller});

  final WatchController controller;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final cards = [
      MiniMetricCard(
        label: 'RAM',
        value: controller.ramUsed.toStringAsFixed(1),
        unit: '/ ${controller.ramTotal.toStringAsFixed(1)} KB',
        color: scheme.primary,
        icon: Icons.memory_rounded,
        history: controller.ramHistory,
      ),
      MiniMetricCard(
        label: 'CPU',
        value: '${controller.cpu.toStringAsFixed(1)}%',
        unit: '主循环占用',
        color: scheme.onSurface,
        icon: Icons.speed_rounded,
        history: controller.cpuHistory,
      ),
      MiniMetricCard(
        label: '完整帧 FPS',
        value: '${controller.fps}',
        unit: '帧/秒',
        color: scheme.secondary,
        icon: Icons.monitor_heart_outlined,
        history: controller.fpsHistory,
      ),
    ];
    return Column(
      children: [
        for (var i = 0; i < cards.length; i++) ...[
          cards[i],
          if (i < cards.length - 1) const SizedBox(height: 8),
        ],
      ],
    );
  }
}

class _IntroFooter extends StatelessWidget {
  const _IntroFooter();

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Padding(
      padding: const EdgeInsets.fromLTRB(12, 4, 12, 6),
      child: Column(
        children: [
          const Text(
            '把表盘、消息和音乐带到大屏。',
            textAlign: TextAlign.center,
            style: TextStyle(fontSize: 13.5, fontWeight: FontWeight.w700),
          ),
          const SizedBox(height: 6),
          Text(
            '保留手表的安静，也保留每一处细节。',
            textAlign: TextAlign.center,
            style: TextStyle(
              color: scheme.onSurfaceVariant,
              fontSize: 11.5,
              height: 1.5,
            ),
          ),
        ],
      ),
    );
  }
}

class _Metrics extends StatelessWidget {
  const _Metrics({required this.controller});

  final WatchController controller;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final cards = [
      MetricCard(
        label: 'RAM',
        value: controller.ramUsed.toStringAsFixed(1),
        unit: '/ ${controller.ramTotal.toStringAsFixed(1)} KB',
        detail:
            '剩余 ${(controller.ramTotal - controller.ramUsed).toStringAsFixed(1)} KB',
        progress: controller.ramUsed / controller.ramTotal,
        centerText:
            '${(controller.ramUsed / controller.ramTotal * 100).round()}%',
        color: scheme.primary,
        icon: Icons.memory_rounded,
        history: controller.ramHistory,
      ),
      MetricCard(
        label: 'CPU',
        value: controller.cpu.toStringAsFixed(1),
        unit: '%',
        detail: '120 MHz · 1 秒采样',
        progress: controller.cpu / 100,
        centerText: '${controller.cpu.round()}%',
        color: scheme.onSurface,
        icon: Icons.speed_rounded,
        history: controller.cpuHistory,
      ),
      MetricCard(
        label: '完整帧 FPS',
        value: '${controller.fps}',
        unit: 'fps',
        detail: controller.fps > 0
            ? '${(1000 / controller.fps).toStringAsFixed(1)} ms / 完整帧'
            : '静止或仅有局部刷新',
        progress: controller.fps / 60,
        centerText: 'LIVE',
        color: scheme.secondary,
        icon: Icons.monitor_heart_outlined,
        history: controller.fpsHistory,
      ),
    ];
    return LayoutBuilder(
      builder: (context, constraints) {
        if (constraints.maxWidth >= 790) {
          return Row(
            children: [
              for (var i = 0; i < cards.length; i++) ...[
                Expanded(child: cards[i]),
                if (i < cards.length - 1) const SizedBox(width: 12),
              ],
            ],
          );
        }
        return Column(
          children: [
            for (var i = 0; i < cards.length; i++) ...[
              cards[i],
              if (i < cards.length - 1) const SizedBox(height: 12),
            ],
          ],
        );
      },
    );
  }
}

Future<void> _showSnapshot(
  BuildContext context,
  WatchController controller,
) async {
  final messenger = ScaffoldMessenger.of(context);
  messenger.hideCurrentSnackBar();
  if (!controller.connected) {
    messenger.showSnackBar(
      const SnackBar(
        content: Text('请先连接手表，再抓取屏幕画面。'),
        duration: Duration(seconds: 2),
      ),
    );
    return;
  }

  messenger.showSnackBar(
    const SnackBar(content: Text('正在抓取手表画面…'), duration: Duration(seconds: 30)),
  );
  final snapshot = await controller.requestSnapshot();
  messenger.hideCurrentSnackBar();
  if (snapshot == null) {
    messenger.showSnackBar(
      const SnackBar(
        content: Text('抓屏失败：链路超时或没有收到完整画面。'),
        duration: Duration(seconds: 3),
      ),
    );
    return;
  }

  final image = await decodeWatchSnapshot(snapshot);
  if (!context.mounted) {
    image.dispose();
    return;
  }
  await showDialog<void>(
    context: context,
    builder: (dialogContext) => Dialog(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(
              '手表屏幕 · ${snapshot.width} × ${snapshot.height}',
              style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w700),
            ),
            const SizedBox(height: 14),
            RawImage(
              image: image,
              width: 256,
              height: 320,
              filterQuality: FilterQuality.none,
            ),
            const SizedBox(height: 16),
            FilledButton(
              onPressed: () => Navigator.of(dialogContext).pop(),
              child: const Text('关闭'),
            ),
          ],
        ),
      ),
    ),
  );
  image.dispose();
}

class _QuickActions extends StatelessWidget {
  const _QuickActions({required this.controller, required this.onNavigate});

  final WatchController controller;
  final ValueChanged<int> onNavigate;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final items = [
      (
        Icons.perm_media_outlined,
        '媒体与通知',
        '音乐同步 · 通知消息',
        () => onNavigate(2),
      ),
      (
        Icons.directions_run_rounded,
        '运动与姿态',
        '步数统计 · 3D & KEY2',
        () => onNavigate(1),
      ),
      (
        Icons.watch_outlined,
        '表盘工作台',
        '32KB Flash · 样式预览',
        () => onNavigate(3),
      ),
      (
        Icons.screenshot_monitor_outlined,
        '屏幕镜像',
        '128 × 160 · RGB565',
        () => _showSnapshot(context, controller),
      ),
    ];
    return LayoutBuilder(
      builder: (context, constraints) {
        final columns = constraints.maxWidth >= 900 ? 4 : 2;
        const gap = 12.0;
        Widget tile(int index) {
          final item = items[index];
          return SizedBox(
            height: 116,
            child: AppCard(
                  onTap: item.$4,
                  padding: const EdgeInsets.all(16),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        children: [
                          Icon(item.$1, size: 21, color: scheme.primary),
                          const Spacer(),
                          Icon(
                            Icons.arrow_forward_ios_rounded,
                            size: 12,
                            color: scheme.onSurfaceVariant,
                          ),
                        ],
                      ),
                      const Spacer(),
                      Text(
                        item.$2,
                        style: const TextStyle(fontWeight: FontWeight.w700),
                      ),
                      const SizedBox(height: 3),
                      Text(
                        item.$3,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(
                          color: scheme.onSurfaceVariant,
                          fontSize: 10.5,
                        ),
                      ),
                    ],
                  ),
                ),
          );
        }

        final rows = <Widget>[];
        for (var start = 0; start < items.length; start += columns) {
          if (rows.isNotEmpty) rows.add(const SizedBox(height: gap));
          rows.add(
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                for (var i = 0; i < columns; i++) ...[
                  if (i > 0) const SizedBox(width: gap),
                  Expanded(
                    child: start + i < items.length
                        ? tile(start + i)
                        : const SizedBox(height: 116),
                  ),
                ],
              ],
            ),
          );
        }
        return Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: rows,
        );
      },
    );
  }
}
