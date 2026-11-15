import 'dart:io';

import 'package:flutter/foundation.dart';

/// 把 Dart 侧未捕获异常记录到 exe 同目录的 `crash.log`。
///
/// 场景：用户反馈「用一下就闪退」时，我们能拿到具体异常与堆栈，
/// 而不是只有 Windows 事件日志里的一个模块偏移。
class CrashLog {
  CrashLog._();

  static final CrashLog instance = CrashLog._();

  File? _file;
  bool _resolved = false;

  File? get _target {
    if (_resolved) return _file;
    _resolved = true;
    if (kIsWeb) return null;
    try {
      final exe = File(Platform.resolvedExecutable);
      final candidate = File(
        '${exe.parent.path}${Platform.pathSeparator}crash.log',
      );
      candidate.writeAsStringSync('', mode: FileMode.append, flush: true);
      _file = candidate;
      return _file;
    } catch (_) {
      // 只读目录（如 Program Files）退回到临时目录。
    }
    try {
      _file = File(
        '${Directory.systemTemp.path}${Platform.pathSeparator}'
        'watch_companion_crash.log',
      );
      _file!.writeAsStringSync('', mode: FileMode.append, flush: true);
    } catch (_) {
      _file = null;
    }
    return _file;
  }

  void record(String source, Object error, StackTrace? stack) {
    final buffer = StringBuffer()
      ..writeln('===== ${DateTime.now().toIso8601String()} · $source =====')
      ..writeln(error);
    if (stack != null) buffer.writeln(stack);
    buffer.writeln();

    final target = _target;
    if (target == null) {
      debugPrint(buffer.toString());
      return;
    }
    try {
      target.writeAsStringSync(
        buffer.toString(),
        mode: FileMode.append,
        flush: true,
      );
    } catch (_) {
      debugPrint(buffer.toString());
    }
  }
}

/// 安装全局异常钩子：Flutter 框架错误、平台回调错误都会写日志。
void installCrashLogging() {
  FlutterError.onError = (details) {
    CrashLog.instance.record(
      'FlutterError',
      details.exceptionAsString(),
      details.stack,
    );
    FlutterError.presentError(details);
  };
  PlatformDispatcher.instance.onError = (error, stack) {
    CrashLog.instance.record('PlatformDispatcher', error, stack);
    return true;
  };
}
