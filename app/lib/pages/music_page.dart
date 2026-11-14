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
  double _position = 0.36;
  late final TextEditingController _lyrics;
  late int _controlSeq;

  @override
  void initState() {
    super.initState();
    _controlSeq = widget.controller.controlSeq;
    widget.controller.addListener(_onControl);
    _lyrics = TextEditingController(
      text: '穿过晚风与灯火\n把时间慢放成一首歌\n在城市睡着以前\n留一点安静给自己',
    );
  }

  @override
  void dispose() {
    widget.controller.removeListener(_onControl);
    _lyrics.dispose();
    super.dispose();
  }

  void _onControl() {
    final seq = widget.controller.controlSeq;
    if (seq == _controlSeq) return;
    _controlSeq = seq;
    final action = widget.controller.lastControlAction;
    if (action != 3 && action != 4) return;
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!mounted) return;
      setState(() => _position = 0);
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(action == 3 ? '手表切到下一首。' : '手表切到上一首。')),
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

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return PageScroll(
      children: [
        PageHeader(
          title: '播放与歌词',
          subtitle: '歌词下行，手表按键上行',
          trailing: StatusPill(
            label: widget.controller.connected ? '已连接' : '未连接',
            color: widget.controller.connected ? null : scheme.onSurfaceVariant,
          ),
        ),
        const SizedBox(height: 22),
        LayoutBuilder(
          builder: (context, constraints) {
            final player = _PlayerCard(
              playing: widget.controller.playing,
              connected: widget.controller.connected,
              position: _position,
              onPlay: () =>
                  widget.controller.setPlayState(!widget.controller.playing),
              onPosition: (value) => setState(() => _position = value),
              onSkip: () {
                setState(() => _position = 0);
                ScaffoldMessenger.of(
                  context,
                ).showSnackBar(const SnackBar(content: Text('电脑端切歌仍为界面演示。')));
              },
            );
            final lyrics = AppCard(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  SectionHeader(
                    title: '当前歌词',
                    subtitle: widget.controller.connected
                        ? '按行发送，每帧 ≤27 字节'
                        : '本地编辑预览，连接后可按行同步',
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
                      hintText: '粘贴当前歌词…',
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
          child: Row(
            children: [
              Icon(Icons.watch_outlined, color: scheme.primary),
              const SizedBox(width: 12),
              Expanded(
                child: Text(
                  '手表按键会通过 0x10 控制帧回传播放 / 暂停；本页状态会跟随变化。',
                  style: TextStyle(fontSize: 12, height: 1.5),
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }
}

class _PlayerCard extends StatelessWidget {
  const _PlayerCard({
    required this.playing,
    required this.connected,
    required this.position,
    required this.onPlay,
    required this.onPosition,
    required this.onSkip,
  });

  final bool playing;
  final bool connected;
  final double position;
  final VoidCallback onPlay;
  final ValueChanged<double> onPosition;
  final VoidCallback onSkip;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
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
          const Text(
            '晚风手记',
            style: TextStyle(fontSize: 23, fontWeight: FontWeight.w700),
          ),
          const SizedBox(height: 5),
          Text(
            '界面演示 · 未连接播放器',
            style: TextStyle(color: scheme.onSurfaceVariant, fontSize: 12),
          ),
          const SizedBox(height: 18),
          Slider(value: position, onChanged: onPosition),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 8),
            child: Row(
              children: [
                Text(
                  _time(position * 222),
                  style: TextStyle(
                    color: scheme.onSurfaceVariant,
                    fontSize: 10,
                  ),
                ),
                const Spacer(),
                Text(
                  '03:42',
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
                onPressed: onSkip,
                tooltip: '上一首（电脑端预览）',
                iconSize: 30,
                icon: const Icon(Icons.skip_previous_rounded),
              ),
              const SizedBox(width: 18),
              IconButton.filled(
                onPressed: onPlay,
                tooltip: connected ? '播放 / 暂停（同步到手表）' : '播放 / 暂停（界面预览）',
                iconSize: 34,
                style: IconButton.styleFrom(minimumSize: const Size(64, 64)),
                icon: Icon(
                  playing ? Icons.pause_rounded : Icons.play_arrow_rounded,
                ),
              ),
              const SizedBox(width: 18),
              IconButton(
                onPressed: onSkip,
                tooltip: '下一首（电脑端预览）',
                iconSize: 30,
                icon: const Icon(Icons.skip_next_rounded),
              ),
            ],
          ),
          const SizedBox(height: 8),
          StatusPill(
            label: connected
                ? (playing ? '手表播放中' : '手表已暂停')
                : (playing ? '演示播放中' : '演示已暂停'),
            color: scheme.primary,
          ),
        ],
      ),
    );
  }

  static String _time(double seconds) {
    final value = seconds.round();
    return '${(value ~/ 60).toString().padLeft(2, '0')}:${(value % 60).toString().padLeft(2, '0')}';
  }
}
