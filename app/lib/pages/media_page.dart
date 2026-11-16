import 'dart:convert';
import 'dart:io';

import 'package:material_ui/material_ui.dart';

import '../state/watch_controller.dart';
import '../theme/app_theme.dart';
import '../widgets/common.dart';

/// 媒体与通知合并页：手机系统媒体控制、歌词下行演示、通知路由与 0x02 手动消息槽。
class MediaPage extends StatefulWidget {
  const MediaPage({super.key, required this.controller});

  final WatchController controller;

  @override
  State<MediaPage> createState() => _MediaPageState();
}

class _MediaPageState extends State<MediaPage> {
  late final TextEditingController _lyrics;
  late final TextEditingController _message;
  int _selectedSlot = 0;

  @override
  void initState() {
    super.initState();
    _lyrics = TextEditingController(
      text: '穿过晚风与灯火\n把时间慢放成一首歌\n在城市睡着以前\n留一点安静给自己',
    );
    _message = TextEditingController(
      text: widget.controller.messageSlots.first,
    );
    if (Platform.isAndroid) {
      // 进入页面时回读原生服务持久化的规则与通知栏出现过的应用。
      widget.controller.refreshMessageRules();
    }
  }

  @override
  void dispose() {
    _lyrics.dispose();
    _message.dispose();
    super.dispose();
  }

  void _selectSlot(int index) {
    setState(() {
      _selectedSlot = index;
      _message.text = widget.controller.messageSlots[index];
      _message.selection = TextSelection.collapsed(
        offset: _message.text.length,
      );
    });
  }

  Future<void> _syncLyrics() async {
    final messenger = ScaffoldMessenger.of(context);
    if (!widget.controller.connected) {
      messenger.hideCurrentSnackBar();
      messenger.showSnackBar(const SnackBar(content: Text('请先连接手表，再同步歌词。')));
      return;
    }
    messenger.showSnackBar(const SnackBar(content: Text('正在同步歌词…')));
    final sent = await widget.controller.syncLyrics(_lyrics.text);
    messenger.hideCurrentSnackBar();
    messenger.showSnackBar(
      SnackBar(content: Text(sent > 0 ? '已同步 $sent 行歌词。' : '同步失败：手表没有回应。')),
    );
  }

  Future<void> _sendMessage() async {
    final messenger = ScaffoldMessenger.of(context);
    if (!widget.controller.connected) {
      messenger.hideCurrentSnackBar();
      messenger.showSnackBar(const SnackBar(content: Text('请先连接手表，再发送消息。')));
      return;
    }
    final ok = await widget.controller.sendMessage(
      _selectedSlot,
      _message.text,
    );
    messenger.hideCurrentSnackBar();
    messenger.showSnackBar(
      SnackBar(
        content: Text(ok ? '已写入消息槽 ${_selectedSlot + 1}。' : '发送失败：手表没有回应。'),
      ),
    );
  }

  Future<void> _pushPersistentMessage() async {
    final messenger = ScaffoldMessenger.of(context);
    final text = _message.text.trim();
    if (text.isEmpty) return;
    final ok = await widget.controller.pushRichMessage(
      app: Platform.isWindows ? 'PC 桌面消息' : '手机消息',
      title: '消息槽 ${_selectedSlot + 1}',
      text: text,
    );
    if (!mounted) return;
    messenger.hideCurrentSnackBar();
    messenger.showSnackBar(
      SnackBar(
        content: Text(
          ok
              ? '已置顶写入手表 AT24C256 动态 ROM（断电不丢失）。'
              : '未连接手表：已存入本地历史队列，连接后可重发。',
        ),
      ),
    );
  }

  Widget _composer(BuildContext context) {
    final bytes = utf8.encode(_message.text).length;
    final scheme = Theme.of(context).colorScheme;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        DropdownButtonFormField<int>(
          initialValue: _selectedSlot,
          decoration: const InputDecoration(
            labelText: '消息槽',
            prefixIcon: Icon(Icons.inbox_outlined),
          ),
          items: List.generate(
            3,
            (index) => DropdownMenuItem(
              value: index,
              child: Text('消息槽 ${index + 1}'),
            ),
          ),
          onChanged: (value) {
            if (value != null) _selectSlot(value);
          },
        ),
        const SizedBox(height: 14),
        TextField(
          controller: _message,
          onChanged: (_) => setState(() {}),
          minLines: 4,
          maxLines: 6,
          decoration: const InputDecoration(
            hintText: '输入要显示在手表上的内容…',
            alignLabelWithHint: true,
          ),
        ),
        const SizedBox(height: 10),
        Row(
          children: [
            Icon(
              Icons.data_usage_rounded,
              size: 15,
              color: bytes > 23 ? scheme.onSurface : scheme.primary,
            ),
            const SizedBox(width: 6),
            Text(
              '$bytes / 23 UTF-8 字节',
              style: TextStyle(
                color: bytes > 23 ? scheme.onSurface : scheme.onSurfaceVariant,
                fontSize: 11,
              ),
            ),
            const Spacer(),
            Text(
              '单条协议上限',
              style: TextStyle(color: scheme.onSurfaceVariant, fontSize: 10),
            ),
          ],
        ),
        const SizedBox(height: 18),
        Wrap(
          spacing: 10,
          runSpacing: 10,
          children: [
            FilledButton.icon(
              onPressed: () {
                widget.controller.setMessageSlot(_selectedSlot, _message.text);
                ScaffoldMessenger.of(context).showSnackBar(
                  SnackBar(content: Text('已保存到本地消息槽 ${_selectedSlot + 1}。')),
                );
              },
              icon: const Icon(Icons.save_outlined, size: 18),
              label: const Text('保存预览'),
            ),
            OutlinedButton.icon(
              onPressed: _sendMessage,
              icon: const Icon(Icons.send_outlined, size: 17),
              label: const Text('0x02 短信槽'),
            ),
            FilledButton.tonalIcon(
              onPressed: _pushPersistentMessage,
              icon: const Icon(Icons.memory_rounded, size: 17),
              label: const Text('写入 AT24C256 持久化消息'),
            ),
          ],
        ),
      ],
    );
  }

  @override
  Widget build(BuildContext context) {
    final controller = widget.controller;
    final scheme = Theme.of(context).colorScheme;
    final missingAccess = Platform.isAndroid && !controller.notificationAccess;
    return PageScroll(
      children: [
        PageHeader(
          title: '媒体',
          subtitle: Platform.isAndroid
              ? '音乐播放与切歌控制、歌词同步、系统通知路由与消息槽'
              : '歌词下行、手表媒体按键控制与消息槽手动测试发送',
          trailing: StatusPill(
            label: controller.hasMedia
                ? (controller.playing ? '手机播放中' : '手机已暂停')
                : (controller.connected ? '已连接 · 等待媒体' : '未连接'),
            color: controller.connected ? null : scheme.onSurfaceVariant,
          ),
        ),
        const SizedBox(height: 22),
        LayoutBuilder(
          builder: (context, constraints) {
            final player = _PlayerCard(
              controller: controller,
              missingAccess: missingAccess,
            );
            final lyrics = AppCard(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  SectionHeader(
                    title: '歌词演示 · 晚风手记',
                    subtitle: controller.connected
                        ? '按行发送，每帧 ≤27 字节'
                        : '本地演示歌词，连接后可按行同步',
                    trailing: Icon(
                      Icons.lyrics_outlined,
                      color: scheme.primary,
                    ),
                  ),
                  const SizedBox(height: 18),
                  TextField(
                    controller: _lyrics,
                    minLines: 7,
                    maxLines: 10,
                    style: const TextStyle(height: 1.8),
                    decoration: const InputDecoration(
                      hintText: '粘贴要演示的歌词…',
                      alignLabelWithHint: true,
                    ),
                  ),
                  const SizedBox(height: 18),
                  Wrap(
                    spacing: 10,
                    runSpacing: 10,
                    children: [
                      FilledButton.icon(
                        onPressed: () {
                          ScaffoldMessenger.of(context).showSnackBar(
                            const SnackBar(content: Text('歌词已保存到本次界面预览。')),
                          );
                        },
                        icon: const Icon(Icons.save_outlined, size: 18),
                        label: const Text('保存预览'),
                      ),
                      OutlinedButton.icon(
                        onPressed: _syncLyrics,
                        icon: const Icon(Icons.sync_rounded, size: 18),
                        label: const Text('同步到手表'),
                      ),
                    ],
                  ),
                ],
              ),
            );
            final rules = _NotificationRules(
              controller: controller,
              enabled: Platform.isAndroid,
            );
            final composerCard = AppCard(
              child: ExpansionTile(
                tilePadding: EdgeInsets.zero,
                childrenPadding: const EdgeInsets.only(top: 8),
                shape: const Border(),
                leading: Icon(Icons.edit_note_rounded, color: scheme.primary),
                title: const Text(
                  '手动发送测试消息',
                  style: TextStyle(fontSize: 16, fontWeight: FontWeight.w700),
                ),
                subtitle: Text(
                  '走 0x02 文本槽，不经过系统通知',
                  style: TextStyle(color: scheme.onSurfaceVariant, fontSize: 11),
                ),
                children: [_composer(context)],
              ),
            );
            if (constraints.maxWidth >= 850) {
              return Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Expanded(flex: 5, child: player),
                      const SizedBox(width: 16),
                      Expanded(flex: 6, child: lyrics),
                    ],
                  ),
                  const SizedBox(height: 16),
                  Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Expanded(flex: 6, child: rules),
                      const SizedBox(width: 16),
                      Expanded(flex: 5, child: composerCard),
                    ],
                  ),
                ],
              );
            }
            return Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                player,
                const SizedBox(height: 16),
                lyrics,
                const SizedBox(height: 16),
                rules,
                const SizedBox(height: 16),
                composerCard,
              ],
            );
          },
        ),
        const SizedBox(height: 16),
        AppCard(
          padding: const EdgeInsets.all(16),
          child: Text(
            '手表按键通过 0x10 控制帧回传播放 / 暂停 / 上下首；系统通知按路由规则筛选后，'
            '在手表打开消息应用时按点阵传给手表显示。',
            style: TextStyle(
              color: scheme.onSurfaceVariant,
              fontSize: 12,
              height: 1.5,
            ),
          ),
        ),
        const SizedBox(height: 16),
        TroubleshootingCard(
          controller: controller,
          showMedia: Platform.isAndroid,
          hint: Platform.isAndroid
              ? '媒体控制与通知同步都依赖“通知读取权限”。'
                    '打开手表一级消息页才回传应用名列表，选中后才渲染整屏详情。'
              : 'Windows 端暂未接入系统通知，通知路由只影响 Android；'
                    '可使用上方歌词同步和“手动发送测试消息”验证手表联动。',
        ),
      ],
    );
  }
}

class _PlayerCard extends StatefulWidget {
  const _PlayerCard({
    required this.controller,
    required this.missingAccess,
  });

  final WatchController controller;
  final bool missingAccess;

  @override
  State<_PlayerCard> createState() => _PlayerCardState();
}

class _PlayerCardState extends State<_PlayerCard> {
  double? _drag;

  @override
  Widget build(BuildContext context) {
    final controller = widget.controller;
    final scheme = Theme.of(context).colorScheme;
    final hasMedia = controller.hasMedia;
    final duration = controller.mediaDuration;
    final position = (_drag ?? controller.mediaPosition)
        .clamp(0.0, duration > 0 ? duration : 0.0)
        .toDouble();
    final subtitle = hasMedia
        ? (controller.mediaArtist.isEmpty ? '未知歌手' : controller.mediaArtist)
        : widget.missingAccess
        ? '通知读取未授权，无法获取手机歌曲'
        : (controller.connected ? '在手机上打开音乐 App 播放' : '手表未连接');

    return AppCard(
      child: Column(
        children: [
          Container(
            width: 174,
            height: 174,
            decoration: BoxDecoration(
              gradient: const RadialGradient(
                colors: [
                  Color(0xFF314B40),
                  Color(0xFF17221E),
                  Color(0xFF101615),
                ],
              ),
              borderRadius: BorderRadius.circular(28),
              border: Border.all(color: AppPalette.line),
            ),
            child: Stack(
              alignment: Alignment.center,
              children: [
                Container(
                  width: 116,
                  height: 116,
                  decoration: BoxDecoration(
                    shape: BoxShape.circle,
                    border: Border.all(
                      color: AppPalette.mint.withValues(alpha: 0.28),
                      width: 14,
                    ),
                  ),
                ),
                Container(
                  width: 62,
                  height: 62,
                  decoration: const BoxDecoration(
                    color: AppPalette.mint,
                    shape: BoxShape.circle,
                  ),
                  child: const Icon(
                    Icons.music_note_rounded,
                    color: Color(0xFF0A1B14),
                    size: 30,
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 22),
          Text(
            hasMedia ? controller.mediaTitle : '未检测到播放',
            maxLines: 2,
            overflow: TextOverflow.ellipsis,
            textAlign: TextAlign.center,
            style: const TextStyle(fontSize: 23, fontWeight: FontWeight.w700),
          ),
          const SizedBox(height: 5),
          Text(
            subtitle,
            maxLines: 2,
            overflow: TextOverflow.ellipsis,
            textAlign: TextAlign.center,
            style: TextStyle(color: scheme.onSurfaceVariant, fontSize: 12),
          ),
          const SizedBox(height: 18),
          Slider(
            value: duration > 0
                ? (position / duration).clamp(0.0, 1.0).toDouble()
                : 0,
            onChanged: duration > 0
                ? (value) => setState(() => _drag = value * duration)
                : null,
            onChangeEnd: duration > 0
                ? (value) {
                    final target = value * duration;
                    setState(() => _drag = null);
                    controller.seekMedia(target);
                  }
                : null,
          ),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 8),
            child: Row(
              children: [
                Text(
                  _time(position),
                  style: TextStyle(color: scheme.onSurfaceVariant, fontSize: 10),
                ),
                const Spacer(),
                Text(
                  _time(duration),
                  style: TextStyle(color: scheme.onSurfaceVariant, fontSize: 10),
                ),
              ],
            ),
          ),
          const SizedBox(height: 10),
          Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              IconButton(
                onPressed: hasMedia
                    ? () => controller.skipMedia(next: false)
                    : null,
                tooltip: '上一首',
                iconSize: 30,
                icon: const Icon(Icons.skip_previous_rounded),
              ),
              const SizedBox(width: 18),
              IconButton.filled(
                onPressed: hasMedia
                    ? () => controller.setPlayState(!controller.playing)
                    : null,
                tooltip: controller.playing ? '暂停手机播放' : '播放手机媒体',
                iconSize: 34,
                style: IconButton.styleFrom(minimumSize: const Size(64, 64)),
                icon: Icon(
                  controller.playing
                      ? Icons.pause_rounded
                      : Icons.play_arrow_rounded,
                ),
              ),
              const SizedBox(width: 18),
              IconButton(
                onPressed: hasMedia
                    ? () => controller.skipMedia(next: true)
                    : null,
                tooltip: '下一首',
                iconSize: 30,
                icon: const Icon(Icons.skip_next_rounded),
              ),
            ],
          ),
          const SizedBox(height: 8),
          StatusPill(
            label: hasMedia
                ? (controller.playing ? '手机播放中' : '手机已暂停')
                : '未检测到系统媒体',
            color: hasMedia ? scheme.primary : scheme.onSurfaceVariant,
          ),
        ],
      ),
    );
  }

  static String _time(double seconds) {
    final value = seconds.isFinite ? seconds.round() : 0;
    final minutes = value ~/ 60;
    final rest = value % 60;
    return '${minutes.toString().padLeft(2, '0')}:'
        '${rest.toString().padLeft(2, '0')}';
  }
}

/// 通知路由规则：全部 / 仅选中应用、发送条数、重复应用合并。
class _NotificationRules extends StatelessWidget {
  const _NotificationRules({required this.controller, required this.enabled});

  final WatchController controller;
  final bool enabled;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return AppCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SectionHeader(
            title: '通知路由',
            subtitle: '手表只显示应用名；你选中某条后才把正文渲染成整屏详情',
            trailing: TextButton.icon(
              onPressed: enabled ? controller.refreshMessageRules : null,
              icon: const Icon(Icons.refresh_rounded, size: 16),
              label: const Text('刷新应用', style: TextStyle(fontSize: 11)),
            ),
          ),
          if (!enabled) ...[
            const SizedBox(height: 12),
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
              decoration: BoxDecoration(
                color: scheme.surfaceContainerHigh,
                borderRadius: BorderRadius.circular(12),
              ),
              child: Row(
                children: [
                  Icon(
                    Icons.info_outline_rounded,
                    size: 15,
                    color: scheme.onSurfaceVariant,
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      'Windows 端暂未接入系统通知，下面的规则与开关只影响 Android。',
                      style: TextStyle(
                        color: scheme.onSurfaceVariant,
                        fontSize: 10.5,
                        height: 1.45,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ],
          const SizedBox(height: 14),
          SegmentedButton<String>(
            segments: const [
              ButtonSegment(
                value: 'all',
                icon: Icon(Icons.all_inbox_rounded),
                label: Text('全部通知'),
              ),
              ButtonSegment(
                value: 'selected',
                icon: Icon(Icons.filter_alt_outlined),
                label: Text('仅选中应用'),
              ),
            ],
            selected: {controller.routeMode},
            showSelectedIcon: false,
            onSelectionChanged: enabled
                ? (value) => controller.setRouteMode(value.first)
                : null,
          ),
          if (controller.routeMode == 'selected') ...[
            const SizedBox(height: 12),
            if (controller.knownApps.isEmpty)
              Text(
                '还没有捕获到通知。等某个应用发来通知后，这里会出现它的名字。',
                style: TextStyle(
                  color: scheme.onSurfaceVariant,
                  fontSize: 11,
                  height: 1.5,
                ),
              )
            else
              for (final app in controller.knownApps)
                CheckboxListTile(
                  contentPadding: EdgeInsets.zero,
                  dense: true,
                  controlAffinity: ListTileControlAffinity.trailing,
                  title: Text(app.label, style: const TextStyle(fontSize: 13.5)),
                  subtitle: Text(
                    app.package,
                    style: TextStyle(
                      color: scheme.onSurfaceVariant,
                      fontSize: 9.5,
                    ),
                  ),
                  value: controller.routePackages.contains(app.package),
                  onChanged: enabled
                      ? (value) => controller.toggleRoutePackage(
                          app.package,
                          value ?? false,
                        )
                      : null,
                ),
          ],
          const Divider(height: 30),
          SectionHeader(
            title: '片外 AT24C256 动态持久化上限',
            subtitle: '最多保留 20 条，断电重启不丢失；空间不足按 FIFO 自动淘汰最旧消息',
            trailing: StatusPill(
              label:
                  '手表 ${controller.extPoolMsgCount}/${controller.extPoolMsgMax} 条 · ${controller.extPoolMsgBytes} B',
            ),
          ),
          const SizedBox(height: 12),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              for (final count in const [5, 10, 15, 20])
                ChoiceChip(
                  label: Text('$count 条'),
                  selected: controller.maxMessages == count,
                  onSelected: (_) => controller.setMaxMessages(count),
                ),
            ],
          ),
          const SizedBox(height: 6),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('重复应用合并显示'),
            subtitle: const Text('同一应用的新通知覆盖旧条目，避免占满全部位置'),
            value: controller.mergeNotifications,
            onChanged: enabled ? controller.setMergeNotifications : null,
          ),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('通知清理开关（开启即清空 / 重启不保留）'),
            subtitle: const Text('同步至手表设置项「通知清理」，开启时自动抹除片外通知缓存'),
            value: controller.autoClearMessages,
            onChanged: controller.setAutoClearMessages,
          ),
          const SizedBox(height: 8),
          Wrap(
            spacing: 10,
            runSpacing: 8,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              OutlinedButton.icon(
                onPressed: () async {
                  final messenger = ScaffoldMessenger.of(context);
                  final ok = await controller.clearWatchMessages();
                  if (!context.mounted) return;
                  messenger.showSnackBar(
                    SnackBar(
                      content: Text(
                        ok ? '已清空手表 AT24C256 动态 ROM 中的所有通知。' : '已清空本地队列（连接后可同步清理手表）。',
                      ),
                    ),
                  );
                },
                icon: const Icon(Icons.cleaning_services_outlined, size: 16),
                label: const Text('立即清理手表通知'),
              ),
              Text(
                enabled
                    ? '本机已缓存 ${controller.cachedNotifications} 条'
                          '${controller.mergeNotifications ? '（已按应用合并）' : ''}'
                    : 'Windows 已缓存 ${controller.localMessages.length} 条可持久化推送消息',
                style: TextStyle(color: scheme.onSurfaceVariant, fontSize: 10.5),
              ),
            ],
          ),
        ],
      ),
    );
  }
}
