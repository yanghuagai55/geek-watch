import 'dart:math' as math;

import 'package:material_ui/material_ui.dart';

import '../state/watch_controller.dart';
import '../theme/app_theme.dart';
import '../widgets/common.dart';

/// 运动页：直接读手表 MPU6050 的今日计步与 12 个 2 小时时段。
class SportPage extends StatelessWidget {
  const SportPage({super.key, required this.controller});

  final WatchController controller;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final synced = controller.sportSyncedAt;
    return PageScroll(
      children: [
        PageHeader(
          title: '运动',
          subtitle: '手表 IMU 实时计步 · 每 5 秒同步',
          trailing: StatusPill(
            label: controller.connected
                ? '实时同步'
                : (synced == null ? '未连接' : '上次数据'),
            icon: Icons.directions_run_rounded,
            color: controller.connected ? null : scheme.onSurfaceVariant,
          ),
        ),
        const SizedBox(height: 22),
        _TodayCard(controller: controller),
        const SizedBox(height: 16),
        _DistributionCard(controller: controller),
        const SizedBox(height: 16),
        _SourceCard(controller: controller),
      ],
    );
  }
}

class _TodayCard extends StatelessWidget {
  const _TodayCard({required this.controller});

  final WatchController controller;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final goal = controller.stepGoal > 0 ? controller.stepGoal : 8000;
    final ratio = (controller.steps / goal).clamp(0.0, 1.0);
    final synced = controller.sportSyncedAt;
    final clock = synced == null
        ? ''
        : '${synced.hour.toString().padLeft(2, '0')}:'
              '${synced.minute.toString().padLeft(2, '0')}';
    final syncedText = synced == null ? '尚未同步' : '上次同步 $clock';
    return AppCard(
      padding: const EdgeInsets.all(22),
      child: LayoutBuilder(
        builder: (context, constraints) {
          final wide = constraints.maxWidth >= 620;
          final ring = SizedBox(
            width: wide ? 148 : 126,
            height: wide ? 148 : 126,
            child: Stack(
              alignment: Alignment.center,
              children: [
                SizedBox.expand(
                  child: CircularProgressIndicator(
                    value: 1,
                    strokeWidth: 10,
                    color: scheme.outlineVariant.withValues(alpha: 0.55),
                  ),
                ),
                SizedBox.expand(
                  child: TweenAnimationBuilder<double>(
                    tween: Tween(end: ratio),
                    duration: const Duration(milliseconds: 600),
                    builder: (context, value, _) => SizedBox.expand(
                      child: CircularProgressIndicator(
                        value: value,
                        strokeWidth: 10,
                        strokeCap: StrokeCap.round,
                        color: scheme.primary,
                        backgroundColor: Colors.transparent,
                      ),
                    ),
                  ),
                ),
                Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Text(
                      '${(ratio * 100).round()}%',
                      style: TextStyle(
                        fontFamily: AppFonts.latin,
                        fontFamilyFallback: AppFonts.headingFallback,
                        fontSize: wide ? 30 : 26,
                        height: 1,
                        fontWeight: FontWeight.w700,
                      ),
                    ),
                    const SizedBox(height: 3),
                    Text(
                      '目标完成',
                      style: TextStyle(
                        color: scheme.onSurfaceVariant,
                        fontSize: 9.5,
                      ),
                    ),
                  ],
                ),
              ],
            ),
          );
          final info = Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                '今日步数',
                style: TextStyle(
                  color: scheme.onSurfaceVariant,
                  fontSize: 10.5,
                  fontWeight: FontWeight.w700,
                  letterSpacing: 1.2,
                ),
              ),
              const SizedBox(height: 4),
              Row(
                crossAxisAlignment: CrossAxisAlignment.baseline,
                textBaseline: TextBaseline.alphabetic,
                children: [
                  Flexible(
                    child: Text(
                      '${controller.steps}',
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                        fontFamily: AppFonts.latin,
                        fontFamilyFallback: AppFonts.headingFallback,
                        fontSize: wide ? 46 : 38,
                        height: 1.05,
                        fontWeight: FontWeight.w700,
                        fontFeatures: const [FontFeature.tabularFigures()],
                      ),
                    ),
                  ),
                  const SizedBox(width: 5),
                  Text(
                    '步',
                    style: TextStyle(
                      color: scheme.onSurfaceVariant,
                      fontSize: 12,
                    ),
                  ),
                  const SizedBox(width: 8),
                  Flexible(
                    child: Text(
                      '/ $goal',
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                        color: scheme.onSurfaceVariant,
                        fontSize: 12,
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 10),
              ClipRRect(
                borderRadius: BorderRadius.circular(3),
                child: LinearProgressIndicator(
                  value: ratio,
                  minHeight: 6,
                  backgroundColor: scheme.outlineVariant.withValues(alpha: 0.6),
                  color: scheme.primary,
                ),
              ),
              const SizedBox(height: 8),
              Text(
                controller.connected
                    ? '已完成 ${(ratio * 100).round()}% · '
                          '${synced == null ? '等待手表数据' : '已同步 $clock'}'
                    : '$syncedText · 显示上次读数',
                style: TextStyle(
                  color: scheme.onSurfaceVariant,
                  fontSize: 10.5,
                ),
              ),
            ],
          );
          final stats = Row(
            children: [
              Expanded(
                child: _SportStat(
                  icon: Icons.straighten_rounded,
                  label: '距离',
                  value: controller.distanceM >= 1000
                      ? (controller.distanceM / 1000).toStringAsFixed(2)
                      : '${controller.distanceM}',
                  unit: controller.distanceM >= 1000 ? 'km' : 'm',
                  color: scheme.primary,
                ),
              ),
              _StatDivider(color: scheme.outlineVariant),
              Expanded(
                child: _SportStat(
                  icon: Icons.local_fire_department_outlined,
                  label: '卡路里',
                  value: '${controller.calories}',
                  unit: 'kcal',
                  color: scheme.onSurface,
                ),
              ),
              _StatDivider(color: scheme.outlineVariant),
              Expanded(
                child: _SportStat(
                  icon: Icons.timer_outlined,
                  label: '活跃时长',
                  value: '${controller.activeMinutes}',
                  unit: '分钟',
                  color: scheme.secondary,
                ),
              ),
            ],
          );
          if (!wide) {
            return Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Row(
                  children: [
                    ring,
                    const SizedBox(width: 18),
                    Expanded(child: info),
                  ],
                ),
                const Divider(height: 30),
                stats,
              ],
            );
          }
          return Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Row(
                children: [
                  ring,
                  const SizedBox(width: 28),
                  Expanded(child: info),
                ],
              ),
              const Divider(height: 32),
              stats,
            ],
          );
        },
      ),
    );
  }
}

class _SportStat extends StatelessWidget {
  const _SportStat({
    required this.icon,
    required this.label,
    required this.value,
    required this.unit,
    required this.color,
  });

  final IconData icon;
  final String label;
  final String value;
  final String unit;
  final Color color;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Icon(icon, size: 15, color: color),
            const SizedBox(width: 6),
            Text(
              label,
              style: TextStyle(
                color: scheme.onSurfaceVariant,
                fontSize: 10.5,
                fontWeight: FontWeight.w700,
                letterSpacing: 0.5,
              ),
            ),
          ],
        ),
        const SizedBox(height: 7),
        Row(
          crossAxisAlignment: CrossAxisAlignment.baseline,
          textBaseline: TextBaseline.alphabetic,
          children: [
            Flexible(
              child: Text(
                value,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  fontFamily: AppFonts.latin,
                  fontFamilyFallback: AppFonts.headingFallback,
                  fontSize: 23,
                  height: 1,
                  fontWeight: FontWeight.w700,
                  fontFeatures: const [FontFeature.tabularFigures()],
                ),
              ),
            ),
            const SizedBox(width: 4),
            Text(
              unit,
              style: TextStyle(
                color: scheme.onSurfaceVariant,
                fontSize: 10.5,
              ),
            ),
          ],
        ),
      ],
    );
  }
}

class _StatDivider extends StatelessWidget {
  const _StatDivider({required this.color});

  final Color color;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 1,
      height: 36,
      margin: const EdgeInsets.symmetric(horizontal: 12),
      color: color.withValues(alpha: 0.6),
    );
  }
}

class _DistributionCard extends StatelessWidget {
  const _DistributionCard({required this.controller});

  final WatchController controller;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final buckets = controller.sportBuckets;
    final total = buckets.fold<int>(0, (sum, value) => sum + value);
    final peak = buckets.fold<int>(0, math.max);
    return AppCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const SectionHeader(
            title: '今日活动分布',
            subtitle: '每 2 小时一段 · 共 12 段',
          ),
          const SizedBox(height: 18),
          _SportBars(buckets: buckets, color: scheme.primary),
          const SizedBox(height: 8),
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              for (final label in const ['0点', '6点', '12点', '18点', '24点'])
                Text(
                  label,
                  style: TextStyle(
                    color: scheme.onSurfaceVariant,
                    fontSize: 9.5,
                  ),
                ),
            ],
          ),
          const SizedBox(height: 12),
          Text(
            total == 0
                ? '今天还没有步数。走两步，柱状图就会长出来。'
                : '峰值 $peak 步 / 2 小时 · 合计 $total 步',
            style: TextStyle(
              color: scheme.onSurfaceVariant,
              fontSize: 10.5,
            ),
          ),
        ],
      ),
    );
  }
}

class _SportBars extends StatelessWidget {
  const _SportBars({required this.buckets, required this.color});

  final List<int> buckets;
  final Color color;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final values = buckets.length >= 12
        ? buckets
        : <int>[...buckets, ...List<int>.filled(12 - buckets.length, 0)];
    final maxValue = values.fold<int>(0, math.max);
    final current = DateTime.now().hour ~/ 2;
    return SizedBox(
      height: 118,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.end,
        children: [
          for (var i = 0; i < 12; i++)
            Expanded(
              child: Padding(
                padding: const EdgeInsets.symmetric(horizontal: 2.5),
                child: Container(
                  height: maxValue == 0
                      ? 3
                      : (values[i] / maxValue * 118).clamp(3.0, 118.0),
                  decoration: BoxDecoration(
                    color: values[i] == 0
                        ? scheme.outlineVariant
                        : color.withValues(alpha: i == current ? 1 : 0.45),
                    borderRadius: BorderRadius.circular(4),
                  ),
                ),
              ),
            ),
        ],
      ),
    );
  }
}

class _SourceCard extends StatelessWidget {
  const _SourceCard({required this.controller});

  final WatchController controller;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return AppCard(
      padding: const EdgeInsets.all(18),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(
                Icons.info_outline_rounded,
                size: 16,
                color: scheme.primary,
              ),
              const SizedBox(width: 8),
              const Text(
                '数据来源与限制',
                style: TextStyle(fontSize: 13, fontWeight: FontWeight.w700),
              ),
            ],
          ),
          const SizedBox(height: 8),
          Text(
            '步数由手表 MPU6050 在固件里计数，App 每 5 秒通过 0x27 读取；'
            '手表重启后今日数据会从 0 开始。小米运动健康没有开放第三方读取，'
            '运动页不依赖它，也不会上传数据。',
            style: TextStyle(
              color: scheme.onSurfaceVariant,
              fontSize: 10.5,
              height: 1.55,
            ),
          ),
          const SizedBox(height: 14),
          Wrap(
            spacing: 10,
            runSpacing: 10,
            children: [
              FilledButton.icon(
                onPressed: controller.connected
                    ? () async {
                        final messenger = ScaffoldMessenger.of(context);
                        final ok = await controller.refreshSport();
                        messenger.hideCurrentSnackBar();
                        messenger.showSnackBar(
                          SnackBar(
                            content: Text(
                              ok ? '已从手表读取今日运动。' : '同步失败，请检查手表连接。',
                            ),
                          ),
                        );
                      }
                    : null,
                icon: const Icon(Icons.sync_rounded, size: 18),
                label: const Text('立即同步'),
              ),
              if (!controller.connected)
                OutlinedButton.icon(
                  onPressed: () => toggleWatchConnection(context, controller),
                  icon: const Icon(Icons.bluetooth_rounded, size: 17),
                  label: const Text('连接设备'),
                ),
            ],
          ),
        ],
      ),
    );
  }
}
