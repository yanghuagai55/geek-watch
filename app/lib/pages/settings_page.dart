import 'package:material_ui/material_ui.dart';

import '../state/watch_controller.dart';
import '../widgets/common.dart';

class SettingsPage extends StatelessWidget {
  const SettingsPage({super.key, required this.controller});

  final WatchController controller;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return PageScroll(
      children: [
        const PageHeader(
          title: '设置',
          subtitle: '连接、显示与设备偏好',
          trailing: StatusPill(label: '本地界面状态', icon: Icons.tune_rounded),
        ),
        const SizedBox(height: 22),
        _Section(
          icon: Icons.bluetooth_connected_rounded,
          title: '连接偏好',
          subtitle: '沿用现有 WinUI 调试台的默认参数',
          children: [
            SegmentedButton<TransportType>(
              segments: const [
                ButtonSegment(
                  value: TransportType.spp,
                  icon: Icon(Icons.bluetooth_rounded),
                  label: Text('SPP'),
                ),
                ButtonSegment(
                  value: TransportType.ble,
                  icon: Icon(Icons.bluetooth_searching_rounded),
                  label: Text('BLE'),
                ),
                ButtonSegment(
                  value: TransportType.serial,
                  icon: Icon(Icons.cable_rounded),
                  label: Text('串口'),
                ),
              ],
              selected: {controller.transport},
              showSelectedIcon: false,
              onSelectionChanged: (value) =>
                  controller.setTransport(value.first),
            ),
            const SizedBox(height: 18),
            if (controller.transport != TransportType.serial)
              TextFormField(
                key: ValueKey(controller.transport),
                initialValue: controller.deviceAddress,
                onChanged: controller.setAddress,
                // Playfair 数字偏宽，地址串按 16px 会被输入框裁掉尾字符。
                style: const TextStyle(fontSize: 13.5, letterSpacing: 0.3),
                decoration: const InputDecoration(
                  labelText: '设备地址',
                  hintText: '00:00:00:00:00:00',
                  prefixIcon: Icon(Icons.radio_button_checked_rounded),
                ),
              )
            else
              Row(
                children: [
                  Expanded(
                    child: TextFormField(
                      key: const ValueKey('serial-port'),
                      initialValue: controller.serialPort,
                      onChanged: controller.setSerialPort,
                      decoration: const InputDecoration(
                        labelText: '串口',
                        prefixIcon: Icon(Icons.cable_rounded),
                      ),
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: DropdownButtonFormField<String>(
                      initialValue: controller.baudRate,
                      decoration: const InputDecoration(labelText: '波特率'),
                      items: const [
                        DropdownMenuItem(value: '9600', child: Text('9600')),
                        DropdownMenuItem(
                          value: '115200',
                          child: Text('115200'),
                        ),
                      ],
                      onChanged: controller.setBaudRate,
                    ),
                  ),
                ],
              ),
            const SizedBox(height: 8),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('启动后自动连接'),
              subtitle: const Text('接入真实传输层后生效'),
              value: controller.autoConnect,
              onChanged: controller.setAutoConnect,
            ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('连接后自动校时'),
              subtitle: const Text('同步电脑或手机的当前时间'),
              value: controller.autoSyncTime,
              onChanged: controller.setAutoSyncTime,
            ),
          ],
        ),
        const SizedBox(height: 16),
        _Section(
          icon: Icons.palette_outlined,
          title: '外观与动效',
          subtitle: '墨黑、暖白与薄荷绿保持一致',
          children: [
            const Align(
              alignment: Alignment.centerLeft,
              child: Text(
                '主题模式',
                style: TextStyle(fontWeight: FontWeight.w600),
              ),
            ),
            const SizedBox(height: 10),
            SegmentedButton<ThemeMode>(
              segments: const [
                ButtonSegment(
                  value: ThemeMode.system,
                  icon: Icon(Icons.settings_brightness_rounded),
                  label: Text('系统'),
                ),
                ButtonSegment(
                  value: ThemeMode.dark,
                  icon: Icon(Icons.dark_mode_outlined),
                  label: Text('墨黑'),
                ),
                ButtonSegment(
                  value: ThemeMode.light,
                  icon: Icon(Icons.light_mode_outlined),
                  label: Text('暖白'),
                ),
              ],
              selected: {controller.themeMode},
              showSelectedIcon: false,
              onSelectionChanged: (value) =>
                  controller.setThemeMode(value.first),
            ),
            const SizedBox(height: 8),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('界面动效'),
              subtitle: const Text('页面切换与仪表过渡'),
              value: controller.animations,
              onChanged: controller.setAnimations,
            ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('演示数据刷新'),
              subtitle: const Text('以 1 Hz 更新性能仪表与曲线'),
              value: controller.demoStreaming,
              onChanged: controller.setDemoStreaming,
            ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('液态玻璃底栏'),
              subtitle: const Text('竖屏底栏使用实时着色器；如遇闪烁或异常可关闭'),
              value: controller.liquidGlass,
              onChanged: controller.setLiquidGlass,
            ),
          ],
        ),
        const SizedBox(height: 16),
        _Section(
          icon: Icons.watch_outlined,
          title: '手表显示',
          subtitle: '此处只预览控件，不会写入设备',
          children: [
            Row(
              children: [
                const Text('亮度', style: TextStyle(fontWeight: FontWeight.w600)),
                const Spacer(),
                Text(
                  '${(controller.brightness * 100).round()}%',
                  style: TextStyle(
                    color: scheme.primary,
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ],
            ),
            Slider(
              value: controller.brightness,
              onChanged: controller.setBrightness,
            ),
            const SizedBox(height: 6),
            DropdownButtonFormField<String>(
              initialValue: controller.screenTimeout,
              decoration: const InputDecoration(
                labelText: '自动熄屏',
                prefixIcon: Icon(Icons.bedtime_outlined),
              ),
              items: const ['15 秒', '30 秒', '1 分钟', '2 分钟', '5 分钟']
                  .map(
                    (value) =>
                        DropdownMenuItem(value: value, child: Text(value)),
                  )
                  .toList(),
              onChanged: (value) {
                if (value != null) controller.setScreenTimeout(value);
              },
            ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('24 小时制'),
              subtitle: const Text('同步影响主页表盘预览'),
              value: controller.use24Hour,
              onChanged: controller.setUse24Hour,
            ),
          ],
        ),
        const SizedBox(height: 16),
        AppCard(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Icon(Icons.cable_outlined, color: scheme.primary),
                  const SizedBox(width: 10),
                  const Text(
                    '下一阶段接入',
                    style: TextStyle(fontSize: 16, fontWeight: FontWeight.w700),
                  ),
                ],
              ),
              const SizedBox(height: 10),
              Text(
                '界面与协议参数已经对齐，真实硬件传输尚未启用。',
                style: TextStyle(
                  color: Theme.of(context).colorScheme.onSurfaceVariant,
                  height: 1.5,
                ),
              ),
              const SizedBox(height: 12),
              const Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  Chip(label: Text('设备消息')),
                  Chip(label: Text('歌词同步')),
                  Chip(label: Text('播放 / 暂停')),
                  Chip(label: Text('屏幕回传')),
                ],
              ),
            ],
          ),
        ),
        const SizedBox(height: 20),
        Wrap(
          spacing: 10,
          runSpacing: 10,
          children: [
            FilledButton.icon(
              onPressed: () {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('设置已应用到本次预览，尚未写入设备或磁盘。')),
                );
              },
              icon: const Icon(Icons.check_rounded, size: 18),
              label: const Text('保存设置'),
            ),
            OutlinedButton.icon(
              onPressed: controller.resetSettings,
              icon: const Icon(Icons.restart_alt_rounded, size: 18),
              label: const Text('恢复默认'),
            ),
          ],
        ),
      ],
    );
  }
}

class _Section extends StatelessWidget {
  const _Section({
    required this.icon,
    required this.title,
    required this.subtitle,
    required this.children,
  });

  final IconData icon;
  final String title;
  final String subtitle;
  final List<Widget> children;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return AppCard(
      padding: const EdgeInsets.all(22),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              Container(
                width: 38,
                height: 38,
                decoration: BoxDecoration(
                  color: scheme.primary.withValues(alpha: 0.1),
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Icon(icon, size: 20, color: scheme.primary),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      title,
                      style: const TextStyle(
                        fontSize: 16,
                        fontWeight: FontWeight.w700,
                      ),
                    ),
                    const SizedBox(height: 2),
                    Text(
                      subtitle,
                      style: TextStyle(
                        color: Theme.of(context).colorScheme.onSurfaceVariant,
                        fontSize: 11,
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
          const SizedBox(height: 20),
          ...children,
        ],
      ),
    );
  }
}
