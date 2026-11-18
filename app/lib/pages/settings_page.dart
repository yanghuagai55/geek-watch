import 'dart:io';

import 'package:material_ui/material_ui.dart';

import '../services/android_spp.dart';
import '../services/lrc_library.dart';
import '../state/watch_controller.dart';
import '../widgets/common.dart';

class SettingsPage extends StatelessWidget {
  const SettingsPage({super.key, required this.controller});

  final WatchController controller;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final forgetNote = Text(
      '试用结束后点这里。会断开连接，并删掉系统里 HC-04 的配对，手机就不会再自动连上这块表。',
      style: TextStyle(
        color: scheme.onSurfaceVariant,
        fontSize: 12,
        height: 1.4,
      ),
    );
    final forgetButton = FilledButton.icon(
      onPressed: () => _forgetBluetoothMemory(context, controller),
      icon: const Icon(Icons.bluetooth_disabled_rounded),
      label: const Text('清除系统蓝牙记忆'),
    );

    return LayoutBuilder(
      builder: (context, constraints) {
        final wide = constraints.maxWidth >= 960;
        final pad = constraints.maxWidth < 560 ? 16.0 : 28.0;
        final screen = MediaQuery.sizeOf(context);
        final portraitPhone = screen.width < 760 && screen.height > screen.width;
        final maxWidth = wide ? 1320.0 : 1180.0;
        final inner = constraints.maxWidth - pad * 2;
        final width = inner < maxWidth ? inner : maxWidth;

        final header = Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            if (Platform.isAndroid) ...[
              if (wide)
                Row(
                  crossAxisAlignment: CrossAxisAlignment.center,
                  children: [
                    forgetButton,
                    const SizedBox(width: 16),
                    Expanded(child: forgetNote),
                  ],
                )
              else ...[
                SizedBox(width: double.infinity, child: forgetButton),
                const SizedBox(height: 8),
                forgetNote,
              ],
              const SizedBox(height: 18),
            ],
            PageHeader(
          title: '设置',
          subtitle: '连接、显示与设备偏好',
          trailing: StatusPill(
            label: controller.connected ? '硬件已连接' : '本地界面状态',
            icon: Icons.tune_rounded,
          ),
            ),
          ],
        );
        final stack = <Widget>[
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
              expandedInsets: wide ? null : EdgeInsets.zero,
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
              segments: [
                ButtonSegment(
                  value: ThemeMode.system,
                  icon: portraitPhone
                      ? null
                      : const Icon(Icons.settings_brightness_rounded),
                  label: const Text('系统'),
                ),
                ButtonSegment(
                  value: ThemeMode.dark,
                  icon: portraitPhone
                      ? null
                      : const Icon(Icons.dark_mode_outlined),
                  label: const Text('墨黑'),
                ),
                ButtonSegment(
                  value: ThemeMode.light,
                  icon: portraitPhone
                      ? null
                      : const Icon(Icons.light_mode_outlined),
                  label: const Text('暖白'),
                ),
              ],
              selected: {controller.themeMode},
              showSelectedIcon: false,
              expandedInsets: wide ? null : EdgeInsets.zero,
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
              expandedInsets: wide ? null : EdgeInsets.zero,
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
              title: const Text('手表震动（1207 转子马达）'),
              subtitle: const Text('PA8 MOS 驱动：短按轻震、长按长震、指南针校准双震与新消息经典三连震'),
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
              expandedInsets: wide ? null : EdgeInsets.zero,
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
            const Divider(height: 24),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('24 小时制'),
              subtitle: const Text('同步作用于手表数字/仪表表盘与 App 预览'),
              value: controller.use24Hour,
              onChanged: controller.setUse24Hour,
            ),
            Row(
              children: [
                const Text('每日步数目标', style: TextStyle(fontWeight: FontWeight.w600)),
                const Spacer(),
                Text(
                  '${controller.stepGoal} 步',
                  style: TextStyle(
                    color: scheme.primary,
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ],
            ),
            Slider(
              value: controller.stepGoal.clamp(2000, 20000).toDouble(),
              min: 2000,
              max: 20000,
              divisions: 18,
              label: '${controller.stepGoal} 步',
              onChanged: (v) => controller.setStepGoal(v.round()),
            ),
          ],
        ),
        const SizedBox(height: 16),
        _Section(
          icon: Icons.memory_rounded,
          title: '配置优先级与 AT24C256 持久化',
          subtitle: '未连手机开机读取手表 I2C EEPROM 默认配置；连上后以 App 为最高优先级覆盖写入',
          children: [
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: [
                StatusPill(
                  label: controller.connected
                      ? (controller.extFlashPresent
                            ? 'AT24C256 (0x${controller.extI2cAddr.toRadixString(16).toUpperCase().padLeft(2, '0')}) 在线'
                            : '未探测到 AT24C256')
                      : '离线本地已记忆（待连接下发）',
                  color: controller.connected && controller.extFlashPresent
                      ? scheme.primary
                      : scheme.onSurfaceVariant,
                ),
                StatusPill(
                  label: controller.mcuCfgSource == 2
                      ? '生效源：App 最高优先级（已写 I2C）'
                      : (controller.mcuCfgSource == 1
                            ? '生效源：手表 I2C EEPROM'
                            : '策略：${controller.appPriorityOverride ? "App 最高优先级" : "跟随 I2C"}'),
                ),
              ],
            ),
            const SizedBox(height: 10),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('App 最高优先级（连接自动覆盖 I2C）'),
              subtitle: const Text(
                '开启后，连接手表会立即将 App 的设置、步数目标和表盘写入 AT24C256；'
                '断开或重上电后手表默认按该 I2C 记录启动',
              ),
              value: controller.appPriorityOverride,
              onChanged: controller.setAppPriorityOverride,
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
        const SizedBox(height: 16),
        AppCard(
          padding: const EdgeInsets.all(22),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  ClipRRect(
                    borderRadius: BorderRadius.circular(16),
                    child: Image.asset(
                      'assets/images/about_logo.png',
                      width: 72,
                      height: 72,
                      fit: BoxFit.contain,
                    ),
                  ),
                  const SizedBox(width: 14),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        const Text(
                          '关于本作品 · 超级 Vibe Coding',
                          style: TextStyle(
                            fontSize: 16,
                            fontWeight: FontWeight.w800,
                          ),
                        ),
                        const SizedBox(height: 4),
                        Text(
                          'AT32 极客智能手表 & 双端伴侣（单片机大作业终极进化体）',
                          style: TextStyle(
                            color: scheme.primary,
                            fontSize: 12,
                            fontWeight: FontWeight.w600,
                          ),
                        ),
                      ],
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 14),
              Text(
                '一份单片机大作业收成能戴在手上的彩色小表：片内 Flash 和片外 EEPROM 加起来大约 100KB，屏幕 SPI 按 30MHz 超频在送。',
                style: TextStyle(
                  color: scheme.onSurfaceVariant,
                  fontSize: 12.5,
                  height: 1.5,
                ),
              ),
              const SizedBox(height: 12),
              const Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  Chip(label: Text('约 100KB 系统')),
                  Chip(label: Text('SPI 30MHz')),
                  Chip(label: Text('128×160')),
                  Chip(label: Text('1207 转子马达')),
                ],
              ),
              const SizedBox(height: 12),
              _AboutFacts(
                facts: const [
                  (
                    '整机存储',
                    '大约 100KB。片内 64KB Flash 预算，再加 AT24C256 的 32KB。实测固件大约 61.5KB，片内还剩约 4KB。',
                  ),
                  (
                    '主控',
                    'AT32F421K8T7，Cortex-M4，16KB SRAM。外部 8MHz 晶振倍到 120MHz；晶振不起振时退回内部时钟。',
                  ),
                  (
                    'SPI 超频',
                    'ST7735S 手册建议 15MHz。APB2 跑在 120MHz，上电按四分频送到 30MHz，DMA1 通道 3 异步刷屏。手表设置里可以退回 15MHz。',
                  ),
                  (
                    '屏幕',
                    '1.8 寸 128×160 RGB565。全宽 4 行脏条带局部刷新，画面不动时 SPI 不传数据。',
                  ),
                  (
                    '外置存储',
                    'AT24C256 走 I2C，ATD1 目录加 PackBits。掉电仍留着设置、相册表盘、最多 20 条长消息和按需页面。',
                  ),
                  (
                    '传感与震动',
                    'MPU6050 加 QMC5883L/P 做九轴。原 PA8 蜂鸣器改成 AO3400A 低边驱动 1207 转子马达，短按、长按、校准和新消息各有一段震动。',
                  ),
                  (
                    '链路',
                    'HC-04 经典蓝牙 SPP，115200。Windows 和 Android 共用这一套 Flutter 界面，通知在手机上光栅成 128 宽点阵再送上表。',
                  ),
                ],
              ),
            ],
          ),
        ),
        const SizedBox(height: 20),
        _SettingsActions(controller: controller, stretch: portraitPhone),
        ];
        final cards = <Widget>[
          for (final child in stack)
            if (child is! SizedBox) child,
        ];
        final body = Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            header,
            if (wide)
              Wrap(
                spacing: 16,
                runSpacing: 16,
                children: [
                  for (final card in cards)
                    SizedBox(width: (width - 16) / 2, child: card),
                ],
              )
            else
              ...stack,
          ],
        );
        return SingleChildScrollView(
          padding: EdgeInsets.fromLTRB(pad, 24, pad, portraitPhone ? 112 : 32),
          child: Align(
            alignment: Alignment.topCenter,
            child: SizedBox(width: width, child: body),
          ),
        );
      },
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

Future<void> _forgetBluetoothMemory(
  BuildContext context,
  WatchController controller,
) async {
  final agreed = await showDialog<bool>(
    context: context,
    builder: (dialogContext) {
      return AlertDialog(
        title: const Text('清除系统蓝牙记忆'),
        content: const Text(
          '会立刻断开手表，并删除这台设备系统里 HC-04 的配对记录。之后要重新输入配对码才能再连接。',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(dialogContext).pop(false),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () => Navigator.of(dialogContext).pop(true),
            child: const Text('清除'),
          ),
        ],
      );
    },
  );
  if (agreed != true || !context.mounted) return;
  final message = await AndroidWatchBridge.forgetBluetooth(
    controller.deviceAddress,
  );
  await controller.disconnect();
  if (!context.mounted) return;
  ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(message)));
}

class _AboutFacts extends StatelessWidget {
  const _AboutFacts({required this.facts});

  final List<(String, String)> facts;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return LayoutBuilder(
      builder: (context, constraints) {
        final two = constraints.maxWidth >= 640;
        Widget tile((String, String) fact) {
          return Container(
            width: double.infinity,
            padding: const EdgeInsets.fromLTRB(12, 10, 12, 10),
            decoration: BoxDecoration(
              color: scheme.surfaceContainerHighest.withValues(alpha: 0.55),
              borderRadius: BorderRadius.circular(12),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  fact.$1,
                  style: TextStyle(
                    color: scheme.primary,
                    fontSize: 12.5,
                    fontWeight: FontWeight.w700,
                  ),
                ),
                const SizedBox(height: 4),
                Text(
                  fact.$2,
                  style: TextStyle(
                    color: scheme.onSurfaceVariant,
                    fontSize: 12,
                    height: 1.45,
                  ),
                ),
              ],
            ),
          );
        }

        if (!two) {
          return Column(
            children: [
              for (var i = 0; i < facts.length; i++) ...[
                if (i > 0) const SizedBox(height: 8),
                tile(facts[i]),
              ],
            ],
          );
        }
        final width = (constraints.maxWidth - 8) / 2;
        return Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            for (final fact in facts) SizedBox(width: width, child: tile(fact)),
          ],
        );
      },
    );
  }
}

class _SettingsActions extends StatelessWidget {
  const _SettingsActions({required this.controller, required this.stretch});

  final WatchController controller;
  final bool stretch;

  @override
  Widget build(BuildContext context) {
    final buttons = <Widget>[
      FilledButton.icon(
        onPressed: () async {
          final messenger = ScaffoldMessenger.of(context);
          final ok = await controller.pushConfigToWatch(saveToI2c: true);
          messenger.showSnackBar(
            SnackBar(
              content: Text(
                ok
                    ? '已以 App 最高优先级覆盖写入手表 AT24C256。'
                    : '未连接手表：已保存在本地，连接后自动下发覆盖 I2C。',
              ),
            ),
          );
        },
        icon: const Icon(Icons.upload_rounded, size: 18),
        label: const Text('覆盖写入手表 I2C'),
      ),
      OutlinedButton.icon(
        onPressed: () async {
          final messenger = ScaffoldMessenger.of(context);
          final ok = await controller.pullConfigFromWatch();
          messenger.showSnackBar(
            SnackBar(
              content: Text(
                ok ? '已从手表 AT24C256 同步当前设置到 App。' : '请先连接手表。',
              ),
            ),
          );
        },
        icon: const Icon(Icons.download_rounded, size: 18),
        label: const Text('从手表 I2C 读取'),
      ),
      OutlinedButton.icon(
        onPressed: controller.resetSettings,
        icon: const Icon(Icons.restart_alt_rounded, size: 18),
        label: const Text('恢复默认'),
      ),
    ];
    if (!stretch) {
      return Wrap(spacing: 10, runSpacing: 10, children: buttons);
    }
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        for (var i = 0; i < buttons.length; i++) ...[
          if (i > 0) const SizedBox(height: 8),
          buttons[i],
        ],
      ],
    );
  }
}
