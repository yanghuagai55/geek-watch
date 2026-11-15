import 'package:material_ui/material_ui.dart';

import '../state/watch_controller.dart';
import '../theme/app_theme.dart';
import '../widgets/common.dart';
import '../widgets/watch_face.dart';

/// 表盘管理预设计页面：预留外接 32KB I2C Flash (AT24C256/FM24C256) 后的表盘下载与切换。
class DialsPage extends StatefulWidget {
  const DialsPage({super.key, required this.controller});

  final WatchController controller;

  @override
  State<DialsPage> createState() => _DialsPageState();
}

enum _DialKind { digitalAmber, analogBam, sportImu, customPhoto }

class _DialSpec {
  const _DialSpec({
    required this.kind,
    required this.name,
    required this.subtitle,
    required this.tag,
    required this.sizeHint,
    required this.builtIn,
    required this.accent,
  });

  final _DialKind kind;
  final String name;
  final String subtitle;
  final String tag;
  final String sizeHint;
  final bool builtIn;
  final Color accent;
}

const _dialAmber = Color(0xFFF5B759);

const _dialPresets = <_DialSpec>[
  _DialSpec(
    kind: _DialKind.digitalAmber,
    name: '琥珀双色数字',
    subtitle: '当前固化在 AT32 本地 Flash；竖屏大字号 + 今日步数条',
    tag: '本地 ROM',
    sizeHint: '当前默认',
    builtIn: true,
    accent: AppPalette.mint,
  ),
  _DialSpec(
    kind: _DialKind.analogBam,
    name: '墨竹极简指针',
    subtitle: '外置表盘槽 #1；高对比刻度圈与秒针平滑插值',
    tag: '待 32KB Flash',
    sizeHint: '约 6.4 KB',
    builtIn: false,
    accent: AppPalette.mintSoft,
  ),
  _DialSpec(
    kind: _DialKind.sportImu,
    name: '九轴运动仪表',
    subtitle: '外置表盘槽 #2；实时地平线倾角、罗盘航向与步数环',
    tag: '待 32KB Flash',
    sizeHint: '约 8.2 KB',
    builtIn: false,
    accent: _dialAmber,
  ),
  _DialSpec(
    kind: _DialKind.customPhoto,
    name: '自定义相册表盘',
    subtitle: '通过 0x28 协议传输 RGB565 图片至外置 Flash 0x3C00..0x7FFF',
    tag: '待 32KB Flash',
    sizeHint: '17 KB 预留区',
    builtIn: false,
    accent: Color(0xFF93C5FD),
  ),
];

class _DialsPageState extends State<DialsPage> {
  int _selected = 0;

  void _onActionPressed(_DialSpec spec) {
    final messenger = ScaffoldMessenger.of(context);
    messenger.hideCurrentSnackBar();
    if (spec.builtIn) {
      messenger.showSnackBar(
        const SnackBar(
          content: Text('“琥珀双色数字”已作为本地默认表盘运行于 AT32 内部 ROM。'),
        ),
      );
      return;
    }
    messenger.showSnackBar(
      SnackBar(
        content: Text(
          '“${spec.name}”已预留 UI 与协议槽位；接入 32KB 外挂 Flash (0x50) 后开放写入。',
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final controller = widget.controller;
    final scheme = Theme.of(context).colorScheme;
    final current = _dialPresets[_selected];

    return PageScroll(
      children: [
        PageHeader(
          title: '表盘',
          subtitle: '预设计界面 · 硬件加焊 32KB 外置 Flash 后解锁表盘下发与自定义相册',
          trailing: const StatusPill(
            label: '32KB 扩展预留',
            color: _dialAmber,
          ),
        ),
        const SizedBox(height: 22),
        LayoutBuilder(
          builder: (context, constraints) {
            final preview = _DialStageCard(
              spec: current,
              controller: controller,
              onInstall: () => _onActionPressed(current),
            );
            final rightColumn = Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                AppCard(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      const SectionHeader(
                        title: '表盘方案预览库',
                        subtitle: '点选查看效果；当前支持预览交互与分区容量规划',
                      ),
                      const SizedBox(height: 14),
                      for (var i = 0; i < _dialPresets.length; i++) ...[
                        if (i > 0) const SizedBox(height: 10),
                        _PresetTile(
                          spec: _dialPresets[i],
                          selected: i == _selected,
                          onTap: () => setState(() => _selected = i),
                        ),
                      ],
                    ],
                  ),
                ),
                const SizedBox(height: 16),
                _FlashPartitionCard(scheme: scheme),
              ],
            );

            if (constraints.maxWidth >= 880) {
              return Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Expanded(flex: 5, child: preview),
                  const SizedBox(width: 16),
                  Expanded(flex: 7, child: rightColumn),
                ],
              );
            }
            return Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                preview,
                const SizedBox(height: 16),
                rightColumn,
              ],
            );
          },
        ),
      ],
    );
  }
}

class _DialStageCard extends StatelessWidget {
  const _DialStageCard({
    required this.spec,
    required this.controller,
    required this.onInstall,
  });

  final _DialSpec spec;
  final WatchController controller;
  final VoidCallback onInstall;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final now = DateTime.now();
    final hh = now.hour.toString().padLeft(2, '0');
    final mm = now.minute.toString().padLeft(2, '0');
    final pct = controller.stepGoal > 0
        ? (controller.steps / controller.stepGoal).clamp(0.0, 1.0)
        : 0.0;

    return AppCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SectionHeader(
            title: spec.name,
            subtitle: spec.subtitle,
            trailing: StatusPill(
              label: spec.tag,
              color: spec.builtIn ? AppPalette.mint : _dialAmber,
            ),
          ),
          const SizedBox(height: 18),
          Center(
            child: Container(
              width: 196,
              padding: const EdgeInsets.all(10),
              decoration: BoxDecoration(
                color: const Color(0xFF0A0D0C),
                borderRadius: BorderRadius.circular(28),
                border: Border.all(color: AppPalette.line, width: 1.5),
                boxShadow: [
                  BoxShadow(
                    color: spec.accent.withValues(alpha: 0.14),
                    blurRadius: 24,
                    offset: const Offset(0, 10),
                  ),
                ],
              ),
              child: AspectRatio(
                aspectRatio: 128 / 160,
                child: ClipRRect(
                  borderRadius: BorderRadius.circular(18),
                  child: Container(
                    color: Colors.black,
                    padding: const EdgeInsets.all(12),
                    child: _buildDialCanvas(hh, mm, pct),
                  ),
                ),
              ),
            ),
          ),
          const SizedBox(height: 18),
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
            decoration: BoxDecoration(
              color: scheme.surfaceContainerHigh,
              borderRadius: BorderRadius.circular(14),
            ),
            child: Row(
              children: [
                Icon(Icons.sd_storage_outlined, size: 17, color: spec.accent),
                const SizedBox(width: 8),
                Expanded(
                  child: Text(
                    spec.builtIn
                        ? '已内置于主控 ROM，无需额外外置 Flash'
                        : '占用外置区：${spec.sizeHint} · 128×160 / 240×240 自适应',
                    style: TextStyle(
                      color: scheme.onSurfaceVariant,
                      fontSize: 11.5,
                    ),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 14),
          Wrap(
            spacing: 10,
            runSpacing: 10,
            children: [
              FilledButton.icon(
                onPressed: onInstall,
                icon: Icon(
                  spec.builtIn
                      ? Icons.check_circle_outline_rounded
                      : Icons.download_for_offline_outlined,
                  size: 18,
                ),
                label: Text(
                  spec.builtIn ? '当前已启用默认表盘' : '下发到手表（待接入 32KB）',
                ),
              ),
              OutlinedButton.icon(
                onPressed: spec.builtIn ? null : onInstall,
                icon: const Icon(Icons.photo_size_select_actual_outlined, size: 18),
                label: const Text('裁切自定义底图'),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _buildDialCanvas(String hh, String mm, double pct) {
    switch (spec.kind) {
      case _DialKind.digitalAmber:
        return Column(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            const Text(
              'WATCH · 128×160',
              style: TextStyle(
                color: Color(0xFF9FB0A8),
                fontSize: 9,
                letterSpacing: 0.8,
              ),
            ),
            Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(
                  hh,
                  style: const TextStyle(
                    color: Colors.white,
                    fontSize: 44,
                    fontWeight: FontWeight.w800,
                    height: 0.96,
                    fontFeatures: [FontFeature.tabularFigures()],
                  ),
                ),
                Text(
                  mm,
                  style: const TextStyle(
                    color: AppPalette.mint,
                    fontSize: 44,
                    fontWeight: FontWeight.w800,
                    height: 0.96,
                    fontFeatures: [FontFeature.tabularFigures()],
                  ),
                ),
              ],
            ),
            Column(
              children: [
                ClipRRect(
                  borderRadius: BorderRadius.circular(4),
                  child: LinearProgressIndicator(
                    value: pct,
                    minHeight: 5,
                    backgroundColor: const Color(0xFF20302A),
                    valueColor: const AlwaysStoppedAnimation(AppPalette.mint),
                  ),
                ),
                const SizedBox(height: 4),
                Text(
                  '${controller.steps} 步',
                  style: const TextStyle(
                    color: Color(0xFF9FB0A8),
                    fontSize: 9.5,
                  ),
                ),
              ],
            ),
          ],
        );
      case _DialKind.analogBam:
        return Column(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            const Text(
              'ANALOG DIAL',
              style: TextStyle(
                color: Color(0xFF8EA199),
                fontSize: 9,
                letterSpacing: 1,
              ),
            ),
            Expanded(
              child: Padding(
                padding: const EdgeInsets.all(6),
                child: Center(
                  child: WatchFace(
                    now: controller.now,
                    use24Hour: controller.use24Hour,
                  ),
                ),
              ),
            ),
            Text(
              '${controller.steps} 步 · $hh:$mm',
              style: const TextStyle(
                color: AppPalette.mintSoft,
                fontSize: 10,
                fontFeatures: [FontFeature.tabularFigures()],
              ),
            ),
          ],
        );
      case _DialKind.sportImu:
        return Column(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Text(
                  '${controller.headingDeg.round()}°',
                  style: const TextStyle(
                    color: _dialAmber,
                    fontSize: 13,
                    fontWeight: FontWeight.w700,
                  ),
                ),
                const Text(
                  '9DOF',
                  style: TextStyle(
                    color: AppPalette.mint,
                    fontSize: 10,
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ],
            ),
            Text(
              '$hh:$mm',
              style: const TextStyle(
                color: Colors.white,
                fontSize: 34,
                fontWeight: FontWeight.w800,
                fontFeatures: [FontFeature.tabularFigures()],
              ),
            ),
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
              decoration: BoxDecoration(
                color: const Color(0xFF161D1A),
                borderRadius: BorderRadius.circular(10),
                border: Border.all(color: const Color(0xFF28342E)),
              ),
              child: Column(
                children: [
                  Text(
                    'R ${controller.rollDeg.toStringAsFixed(1)}°  P ${controller.pitchDeg.toStringAsFixed(1)}°',
                    style: const TextStyle(
                      color: Color(0xFFC2D0C9),
                      fontSize: 9.5,
                    ),
                  ),
                  const SizedBox(height: 3),
                  Text(
                    '${controller.steps} / ${controller.stepGoal} 步',
                    style: const TextStyle(
                      color: AppPalette.mint,
                      fontSize: 9.5,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                ],
              ),
            ),
          ],
        );
      case _DialKind.customPhoto:
        return Container(
          decoration: BoxDecoration(
            gradient: const LinearGradient(
              begin: Alignment.topLeft,
              end: Alignment.bottomRight,
              colors: [
                Color(0xFF1F384A),
                Color(0xFF13222E),
                Color(0xFF0E161B),
              ],
            ),
            borderRadius: BorderRadius.circular(12),
          ),
          padding: const EdgeInsets.all(10),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Row(
                children: [
                  Icon(Icons.image_outlined, size: 13, color: Color(0xFF93C5FD)),
                  SizedBox(width: 4),
                  Text(
                    '0x3C00 FLASH',
                    style: TextStyle(color: Color(0xFFBFDBFE), fontSize: 9),
                  ),
                ],
              ),
              Text(
                '$hh\n$mm',
                style: const TextStyle(
                  color: Colors.white,
                  fontSize: 38,
                  fontWeight: FontWeight.w800,
                  height: 0.94,
                  fontFeatures: [FontFeature.tabularFigures()],
                ),
              ),
              const Text(
                '17 KB 静态图缓冲区',
                style: TextStyle(color: Color(0xFF93C5FD), fontSize: 9.5),
              ),
            ],
          ),
        );
    }
  }
}

class _PresetTile extends StatelessWidget {
  const _PresetTile({
    required this.spec,
    required this.selected,
    required this.onTap,
  });

  final _DialSpec spec;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(16),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 180),
        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
        decoration: BoxDecoration(
          color: selected
              ? spec.accent.withValues(alpha: 0.12)
              : scheme.surfaceContainerHigh.withValues(alpha: 0.65),
          borderRadius: BorderRadius.circular(16),
          border: Border.all(
            color: selected
                ? spec.accent.withValues(alpha: 0.65)
                : scheme.outlineVariant.withValues(alpha: 0.6),
            width: selected ? 1.4 : 1,
          ),
        ),
        child: Row(
          children: [
            Container(
              width: 10,
              height: 34,
              decoration: BoxDecoration(
                color: spec.accent,
                borderRadius: BorderRadius.circular(999),
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      Flexible(
                        child: Text(
                          spec.name,
                          style: const TextStyle(
                            fontSize: 14.5,
                            fontWeight: FontWeight.w700,
                          ),
                        ),
                      ),
                      const SizedBox(width: 8),
                      Container(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 8,
                          vertical: 2,
                        ),
                        decoration: BoxDecoration(
                          color: spec.accent.withValues(alpha: 0.16),
                          borderRadius: BorderRadius.circular(999),
                        ),
                        child: Text(
                          spec.tag,
                          style: TextStyle(
                            color: spec.accent,
                            fontSize: 10,
                            fontWeight: FontWeight.w600,
                          ),
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 4),
                  Text(
                    spec.subtitle,
                    style: TextStyle(
                      color: scheme.onSurfaceVariant,
                      fontSize: 11,
                      height: 1.4,
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(width: 8),
            Icon(
              selected
                  ? Icons.radio_button_checked_rounded
                  : Icons.radio_button_off_rounded,
              size: 20,
              color: selected ? spec.accent : scheme.onSurfaceVariant,
            ),
          ],
        ),
      ),
    );
  }
}

class _FlashPartitionCard extends StatelessWidget {
  const _FlashPartitionCard({required this.scheme});

  final ColorScheme scheme;

  @override
  Widget build(BuildContext context) {
    return AppCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const SectionHeader(
            title: '32KB 外置 I2C Flash 分区规划 (0x50)',
            subtitle: '硬件接口 PA9/PA10，总容量 32,768 字节；后半区专用于表盘与图流',
          ),
          const SizedBox(height: 14),
          ClipRRect(
            borderRadius: BorderRadius.circular(8),
            child: SizedBox(
              height: 12,
              child: Row(
                children: [
                  Expanded(
                    flex: 2,
                    child: Container(color: const Color(0xFF4B6359)),
                  ),
                  const SizedBox(width: 2),
                  Expanded(
                    flex: 9,
                    child: Container(color: const Color(0xFF3E7B68)),
                  ),
                  const SizedBox(width: 2),
                  Expanded(
                    flex: 4,
                    child: Container(color: const Color(0xFF5A8D9E)),
                  ),
                  const SizedBox(width: 2),
                  Expanded(
                    flex: 17,
                    child: Container(color: _dialAmber),
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(height: 14),
          const _PartitionRow(
            color: Color(0xFF4B6359),
            range: '0x0000 – 0x07FF',
            title: '设置 / 运动参数持久化',
            size: '2 KB',
          ),
          const SizedBox(height: 8),
          const _PartitionRow(
            color: Color(0xFF3E7B68),
            range: '0x0800 – 0x2CFF',
            title: '消息点阵缓存（LINK_EXT_OFF_MSG）',
            size: '9 KB',
          ),
          const SizedBox(height: 8),
          const _PartitionRow(
            color: Color(0xFF5A8D9E),
            range: '0x2D00 – 0x3BFF',
            title: '歌词与歌名点阵（LINK_EXT_OFF_MEDIA）',
            size: '3.75 KB',
          ),
          const SizedBox(height: 8),
          const _PartitionRow(
            color: _dialAmber,
            range: '0x3C00 – 0x7FFF',
            title: '表盘资产与自定义图流预留区（LINK_EXT_OFF_IMG）',
            size: '17 KB',
          ),
        ],
      ),
    );
  }
}

class _PartitionRow extends StatelessWidget {
  const _PartitionRow({
    required this.color,
    required this.range,
    required this.title,
    required this.size,
  });

  final Color color;
  final String range;
  final String title;
  final String size;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Row(
      children: [
        Container(
          width: 10,
          height: 10,
          decoration: BoxDecoration(
            color: color,
            borderRadius: BorderRadius.circular(3),
          ),
        ),
        const SizedBox(width: 8),
        SizedBox(
          width: 112,
          child: Text(
            range,
            style: TextStyle(
              color: scheme.onSurfaceVariant,
              fontSize: 10.5,
              fontFeatures: const [FontFeature.tabularFigures()],
            ),
          ),
        ),
        Expanded(
          child: Text(
            title,
            style: const TextStyle(fontSize: 11.5),
          ),
        ),
        Text(
          size,
          style: TextStyle(
            color: scheme.onSurfaceVariant,
            fontSize: 11,
            fontWeight: FontWeight.w600,
          ),
        ),
      ],
    );
  }
}
