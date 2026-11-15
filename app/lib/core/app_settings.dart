// AT32 手表助手 · GPL-3.0-or-later
//
// 极简设置持久化：%APPDATA%\AT32WatchCompanion\settings.json。
// 不引入 shared_preferences，保持 Windows 端零插件依赖。

import 'dart:convert';
import 'dart:io';

class AppSettings {
  const AppSettings._();

  static String get directory {
    final base = Platform.environment['APPDATA'];
    if (base == null || base.isEmpty) {
      return '${Directory.current.path}\\AT32WatchCompanion';
    }
    return '$base\\AT32WatchCompanion';
  }

  static String get _path => '$directory\\settings.json';

  static Future<Map<String, dynamic>> load() async {
    try {
      final file = File(_path);
      if (!await file.exists()) return <String, dynamic>{};
      final raw = jsonDecode(await file.readAsString());
      return raw is Map<String, dynamic> ? raw : <String, dynamic>{};
    } catch (_) {
      return <String, dynamic>{};
    }
  }

  static Future<void> save(Map<String, Object?> values) async {
    try {
      final folder = Directory(directory);
      if (!await folder.exists()) await folder.create(recursive: true);
      await File(_path).writeAsString(jsonEncode(values));
    } catch (_) {
      // 设置写入失败不影响主功能。
    }
  }
}
