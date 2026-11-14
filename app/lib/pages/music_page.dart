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
  bool _playing = true;
  double _position = 0.36;
  late final TextEditingController _lyrics;

  @override
  void initState() {
    super.initState();
    _lyrics = TextEditingController(
      text: '穿过晚风与灯火\n把时间慢放成一首歌\n在城市睡着以前\n留一点安静给自己',
    );
  }

  @override
  void dispose() {
    _lyrics.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return PageScroll(
      children: [
        PageHeader(
          title: '播放与歌词',
          subtitle: '手表音乐页与电脑反向控制的界面预览',
          trailing: StatusPill(label: '控制待接入', color: scheme.onSurfaceVariant),
        ),
        const SizedBox(height: 22),
        LayoutBuilder(
          builder: (context, constraints) {
            final player = _PlayerCard(
              playing: _playing,
              position: _position,
              onPlay: () => setState(() => _playing = !_playing),
              onPosition: (value) => setState(() => _position = value),
              onSkip: () {
                setState(() => _position = 0);
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('已预览切歌交互，未控制任何播放器。')),
                );
              },
            );
            final lyrics = AppCard(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  SectionHeader(
                    title: '当前歌词',
                    subtitle: 'UTF-8 文本 · 本地编辑预览',
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
                        onPressed: () => showPrototypeMessage(context, '歌词同步'),
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
                  '后续由手表按键发出 0x10 控制帧：播放 / 暂停、上一首、下一首。',
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
    required this.position,
    required this.onPlay,
    required this.onPosition,
    required this.onSkip,
  });

  final bool playing;
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
                tooltip: '上一首（界面预览）',
                iconSize: 30,
                icon: const Icon(Icons.skip_previous_rounded),
              ),
              const SizedBox(width: 18),
              IconButton.filled(
                onPressed: onPlay,
                tooltip: '播放 / 暂停（界面预览）',
                iconSize: 34,
                style: IconButton.styleFrom(minimumSize: const Size(64, 64)),
                icon: Icon(
                  playing ? Icons.pause_rounded : Icons.play_arrow_rounded,
                ),
              ),
              const SizedBox(width: 18),
              IconButton(
                onPressed: onSkip,
                tooltip: '下一首（界面预览）',
                iconSize: 30,
                icon: const Icon(Icons.skip_next_rounded),
              ),
            ],
          ),
          const SizedBox(height: 8),
          StatusPill(label: playing ? '演示播放中' : '演示已暂停', color: scheme.primary),
        ],
      ),
    );
  }

  static String _time(double seconds) {
    final value = seconds.round();
    return '${(value ~/ 60).toString().padLeft(2, '0')}:${(value % 60).toString().padLeft(2, '0')}';
  }
}
