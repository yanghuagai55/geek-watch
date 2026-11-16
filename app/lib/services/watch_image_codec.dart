// AT32 手表助手 · GPL-3.0-or-later
//
// 针对 128×160 ST7735S 屏与外挂 AT24C256 (32KB) 设计的端到端压缩图像编解码器：
// - UI_EXT_FMT_1BPP      (1): 128×160 黑白像素流，2,560 字节（支持 Atkinson 抖动）
// - UI_EXT_FMT_PAL4_RLE  (4): Median-Cut 16 色自适应调色板 + Floyd-Steinberg 抖动 + 逐行偏移 RLE
//                             最坏不超过 10,752 字节（保证必能写入 AT24C256 17KB 图库区！）
// - UI_EXT_FMT_RGB565_RLE(5): 无损真彩 16-bit 逐行偏移 PackBits RLE

import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'watch_protocol.dart';

export 'watch_protocol.dart' show argbToRgb565, rgb565ToArgb;

const int watchDisplayWidth = 128;
const int watchDisplayHeight = 160;
const int watchExtImgMaxBytes = 0x4400; // 17,408 B (0x3C00..0x7FFF)

enum WatchExtImgFormat {
  pal4Rle(4, '16色抖动压缩 (PAL4-RLE)'),
  rgb565Rle(5, '真彩行程压缩 (RGB565-RLE)'),
  mono1bpp(1, '极简黑白点阵 (1bpp)');

  const WatchExtImgFormat(this.code, this.label);
  final int code;
  final String label;
}

class EncodedWatchImage {
  const EncodedWatchImage({
    required this.width,
    required this.height,
    required this.format,
    required this.bytes,
    required this.previewRgba,
  });

  final int width;
  final int height;
  final WatchExtImgFormat format;
  final Uint8List bytes;
  final Uint8List previewRgba;

  int get rawRgb565Bytes => width * height * 2;
  bool get fitsFlash => bytes.length <= watchExtImgMaxBytes;
  double get compressionRatio =>
      bytes.isEmpty ? 1.0 : rawRgb565Bytes / bytes.length;
}

int _clampByte(int v) => v < 0 ? 0 : (v > 255 ? 255 : v);

int _snapRgb565(int r, int g, int b) {
  final r5 = (_clampByte(r) >> 3) & 0x1F;
  final g6 = (_clampByte(g) >> 2) & 0x3F;
  final b5 = (_clampByte(b) >> 3) & 0x1F;
  return (r5 << 11) | (g6 << 5) | b5;
}

/// 将任意已编码图片（PNG/JPG/BMP/WebP）裁切缩放为手表 128×160 RGBA 缓冲，并支持亮度/对比度调节。
Future<Uint8List> cropImageBytesToRgba(
  Uint8List fileBytes, {
  int targetWidth = watchDisplayWidth,
  int targetHeight = watchDisplayHeight,
  double zoom = 1.0,
  double panX = 0.0,
  double panY = 0.0,
  double brightness = 0.0,
  double contrast = 1.0,
}) async {
  final codec = await ui.instantiateImageCodec(fileBytes);
  final frame = await codec.getNextFrame();
  final img = frame.image;
  try {
    final recorder = ui.PictureRecorder();
    final canvas = ui.Canvas(
      recorder,
      ui.Rect.fromLTWH(0, 0, targetWidth.toDouble(), targetHeight.toDouble()),
    );
    final iw = img.width.toDouble();
    final ih = img.height.toDouble();
    final baseScale = math.max(targetWidth / iw, targetHeight / ih);
    final drawW = iw * baseScale * zoom.clamp(1.0, 3.0);
    final drawH = ih * baseScale * zoom.clamp(1.0, 3.0);
    final maxDx = math.max(0.0, (drawW - targetWidth) / 2);
    final maxDy = math.max(0.0, (drawH - targetHeight) / 2);
    final left = (targetWidth - drawW) / 2 - panX.clamp(-1.0, 1.0) * maxDx;
    final top = (targetHeight - drawH) / 2 - panY.clamp(-1.0, 1.0) * maxDy;

    canvas.drawImageRect(
      img,
      ui.Rect.fromLTWH(0, 0, iw, ih),
      ui.Rect.fromLTWH(left, top, drawW, drawH),
      ui.Paint()..filterQuality = ui.FilterQuality.high,
    );
    final pic = recorder.endRecording();
    final outImg = await pic.toImage(targetWidth, targetHeight);
    pic.dispose();
    final bd = await outImg.toByteData(format: ui.ImageByteFormat.rawRgba);
    outImg.dispose();
    if (bd == null) {
      return Uint8List(targetWidth * targetHeight * 4);
    }
    final rgba = Uint8List.fromList(bd.buffer.asUint8List());
    if (brightness.abs() > 0.001 || (contrast - 1.0).abs() > 0.001) {
      final bShift = brightness * 255.0;
      for (var i = 0; i < rgba.length; i += 4) {
        rgba[i] = _clampByte(((rgba[i] - 128) * contrast + 128 + bShift).round());
        rgba[i + 1] = _clampByte(
          ((rgba[i + 1] - 128) * contrast + 128 + bShift).round(),
        );
        rgba[i + 2] = _clampByte(
          ((rgba[i + 2] - 128) * contrast + 128 + bShift).round(),
        );
        rgba[i + 3] = 255;
      }
    }
    return rgba;
  } finally {
    img.dispose();
    codec.dispose();
  }
}

/// 内置 4 套 128×160 程序生成高清表盘底图（无需外接素材即可直接测试与写入 AT24C256）。
Uint8List generateBuiltInWallpaperRgba(
  String preset, {
  int width = watchDisplayWidth,
  int height = watchDisplayHeight,
}) {
  final out = Uint8List(width * height * 4);
  for (var y = 0; y < height; y++) {
    final ny = y / (height - 1);
    for (var x = 0; x < width; x++) {
      final nx = x / (width - 1);
      int r = 10, g = 16, b = 20;
      switch (preset) {
        case 'aurora':
          final wave =
              0.38 +
              0.12 * math.sin(nx * math.pi * 2.3) +
              0.06 * math.cos(nx * math.pi * 4.5);
          final band = math.exp(-math.pow((ny - wave) * 6.5, 2));
          final mount =
              0.72 +
              0.08 * math.sin(nx * 5.2 + 0.4).abs() -
              0.04 * math.cos(nx * 9.0);
          if (ny > mount) {
            r = 8;
            g = 14;
            b = 18;
          } else {
            r = _clampByte((10 + band * 48 + (1 - ny) * 18).round());
            g = _clampByte((20 + band * 185 + (1 - ny) * 32).round());
            b = _clampByte((32 + band * 130 + (1 - ny) * 48).round());
          }
        case 'amber':
          final dx = nx - 0.5;
          final dy = (ny - 0.38) * (height / width);
          final d = math.sqrt(dx * dx + dy * dy);
          final glow = math.max(0.0, 1.0 - d * 1.65);
          r = _clampByte((18 + glow * glow * 230).round());
          g = _clampByte((14 + glow * glow * 145).round());
          b = _clampByte((10 + glow * 36).round());
          if (ny > 0.74) {
            final horizon = 1.0 - (ny - 0.74) / 0.26;
            r = _clampByte((r * horizon * 0.55).round());
            g = _clampByte((g * horizon * 0.5).round());
            b = _clampByte((b * horizon * 0.6).round());
          }
        case 'cyber':
          final bgGlow = math.exp(-math.pow((ny - 0.45) * 3.5, 2));
          r = _clampByte((14 + bgGlow * 70 + (nx * 24)).round());
          g = _clampByte((10 + bgGlow * 28 + (1 - ny) * 45).round());
          b = _clampByte((32 + bgGlow * 140 + (nx * 40)).round());
          if ((y % 16 == 0) || (x % 16 == 0)) {
            g = _clampByte(g + 38);
            b = _clampByte(b + 55);
          }
          if ((y - (height * 0.55)).abs() < 1.5) {
            r = 255;
            g = 165;
            b = 85;
          }
        default: // 'starry'
          r = _clampByte((8 + (1 - ny) * 18).round());
          g = _clampByte((14 + (1 - ny) * 32).round());
          b = _clampByte((26 + (1 - ny) * 58).round());
          final hVal = ((x * 73856093) ^ (y * 19349663)) & 0xFF;
          if (hVal > 251 && ny < 0.78) {
            r = 236;
            g = 242;
            b = 225;
          } else if (ny > 0.78) {
            r = 12;
            g = 22;
            b = 20;
          }
      }
      final idx = (y * width + x) * 4;
      out[idx] = r;
      out[idx + 1] = g;
      out[idx + 2] = b;
      out[idx + 3] = 255;
    }
  }
  return out;
}

EncodedWatchImage encodeWatchImage(
  Uint8List rgba, {
  required WatchExtImgFormat format,
  int width = watchDisplayWidth,
  int height = watchDisplayHeight,
  bool dither = true,
  int accentArgb = 0xFF73D4AE,
}) {
  return switch (format) {
    WatchExtImgFormat.pal4Rle =>
      _encodePal4Rle(rgba, width: width, height: height, dither: dither),
    WatchExtImgFormat.rgb565Rle => _encodeRgb565Rle(
      rgba,
      width: width,
      height: height,
    ),
    WatchExtImgFormat.mono1bpp => _encodeMono1bpp(
      rgba,
      width: width,
      height: height,
      dither: dither,
      accentArgb: accentArgb,
    ),
  };
}

class _ColorBox {
  _ColorBox(this.pixels);
  final List<int> pixels; // packed 0xRRGGBB

  int get length => pixels.length;

  (int minR, int maxR, int minG, int maxG, int minB, int maxB) _bounds() {
    var minR = 255, maxR = 0, minG = 255, maxG = 0, minB = 255, maxB = 0;
    for (final c in pixels) {
      final r = (c >> 16) & 0xFF;
      final g = (c >> 8) & 0xFF;
      final b = c & 0xFF;
      if (r < minR) minR = r;
      if (r > maxR) maxR = r;
      if (g < minG) minG = g;
      if (g > maxG) maxG = g;
      if (b < minB) minB = b;
      if (b > maxB) maxB = b;
    }
    return (minR, maxR, minG, maxG, minB, maxB);
  }

  int get maxSpan {
    final (minR, maxR, minG, maxG, minB, maxB) = _bounds();
    return math.max(maxR - minR, math.max(maxG - minG, maxB - minB));
  }

  List<_ColorBox> split() {
    if (pixels.length < 2) return [this];
    final (minR, maxR, minG, maxG, minB, maxB) = _bounds();
    final dr = maxR - minR;
    final dg = maxG - minG;
    final db = maxB - minB;
    final shift = dr >= dg && dr >= db ? 16 : (dg >= db ? 8 : 0);
    final sorted = List<int>.from(pixels)
      ..sort((a, b) => ((a >> shift) & 0xFF).compareTo((b >> shift) & 0xFF));
    final mid = sorted.length >> 1;
    return [
      _ColorBox(sorted.sublist(0, mid)),
      _ColorBox(sorted.sublist(mid)),
    ];
  }

  int averageRgb565() {
    if (pixels.isEmpty) return 0;
    var sr = 0, sg = 0, sb = 0;
    for (final c in pixels) {
      sr += (c >> 16) & 0xFF;
      sg += (c >> 8) & 0xFF;
      sb += c & 0xFF;
    }
    final n = pixels.length;
    return _snapRgb565((sr / n).round(), (sg / n).round(), (sb / n).round());
  }
}

List<int> _medianCutPalette16(Uint8List rgba, int width, int height) {
  final step = (width * height) > 5120 ? 2 : 1;
  final samples = <int>[];
  for (var y = 0; y < height; y += step) {
    for (var x = 0; x < width; x += step) {
      final i = (y * width + x) * 4;
      samples.add((rgba[i] << 16) | (rgba[i + 1] << 8) | rgba[i + 2]);
    }
  }
  final boxes = <_ColorBox>[_ColorBox(samples)];
  while (boxes.length < 16) {
    boxes.sort((a, b) => b.maxSpan.compareTo(a.maxSpan));
    final top = boxes.first;
    if (top.length < 2 || top.maxSpan == 0) break;
    boxes.removeAt(0);
    boxes.addAll(top.split());
  }
  final pal = <int>[for (final box in boxes) box.averageRgb565()];
  while (pal.length < 16) {
    pal.add(pal.isEmpty ? 0 : pal.last);
  }
  return pal.sublist(0, 16);
}

EncodedWatchImage _encodePal4Rle(
  Uint8List rgba, {
  required int width,
  required int height,
  required bool dither,
}) {
  final pal565 = _medianCutPalette16(rgba, width, height);
  final palArgb = <int>[for (final c in pal565) rgb565ToArgb(c)];
  final palR = <int>[for (final c in palArgb) (c >> 16) & 0xFF];
  final palG = <int>[for (final c in palArgb) (c >> 8) & 0xFF];
  final palB = <int>[for (final c in palArgb) c & 0xFF];

  final err = Int32List(width * height * 3);
  for (var i = 0; i < width * height; i++) {
    err[i * 3] = rgba[i * 4];
    err[i * 3 + 1] = rgba[i * 4 + 1];
    err[i * 3 + 2] = rgba[i * 4 + 2];
  }

  final indices = Uint8List(width * height);
  final preview = Uint8List(width * height * 4);

  int nearestColor(int r, int g, int b) {
    var best = 0;
    var bestDist = 0x7FFFFFFF;
    for (var k = 0; k < 16; k++) {
      final dr = r - palR[k];
      final dg = g - palG[k];
      final db = b - palB[k];
      final dist = dr * dr * 3 + dg * dg * 4 + db * db * 2;
      if (dist < bestDist) {
        bestDist = dist;
        best = k;
        if (dist == 0) break;
      }
    }
    return best;
  }

  void addErr(int x, int y, int er, int eg, int eb, int mul) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    final p = (y * width + x) * 3;
    err[p] += (er * mul) >> 4;
    err[p + 1] += (eg * mul) >> 4;
    err[p + 2] += (eb * mul) >> 4;
  }

  for (var y = 0; y < height; y++) {
    for (var x = 0; x < width; x++) {
      final p = (y * width + x) * 3;
      final r = _clampByte(err[p]);
      final g = _clampByte(err[p + 1]);
      final b = _clampByte(err[p + 2]);
      final idx = nearestColor(r, g, b);
      indices[y * width + x] = idx;
      final px = (y * width + x) * 4;
      preview[px] = palR[idx];
      preview[px + 1] = palG[idx];
      preview[px + 2] = palB[idx];
      preview[px + 3] = 255;

      if (dither) {
        final er = (r - palR[idx]).clamp(-64, 64);
        final eg = (g - palG[idx]).clamp(-64, 64);
        final eb = (b - palB[idx]).clamp(-64, 64);
        addErr(x + 1, y, er, eg, eb, 7);
        addErr(x - 1, y + 1, er, eg, eb, 3);
        addErr(x, y + 1, er, eg, eb, 5);
        addErr(x + 1, y + 1, er, eg, eb, 1);
      }
    }
  }

  // 组装二进制：palette[16](32B) + row_end[H](2*H B) + RLE 像素数据
  final headerLen = 32 + height * 2;
  final data = BytesBuilder(copy: false);
  final rowEnds = Uint16List(height);

  for (var y = 0; y < height; y++) {
    final rowOff = y * width;
    var x = 0;
    while (x < width) {
      final first = indices[rowOff + x];
      var run = 1;
      while (x + run < width &&
          run < 129 &&
          indices[rowOff + x + run] == first) {
        run++;
      }
      if (run >= 2) {
        data.addByte(0x80 | (run - 2));
        data.addByte(first & 0x0F);
        x += run;
      } else {
        final litStart = x;
        var lit = 0;
        while (x < width && lit < 128) {
          final rem = width - x;
          if (rem >= 3 &&
              indices[rowOff + x] == indices[rowOff + x + 1] &&
              indices[rowOff + x] == indices[rowOff + x + 2]) {
            break;
          }
          x++;
          lit++;
        }
        data.addByte(lit - 1);
        for (var k = 0; k < lit; k += 2) {
          final hi = indices[rowOff + litStart + k] & 0x0F;
          final lo = (k + 1 < lit) ? (indices[rowOff + litStart + k + 1] & 0x0F) : 0;
          data.addByte((hi << 4) | lo);
        }
      }
    }
    rowEnds[y] = data.length;
  }

  final body = data.takeBytes();
  final out = Uint8List(headerLen + body.length);
  for (var k = 0; k < 16; k++) {
    out[k * 2] = pal565[k] & 0xFF;
    out[k * 2 + 1] = (pal565[k] >> 8) & 0xFF;
  }
  for (var y = 0; y < height; y++) {
    final v = rowEnds[y];
    out[32 + y * 2] = v & 0xFF;
    out[32 + y * 2 + 1] = (v >> 8) & 0xFF;
  }
  out.setRange(headerLen, out.length, body);

  return EncodedWatchImage(
    width: width,
    height: height,
    format: WatchExtImgFormat.pal4Rle,
    bytes: out,
    previewRgba: preview,
  );
}

EncodedWatchImage _encodeRgb565Rle(
  Uint8List rgba, {
  required int width,
  required int height,
}) {
  final pixels565 = Uint16List(width * height);
  final preview = Uint8List(width * height * 4);
  for (var i = 0; i < width * height; i++) {
    final c = _snapRgb565(rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]);
    pixels565[i] = c;
    final argb = rgb565ToArgb(c);
    preview[i * 4] = (argb >> 16) & 0xFF;
    preview[i * 4 + 1] = (argb >> 8) & 0xFF;
    preview[i * 4 + 2] = argb & 0xFF;
    preview[i * 4 + 3] = 255;
  }

  final headerLen = height * 2;
  final data = BytesBuilder(copy: false);
  final rowEnds = Uint16List(height);

  for (var y = 0; y < height; y++) {
    final rowOff = y * width;
    var x = 0;
    while (x < width) {
      final first = pixels565[rowOff + x];
      var run = 1;
      while (x + run < width &&
          run < 129 &&
          pixels565[rowOff + x + run] == first) {
        run++;
      }
      if (run >= 2) {
        data.addByte(0x80 | (run - 2));
        data.addByte(first & 0xFF);
        data.addByte((first >> 8) & 0xFF);
        x += run;
      } else {
        final litStart = x;
        var lit = 0;
        while (x < width && lit < 128) {
          if (x + 1 < width && pixels565[rowOff + x] == pixels565[rowOff + x + 1]) {
            break;
          }
          x++;
          lit++;
        }
        data.addByte(lit - 1);
        for (var k = 0; k < lit; k++) {
          final c = pixels565[rowOff + litStart + k];
          data.addByte(c & 0xFF);
          data.addByte((c >> 8) & 0xFF);
        }
      }
    }
    rowEnds[y] = data.length;
  }

  final body = data.takeBytes();
  final out = Uint8List(headerLen + body.length);
  for (var y = 0; y < height; y++) {
    final v = rowEnds[y];
    out[y * 2] = v & 0xFF;
    out[y * 2 + 1] = (v >> 8) & 0xFF;
  }
  out.setRange(headerLen, out.length, body);

  return EncodedWatchImage(
    width: width,
    height: height,
    format: WatchExtImgFormat.rgb565Rle,
    bytes: out,
    previewRgba: preview,
  );
}

EncodedWatchImage _encodeMono1bpp(
  Uint8List rgba, {
  required int width,
  required int height,
  required bool dither,
  required int accentArgb,
}) {
  final luma = Int32List(width * height);
  for (var i = 0; i < width * height; i++) {
    luma[i] = (rgba[i * 4] * 77 + rgba[i * 4 + 1] * 150 + rgba[i * 4 + 2] * 29) >> 8;
  }
  final bpr = (width + 7) >> 3;
  final out = Uint8List(bpr * height);
  final preview = Uint8List(width * height * 4);
  final ar = (accentArgb >> 16) & 0xFF;
  final ag = (accentArgb >> 8) & 0xFF;
  final ab = accentArgb & 0xFF;

  void addErr(int x, int y, int e, int mul) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    luma[y * width + x] += (e * mul) >> 4;
  }

  for (var y = 0; y < height; y++) {
    for (var x = 0; x < width; x++) {
      final p = y * width + x;
      final v = luma[p].clamp(0, 255);
      final on = v >= 128;
      if (on) {
        out[y * bpr + (x >> 3)] |= 0x80 >> (x & 7);
      }
      preview[p * 4] = on ? ar : 8;
      preview[p * 4 + 1] = on ? ag : 12;
      preview[p * 4 + 2] = on ? ab : 10;
      preview[p * 4 + 3] = 255;
      if (dither) {
        final e = (v - (on ? 255 : 0)).clamp(-64, 64);
        addErr(x + 1, y, e, 7);
        addErr(x - 1, y + 1, e, 3);
        addErr(x, y + 1, e, 5);
        addErr(x + 1, y + 1, e, 1);
      }
    }
  }
  return EncodedWatchImage(
    width: width,
    height: height,
    format: WatchExtImgFormat.mono1bpp,
    bytes: out,
    previewRgba: preview,
  );
}

/// 纯 Dart 解码器（与 firmware `ui_draw.c` `decode_ext_rle_row` 完全一致，用于预览与自检）。
Uint8List decodeWatchExtImageToRgba(
  Uint8List blob, {
  required WatchExtImgFormat format,
  int width = watchDisplayWidth,
  int height = watchDisplayHeight,
  int accentArgb = 0xFF73D4AE,
}) {
  final rgba = Uint8List(width * height * 4);
  if (format == WatchExtImgFormat.mono1bpp) {
    final bpr = (width + 7) >> 3;
    final ar = (accentArgb >> 16) & 0xFF;
    final ag = (accentArgb >> 8) & 0xFF;
    final ab = accentArgb & 0xFF;
    for (var y = 0; y < height; y++) {
      for (var x = 0; x < width; x++) {
        final idx = y * bpr + (x >> 3);
        final on = idx < blob.length && ((blob[idx] >> (7 - (x & 7))) & 1) != 0;
        final p = (y * width + x) * 4;
        rgba[p] = on ? ar : 8;
        rgba[p + 1] = on ? ag : 12;
        rgba[p + 2] = on ? ab : 10;
        rgba[p + 3] = 255;
      }
    }
    return rgba;
  }

  final pal = List<int>.filled(16, 0xFF000000);
  var tblOff = 0;
  if (format == WatchExtImgFormat.pal4Rle) {
    for (var k = 0; k < 16 && k * 2 + 1 < blob.length; k++) {
      pal[k] = rgb565ToArgb(blob[k * 2] | (blob[k * 2 + 1] << 8));
    }
    tblOff = 32;
  }
  final dataOff = tblOff + height * 2;
  for (var y = 0; y < height; y++) {
    final start = y == 0
        ? 0
        : (blob[tblOff + (y - 1) * 2] | (blob[tblOff + (y - 1) * 2 + 1] << 8));
    var pos = dataOff + start;
    var px = 0;
    while (px < width && pos < blob.length) {
      final hdr = blob[pos++];
      final isRep = (hdr & 0x80) != 0;
      final n = (hdr & 0x7F) + (isRep ? 2 : 1);
      if (format == WatchExtImgFormat.pal4Rle) {
        if (isRep) {
          final argb = pal[blob[pos++] & 0x0F];
          for (var i = 0; i < n && px < width; i++, px++) {
            final p = (y * width + px) * 4;
            rgba[p] = (argb >> 16) & 0xFF;
            rgba[p + 1] = (argb >> 8) & 0xFF;
            rgba[p + 2] = argb & 0xFF;
            rgba[p + 3] = 255;
          }
        } else {
          var nib = 0;
          for (var i = 0; i < n && px < width; i++, px++) {
            int cIdx;
            if ((i & 1) == 0) {
              nib = blob[pos++];
              cIdx = (nib >> 4) & 0x0F;
            } else {
              cIdx = nib & 0x0F;
            }
            final argb = pal[cIdx];
            final p = (y * width + px) * 4;
            rgba[p] = (argb >> 16) & 0xFF;
            rgba[p + 1] = (argb >> 8) & 0xFF;
            rgba[p + 2] = argb & 0xFF;
            rgba[p + 3] = 255;
          }
        }
      } else {
        if (isRep) {
          final c565 = blob[pos] | (blob[pos + 1] << 8);
          pos += 2;
          final argb = rgb565ToArgb(c565);
          for (var i = 0; i < n && px < width; i++, px++) {
            final p = (y * width + px) * 4;
            rgba[p] = (argb >> 16) & 0xFF;
            rgba[p + 1] = (argb >> 8) & 0xFF;
            rgba[p + 2] = argb & 0xFF;
            rgba[p + 3] = 255;
          }
        } else {
          for (var i = 0; i < n && px < width; i++, px++) {
            final c565 = blob[pos] | (blob[pos + 1] << 8);
            pos += 2;
            final argb = rgb565ToArgb(c565);
            final p = (y * width + px) * 4;
            rgba[p] = (argb >> 16) & 0xFF;
            rgba[p + 1] = (argb >> 8) & 0xFF;
            rgba[p + 2] = argb & 0xFF;
            rgba[p + 3] = 255;
          }
        }
      }
    }
  }
  return rgba;
}

Future<ui.Image> rgbaToUiImage(
  Uint8List rgba, {
  int width = watchDisplayWidth,
  int height = watchDisplayHeight,
}) async {
  final buffer = await ui.ImmutableBuffer.fromUint8List(rgba);
  final descriptor = ui.ImageDescriptor.raw(
    buffer,
    width: width,
    height: height,
    pixelFormat: ui.PixelFormat.rgba8888,
  );
  final codec = await descriptor.instantiateCodec();
  final frame = await codec.getNextFrame();
  codec.dispose();
  descriptor.dispose();
  buffer.dispose();
  return frame.image;
}
