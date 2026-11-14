import 'dart:convert';

import 'package:material_ui/material_ui.dart';

import '../state/watch_controller.dart';
import '../widgets/common.dart';

class MessagesPage extends StatefulWidget {
  const MessagesPage({super.key, required this.controller});

  final WatchController controller;

  @override
  State<MessagesPage> createState() => _MessagesPageState();
}

class _MessagesPageState extends State<MessagesPage> {
  late final TextEditingController _message;
  int _selectedSlot = 0;

  @override
  void initState() {
    super.initState();
    _message = TextEditingController(
      text: widget.controller.messageSlots.first,
    );
  }

  @override
  void dispose() {
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

  @override
  Widget build(BuildContext context) {
    final bytes = utf8.encode(_message.text).length;
    final scheme = Theme.of(context).colorScheme;
    return PageScroll(
      children: [
        PageHeader(
          title: '设备消息',
          subtitle: '管理手表上的三个消息槽',
          trailing: StatusPill(
            label: widget.controller.connected ? '已连接' : '未连接',
            color: widget.controller.connected ? null : scheme.onSurfaceVariant,
          ),
        ),
        const SizedBox(height: 22),
        LayoutBuilder(
          builder: (context, constraints) {
            final composer = AppCard(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  SectionHeader(
                    title: '编辑消息',
                    subtitle: widget.controller.connected
                        ? '发送时按 UTF-8 字符边界截到 23 字节'
                        : '先在本地预览内容，连接后即可写入手表',
                  ),
                  const SizedBox(height: 20),
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
                    minLines: 5,
                    maxLines: 7,
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
                          color: bytes > 23
                              ? scheme.onSurface
                              : scheme.onSurfaceVariant,
                          fontSize: 11,
                        ),
                      ),
                      const Spacer(),
                      Text(
                        '单条协议上限',
                        style: TextStyle(
                          color: Theme.of(context).colorScheme.onSurfaceVariant,
                          fontSize: 10,
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 20),
                  Wrap(
                    spacing: 10,
                    runSpacing: 10,
                    children: [
                      FilledButton.icon(
                        onPressed: () {
                          widget.controller.setMessageSlot(
                            _selectedSlot,
                            _message.text,
                          );
                          ScaffoldMessenger.of(context).showSnackBar(
                            SnackBar(
                              content: Text('已保存到本地消息槽 ${_selectedSlot + 1}。'),
                            ),
                          );
                        },
                        icon: const Icon(Icons.save_outlined, size: 18),
                        label: const Text('保存预览'),
                      ),
                      OutlinedButton.icon(
                        onPressed: _sendMessage,
                        icon: const Icon(Icons.send_outlined, size: 17),
                        label: const Text('发送到手表'),
                      ),
                    ],
                  ),
                ],
              ),
            );
            final slots = _MessageSlots(
              slots: widget.controller.messageSlots,
              selected: _selectedSlot,
              onSelected: _selectSlot,
            );
            if (constraints.maxWidth >= 840) {
              return Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Expanded(flex: 6, child: composer),
                  const SizedBox(width: 16),
                  Expanded(flex: 5, child: slots),
                ],
              );
            }
            return Column(
              children: [composer, const SizedBox(height: 16), slots],
            );
          },
        ),
        const SizedBox(height: 16),
        AppCard(
          padding: const EdgeInsets.all(16),
          child: Text(
            '协议说明：0x02 消息帧使用「槽号 + UTF-8」；发送阶段仍会按完整字符截断，避免产生半个汉字。',
            style: TextStyle(
              color: Theme.of(context).colorScheme.onSurfaceVariant,
              fontSize: 12,
              height: 1.5,
            ),
          ),
        ),
      ],
    );
  }
}

class _MessageSlots extends StatelessWidget {
  const _MessageSlots({
    required this.slots,
    required this.selected,
    required this.onSelected,
  });

  final List<String> slots;
  final int selected;
  final ValueChanged<int> onSelected;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Padding(
          padding: EdgeInsets.only(left: 2, bottom: 12),
          child: Text(
            '消息槽预览',
            style: TextStyle(fontSize: 16, fontWeight: FontWeight.w700),
          ),
        ),
        for (var i = 0; i < slots.length; i++) ...[
          AppCard(
            onTap: () => onSelected(i),
            color: selected == i
                ? scheme.primary.withValues(alpha: 0.08)
                : null,
            padding: const EdgeInsets.all(16),
            child: Row(
              children: [
                Container(
                  width: 38,
                  height: 38,
                  decoration: BoxDecoration(
                    color: selected == i
                        ? scheme.primary
                        : scheme.surfaceContainerHigh,
                    borderRadius: BorderRadius.circular(12),
                  ),
                  alignment: Alignment.center,
                  child: Text(
                    '${i + 1}',
                    style: TextStyle(
                      color: selected == i
                          ? scheme.onPrimary
                          : scheme.onSurfaceVariant,
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        slots[i].isEmpty ? '空消息' : slots[i],
                        maxLines: 2,
                        overflow: TextOverflow.ellipsis,
                        style: const TextStyle(fontWeight: FontWeight.w600),
                      ),
                      const SizedBox(height: 4),
                      Text(
                        '${utf8.encode(slots[i]).length} 字节 · 本地预览',
                        style: TextStyle(
                          color: Theme.of(context).colorScheme.onSurfaceVariant,
                          fontSize: 10,
                        ),
                      ),
                    ],
                  ),
                ),
                if (selected == i)
                  Icon(
                    Icons.check_circle_rounded,
                    color: scheme.primary,
                    size: 18,
                  ),
              ],
            ),
          ),
          if (i < slots.length - 1) const SizedBox(height: 10),
        ],
      ],
    );
  }
}
