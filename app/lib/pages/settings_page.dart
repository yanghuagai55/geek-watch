import 'dart:io';

import 'package:material_ui/material_ui.dart';

import '../services/lrc_library.dart';
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
        PageHeader(
          title: '设置',
          subtitle: '连接、显示与设备偏好',
          trailing: StatusPill(
            label: controller.connected ? '硬件已连接' : '本地界面状态',
            icon: Icons.tune_rounded,
          ),
        ),
        const SizedBox(height: 22),
        _Section(
          icon: Icons.bluetooth_connected_rounded,
          title: '连接偏好',
          subtitle: '沿用现有 WinUI 调试台的默认参数',
          children: [
            SegmentedButton<TransportType>(
              segments: [
                const ButtonSegment(
                  value: TransportType.spp,
                  icon: Icon(Icons.bluetooth_rounded),
                  label: Text('SPP'),
                ),
                if (Platform.isWindows)
                  const ButtonSegment(
                    value: TransportType.ble,
                    icon: Icon(Icons.bluetooth_searching_rounded),
                    label: Text('BLE'),
                  ),
                if (Platform.isWindows)
                  const ButtonSegment(
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
              subtitle: const Text('应用启动后自动建立当前链路'),
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
        if (Platform.isAndroid) ...[
          const SizedBox(height: 16),
          _Section(
            icon: Icons.developer_mode_rounded,
            title: '安卓后台与系统集成',
            subtitle: '前台服务保持连接；App 退出后仍会同步媒体与控制播放',
            children: [
              SwitchListTile(
                contentPadding: EdgeInsets.zero,
                title: const Text('跟随系统媒体'),
                subtitle: const Text('读取汽水音乐等应用的会话，标题按两行一页轮播'),
                value: controller.mediaFollow,
                onChanged: controller.setMediaFollow,
              ),
              SwitchListTile(
                contentPadding: EdgeInsets.zero,
                title: const Text('转发系统通知'),
                subtitle: const Text('把通知标题与内容写入手表消息槽（长文会截断）'),
                value: controller.notificationForward,
                onChanged: controller.setNotificationForward,
              ),
              ListTile(
                contentPadding: EdgeInsets.zero,
                title: const Text('通知读取权限'),
                subtitle: Text(
                  controller.notificationAccess
                      ? '已授权：可读取媒体会话与通知'
                      : '未授权：媒体标题与通知都读不到',
                ),
                trailing: OutlinedButton(
                  onPressed: () async {
                    await controller.openNotificationAccess();
                    await Future<void>.delayed(const Duration(seconds: 2));
                    await controller.refreshAndroidStatus();
                  },
                  child: Text(controller.notificationAccess ? '重新授权' : '去授权'),
                ),
              ),
              ListTile(
                contentPadding: EdgeInsets.zero,
                title: const Text('电池优化白名单'),
                subtitle: Text(
                  controller.batteryExempt
                      ? '已加入：后台链路更稳定'
                      : '未加入：MIUI / HyperOS 可能清理后台',
                ),
                trailing: OutlinedButton(
                  onPressed: () async {
                    await controller.openBatterySettings();
                    await Future<void>.delayed(const Duration(seconds: 2));
                    await controller.refreshAndroidStatus();
                  },
                  child: const Text('去设置'),
                ),
              ),
            ],
          ),
        ],
        if (Platform.isWindows) ...[
          const SizedBox(height: 16),
          _Section(
            icon: Icons.queue_music_rounded,
            title: '系统媒体与后台',
            subtitle: '读取 Windows 媒体会话；关闭窗口后可驻留托盘',
            children: [
              SwitchListTile(
                contentPadding: EdgeInsets.zero,
                title: const Text('跟随系统媒体'),
                subtitle: const Text('标题过长会分页轮播；有同名 .lrc 时按时间轴推送歌词'),
                value: controller.mediaFollow,
                onChanged: controller.setMediaFollow,
              ),
              if (controller.mediaTitle.isNotEmpty)
                Padding(
                  padding: const EdgeInsets.only(top: 4, bottom: 8),
                  child: Row(
                    children: [
                      Icon(
                        Icons.play_circle_outline_rounded,
                        size: 16,
                        color: scheme.primary,
                      ),
                      const SizedBox(width: 8),
                      Expanded(
                        child: Text(
                          controller.mediaArtist.isEmpty
                              ? controller.mediaTitle
                              : '${controller.mediaTitle} · ${controller.mediaArtist}',
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                          style: TextStyle(
                            color: scheme.onSurfaceVariant,
                            fontSize: 11.5,
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              SwitchListTile(
                contentPadding: EdgeInsets.zero,
                title: const Text('关闭窗口后驻留托盘'),
                subtitle: const Text('继续在右下角托盘运行，双击图标恢复窗口'),
                value: controller.backgroundEnabled,
                onChanged: controller.setBackgroundEnabled,
              ),
              const SizedBox(height: 4),
              Align(
                alignment: Alignment.centerLeft,
                child: OutlinedButton.icon(
                  onPressed: () async {
                    if (!Platform.isWindows) return;
                    final folder = Directory(LrcLibrary.defaultDirectory);
                    if (!await folder.exists()) {
                      await folder.create(recursive: true);
                    }
                    await Process.run('explorer', <String>[folder.path]);
                  },
                  icon: const Icon(Icons.folder_open_rounded, size: 17),
                  label: const Text('打开歌词目录'),
                ),
              ),
            ],
          ),
        ],
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
          title: '手表设置',
          subtitle: controller.connected
              ? (controller.watchSettingsLoaded
                    ? '与手表设置页一致，改动即时生效'
                    : '正在读取手表设置…')
              : '连接后读取手表当前档位',
          children: [
            Row(
              children: [
                const Text('亮度', style: TextStyle(fontWeight: FontWeight.w600)),
                const Spacer(),
                Text(
                  const ['低', '中', '高'][controller.watchBrightness],
                  style: TextStyle(
                    color: scheme.primary,
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 10),
            SegmentedButton<int>(
              segments: const [
                ButtonSegment(value: 0, label: Text('低')),
                ButtonSegment(value: 1, label: Text('中')),
                ButtonSegment(value: 2, label: Text('高')),
              ],
              selected: {controller.watchBrightness},
              showSelectedIcon: false,
              onSelectionChanged: (value) =>
                  controller.setWatchBrightness(value.first),
            ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('抬腕亮屏'),
              subtitle: const Text('抬起手腕时自动点亮屏幕'),
              value: controller.raiseWrist,
              onChanged: controller.setRaiseWrist,
            ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('按键音'),
              subtitle: const Text('按键时蜂鸣器短鸣'),
              value: controller.keyBeep,
              onChanged: controller.setKeyBeep,
            ),
            const SizedBox(height: 4),
            Row(
              children: [
                const Text('状态灯', style: TextStyle(fontWeight: FontWeight.w600)),
                const Spacer(),
                Text(
                  const ['关闭', '常亮', '闪烁'][controller.ledMode],
                  style: TextStyle(
                    color: scheme.primary,
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 10),
            SegmentedButton<int>(
              segments: const [
                ButtonSegment(value: 0, label: Text('关闭')),
                ButtonSegment(value: 1, label: Text('常亮')),
                ButtonSegment(value: 2, label: Text('闪烁')),
              ],
              selected: {controller.ledMode},
              showSelectedIcon: false,
              onSelectionChanged: (value) => controller.setLedMode(value.first),
            ),
            const SizedBox(height: 14),
            DropdownButtonFormField<int>(
              key: ValueKey(controller.screenOff),
              initialValue: controller.screenOff,
              decoration: const InputDecoration(
                labelText: '自动熄屏',
                prefixIcon: Icon(Icons.bedtime_outlined),
              ),
              items: const ['关', '30 秒', '1 分钟', '2 分钟', '5 分钟']
                  .asMap()
                  .entries
                  .map(
                    (entry) => DropdownMenuItem(
                      value: entry.key,
                      child: Text(entry.value),
                    ),
                  )
                  .toList(),
              onChanged: (value) {
                if (value != null) controller.setScreenOff(value);
              },
            ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('蓝牙'),
              subtitle: const Text('需在手表设置里切换，手机端只读显示'),
              value: controller.watchBluetooth,
              onChanged: null,
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
                  Icon(
                    controller.connected
                        ? Icons.link_rounded
                        : Icons.cable_outlined,
                    color: scheme.primary,
                  ),
                  const SizedBox(width: 10),
                  Text(
                    controller.connected ? '链路已就绪' : '当前链路',
                    style: const TextStyle(
                      fontSize: 16,
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 10),
              Text(
                controller.connected
                    ? '${controller.transportName} · 已连接，性能、消息、歌词与抓屏均走真实设备。'
                    : '经典 SPP 已在 Windows 桌面端接入；BLE 与串口链路后续补齐。',
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
              onPressed: () async {
                final messenger = ScaffoldMessenger.of(context);
                final ok = await controller.refreshWatchSettings();
                messenger.showSnackBar(
                  SnackBar(
                    content: Text(ok ? '已读取手表当前设置。' : '请先连接手表。'),
                  ),
                );
              },
              icon: const Icon(Icons.sync_rounded, size: 18),
              label: const Text('读取手表设置'),
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
