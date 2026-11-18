import 'dart:io';
import 'dart:math' as math;

import 'package:material_ui/material_ui.dart';

import '../state/watch_controller.dart';
import '../theme/app_theme.dart';

class AppCard extends StatelessWidget {
  const AppCard({
    super.key,
    required this.child,
    this.padding = const EdgeInsets.all(20),
    this.color,
    this.gradient,
    this.borderColor,
    this.onTap,
    this.radius = 20,
  });

  final Widget child;
  final EdgeInsetsGeometry padding;
  final Color? color;
  final Gradient? gradient;
  final Color? borderColor;
  final VoidCallback? onTap;
  final double radius;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final borderRadius = BorderRadius.circular(radius);
    return Material(
      color: Colors.transparent,
      child: Ink(
        decoration: BoxDecoration(
          color: gradient == null ? color ?? scheme.surfaceContainer : null,
          gradient: gradient,
          borderRadius: borderRadius,
          border: Border.all(
            color: borderColor ?? scheme.outlineVariant.withValues(alpha: 0.78),
          ),
        ),
        child: InkWell(
          onTap: onTap,
          borderRadius: borderRadius,
          child: Padding(padding: padding, child: child),
        ),
      ),
    );
  }
}

class PageScroll extends StatelessWidget {
  const PageScroll({
    super.key,
    required this.children,
    this.maxWidth = 1220.0,
  });

  final List<Widget> children;
  final double maxWidth;

  @override
  Widget build(BuildContext context) {
    final size = MediaQuery.sizeOf(context);
    final padding = size.width < 560
        ? 16.0
        : (size.width < 900 ? 22.0 : 28.0);
    final portraitPhone = size.width < 760 && size.height > size.width;
    return SingleChildScrollView(
      padding: EdgeInsets.fromLTRB(
        padding,
        portraitPhone ? 18 : 24,
        padding,
        portraitPhone ? 112 : 32,
      ),
      child: LayoutBuilder(
        builder: (context, constraints) {
          final available = constraints.maxWidth;
          final width = available.isFinite
              ? math.min(available, maxWidth)
              : maxWidth;
          return Center(
            child: SizedBox(
              width: width,
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: children,
              ),
            ),
          );
        },
      ),
    );
  }
}

/// 等高并排双栏：宽屏下通过 `IntrinsicHeight` + `stretch` 保证左右卡片底边严格齐平，
/// 窄屏下自动退化为等距纵向堆叠。注意：内部子组件不得包含 `LayoutBuilder`。
class AlignedPair extends StatelessWidget {
  const AlignedPair({
    super.key,
    required this.wide,
    required this.left,
    required this.right,
    this.gap = 16,
    this.leftFlex = 1,
    this.rightFlex = 1,
  });

  final bool wide;
  final Widget left;
  final Widget right;
  final double gap;
  final int leftFlex;
  final int rightFlex;

  @override
  Widget build(BuildContext context) {
    if (!wide) {
      return Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [left, SizedBox(height: gap), right],
      );
    }
    return IntrinsicHeight(
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Expanded(flex: leftFlex, child: left),
          SizedBox(width: gap),
          Expanded(flex: rightFlex, child: right),
        ],
      ),
    );
  }
}

/// 均衡双栏瀑布布局：用于多张高度各异的卡片页面，避免 `Wrap` 造成的阶梯式死白留空。
class BalancedTwoColumn extends StatelessWidget {
  const BalancedTwoColumn({
    super.key,
    required this.wide,
    required this.leftChildren,
    required this.rightChildren,
    this.narrowOrder,
    this.gap = 16,
    this.leftFlex = 1,
    this.rightFlex = 1,
  });

  final bool wide;
  final List<Widget> leftChildren;
  final List<Widget> rightChildren;
  final List<Widget>? narrowOrder;
  final double gap;
  final int leftFlex;
  final int rightFlex;

  List<Widget> _spaced(List<Widget> items) {
    final out = <Widget>[];
    for (var i = 0; i < items.length; i++) {
      if (i > 0) out.add(SizedBox(height: gap));
      out.add(items[i]);
    }
    return out;
  }

  @override
  Widget build(BuildContext context) {
    if (!wide) {
      final items = narrowOrder ?? <Widget>[...leftChildren, ...rightChildren];
      return Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: _spaced(items),
      );
    }
    return Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Expanded(
          flex: leftFlex,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: _spaced(leftChildren),
          ),
        ),
        SizedBox(width: gap),
        Expanded(
          flex: rightFlex,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: _spaced(rightChildren),
          ),
        ),
      ],
    );
  }
}

class PageHeader extends StatelessWidget {
  const PageHeader({
    super.key,
    required this.title,
    required this.subtitle,
    this.trailing,
  });

  final String title;
  final String subtitle;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    final textTheme = Theme.of(context).textTheme;
    return LayoutBuilder(
      builder: (context, constraints) {
        final narrow = constraints.maxWidth < 640;
        final titleBlock = Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              title,
              style: (narrow ? textTheme.headlineSmall : textTheme.headlineMedium)
                  ?.copyWith(
                    fontWeight: FontWeight.w700,
                    letterSpacing: narrow ? -0.4 : -0.6,
                  ),
            ),
            const SizedBox(height: 4),
            Text(
              subtitle,
              maxLines: narrow ? 2 : 3,
              overflow: TextOverflow.ellipsis,
              style: textTheme.bodyMedium?.copyWith(
                color: Theme.of(context).colorScheme.onSurfaceVariant,
                fontSize: narrow ? 12.5 : null,
                height: 1.35,
              ),
            ),
          ],
        );
        if (trailing == null) return titleBlock;
        if (narrow) {
          return Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Expanded(child: titleBlock),
              const SizedBox(width: 10),
              ConstrainedBox(
                constraints: BoxConstraints(
                  maxWidth: constraints.maxWidth * 0.44,
                ),
                child: trailing!,
              ),
            ],
          );
        }
        return Row(
          children: [
            Expanded(child: titleBlock),
            const SizedBox(width: 16),
            trailing!,
          ],
        );
      },
    );
  }
}

class StatusPill extends StatelessWidget {
  const StatusPill({
    super.key,
    required this.label,
    this.color,
    this.icon,
    this.onTap,
  });

  final String label;
  final Color? color;
  final IconData? icon;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final accent = color ?? context.appColors.accent;
    final body = Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5.5),
      decoration: BoxDecoration(
        color: accent.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(999),
        border: Border.all(color: accent.withValues(alpha: 0.24)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (icon != null)
            Icon(icon, size: 13, color: accent)
          else
            Container(
              width: 6,
              height: 6,
              decoration: BoxDecoration(color: accent, shape: BoxShape.circle),
            ),
          const SizedBox(width: 6),
          Flexible(
            child: Text(
              label,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                color: accent,
                fontSize: 11,
                fontWeight: FontWeight.w700,
                letterSpacing: 0.2,
              ),
            ),
          ),
        ],
      ),
    );
    if (onTap == null) return body;
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(999),
      child: body,
    );
  }
}

class SectionHeader extends StatelessWidget {
  const SectionHeader({
    super.key,
    required this.title,
    this.subtitle,
    this.trailing,
  });

  final String title;
  final String? subtitle;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                title,
                style: Theme.of(context).textTheme.titleMedium?.copyWith(
                  fontWeight: FontWeight.w700,
                ),
              ),
              if (subtitle != null) ...[
                const SizedBox(height: 3),
                Text(
                  subtitle!,
                  style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: Theme.of(context).colorScheme.onSurfaceVariant,
                  ),
                ),
              ],
            ],
          ),
        ),
        if (trailing != null) ...[const SizedBox(width: 8), trailing!],
      ],
    );
  }
}

class AppBrand extends StatelessWidget {
  const AppBrand({super.key, this.compact = false, this.textOpacity = 1});

  final bool compact;
  final double textOpacity;

  @override
  Widget build(BuildContext context) {
    final mark = Container(
      width: 42,
      height: 42,
      decoration: BoxDecoration(
        gradient: const LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [AppPalette.mintSoft, AppPalette.mint],
        ),
        borderRadius: BorderRadius.circular(14),
      ),
      child: const Icon(Icons.watch_outlined, color: Color(0xFF0A1B14)),
    );
    if (compact) return Center(child: mark);
    return Row(
      children: [
        mark,
        const SizedBox(width: 12),
        Expanded(
          child: Opacity(
            opacity: textOpacity,
            child: Transform.translate(
              offset: Offset(8 * (1 - textOpacity), 0),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Text(
                    'AT32',
                    style: TextStyle(fontSize: 17, fontWeight: FontWeight.w800),
                  ),
                  Text(
                    'WATCH STUDIO',
                    style: TextStyle(
                      fontSize: 9,
                      fontWeight: FontWeight.w600,
                      letterSpacing: 1.5,
                      color: Theme.of(context).colorScheme.primary,
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ],
    );
  }
}

void showPrototypeMessage(BuildContext context, String feature) {
  final messenger = ScaffoldMessenger.of(context);
  messenger.hideCurrentSnackBar();
  messenger.showSnackBar(
    SnackBar(
      content: Text('$feature将在传输层接入后开放，当前为界面预览。'),
      duration: const Duration(seconds: 2),
    ),
  );
}

/// 连接/断开统一入口：有会话时走真实链路，无会话时保持界面预览行为。
Future<void> setWatchConnection(
  BuildContext context,
  WatchController controller, {
  required bool connect,
}) async {
  final messenger = ScaffoldMessenger.of(context);
  messenger.hideCurrentSnackBar();

  if (connect) {
    if (controller.connected) return;
    if (!controller.hasHardware) {
      controller.setPreviewConnected(true);
      messenger.showSnackBar(
        const SnackBar(
          content: Text('仅切换界面连接态，未连接真实手表。'),
          duration: Duration(seconds: 2),
        ),
      );
      return;
    }
    messenger.showSnackBar(
      SnackBar(content: Text('正在连接${controller.transportName}…')),
    );
    final error = await controller.connect();
    messenger.hideCurrentSnackBar();
    messenger.showSnackBar(
      SnackBar(
        content: Text(
          error == null ? '已连接${controller.transportName}。' : '连接失败：$error',
        ),
        duration: const Duration(seconds: 4),
      ),
    );
    return;
  }

  if (!controller.connected) return;
  await controller.disconnect();
  messenger.hideCurrentSnackBar();
  messenger.showSnackBar(
    const SnackBar(content: Text('已断开手表链路。'), duration: Duration(seconds: 2)),
  );
}

Future<void> toggleWatchConnection(
  BuildContext context,
  WatchController controller,
) {
  return setWatchConnection(
    context,
    controller,
    connect: !controller.connected,
  );
}

/// 页面底部的小字权限/疑难解答区，音乐页与消息页共用。
class TroubleshootingCard extends StatelessWidget {
  const TroubleshootingCard({
    super.key,
    required this.controller,
    required this.hint,
    this.showMedia = false,
  });

  final WatchController controller;
  final String hint;
  final bool showMedia;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final colors = context.appColors;
    final android = Platform.isAndroid;
    final mediaText = controller.hasMedia
        ? [
            controller.mediaTitle,
            if (controller.mediaArtist.isNotEmpty) controller.mediaArtist,
          ].join(' · ')
        : '未检测到';
    return AppCard(
      padding: const EdgeInsets.fromLTRB(18, 16, 18, 16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                width: 28,
                height: 28,
                decoration: BoxDecoration(
                  color: colors.accentContainer,
                  borderRadius: BorderRadius.circular(8),
                ),
                child: Icon(
                  Icons.help_outline_rounded,
                  size: 16,
                  color: colors.accent,
                ),
              ),
              const SizedBox(width: 10),
              const Expanded(
                child: Text(
                  '疑难解答 · 当前权限',
                  style: TextStyle(fontSize: 13.5, fontWeight: FontWeight.w700),
                ),
              ),
              if (android)
                TextButton(
                  onPressed: controller.refreshAndroidStatus,
                  style: TextButton.styleFrom(
                    padding: const EdgeInsets.symmetric(horizontal: 8),
                    minimumSize: const Size(0, 28),
                    tapTargetSize: MaterialTapTargetSize.shrinkWrap,
                  ),
                  child: const Text('刷新', style: TextStyle(fontSize: 11.5)),
                ),
            ],
          ),
          const SizedBox(height: 10),
          if (android) ...[
            _StatusLine(
              icon: Icons.notifications_active_outlined,
              label: '通知读取权限',
              value: controller.notificationAccess ? '已授权' : '未授权',
              ok: controller.notificationAccess,
            ),
            _StatusLine(
              icon: Icons.battery_saver_outlined,
              label: '电池优化白名单',
              value: controller.batteryExempt ? '已加入' : '未加入',
              ok: controller.batteryExempt,
            ),
          ] else
            const _StatusLine(
              icon: Icons.notifications_off_outlined,
              label: '系统通知转发',
              value: 'Windows 暂未接入',
              ok: false,
            ),
          _StatusLine(
            icon: Icons.bluetooth_rounded,
            label: '手表连接',
            value: controller.connected ? 'SPP 已连接' : '未连接',
            ok: controller.connected,
          ),
          if (showMedia)
            _StatusLine(
              icon: Icons.music_note_outlined,
              label: '系统媒体',
              value: mediaText,
              ok: controller.hasMedia,
            )
          else if (android)
            _StatusLine(
              icon: Icons.inbox_outlined,
              label: '已缓存通知',
              value:
                  '${controller.cachedNotifications} 条（最多 ${controller.extPoolMsgMax} 条）',
              ok: controller.cachedNotifications > 0,
            )
          else
            _StatusLine(
              icon: Icons.inbox_outlined,
              label: '持久化消息池',
              value: controller.extFlashPresent
                  ? '${controller.extPoolMsgCount}/${controller.extPoolMsgMax} 条'
                  : '${controller.localMessages.length}/${controller.maxMessages} 条',
              ok:
                  controller.extPoolMsgCount > 0 ||
                  controller.localMessages.isNotEmpty,
            ),
          if (android &&
              (!controller.notificationAccess ||
                  !controller.batteryExempt)) ...[
            const SizedBox(height: 8),
            Wrap(
              spacing: 8,
              runSpacing: 6,
              children: [
                if (!controller.notificationAccess)
                  OutlinedButton(
                    onPressed: () async {
                      await controller.openNotificationAccess();
                      await Future<void>.delayed(const Duration(seconds: 2));
                      await controller.refreshAndroidStatus();
                    },
                    style: OutlinedButton.styleFrom(
                      minimumSize: const Size(0, 32),
                      padding: const EdgeInsets.symmetric(horizontal: 12),
                      textStyle: const TextStyle(fontSize: 11.5),
                    ),
                    child: const Text('去授权通知读取'),
                  ),
                if (!controller.batteryExempt)
                  OutlinedButton(
                    onPressed: () async {
                      await controller.openBatterySettings();
                      await Future<void>.delayed(const Duration(seconds: 2));
                      await controller.refreshAndroidStatus();
                    },
                    style: OutlinedButton.styleFrom(
                      minimumSize: const Size(0, 32),
                      padding: const EdgeInsets.symmetric(horizontal: 12),
                      textStyle: const TextStyle(fontSize: 11.5),
                    ),
                    child: const Text('加入电池白名单'),
                  ),
              ],
            ),
          ],
          const SizedBox(height: 10),
          Container(
            width: double.infinity,
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 9),
            decoration: BoxDecoration(
              color: colors.insetSurface,
              borderRadius: BorderRadius.circular(10),
            ),
            child: Text(
              hint,
              style: TextStyle(
                color: scheme.onSurfaceVariant,
                fontSize: 11,
                height: 1.45,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _StatusLine extends StatelessWidget {
  const _StatusLine({
    required this.icon,
    required this.label,
    required this.value,
    required this.ok,
  });

  final IconData icon;
  final String label;
  final String value;
  final bool ok;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final colors = context.appColors;
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 3.5),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Padding(
            padding: const EdgeInsets.only(top: 2),
            child: Icon(icon, size: 13.5, color: scheme.onSurfaceVariant),
          ),
          const SizedBox(width: 8),
          SizedBox(
            width: 96,
            child: Text(
              label,
              style: TextStyle(
                color: scheme.onSurfaceVariant,
                fontSize: 11.5,
              ),
            ),
          ),
          Expanded(
            child: Text(
              value,
              maxLines: 2,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                color: ok ? colors.accent : colors.warning,
                fontSize: 11.5,
                fontWeight: FontWeight.w600,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
