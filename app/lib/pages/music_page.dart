import 'dart:io';

import 'package:material_ui/material_ui.dart';

import '../state/watch_controller.dart';
import '../theme/app_theme.dart';
import '../widgets/common.dart';

class MusicPage extends StatefulWidget {
  const MusicPage({super.key, required this.controller});

  final WatchController controller;

  @override
  State<MusicPage> createState() => _MusicPageState();
}

class _MusicPageState extends State<MusicPage> {
  late final TextEditingController _lyrics;

  @override
  void initState() {
    super.initState();
    // 演示歌词单独收录，不再当成“当前歌曲”。
    _lyrics = TextEditingController(
      text: '穿过晚风与灯火\n把时间慢放成一首歌\n在城市睡着以前\n留一点安静给自己',
    );
  }

  @override
  void dispose() {
    _lyrics.dispose();
    super.dispose();
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

  @override
  Widget build(BuildContext context) {
    final controller = widget.controller;
    final scheme = Theme.of(context).colorScheme;
    final missingAccess = Platform.isAndroid && !controller.notificationAccess;
    return PageScroll(
      children: [
        PageHeader(
          title: '正在播放',
          subtitle: Platform.isAndroid
              ? '跟随手机系统媒体；播放、切歌与进度可直接控制手机'
              : '歌词下行，手表按键上行',
          trailing: StatusPill(
            label: controller.hasMedia
                ? (controller.playing ? '手机播放中' : '手机已暂停')
                : (controller.connected ? '等待媒体' : '未连接'),
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
                        ? '仅作演示，按行发送，每帧 ≤27 字节'
                        : '本地演示歌词，连接后可按行同步',
                    trailing: Icon(
                      Icons.lyrics_outlined,
                      color: scheme.primary,
                    ),
                  ),
                  const SizedBox(height: 18),
                  TextField(
                    controller: _lyrics,
                    minLines: 8,
                    maxLines: 12,
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
            if (constraints.maxWidth >= 850) {
              return Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Expanded(flex: 5, child: player),
                  const SizedBox(width: 16),
                  Expanded(flex: 6, child: lyrics),
                ],
              );
            }
            return Column(
              children: [player, const SizedBox(height: 16), lyrics],
            );
          },
        ),
        const SizedBox(height: 16),
        AppCard(
          padding: const EdgeInsets.all(16),
          child: Text(
            '手表按键通过 0x10 控制帧回传播放 / 暂停 / 上下首，由前台服务直接操作系统媒体；'
            'App 退到后台后仍可控制。',
            style: TextStyle(
              color: scheme.onSurfaceVariant,
              fontSize: 12,
              height: 1.5,
            ),
          ),
        ),
        if (Platform.isAndroid) ...[
          const SizedBox(height: 16),
          TroubleshootingCard(
            controller: controller,
            showMedia: true,
            hint: '媒体标题与控制依赖“通知读取权限”；'
                '汽水音乐等 App 需要有活动媒体会话。'
                '电池白名单可减少 MIUI/HyperOS 清理后台。',
          ),
        ],
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
                  style: TextStyle(
                    color: scheme.onSurfaceVariant,
                    fontSize: 10,
                  ),
                ),
                const Spacer(),
                Text(
                  _time(duration),
                  style: TextStyle(
                    color: scheme.onSurfaceVariant,
                    fontSize: 10,
                  ),
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
