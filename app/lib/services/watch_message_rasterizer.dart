// AT32 手表助手 · GPL-3.0-or-later
//
// 跨平台消息点阵栅格化（Windows / Android 共用）：
//  - 单行列表头：128×22（352 B，用于 0x2D 增量写入 AT24C256 动态 ROM）
//  - 整屏详情图：128×160（2560 B，用于 0x26/0x23 动态槽位持久化）

import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/painting.dart';

class WatchMessageItem {
  const WatchMessageItem({
    required this.app,
    this.title = '',
    required this.text,
  });

  final String app;
  final String title;
  final String text;
}

abstract final class WatchMessageRasterizer {
  static const int width = 128;
  static const int rowHeight = 22;
  static const int rowBytes = width * rowHeight ~/ 8;
  static const int detailHeight = 160;
  static const int detailBytes = width * detailHeight ~/ 8;
  static const int compassAssetId = 1;
  static const int compassW = 64;
  static const int compassH = 64;
  static const int compassBytes = compassW * compassH ~/ 8;
  static const int msgEmptyAssetId = 2;
  static const int msgEmptyW = 96;
  static const int msgEmptyH = 40;
  static const int msgEmptyBytes = msgEmptyW * msgEmptyH ~/ 8;
  static const double _margin = 4;

  static TextStyle _style(double size, {FontWeight weight = FontWeight.w400}) {
    return TextStyle(
      color: const ui.Color(0xFFFFFFFF),
      fontSize: size,
      fontWeight: weight,
      height: 1.15,
      fontFamily: 'Microsoft YaHei UI',
      fontFamilyFallback: const ['Microsoft YaHei', 'SimSun', 'Segoe UI'],
    );
  }

  /// 渲染单行应用名/标题点阵（128×22，共 352 字节）。
  static Future<Uint8List> renderRow(WatchMessageItem item) async {
    final recorder = ui.PictureRecorder();
    final canvas = Canvas(recorder);
    final rawHeading = item.app.trim().isNotEmpty
        ? item.app.trim()
        : item.title.trim().isNotEmpty
        ? item.title.trim()
        : item.text.trim();
    final heading = rawHeading.isEmpty ? '消息' : rawHeading;
    final painter = TextPainter(
      text: TextSpan(
        text: heading,
        style: _style(12, weight: FontWeight.w700),
      ),
      textDirection: ui.TextDirection.ltr,
      maxLines: 1,
      ellipsis: '…',
    )..layout(maxWidth: width - _margin * 2);
    final dy = (rowHeight - painter.height) / 2;
    painter.paint(canvas, ui.Offset(_margin, dy));
    painter.dispose();
    return _pack(recorder, width, rowHeight, rowBytes);
  }

  /// 渲染整屏详情点阵（128×160，共 2560 字节），自动匹配字号换行。
  static Future<Uint8List> renderDetail(WatchMessageItem? item) async {
    final recorder = ui.PictureRecorder();
    final canvas = Canvas(recorder);
    final text = _detailText(item);
    if (text.isEmpty) {
      final empty = TextPainter(
        text: TextSpan(text: '无消息', style: _style(16)),
        textDirection: ui.TextDirection.ltr,
      )..layout(maxWidth: width - _margin * 2);
      empty.paint(
        canvas,
        ui.Offset((width - empty.width) / 2, (detailHeight - empty.height) / 2),
      );
      empty.dispose();
      return _pack(recorder, width, detailHeight, detailBytes);
    }

    final maxW = width - _margin * 2;
    TextPainter? chosen;
    for (var size = 16.0; size >= 9.0; size -= 1.0) {
      final painter = TextPainter(
        text: TextSpan(text: text, style: _style(size)),
        textDirection: ui.TextDirection.ltr,
        ellipsis: '…',
      )..layout(maxWidth: maxW);
      chosen?.dispose();
      chosen = painter;
      if (painter.height <= detailHeight - 6) break;
    }
    chosen?.paint(canvas, const ui.Offset(_margin, 3));
    chosen?.dispose();
    return _pack(recorder, width, detailHeight, detailBytes);
  }

  /// 光栅化 64×64 1bpp 指南针表盘底图（外圈 + 8 刻度 + N/E/S/W 方位标），供 0x2F 写入片外 ROM。
  static Future<Uint8List> renderCompassAsset() async {
    final recorder = ui.PictureRecorder();
    final canvas = ui.Canvas(
      recorder,
      ui.Rect.fromLTWH(0, 0, compassW.toDouble(), compassH.toDouble()),
    );
    const white = ui.Color(0xFFFFFFFF);
    final ringPaint = ui.Paint()
      ..color = white
      ..style = ui.PaintingStyle.stroke
      ..strokeWidth = 2.0
      ..isAntiAlias = false;
    final tickPaint = ui.Paint()
      ..color = white
      ..strokeWidth = 1.4
      ..isAntiAlias = false;

    canvas.drawCircle(const ui.Offset(32, 32), 29.0, ringPaint);
    for (var i = 0; i < 8; i++) {
      final rad = i * math.pi / 4.0;
      final rIn = (i % 2 == 0) ? 23.0 : 25.0;
      canvas.drawLine(
        ui.Offset(32 + math.sin(rad) * rIn, 32 - math.cos(rad) * rIn),
        ui.Offset(32 + math.sin(rad) * 28.0, 32 - math.cos(rad) * 28.0),
        tickPaint,
      );
    }
    const labels = <(String, ui.Offset)>[
      ('N', ui.Offset(32, 12.5)),
      ('E', ui.Offset(51.5, 32)),
      ('S', ui.Offset(32, 51.5)),
      ('W', ui.Offset(12.5, 32)),
    ];
    for (final (ch, pos) in labels) {
      final tp = TextPainter(
        text: TextSpan(
          text: ch,
          style: const TextStyle(
            color: white,
            fontSize: 9,
            fontWeight: FontWeight.w700,
            height: 1.0,
          ),
        ),
        textDirection: ui.TextDirection.ltr,
      )..layout();
      tp.paint(
        canvas,
        ui.Offset(pos.dx - tp.width / 2, pos.dy - tp.height / 2),
      );
      tp.dispose();
    }
    return _pack(recorder, compassW, compassH, compassBytes);
  }

  /// 光栅化 96×40 1bpp 空消息横幅（信封图标 + 提示文案），供 0x2F 写入片外 ROM。
  static Future<Uint8List> renderMsgEmptyAsset() async {
    final recorder = ui.PictureRecorder();
    final canvas = ui.Canvas(
      recorder,
      ui.Rect.fromLTWH(0, 0, msgEmptyW.toDouble(), msgEmptyH.toDouble()),
    );
    const white = ui.Color(0xFFFFFFFF);
    final stroke = ui.Paint()
      ..color = white
      ..style = ui.PaintingStyle.stroke
      ..strokeWidth = 1.6
      ..isAntiAlias = false;

    final box = ui.RRect.fromRectAndRadius(
      const ui.Rect.fromLTWH(31, 3, 34, 21),
      const ui.Radius.circular(2),
    );
    canvas.drawRRect(box, stroke);
    final flap = ui.Path()
      ..moveTo(32, 5)
      ..lineTo(48, 15)
      ..lineTo(64, 5);
    canvas.drawPath(flap, stroke);

    final tp = TextPainter(
      text: const TextSpan(
        text: '暂无通知',
        style: TextStyle(
          color: white,
          fontSize: 11,
          fontWeight: FontWeight.w600,
          height: 1.05,
        ),
      ),
      textDirection: ui.TextDirection.ltr,
      textAlign: TextAlign.center,
    )..layout(maxWidth: msgEmptyW.toDouble());
    tp.paint(canvas, ui.Offset((msgEmptyW - tp.width) / 2, 27));
    tp.dispose();
    return _pack(recorder, msgEmptyW, msgEmptyH, msgEmptyBytes);
  }

  static String _detailText(WatchMessageItem? item) {
    if (item == null) return '';
    final app = item.app.trim();
    final title = item.title.trim();
    final body = item.text.trim().replaceAll('\r', '');
    final parts = <String>[
      if (app.isNotEmpty) app,
      if (title.isNotEmpty && title != app) title,
      if (body.isNotEmpty && body != title) body,
    ];
    return parts.join('\n');
  }

  static Future<Uint8List> _pack(
    ui.PictureRecorder recorder,
    int w,
    int h,
    int expectedBytes,
  ) async {
    final picture = recorder.endRecording();
    final image = await picture.toImage(w, h);
    final byteData = await image.toByteData(format: ui.ImageByteFormat.rawRgba);
    final bits = Uint8List(expectedBytes);
    if (byteData != null) {
      final rgba = byteData.buffer.asUint8List(
        byteData.offsetInBytes,
        byteData.lengthInBytes,
      );
      final stride = w ~/ 8;
      for (var row = 0; row < h; row++) {
        for (var col = 0; col < w; col++) {
          if (rgba[(row * w + col) * 4 + 3] > 96) {
            bits[row * stride + (col >> 3)] |= 0x80 >> (col & 7);
          }
        }
      }
    }
    image.dispose();
    picture.dispose();
    return bits;
  }
}
