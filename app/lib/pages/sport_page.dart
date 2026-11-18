import 'dart:math' as math;

import 'package:material_ui/material_ui.dart';

import '../state/watch_controller.dart';
import '../theme/app_theme.dart';
import '../widgets/common.dart';
import '../widgets/watch_3d_view.dart';

/// 运动与 9 轴姿态页：大视图 3D 实时姿态、KEY2 磁强计硬铁校准与今日计步统计。
class SportPage extends StatelessWidget {
  const SportPage({super.key, required this.controller});

  final WatchController controller;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final colors = context.appColors;
    final synced = controller.sportSyncedAt;
    return PageScroll(
      children: [
        PageHeader(
          title: '运动与 9 轴姿态',
          subtitle: '3D 实时装配姿态 · KEY2 磁力计校准 · IMU 计步同步',
          trailing: StatusPill(
            label: controller.liveSensor
                ? '${controller.dofBadge} 实时'
                : (controller.connected
                      ? '实时同步'
                      : (synced == null ? '3D 预览' : '上次数据')),
            icon: Icons.view_in_ar_rounded,
            color: controller.liveSensor || controller.connected
                ? colors.accent
                : scheme.onSurfaceVariant,
          ),
        ),
        const SizedBox(height: 20),
        LayoutBuilder(
          builder: (context, constraints) {
            final twoCol = constraints.maxWidth >= 900;
            final wideInner = constraints.maxWidth >= 620;
            final stageWide = constraints.maxWidth >= 820;
            return Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                AlignedPair(
                  wide: twoCol,
                  leftFlex: 11,
                  rightFlex: 10,
                  left: _TodayCard(
                    controller: controller,
                    wide: twoCol ? false : wideInner,
                    stretch: twoCol,
                  ),
                  right: _DistributionCard(
                    controller: controller,
                    stretch: twoCol,
                  ),
                ),
                const SizedBox(height: 16),
                _AttitudeStageCard(
                  controller: controller,
                  wide: stageWide,
                  fourColTelemetry: constraints.maxWidth >= 980,
                ),
                const SizedBox(height: 16),
                _SourceCard(controller: controller, wide: wideInner),
              ],
            );
          },
        ),
      ],
    );
  }
}

class _AttitudeStageCard extends StatelessWidget {
  const _AttitudeStageCard({
    required this.controller,
    required this.wide,
    required this.fourColTelemetry,
  });

  final WatchController controller;
  final bool wide;
  final bool fourColTelemetry;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final colors = context.appColors;
    final viewport = Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Row(
          children: [
            Container(
              width: 32,
              height: 32,
              decoration: BoxDecoration(
                color: colors.accentContainer,
                borderRadius: BorderRadius.circular(10),
              ),
              child: Icon(
                Icons.threed_rotation_rounded,
                size: 18,
                color: colors.accent,
              ),
            ),
            const SizedBox(width: 10),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Text(
                    '3D 实时姿态大视图',
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(fontSize: 15, fontWeight: FontWeight.w700),
                  ),
                  Text(
                    controller.liveSensor
                        ? '${controller.sensor?.magChip.isNotEmpty == true ? controller.sensor!.magChip : "QMC5883P"} · MPU6050'
                        : '拖拽旋转 · 滚轮/按钮缩放',
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      fontSize: 11,
                      color: scheme.onSurfaceVariant,
                    ),
                  ),
                ],
              ),
            ),
            if (controller.connected) ...[
              const SizedBox(width: 8),
              StatusPill(
                label: controller.sensorStreaming ? '采样中' : '已暂停',
                icon: controller.sensorStreaming
                    ? Icons.sensors_rounded
                    : Icons.pause_circle_outline_rounded,
                color: controller.sensorStreaming
                    ? colors.accent
                    : scheme.onSurfaceVariant,
                onTap: () =>
                    controller.setSensorStreaming(!controller.sensorStreaming),
              ),
            ],
          ],
        ),
        const SizedBox(height: 14),
        Watch3DView(
          key: const Key('sport-watch-3d-large'),
          controller: controller,
          mode: Watch3DMode.stageLarge,
          height: wide ? 336 : 248,
          onTapExpand: () => _openFullscreen3D(context, controller),
        ),
        const SizedBox(height: 12),
        _TelemetryStrip(
          controller: controller,
          fourCols: wide ? false : fourColTelemetry,
        ),
      ],
    );

    final calPanel = _Key2CalibrationPanel(controller: controller);

    return AppCard(
      padding: const EdgeInsets.all(20),
      child: wide
          ? IntrinsicHeight(
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Expanded(flex: 11, child: viewport),
                  const SizedBox(width: 18),
                  Expanded(flex: 10, child: calPanel),
                ],
              ),
            )
          : Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                viewport,
                const SizedBox(height: 18),
                calPanel,
              ],
            ),
    );
  }
}

class _TelemetryStrip extends StatelessWidget {
  const _TelemetryStrip({required this.controller, required this.fourCols});

  final WatchController controller;
  final bool fourCols;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final colors = context.appColors;

    final accText = '${controller.ax}, ${controller.ay}, ${controller.az} mg';
    final magText = '${controller.mx}, ${controller.my}, ${controller.mz} mG';
    final batText = controller.batteryMv > 0
        ? '${(controller.batteryMv / 1000.0).toStringAsFixed(2)} V'
        : '-- V';

    final tiles = [
      (
        'Roll / Pitch / Heading',
        '${controller.rollDeg.round()}° / ${controller.pitchDeg.round()}° / ${controller.headingDeg.round()}°',
      ),
      ('加速度计 A', accText),
      ('磁力计 M', magText),
      ('融合 · 电量', '${controller.dofBadge} · $batText'),
    ];

    Widget buildTile((String, String) item) {
      return Container(
        padding: const EdgeInsets.symmetric(horizontal: 11, vertical: 9),
        decoration: BoxDecoration(
          color: colors.insetSurface,
          borderRadius: BorderRadius.circular(12),
          border: Border.all(color: scheme.outlineVariant),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              item.$1,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(fontSize: 10, color: scheme.onSurfaceVariant),
            ),
            const SizedBox(height: 3),
            Text(
              item.$2,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(
                fontSize: 11.5,
                fontWeight: FontWeight.w700,
                fontFeatures: [FontFeature.tabularFigures()],
              ),
            ),
          ],
        ),
      );
    }

    if (fourCols) {
      return Row(
        children: [
          for (var i = 0; i < tiles.length; i++) ...[
            if (i > 0) const SizedBox(width: 8),
            Expanded(child: buildTile(tiles[i])),
          ],
        ],
      );
    }
    return Column(
      children: [
        Row(
          children: [
            Expanded(child: buildTile(tiles[0])),
            const SizedBox(width: 8),
            Expanded(child: buildTile(tiles[1])),
          ],
        ),
        const SizedBox(height: 8),
        Row(
          children: [
            Expanded(child: buildTile(tiles[2])),
            const SizedBox(width: 8),
            Expanded(child: buildTile(tiles[3])),
          ],
        ),
      ],
    );
  }
}

class _Key2CalibrationPanel extends StatelessWidget {
  const _Key2CalibrationPanel({required this.controller});

  final WatchController controller;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final colors = context.appColors;
    final phase = controller.magCalPhase;
    final calActive =
        phase == MagCalPhase.calibrating || phase == MagCalPhase.readyToFinish;
    final ready = phase == MagCalPhase.readyToFinish;
    final done = phase == MagCalPhase.done;

    final warnColor = colors.warning;
    final mint = colors.accent;
    final borderColor = switch (phase) {
      MagCalPhase.calibrating => warnColor,
      MagCalPhase.readyToFinish || MagCalPhase.done => mint,
      MagCalPhase.idle => scheme.outlineVariant,
    };

    final statusTitle = switch (phase) {
      MagCalPhase.idle => 'KEY2 磁强计硬铁校准',
      MagCalPhase.calibrating => '正在采集磁场包络 · 请 360° 旋转',
      MagCalPhase.readyToFinish => '跨度已达标！请再按 KEY2 完成',
      MagCalPhase.done => 'KEY2 磁强计校准已完成',
    };
    final badgeLabel = switch (phase) {
      MagCalPhase.idle => '按 KEY2 启动',
      MagCalPhase.calibrating => '旋转采集中',
      MagCalPhase.readyToFinish => '可按 KEY2 保存',
      MagCalPhase.done => '已锁存偏置',
    };

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: colors.insetSurface,
        borderRadius: BorderRadius.circular(18),
        border: Border.all(color: borderColor, width: calActive ? 1.5 : 1.0),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(
                done
                    ? Icons.verified_rounded
                    : (calActive ? Icons.explore_rounded : Icons.tune_rounded),
                size: 18,
                color: phase == MagCalPhase.calibrating ? warnColor : mint,
              ),
              const SizedBox(width: 8),
              Expanded(
                child: Text(
                  statusTitle,
                  style: const TextStyle(
                    fontSize: 13.5,
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ),
              const SizedBox(width: 6),
              StatusPill(
                label: badgeLabel,
                color: phase == MagCalPhase.calibrating ? warnColor : mint,
              ),
            ],
          ),
          const SizedBox(height: 12),
          _CalStepRow(
            index: '1',
            active: phase == MagCalPhase.idle,
            done: phase != MagCalPhase.idle,
            title: '手表进入 运动 → KEY3 指南针（或传感器数值）',
            detail: '在一级指南针按 KEY2 直接校准，或进二级数值确认 K2c 后按 KEY2',
          ),
          const SizedBox(height: 8),
          _CalStepRow(
            index: '2',
            active: phase == MagCalPhase.calibrating,
            done: ready || done,
            title: '围绕三轴缓慢旋转 360°（八字绕腕）',
            detail: '固件要求 M.x / M.y / M.z 跨度均 ≥ 120 mG 方可收敛',
          ),
          const SizedBox(height: 8),
          _CalStepRow(
            index: '3',
            active: phase == MagCalPhase.readyToFinish,
            done: done,
            title: '再次短按 KEY2 完成硬铁偏移写入',
            detail: done
                ? '估测中心偏置：(${controller.magOffsetX}, ${controller.magOffsetY}, ${controller.magOffsetZ}) mG'
                : '或按 KEY3 复位姿态角并重新学习静止陀螺零偏',
          ),
          const SizedBox(height: 14),
          _AxisSpanMeter(
            axis: 'X 轴跨度',
            span: controller.magSpanX,
            target: WatchController.magSpanTarget,
            accent: mint,
            warnColor: warnColor,
          ),
          const SizedBox(height: 6),
          _AxisSpanMeter(
            axis: 'Y 轴跨度',
            span: controller.magSpanY,
            target: WatchController.magSpanTarget,
            accent: mint,
            warnColor: warnColor,
          ),
          const SizedBox(height: 6),
          _AxisSpanMeter(
            axis: 'Z 轴跨度',
            span: controller.magSpanZ,
            target: WatchController.magSpanTarget,
            accent: mint,
            warnColor: warnColor,
          ),
          if (controller.calStatusNote != null) ...[
            const SizedBox(height: 10),
            Container(
              width: double.infinity,
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
              decoration: BoxDecoration(
                color: calActive
                    ? colors.warningContainer
                    : colors.accentContainer,
                borderRadius: BorderRadius.circular(10),
              ),
              child: Text(
                controller.calStatusNote!,
                style: TextStyle(
                  fontSize: 11,
                  height: 1.4,
                  fontWeight: FontWeight.w600,
                  color: scheme.onSurface,
                ),
              ),
            ),
          ],
          const SizedBox(height: 14),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              FilledButton.icon(
                onPressed: controller.keyBusy
                    ? null
                    : () => controller.triggerKey2Calibration(),
                icon: Icon(
                  calActive
                      ? Icons.check_circle_outline_rounded
                      : Icons.touch_app_rounded,
                  size: 17,
                ),
                label: Text(
                  calActive
                      ? (controller.connected ? '发送 KEY2 完成校准' : '完成 KEY2 校准')
                      : (controller.connected ? '远程按 KEY2 启动校准' : '体验 KEY2 校准'),
                ),
              ),
              OutlinedButton(
                onPressed: calActive
                    ? controller.completeMagCalTracking
                    : () => controller.beginMagCalTracking(),
                child: Text(calActive ? '手表已按 KEY2 · 完成' : '手表已按 KEY2 · 追踪'),
              ),
              OutlinedButton.icon(
                onPressed: controller.keyBusy
                    ? null
                    : () => controller.triggerKey3Reset(),
                icon: const Icon(Icons.restart_alt_rounded, size: 17),
                label: const Text('KEY3 归零'),
              ),
              if (calActive)
                TextButton(
                  onPressed: controller.cancelMagCalTracking,
                  child: const Text('重置提示'),
                ),
            ],
          ),
        ],
      ),
    );
  }
}

class _CalStepRow extends StatelessWidget {
  const _CalStepRow({
    required this.index,
    required this.active,
    required this.done,
    required this.title,
    required this.detail,
  });

  final String index;
  final bool active;
  final bool done;
  final String title;
  final String detail;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final colors = context.appColors;
    final badgeColor = done
        ? colors.accent
        : (active ? colors.warning : scheme.onSurfaceVariant);

    return Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Container(
          width: 20,
          height: 20,
          alignment: Alignment.center,
          decoration: BoxDecoration(
            shape: BoxShape.circle,
            color: badgeColor.withValues(alpha: done || active ? 0.18 : 0.1),
            border: Border.all(color: badgeColor),
          ),
          child: done
              ? Icon(Icons.check_rounded, size: 13, color: badgeColor)
              : Text(
                  index,
                  style: TextStyle(
                    fontSize: 10.5,
                    fontWeight: FontWeight.w700,
                    color: badgeColor,
                  ),
                ),
        ),
        const SizedBox(width: 10),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                title,
                style: TextStyle(
                  fontSize: 12,
                  fontWeight: active || done ? FontWeight.w700 : FontWeight.w600,
                ),
              ),
              const SizedBox(height: 1),
              Text(
                detail,
                style: TextStyle(
                  fontSize: 10.5,
                  height: 1.35,
                  color: scheme.onSurfaceVariant,
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }
}

class _AxisSpanMeter extends StatelessWidget {
  const _AxisSpanMeter({
    required this.axis,
    required this.span,
    required this.target,
    required this.accent,
    required this.warnColor,
  });

  final String axis;
  final int span;
  final int target;
  final Color accent;
  final Color warnColor;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final ratio = (span / target).clamp(0.0, 1.0);
    final reached = span >= target;
    final barColor = reached ? accent : warnColor;

    return Row(
      children: [
        SizedBox(
          width: 58,
          child: Text(
            axis,
            style: TextStyle(fontSize: 10.5, color: scheme.onSurfaceVariant),
          ),
        ),
        Expanded(
          child: ClipRRect(
            borderRadius: BorderRadius.circular(999),
            child: LinearProgressIndicator(
              value: ratio,
              minHeight: 7,
              backgroundColor: scheme.outlineVariant.withValues(alpha: 0.45),
              valueColor: AlwaysStoppedAnimation<Color>(barColor),
            ),
          ),
        ),
        const SizedBox(width: 8),
        SizedBox(
          width: 62,
          child: Text(
            '$span/$target',
            textAlign: TextAlign.right,
            style: TextStyle(
              fontSize: 10.5,
              fontWeight: FontWeight.w700,
              color: reached ? accent : scheme.onSurfaceVariant,
              fontFeatures: const [FontFeature.tabularFigures()],
            ),
          ),
        ),
      ],
    );
  }
}

Future<void> _openFullscreen3D(
  BuildContext context,
  WatchController controller,
) async {
  await showDialog<void>(
    context: context,
    builder: (dialogContext) {
      return Dialog.fullscreen(
        child: Scaffold(
          appBar: AppBar(
            title: const Text('AT32 Watch · 3D 全屏交互视图'),
            leading: IconButton(
              icon: const Icon(Icons.close_rounded),
              onPressed: () => Navigator.of(dialogContext).pop(),
            ),
          ),
          body: AnimatedBuilder(
            animation: controller,
            builder: (context, _) {
              final colors = context.appColors;
              final calibrating =
                  controller.magCalPhase == MagCalPhase.calibrating ||
                  controller.magCalPhase == MagCalPhase.readyToFinish;
              return Padding(
                padding: const EdgeInsets.fromLTRB(16, 8, 16, 16),
                child: Column(
                  children: [
                    Expanded(
                      child: Watch3DView(
                        controller: controller,
                        mode: Watch3DMode.stageLarge,
                        height: double.infinity,
                      ),
                    ),
                    const SizedBox(height: 12),
                    Row(
                      children: [
                        StatusPill(
                          label:
                              controller.magCalPhase ==
                                  MagCalPhase.readyToFinish
                              ? '按 KEY2 完成保存'
                              : (controller.magCalPhase ==
                                        MagCalPhase.calibrating
                                    ? '360° 采集中 · 按 KEY2 结束'
                                    : 'KEY2 磁力计校准'),
                          color: calibrating ? colors.warning : colors.accent,
                        ),
                        const Spacer(),
                        FilledButton.icon(
                          onPressed: controller.keyBusy
                              ? null
                              : () => controller.triggerKey2Calibration(),
                          icon: const Icon(Icons.touch_app_rounded, size: 17),
                          label: Text(
                            calibrating ? 'KEY2 完成校准' : 'KEY2 开始校准',
                          ),
                        ),
                      ],
                    ),
                  ],
                ),
              );
            },
          ),
        ),
      );
    },
  );
}

class _TodayCard extends StatelessWidget {
  const _TodayCard({
    required this.controller,
    required this.wide,
    required this.stretch,
  });

  final WatchController controller;
  final bool wide;
  final bool stretch;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final colors = context.appColors;
    final goal = controller.stepGoal > 0 ? controller.stepGoal : 8000;
    final ratio = (controller.steps / goal).clamp(0.0, 1.0);
    final synced = controller.sportSyncedAt;
    final clock = synced == null
        ? ''
        : '${synced.hour.toString().padLeft(2, '0')}:'
              '${synced.minute.toString().padLeft(2, '0')}';
    final syncedText = synced == null ? '尚未同步' : '上次同步 $clock';

    final ring = SizedBox(
      width: wide ? 140 : 118,
      height: wide ? 140 : 118,
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
                  color: colors.accent,
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
                  fontSize: wide ? 28 : 24,
                  height: 1,
                  fontWeight: FontWeight.w700,
                ),
              ),
              const SizedBox(height: 3),
              Text(
                '目标完成',
                style: TextStyle(color: scheme.onSurfaceVariant, fontSize: 9.5),
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
                  fontSize: wide ? 44 : 36,
                  height: 1.05,
                  fontWeight: FontWeight.w700,
                  fontFeatures: const [FontFeature.tabularFigures()],
                ),
              ),
            ),
            const SizedBox(width: 5),
            Text(
              '步',
              style: TextStyle(color: scheme.onSurfaceVariant, fontSize: 12),
            ),
            const SizedBox(width: 8),
            Flexible(
              child: Text(
                '/ $goal',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(color: scheme.onSurfaceVariant, fontSize: 12),
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
            color: colors.accent,
          ),
        ),
        const SizedBox(height: 8),
        Text(
          controller.connected
              ? '已完成 ${(ratio * 100).round()}% · '
                    '${synced == null ? '等待手表数据' : '已同步 $clock'}'
              : '$syncedText · 显示上次读数',
          style: TextStyle(color: scheme.onSurfaceVariant, fontSize: 10.5),
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
            color: colors.accent,
          ),
        ),
        _StatDivider(color: scheme.outlineVariant),
        Expanded(
          child: _SportStat(
            icon: Icons.local_fire_department_outlined,
            label: '卡路里',
            value: '${controller.calories}',
            unit: 'kcal',
            color: colors.warning,
          ),
        ),
        _StatDivider(color: scheme.outlineVariant),
        Expanded(
          child: _SportStat(
            icon: Icons.timer_outlined,
            label: '活跃时长',
            value: '${controller.activeMinutes}',
            unit: '分钟',
            color: colors.info,
          ),
        ),
      ],
    );

    return AppCard(
      padding: const EdgeInsets.all(22),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              ring,
              SizedBox(width: wide ? 24 : 16),
              Expanded(child: info),
            ],
          ),
          if (stretch) const Spacer(),
          const Divider(height: 28),
          stats,
        ],
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
            const SizedBox(width: 5),
            Flexible(
              child: Text(
                label,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  color: scheme.onSurfaceVariant,
                  fontSize: 10.5,
                  fontWeight: FontWeight.w700,
                  letterSpacing: 0.4,
                ),
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
                style: const TextStyle(
                  fontFamily: AppFonts.latin,
                  fontFamilyFallback: AppFonts.headingFallback,
                  fontSize: 22,
                  height: 1,
                  fontWeight: FontWeight.w700,
                  fontFeatures: [FontFeature.tabularFigures()],
                ),
              ),
            ),
            const SizedBox(width: 4),
            Text(
              unit,
              style: TextStyle(color: scheme.onSurfaceVariant, fontSize: 10.5),
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
      margin: const EdgeInsets.symmetric(horizontal: 8),
      color: color.withValues(alpha: 0.6),
    );
  }
}

class _DistributionCard extends StatelessWidget {
  const _DistributionCard({required this.controller, required this.stretch});

  final WatchController controller;
  final bool stretch;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final colors = context.appColors;
    final buckets = controller.sportBuckets;
    final total = buckets.fold<int>(0, (sum, value) => sum + value);
    final peak = buckets.fold<int>(0, math.max);
    return AppCard(
      padding: const EdgeInsets.all(22),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SectionHeader(
            title: '今日活动分布',
            subtitle: '每 2 小时一段 · 共 12 段',
            trailing: StatusPill(
              label: total > 0 ? '峰值 $peak 步' : '暂无步数',
              color: total > 0 ? colors.accent : scheme.onSurfaceVariant,
            ),
          ),
          const SizedBox(height: 18),
          _SportBars(buckets: buckets, color: colors.accent),
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
          if (stretch) const Spacer(),
          const SizedBox(height: 12),
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 9),
            decoration: BoxDecoration(
              color: colors.insetSurface,
              borderRadius: BorderRadius.circular(10),
            ),
            child: Text(
              total == 0
                  ? '今天还没有步数。走两步，柱状图就会长出来。'
                  : '峰值 $peak 步 / 2 小时 · 合计 $total 步 · 目标 ${controller.stepGoal} 步',
              style: TextStyle(color: scheme.onSurfaceVariant, fontSize: 11),
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
      height: 126,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.end,
        children: [
          for (var i = 0; i < 12; i++)
            Expanded(
              child: Padding(
                padding: const EdgeInsets.symmetric(horizontal: 2.5),
                child: Container(
                  height: maxValue == 0
                      ? 4
                      : (values[i] / maxValue * 126).clamp(4.0, 126.0),
                  decoration: BoxDecoration(
                    color: values[i] == 0
                        ? scheme.outlineVariant.withValues(alpha: 0.7)
                        : color.withValues(alpha: i == current ? 1 : 0.48),
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
  const _SourceCard({required this.controller, required this.wide});

  final WatchController controller;
  final bool wide;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final colors = context.appColors;
    final infoBlock = Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Icon(Icons.info_outline_rounded, size: 16, color: colors.accent),
            const SizedBox(width: 8),
            const Text(
              '数据来源与限制',
              style: TextStyle(fontSize: 13.5, fontWeight: FontWeight.w700),
            ),
          ],
        ),
        const SizedBox(height: 8),
        Text(
          '步数由手表 MPU6050 在固件里计数（0x27 读取）；9 轴融合姿态由片上 Q14 定点算法实时融合 MPU6050 与 QMC5883P 并通过 0x13（DBG 行）回传。'
          '进入手表「运动 → 传感器数值」按 KEY2 可完成 360° 硬铁偏置校准，按 KEY3 复位零偏与航向。',
          style: TextStyle(
            color: scheme.onSurfaceVariant,
            fontSize: 11,
            height: 1.5,
          ),
        ),
      ],
    );
    final actions = Wrap(
      spacing: 10,
      runSpacing: 10,
      children: [
        FilledButton.icon(
          onPressed: controller.connected
              ? () async {
                  final messenger = ScaffoldMessenger.of(context);
                  final sportOk = await controller.refreshSport();
                  final sensorOk = await controller.refreshSensor();
                  if (!context.mounted) return;
                  messenger.hideCurrentSnackBar();
                  messenger.showSnackBar(
                    SnackBar(
                      content: Text(
                        sportOk || sensorOk
                            ? '已同步今日运动与 9 轴姿态数据。'
                            : '同步失败，请检查手表连接。',
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
    );

    return AppCard(
      padding: const EdgeInsets.all(18),
      child: wide
          ? Row(
              crossAxisAlignment: CrossAxisAlignment.center,
              children: [
                Expanded(child: infoBlock),
                const SizedBox(width: 20),
                actions,
              ],
            )
          : Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                infoBlock,
                const SizedBox(height: 14),
                actions,
              ],
            ),
    );
  }
}
