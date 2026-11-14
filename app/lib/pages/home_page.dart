import 'package:material_ui/material_ui.dart';

import '../services/snapshot_image.dart';
import '../state/watch_controller.dart';
import '../theme/app_theme.dart';
import '../widgets/common.dart';
import '../widgets/metric_card.dart';
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
            const SectionHeader(title: '快捷功能', subtitle: '消息、运动、画面与播放控制'),
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
                  '把表盘、消息和音乐带到大屏。\n保留手表的安静，也保留每一处细节。',
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
                      onPressed: () => onNavigate(2),
                      icon: const Icon(
                        Icons.chat_bubble_outline_rounded,
                        size: 17,
                      ),
                      label: const Text('消息预览'),
                    ),
                  ],
                ),
              ],
            ),
          );
          final watch = WatchPreview(
            controller: controller,
            width: stacked ? 148 : 174,
          );
          if (stacked) {
            return Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                intro,
                const SizedBox(height: 24),
                Center(child: watch),
              ],
            );
          }
          return Row(
            children: [
              Expanded(child: intro),
              const SizedBox(width: 32),
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
          _PortraitWatchCard(controller: controller),
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

class _PortraitWatchCard extends StatelessWidget {
  const _PortraitWatchCard({required this.controller});

  final WatchController controller;

  @override
  Widget build(BuildContext context) {
    const weekdays = ['一', '二', '三', '四', '五', '六', '日'];
    return LayoutBuilder(
      builder: (context, constraints) {
        final watchWidth = constraints.maxWidth < 330 ? 116.0 : 136.0;
        return AppCard(
          radius: 22,
          padding: const EdgeInsets.all(18),
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
                    const StatusPill(label: '屏幕预览', color: AppPalette.mint),
                    const SizedBox(height: 12),
                    const Text(
                      '实时表盘',
                      style: TextStyle(
                        color: AppPalette.warmWhite,
                        fontSize: 20,
                        fontWeight: FontWeight.w700,
                      ),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      '${controller.now.month}月${controller.now.day}日 · 周${weekdays[controller.now.weekday - 1]}',
                      style: const TextStyle(
                        color: AppPalette.muted,
                        fontSize: 11,
                      ),
                    ),
                    const SizedBox(height: 10),
                    Text(
                      '${controller.transportLabel} · ${controller.connectionLabel}',
                      style: TextStyle(
                        color: controller.connected
                            ? AppPalette.mint
                            : AppPalette.muted,
                        fontSize: 10.5,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                    const SizedBox(height: 16),
                    FilledButton.icon(
                      onPressed: () =>
                          toggleWatchConnection(context, controller),
                      icon: Icon(
                        controller.connected
                            ? Icons.link_off_rounded
                            : Icons.bluetooth_rounded,
                        size: 16,
                      ),
                      label: Text(controller.connected ? '断开设备' : '连接设备'),
                      style: FilledButton.styleFrom(
                        minimumSize: const Size(0, 36),
                        padding: const EdgeInsets.symmetric(
                          horizontal: 14,
                          vertical: 8,
                        ),
                        visualDensity: VisualDensity.compact,
                        textStyle: const TextStyle(
                          fontSize: 12.5,
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 12),
              SizedBox(
                width: watchWidth,
                child: WatchPreview(controller: controller, width: watchWidth),
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
        Icons.chat_bubble_outline_rounded,
        '设备消息',
        '通知路由 · 手动发送',
        () => onNavigate(2),
      ),
      (
        Icons.directions_run_rounded,
        '运动数据',
        '今日步数 · 活动分布',
        () => onNavigate(1),
      ),
      (
        Icons.screenshot_monitor_outlined,
        '屏幕镜像',
        '128 × 160 · RGB565',
        () => _showSnapshot(context, controller),
      ),
      (
        Icons.play_circle_outline_rounded,
        '播放控制',
        '播放暂停 · 上下一首',
        () => onNavigate(3),
      ),
    ];
    return LayoutBuilder(
      builder: (context, constraints) {
        final columns = constraints.maxWidth >= 900 ? 4 : 2;
        const gap = 12.0;
        final itemWidth =
            (constraints.maxWidth - gap * (columns - 1)) / columns;
        return Wrap(
          spacing: gap,
          runSpacing: gap,
          children: [
            for (final item in items)
              SizedBox(
                width: itemWidth,
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
              ),
          ],
        );
      },
    );
  }
}
