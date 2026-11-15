// AT32 手表助手 · GPL-3.0-or-later

import 'dart:io';

import 'package:flutter/services.dart';

/// 系统托盘：设置页开关控制，关闭窗口后驻留由原生侧处理。
class SystemTray {
  const SystemTray._();

  static const MethodChannel _channel = MethodChannel('watch_companion/tray');

  static Future<void> setEnabled(bool enabled) async {
    if (!Platform.isWindows) return;
    try {
      await _channel.invokeMethod<void>('setEnabled', {'enabled': enabled});
    } on MissingPluginException {
      // 测试或非 Windows 环境没有原生实现。
    } on PlatformException {
      // 忽略托盘失败，不影响主功能。
    }
  }
}
