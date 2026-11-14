// AT32 手表助手 · GPL-3.0-or-later
//
// 简易 LRC 歌词库：按媒体标题在 %APPDATA%\AT32WatchCompanion\lyrics 下找同名
// .lrc 文件。没有歌词文件时，媒体跟随只显示标题/艺人。

import 'dart:io';

class LrcLine {
  const LrcLine(this.timeMs, this.text);

  final int timeMs;
  final String text;
}

/// 当前时间点对应的一行歌词，以及它到下一行的时间窗口。
class LrcCue {
  const LrcCue({
    required this.text,
    required this.startMs,
    required this.endMs,
  });

  final String text;
  final int startMs;
  final int endMs;

  int get durationMs => endMs - startMs;
}

class LrcLibrary {
  LrcLibrary({String? directory}) : directory = directory ?? defaultDirectory;

  final String directory;
  final Map<String, List<LrcLine>> _cache = <String, List<LrcLine>>{};

  static String get defaultDirectory {
    final base = Platform.environment['APPDATA'];
    if (base == null || base.isEmpty) {
      return '${Directory.current.path}\\lyrics';
    }
    return '$base\\AT32WatchCompanion\\lyrics';
  }

  /// 返回当前歌词行及其到下一行的时间窗口，供分页/进度推送使用。
  Future<LrcCue?> cueAt(String title, int positionMs) async {
    final lines = await _load(title);
    if (lines == null || lines.isEmpty) return null;
    var index = -1;
    for (var i = 0; i < lines.length; i++) {
      if (lines[i].timeMs <= positionMs) {
        index = i;
      } else {
        break;
      }
    }
    if (index < 0) return null;
    final line = lines[index];
    final end = index + 1 < lines.length
        ? lines[index + 1].timeMs
        : line.timeMs + 5000;
    final text = line.text.trim();
    if (text.isEmpty) return null;
    return LrcCue(text: text, startMs: line.timeMs, endMs: end);
  }

  Future<List<LrcLine>?> _load(String title) async {
    final name = _fileName(title);
    if (name == null) return null;
    final cached = _cache[name];
    if (cached != null) return cached;

    final file = File('$directory\\$name.lrc');
    if (!await file.exists()) {
      _cache[name] = const <LrcLine>[];
      return null;
    }
    try {
      final content = await file.readAsString();
      final lines = parse(content);
      _cache[name] = lines;
      return lines;
    } catch (_) {
      _cache[name] = const <LrcLine>[];
      return null;
    }
  }

  static List<LrcLine> parse(String content) {
    final result = <LrcLine>[];
    final stamp = RegExp(r'\[(\d{1,3}):(\d{1,2})(?:\.(\d{1,3}))?\]');
    for (final raw in content.split('\n')) {
      final matches = stamp.allMatches(raw).toList();
      if (matches.isEmpty) continue;
      final text = raw.replaceAll(stamp, '').trim();
      if (text.isEmpty) continue;
      for (final match in matches) {
        final minutes = int.tryParse(match.group(1) ?? '') ?? 0;
        final seconds = int.tryParse(match.group(2) ?? '') ?? 0;
        final fraction = match.group(3) ?? '';
        final millis = switch (fraction.length) {
          0 => 0,
          1 => int.parse(fraction) * 100,
          2 => int.parse(fraction) * 10,
          _ => int.parse(fraction.substring(0, 3)),
        };
        result.add(LrcLine((minutes * 60 + seconds) * 1000 + millis, text));
      }
    }
    result.sort((a, b) => a.timeMs.compareTo(b.timeMs));
    return result;
  }

  static String? _fileName(String title) {
    var name = title.trim();
    if (name.isEmpty) return null;
    name = name.replaceAll(RegExp(r'[\\/:*?"<>|]'), '_');
    if (name.length > 80) name = name.substring(0, 80);
    return name;
  }
}
