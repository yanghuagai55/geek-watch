import 'dart:convert';
import 'dart:io';

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
    if (Platform.isAndroid) {
      // 进入页面时回读原生服务持久化的规则与通知栏出现过的应用。
      widget.controller.refreshMessageRules();
    }
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
        const SizedBox(height: 18),
        Wrap(
          spacing: 10,
          runSpacing: 10,
          children: [
            FilledButton.icon(
              onPressed: () {
                widget.controller.setMessageSlot(_selectedSlot, _message.text);
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
    );
  }

  @override
  Widget build(BuildContext context) {
    final controller = widget.controller;
    final scheme = Theme.of(context).colorScheme;
    return PageScroll(
      children: [
        PageHeader(
          title: '设备消息',
          subtitle: Platform.isAndroid
              ? '系统通知按规则筛选后，渲染成点阵送到手表消息页'
              : '通过消息槽把文本发到手表',
          trailing: StatusPill(
            label: controller.connected ? '已连接' : '未连接',
            color: controller.connected ? null : scheme.onSurfaceVariant,
          ),
        ),
        const SizedBox(height: 22),
        if (Platform.isAndroid) ...[
          _NotificationRules(controller: controller),
          const SizedBox(height: 16),
          AppCard(
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
                style: TextStyle(
                  color: scheme.onSurfaceVariant,
                  fontSize: 11,
                ),
              ),
              children: [_composer(context)],
            ),
          ),
          const SizedBox(height: 16),
          TroubleshootingCard(
            controller: controller,
            hint:
                '手表打开一级消息页时才回传应用名列表，选中后才加载详情。'
                '若列表为空，请先开启通知读取权限，并检查路由规则是否放行目标应用。',
          ),
        ] else ...[
          AppCard(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                SectionHeader(
                  title: '编辑消息',
                  subtitle: controller.connected
                      ? '发送时按 UTF-8 字符边界截到 23 字节'
                      : '先在本地预览内容，连接后即可写入手表',
                ),
                const SizedBox(height: 20),
                _composer(context),
              ],
            ),
          ),
          const SizedBox(height: 16),
          AppCard(
            padding: const EdgeInsets.all(16),
            child: Text(
              '协议说明：0x02 消息帧使用「槽号 + UTF-8」；发送阶段会按完整字符截断，'
              '避免产生半个汉字。手表一级消息页显示的是最近几条系统通知，与这三个槽互不影响。',
              style: TextStyle(
                color: Theme.of(context).colorScheme.onSurfaceVariant,
                fontSize: 12,
                height: 1.5,
              ),
            ),
          ),
        ],
      ],
    );
  }
}

/// 通知路由规则：全部 / 仅选中应用、发送条数、重复应用合并。
class _NotificationRules extends StatelessWidget {
  const _NotificationRules({required this.controller});

  final WatchController controller;

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
              onPressed: controller.refreshMessageRules,
              icon: const Icon(Icons.refresh_rounded, size: 16),
              label: const Text('刷新应用', style: TextStyle(fontSize: 11)),
            ),
          ),
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
            onSelectionChanged: (value) =>
                controller.setRouteMode(value.first),
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
                  title: Text(
                    app.label,
                    style: const TextStyle(fontSize: 13.5),
                  ),
                  subtitle: Text(
                    app.package,
                    style: TextStyle(
                      color: scheme.onSurfaceVariant,
                      fontSize: 9.5,
                    ),
                  ),
                  value: controller.routePackages.contains(app.package),
                  onChanged: (value) => controller.toggleRoutePackage(
                    app.package,
                    value ?? false,
                  ),
                ),
          ],
          const Divider(height: 30),
          const SectionHeader(
            title: '发送限制',
            subtitle: '手表最多保留 5 条，超出后丢掉最旧的',
          ),
          const SizedBox(height: 12),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              for (var count = 1; count <= 5; count++)
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
            onChanged: controller.setMergeNotifications,
          ),
          Text(
            '当前已缓存 ${controller.cachedNotifications} 条'
            '${controller.mergeNotifications ? '（已按应用合并）' : ''}',
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
