// AT32 手表助手 · GPL-3.0-or-later
//
// Windows 全局媒体会话（SMTC）只提供标题 / 艺人 / 播放状态，不提供歌词。
// 歌词通过同名 .lrc 文件补足，见 lrc_library.dart。

import 'dart:io';

import 'package:flutter/services.dart';

class NowPlaying {
  const NowPlaying({
    required this.hasSession,
    required this.title,
    required this.artist,
    required this.appId,
    required this.status,
    required this.positionMs,
    required this.durationMs,
  });

  final bool hasSession;
  final String title;
  final String artist;
  final String appId;
  final String status;
  final int positionMs;
  final int durationMs;

  bool get playing => status == 'playing';

  static NowPlaying? fromMap(Object? raw) {
    if (raw is! Map) return null;
    String text(Object? value) => value is String ? value : '';
    int number(Object? value) => value is int ? value : 0;
    return NowPlaying(
      hasSession: raw['hasSession'] == true,
      title: text(raw['title']),
      artist: text(raw['artist']),
      appId: text(raw['appId']),
      status: text(raw['status']),
      positionMs: number(raw['position']),
      durationMs: number(raw['duration']),
    );
  }
}

class MediaMonitor {
  const MediaMonitor();

  static const MethodChannel _channel = MethodChannel('watch_companion/media');

  Future<NowPlaying?> nowPlaying() async {
    if (!Platform.isWindows) return null;
    try {
      final raw = await _channel.invokeMethod<Object?>('nowPlaying');
      return NowPlaying.fromMap(raw);
    } on MissingPluginException {
      return null;
    } on PlatformException {
      return null;
    }
  }

  /// 控制当前系统媒体：play / pause / toggle / next / previous。
  Future<void> control(String action) async {
    if (!Platform.isWindows) return;
    try {
      await _channel.invokeMethod<void>('control', {'action': action});
    } on MissingPluginException {
      // 测试或非 Windows 环境没有原生实现。
    } on PlatformException {
      // 没有可控制的媒体会话时忽略。
    }
  }
}
