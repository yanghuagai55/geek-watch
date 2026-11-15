// AT32 手表助手 · GPL-3.0-or-later
//
// 把一行/两行文字栅格化成手表能直接贴图的 128×32 单色点阵。
// 固件内置字库只有约 500 字，任意歌词/视频标题必须走这条点阵通道。

import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/painting.dart';

import 'watch_protocol.dart';

class WatchBitmapLine {
  const WatchBitmapLine(this.text, {this.fontSize = 16});

  final String text;
  final double fontSize;
}

/// 返回 512 字节、MSB 在前的 128×32 单色位图。
Future<Uint8List> renderWatchBitmap(List<WatchBitmapLine> lines) async {
  const width = watchBitmapWidth;
  const height = watchBitmapHeight;
  final recorder = ui.PictureRecorder();
  final canvas = Canvas(recorder);
  final painters = <TextPainter>[];
  var totalHeight = 0.0;

  for (final line in lines) {
    final painter = TextPainter(
      text: TextSpan(
        text: line.text,
        style: TextStyle(
          color: const ui.Color(0xFFFFFFFF),
          fontSize: line.fontSize,
          height: 1.05,
          fontFamily: 'Microsoft YaHei UI',
          fontFamilyFallback: const ['Microsoft YaHei', 'SimSun', 'Segoe UI'],
        ),
      ),
      textDirection: ui.TextDirection.ltr,
      textAlign: ui.TextAlign.center,
      maxLines: 1,
      ellipsis: '…',
    )..layout(maxWidth: width.toDouble());
    painters.add(painter);
    totalHeight += painter.height;
  }
  totalHeight += (painters.length - 1) * 2;

  var y = (height - totalHeight) / 2;
  for (final painter in painters) {
    painter.paint(canvas, ui.Offset((width - painter.width) / 2, y));
    y += painter.height + 2;
  }

  final picture = recorder.endRecording();
  final image = await picture.toImage(width, height);
  final byteData = await image.toByteData(format: ui.ImageByteFormat.rawRgba);
  final bits = Uint8List(watchBitmapBytes);
  if (byteData != null) {
    final rgba = byteData.buffer.asUint8List(
      byteData.offsetInBytes,
      byteData.lengthInBytes,
    );
    for (var row = 0; row < height; row++) {
      for (var col = 0; col < width; col++) {
        final alpha = rgba[(row * width + col) * 4 + 3];
        if (alpha > 96) {
          bits[row * (width ~/ 8) + (col >> 3)] |= 0x80 >> (col & 7);
        }
      }
    }
  }

  image.dispose();
  picture.dispose();
  for (final painter in painters) {
    painter.dispose();
  }
  return bits;
}
