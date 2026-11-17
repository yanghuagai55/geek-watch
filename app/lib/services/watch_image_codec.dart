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
const int watchOnChipHotSwapMaxBytes = 9216; // 9 KB AT32 片内 Flash 热交换槽（0x0800DC00..0x08010000）
const int watchExtImgMaxBytes = 0x5400; // 21,504 B (与固件 LINK_EXT_IMG_MAX 一致，兼容片外 AT24C256 退避)

/// 每张图片独立的局部刷新与布局评估档案：
/// - 判定是否可放入 9KB AT32 片内热交换槽（XIP 120MHz 零 I2C 读）
/// - 评估上/中/下 3 区的边缘密度与背景亮度，给出低扰动局部刷新时钟推荐位置与对比蒙版
class WatchImageProfile {
  const WatchImageProfile({
    required this.fitsOnChipHotSwap,
    required this.onChipBudgetBytes,
    required this.flatRatio,
    required this.recommendedAlign,
    required this.recommendedScrim,
    required this.recommendedTextColor,
    required this.recommendedAccentColor,
    required this.zoneBusyScores,
  });

  final bool fitsOnChipHotSwap;
  final int onChipBudgetBytes;
  final double flatRatio;
  final int recommendedAlign; // 0=居中, 1=居顶, 2=居底
  final bool recommendedScrim;
  final int recommendedTextColor;
  final int recommendedAccentColor;
  final List<double> zoneBusyScores; // [居顶, 居中, 居底] 复杂度(越低局部刷新越快/少遮挡主体)
}

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
  bool get fitsOnChipHotSwap => bytes.length <= watchOnChipHotSwapMaxBytes;
  double get compressionRatio =>
      bytes.isEmpty ? 1.0 : rawRgb565Bytes / bytes.length;
}

/// 读取源图原始像素尺寸（用于 2D 裁切选区交互画布确定原始纵横比与边界）。
Future<({int width, int height})> probeImageSize(Uint8List fileBytes) async {
  final codec = await ui.instantiateImageCodec(fileBytes);
  final frame = await codec.getNextFrame();
  final img = frame.image;
  try {
    return (width: img.width, height: img.height);
  } finally {
    img.dispose();
    codec.dispose();
  }
}

int _clampByte(int v) => v < 0 ? 0 : (v > 255 ? 255 : v);

int _snapRgb565(int r, int g, int b) {
  final r5 = (_clampByte(r) >> 3) & 0x1F;
  final g6 = (_clampByte(g) >> 2) & 0x3F;
  final b5 = (_clampByte(b) >> 3) & 0x1F;
  return (r5 << 11) | (g6 << 5) | b5;
}

/// 将任意已编码图片（PNG/JPG/BMP/WebP）裁切缩放为手表 128×160 RGBA 缓冲，支持 2D 区域选取与亮度/对比度调节。
Future<Uint8List> cropImageBytesToRgba(
  Uint8List fileBytes, {
  int targetWidth = watchDisplayWidth,
  int targetHeight = watchDisplayHeight,
  double zoom = 1.0,
  double panX = 0.0,
  double panY = 0.0,
  ui.Rect? cropRectNorm,
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
    if (cropRectNorm != null &&
        cropRectNorm.width > 0.01 &&
        cropRectNorm.height > 0.01) {
      final nl = cropRectNorm.left.clamp(0.0, 0.99);
      final nt = cropRectNorm.top.clamp(0.0, 0.99);
      final nr = (cropRectNorm.left + cropRectNorm.width).clamp(nl + 0.01, 1.0);
      final nb = (cropRectNorm.top + cropRectNorm.height).clamp(nt + 0.01, 1.0);
      canvas.drawImageRect(
        img,
        ui.Rect.fromLTRB(nl * iw, nt * ih, nr * iw, nb * ih),
        ui.Rect.fromLTWH(0, 0, targetWidth.toDouble(), targetHeight.toDouble()),
        ui.Paint()..filterQuality = ui.FilterQuality.high,
      );
    } else {
      final baseScale = math.max(targetWidth / iw, targetHeight / ih);
      final drawW = iw * baseScale * zoom.clamp(1.0, 4.0);
      final drawH = ih * baseScale * zoom.clamp(1.0, 4.0);
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
    }
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

/// 针对每一张裁切后的图片自适应拟合：若默认抖动下 PAL4-RLE 略微超出 9KB 片内热槽，
/// 自动阶梯提高平坦区抑制强度，优先确保压入 AT32 片内 Flash 热交换槽以实现 120MHz XIP 秒刷。
EncodedWatchImage encodeWatchImageAdaptive(
  Uint8List rgba, {
  required WatchExtImgFormat format,
  int width = watchDisplayWidth,
  int height = watchDisplayHeight,
  bool dither = true,
  int accentArgb = 0xFF73D4AE,
}) {
  if (format != WatchExtImgFormat.pal4Rle) {
    return encodeWatchImage(
      rgba,
      format: format,
      width: width,
      height: height,
      dither: dither,
      accentArgb: accentArgb,
    );
  }
  var best = _encodePal4Rle(
    rgba,
    width: width,
    height: height,
    dither: dither,
    flatSnapThreshold: 14,
  );
  if (best.fitsOnChipHotSwap || !dither) return best;
  for (final snap in const [22, 32]) {
    final cand = _encodePal4Rle(
      rgba,
      width: width,
      height: height,
      dither: true,
      flatSnapThreshold: snap,
    );
    if (cand.bytes.length < best.bytes.length) best = cand;
    if (best.fitsOnChipHotSwap) return best;
  }
  final nodith = _encodePal4Rle(
    rgba,
    width: width,
    height: height,
    dither: false,
    flatSnapThreshold: 24,
  );
  if (nodith.bytes.length < best.bytes.length) best = nodith;
  return best;
}

/// 为当前图片生成专属局部刷新与版面评估档案（平坦度 / 上中下三区时间最佳落位 / 对比度）。
WatchImageProfile analyzeWatchImageProfile(
  Uint8List rgba,
  EncodedWatchImage encoded, {
  int width = watchDisplayWidth,
  int height = watchDisplayHeight,
}) {
  // 三区对应时钟常见落位：zone 1=居顶(y: 8..50), zone 0=居中(y: 54..106), zone 2=居底(y: 110..152)
  final zones = <(int y0, int y1)>[
    (height * 54 ~/ 160, height * 106 ~/ 160), // 0 = center
    (height * 8 ~/ 160, height * 50 ~/ 160), // 1 = top
    (height * 110 ~/ 160, height * 152 ~/ 160), // 2 = bottom
  ];
  final zoneBusy = <double>[0, 0, 0];
  final zoneLuma = <double>[0, 0, 0];

  var flatCount = 0;
  final totalPx = width * height;
  for (var y = 0; y < height; y++) {
    for (var x = 1; x < width; x++) {
      final a = (y * width + x) * 4;
      final b = (y * width + x - 1) * 4;
      final diff =
          (rgba[a] - rgba[b]).abs() +
          (rgba[a + 1] - rgba[b + 1]).abs() +
          (rgba[a + 2] - rgba[b + 2]).abs();
      if (diff <= 14) flatCount++;
    }
  }

  for (var z = 0; z < 3; z++) {
    final (y0, y1) = zones[z];
    var edges = 0.0;
    var lumaSum = 0.0;
    var n = 0;
    for (var y = y0; y < y1; y++) {
      for (var x = (width * 18) ~/ 100; x < (width * 82) ~/ 100; x++) {
        final i = (y * width + x) * 4;
        final lum = (rgba[i] * 77 + rgba[i + 1] * 150 + rgba[i + 2] * 29) >> 8;
        lumaSum += lum;
        if (x > 0) {
          final j = i - 4;
          edges +=
              ((rgba[i] - rgba[j]).abs() +
                  (rgba[i + 1] - rgba[j + 1]).abs() +
                  (rgba[i + 2] - rgba[j + 2]).abs()) /
              3.0;
        }
        n++;
      }
    }
    zoneBusy[z] = n > 0 ? edges / n : 0.0;
    zoneLuma[z] = n > 0 ? lumaSum / n : 0.0;
  }

  // 选择复杂度最低（主体遮挡最少、局部刷新最干净）的区域；仅轻微偏好上/下避开居中人脸
  var bestAlign = 1; // top default on ties
  var bestScore = zoneBusy[1] * 0.92;
  final scoreBot = zoneBusy[2] * 0.90;
  final scoreMid = zoneBusy[0] * 1.08;
  if (scoreBot < bestScore) {
    bestScore = scoreBot;
    bestAlign = 2;
  }
  if (scoreMid < bestScore) {
    bestAlign = 0;
  }

  final avgLuma = zoneLuma[bestAlign];
  final lightBg = avgLuma >= 150.0;
  return WatchImageProfile(
    fitsOnChipHotSwap: encoded.fitsOnChipHotSwap,
    onChipBudgetBytes: watchOnChipHotSwapMaxBytes,
    flatRatio: totalPx > 0 ? (flatCount / totalPx).clamp(0.0, 1.0) : 0.0,
    recommendedAlign: bestAlign,
    recommendedScrim: lightBg || zoneBusy[bestAlign] >= 12.0,
    recommendedTextColor: lightBg ? 0xFF101820 : 0xFFF4F6EA,
    recommendedAccentColor: lightBg ? 0xFF0F8B6D : 0xFF73D4AE,
    zoneBusyScores: zoneBusy,
  );
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
  final totalPx = width * height;
  final step = totalPx > 10240 ? 2 : 1;
  final anchors = <int>[];
  final samples = <int>[];
  var nearWhiteCount = 0;
  var nearBlackCount = 0;
  var scanned = 0;

  for (var y = 0; y < height; y += step) {
    for (var x = 0; x < width; x += step) {
      final i = (y * width + x) * 4;
      final r = rgba[i], g = rgba[i + 1], b = rgba[i + 2];
      scanned++;
      if (r >= 244 && g >= 244 && b >= 244) {
        nearWhiteCount++;
      } else if (r <= 12 && g <= 12 && b <= 12) {
        nearBlackCount++;
      } else {
        samples.add((r << 16) | (g << 8) | b);
      }
    }
  }

  // 锚定纯白 #FFFFFF / 纯黑 #000000，防止均值化把纯白背景漂移成灰白噪点
  if (nearWhiteCount * 100 >= scanned) {
    anchors.add(0xFFFF);
  }
  if (nearBlackCount * 100 >= scanned) {
    anchors.add(0x0000);
  }
  if (samples.isEmpty) {
    for (var y = 0; y < height; y += step) {
      for (var x = 0; x < width; x += step) {
        final i = (y * width + x) * 4;
        samples.add((rgba[i] << 16) | (rgba[i + 1] << 8) | rgba[i + 2]);
      }
    }
  }

  final cutSlots = 16 - anchors.length;
  final boxes = <_ColorBox>[_ColorBox(samples)];
  while (boxes.length < cutSlots) {
    boxes.sort((a, b) => b.maxSpan.compareTo(a.maxSpan));
    final top = boxes.first;
    if (top.length < 2 || top.maxSpan == 0) break;
    boxes.removeAt(0);
    boxes.addAll(top.split());
  }
  final pal = <int>[...anchors];
  for (final box in boxes) {
    final c = box.averageRgb565();
    if (!pal.contains(c)) {
      pal.add(c);
    }
  }
  // 补齐剩余色槽，防止空槽重复占满 16 色表
  for (final box in boxes) {
    if (pal.length >= 16) break;
    final c = box.averageRgb565();
    if (pal.length < 16) pal.add(c);
  }
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
  int flatSnapThreshold = 14,
}) {
  final pal565 = _medianCutPalette16(rgba, width, height);
  final whiteIdx = pal565.indexOf(0xFFFF);
  final blackIdx = pal565.indexOf(0x0000);
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

  (int idx, int dist) nearestColor(int r, int g, int b) {
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
    return (best, bestDist);
  }

  int neighborDelta(int x, int y, int r0, int g0, int b0) {
    var maxD = 0;
    if (x > 0) {
      final j = (y * width + x - 1) * 4;
      final d = (r0 - rgba[j]).abs() + (g0 - rgba[j + 1]).abs() + (b0 - rgba[j + 2]).abs();
      if (d > maxD) maxD = d;
    }
    if (y > 0) {
      final j = ((y - 1) * width + x) * 4;
      final d = (r0 - rgba[j]).abs() + (g0 - rgba[j + 1]).abs() + (b0 - rgba[j + 2]).abs();
      if (d > maxD) maxD = d;
    }
    return maxD;
  }

  void addErr(int x, int y, int er, int eg, int eb, int mul) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    final p = (y * width + x) * 3;
    // 禁止把彩色/边缘量化误差扩散进纯白背景或纯黑区！
    final sx = (y * width + x) * 4;
    if (rgba[sx] >= 245 && rgba[sx + 1] >= 245 && rgba[sx + 2] >= 245) return;
    if (rgba[sx] <= 8 && rgba[sx + 1] <= 8 && rgba[sx + 2] <= 8) return;
    err[p] += (er * mul) >> 4;
    err[p + 1] += (eg * mul) >> 4;
    err[p + 2] += (eb * mul) >> 4;
  }

  for (var y = 0; y < height; y++) {
    for (var x = 0; x < width; x++) {
      final sx = (y * width + x) * 4;
      final sr = rgba[sx], sg = rgba[sx + 1], sb = rgba[sx + 2];
      final grad = neighborDelta(x, y, sr, sg, sb);
      final p = (y * width + x) * 3;
      final r = _clampByte(err[p]);
      final g = _clampByte(err[p + 1]);
      final b = _clampByte(err[p + 2]);

      int idx;
      var allowDiffuse = dither;
      if (whiteIdx >= 0 && sr >= 244 && sg >= 244 && sb >= 244 && grad <= 28) {
        // 纯白/近白平整区：直接锁定纯白 #FFFFFF 且零误差扩散，彻底根除白底彩点噪点
        idx = whiteIdx;
        allowDiffuse = false;
      } else if (blackIdx >= 0 && sr <= 10 && sg <= 10 && sb <= 10 && grad <= 24) {
        idx = blackIdx;
        allowDiffuse = false;
      } else {
        final (nearIdx, dist) = nearestColor(r, g, b);
        idx = nearIdx;
        // 非渐变平坦色块（如动漫色块/渐变边缘之外的低梯度区）抑制抖动颗粒感，提高压缩率
        if (grad <= flatSnapThreshold && dist <= flatSnapThreshold * flatSnapThreshold * 18) {
          allowDiffuse = false;
        }
      }

      indices[y * width + x] = idx;
      final px = (y * width + x) * 4;
      preview[px] = palR[idx];
      preview[px + 1] = palG[idx];
      preview[px + 2] = palB[idx];
      preview[px + 3] = 255;

      if (allowDiffuse) {
        final er = (r - palR[idx]).clamp(-48, 48);
        final eg = (g - palG[idx]).clamp(-48, 48);
        final eb = (b - palB[idx]).clamp(-48, 48);
        addErr(x + 1, y, er, eg, eb, 7);
        addErr(x - 1, y + 1, er, eg, eb, 3);
        addErr(x, y + 1, er, eg, eb, 5);
        addErr(x + 1, y + 1, er, eg, eb, 1);
      }
    }
  }

  // 组装二进制：palette[16](32B) + row_end[H](2*H B) + 位最优 Nibble-RLE 像素流
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
      if (run >= 3) {
        data.addByte(0x80 | (run - 2));
        data.addByte(first & 0x0F);
        x += run;
      } else {
        final litStart = x;
        var lit = 0;
        while (x < width && lit < 128) {
          final rem = width - x;
          // 仅当后续出现 >=4 连续同色像素时才切断字面段（2像素=1B，打断开销需 >=4 才回本）
          if (rem >= 4 &&
              indices[rowOff + x] == indices[rowOff + x + 1] &&
              indices[rowOff + x] == indices[rowOff + x + 2] &&
              indices[rowOff + x] == indices[rowOff + x + 3]) {
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
