// AT32 手表助手 · GPL-3.0-or-later

import 'dart:async';
import 'dart:ui' as ui;

import 'watch_protocol.dart';

/// 把抓屏回传的 RGBA 像素转成可绘制的 [ui.Image]。
Future<ui.Image> decodeWatchSnapshot(WatchSnapshot snapshot) {
  final completer = Completer<ui.Image>();
  ui.decodeImageFromPixels(
    snapshot.rgba,
    snapshot.width,
    snapshot.height,
    ui.PixelFormat.rgba8888,
    completer.complete,
  );
  return completer.future;
}
