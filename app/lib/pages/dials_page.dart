import 'dart:async';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/gestures.dart' show PointerScrollEvent;
import 'package:material_ui/material_ui.dart';

import '../services/image_picker_bridge.dart';
import '../services/watch_image_codec.dart';
import '../state/watch_controller.dart';
import '../theme/app_theme.dart';
import '../widgets/common.dart';
import '../widgets/watch_face.dart';

/// 表盘管理与自定义中心：适配外接 32KB AT24C256 I2C EEPROM（0x50）。
/// 支持 4 款表盘实时预览与全参数定制，以及 128×160 图片压缩算法（PAL4-RLE / RGB565-RLE / 1bpp）写入手表。
class DialsPage extends StatefulWidget {
  const DialsPage({super.key, required this.controller});

  final WatchController controller;

  @override
  State<DialsPage> createState() => _DialsPageState();
}

const _dialAmber = Color(0xFFF5B759);

class _DialSpec {
  const _DialSpec({
    required this.id,
    required this.name,
    required this.subtitle,
    required this.tag,
    required this.sizeHint,
    required this.accent,
  });

  final int id;
  final String name;
  final String subtitle;
  final String tag;
  final String sizeHint;
  final Color accent;
}

const _dialPresets = <_DialSpec>[
  _DialSpec(
    id: 0,
    name: '琥珀双色数字',
    subtitle: '片内核心 · SHD1 字体按需加载',
    tag: '片内极速',
    sizeHint: '省电高对比',
    accent: AppPalette.mint,
  ),
  _DialSpec(
    id: 1,
    name: '墨竹极简指针',
    subtitle: 'ANL1 片外加载入 SRAM 渲染',
    tag: 'ANL1 PIC',
    sizeHint: '矢量指针',
    accent: AppPalette.mintSoft,
  ),
  _DialSpec(
    id: 2,
    name: '九轴水平仪表',
    subtitle: 'ANL1 模态 + 9-DOF 实时地平线',
    tag: 'ANL1 PIC',
    sizeHint: '9 轴实时',
    accent: _dialAmber,
  ),
  _DialSpec(
    id: 3,
    name: '自定义相册表盘',
    subtitle: '≤9KB 直入片内热交换槽 · 大图自动退避 EEPROM',
    tag: '片内热槽',
    sizeHint: 'XIP 120MHz',
    accent: Color(0xFF93C5FD),
  ),
];

class _ThemePreset {
  const _ThemePreset(this.name, this.text, this.accent, this.bg);
  final String name;
  final int text;
  final int accent;
  final int bg;
}

const _colorThemes = <_ThemePreset>[
  _ThemePreset('翡翠薄荷', 0xFFF0F1E4, 0xFF73D4AE, 0xFF000000),
  _ThemePreset('琥珀暖金', 0xFFFFF5E0, 0xFFF5B759, 0xFF090704),
  _ThemePreset('深空曜蓝', 0xFFE6F4FF, 0xFF60A5FA, 0xFF050B14),
  _ThemePreset('绯樱浅紫', 0xFFFCE7F3, 0xFFF472B6, 0xFF120710),
  _ThemePreset('极简赤白', 0xFFFFFFFF, 0xFFEF4444, 0xFF000000),
  _ThemePreset('深林墨绿', 0xFFDCFCE7, 0xFF34D399, 0xFF061612),
];

const _swatchColors = <int>[
  0xFFF0F1E4,
  0xFFFFFFFF,
  0xFF73D4AE,
  0xFFF5B759,
  0xFF60A5FA,
  0xFFF472B6,
  0xFFEF4444,
  0xFF34D399,
];

const _bgSwatches = <int>[
  0xFF000000,
  0xFF0A0F10,
  0xFF08141E,
  0xFF140914,
  0xFF121008,
  0xFF071A14,
];

const _compLabels = <String>['日期星期', '步数进度', '九轴航向', '电池电量', '隐藏'];

const _wallpaperPresets = <(String id, String label)>[
  ('aurora', '极光山峦'),
  ('amber', '暖阳琥珀'),
  ('cyber', '赛博网格'),
  ('starry', '星轨夜空'),
];

class _DialsPageState extends State<DialsPage> {
  String _wallpaperId = 'aurora';
  String? _pickedImageName;
  Uint8List? _pickedFileBytes;

  ui.Image? _sourceUiImage;
  int _srcWidth = watchDisplayWidth;
  int _srcHeight = watchDisplayHeight;
  ui.Rect _cropRectNorm = const ui.Rect.fromLTWH(0, 0, 1, 1);

  double _brightness = 0.0;
  double _contrast = 1.0;
  WatchExtImgFormat _format = WatchExtImgFormat.pal4Rle;
  bool _dither = true;

  EncodedWatchImage? _encoded;
  WatchImageProfile? _profile;
  ui.Image? _previewImage;
  bool _encoding = false;
  bool _encodePending = false;
  bool _uploading = false;
  int _sentChunks = 0;
  int _totalChunks = 0;
  Timer? _cropDebounce;

  @override
  void initState() {
    super.initState();
    unawaited(_rebuildImagePreview());
  }

  @override
  void dispose() {
    _cropDebounce?.cancel();
    _previewImage?.dispose();
    _sourceUiImage?.dispose();
    super.dispose();
  }

  /// 计算源图坐标下最大的居中 4:5 (128:160) 选区矩形。
  ui.Rect _defaultCropRect(int sw, int sh, {double scale = 1.0, double biasY = 0.5}) {
    if (sw <= 0 || sh <= 0) return const ui.Rect.fromLTWH(0, 0, 1, 1);
    final imgAspect = sw / sh;
    const targetAspect = watchDisplayWidth / watchDisplayHeight; // 0.8
    double maxW, maxH;
    if (imgAspect >= targetAspect) {
      maxH = 1.0;
      maxW = (targetAspect / imgAspect).clamp(0.15, 1.0);
    } else {
      maxW = 1.0;
      maxH = (imgAspect / targetAspect).clamp(0.15, 1.0);
    }
    final w = (maxW * scale).clamp(0.12, maxW);
    final h = (maxH * scale).clamp(0.12, maxH);
    final left = ((1.0 - w) * 0.5).clamp(0.0, 1.0 - w);
    final top = ((1.0 - h) * biasY).clamp(0.0, 1.0 - h);
    return ui.Rect.fromLTWH(left, top, w, h);
  }

  void _onCropChanged(ui.Rect next, {bool immediate = false}) {
    setState(() => _cropRectNorm = next);
    _cropDebounce?.cancel();
    if (immediate) {
      unawaited(_rebuildImagePreview());
    } else {
      _cropDebounce = Timer(const Duration(milliseconds: 55), () {
        if (mounted) unawaited(_rebuildImagePreview());
      });
    }
  }

  void _applyRecommendedProfile() {
    final p = _profile;
    if (p == null) return;
    widget.controller.updateDialConfig(
      id: 3,
      photoAlign: p.recommendedAlign,
      photoScrim: p.recommendedScrim,
      textColor: p.recommendedTextColor,
      accentColor: p.recommendedAccentColor,
      syncToWatch: false,
    );
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(
          '已套用图像局部刷新方案：时间${const ['居中', '居顶', '居底'][p.recommendedAlign]} · ${p.recommendedScrim ? '开对比胶囊' : '透明叠字'}。',
        ),
      ),
    );
  }

  Future<void> _pickLocalPhoto() async {
    final messenger = ScaffoldMessenger.of(context);
    final picked = await ImagePickerBridge.pickImage();
    if (!mounted || picked == null) return;

    final codec = await ui.instantiateImageCodec(picked.bytes);
    final frame = await codec.getNextFrame();
    final srcImg = frame.image;
    codec.dispose();
    if (!mounted) {
      srcImg.dispose();
      return;
    }

    final oldSrc = _sourceUiImage;
    setState(() {
      _pickedFileBytes = picked.bytes;
      _pickedImageName = picked.name;
      _sourceUiImage = srcImg;
      _srcWidth = srcImg.width;
      _srcHeight = srcImg.height;
      _cropRectNorm = _defaultCropRect(srcImg.width, srcImg.height, biasY: 0.25);
      _brightness = 0.0;
      _contrast = 1.0;
    });
    oldSrc?.dispose();
    widget.controller.updateDialConfig(id: 3, syncToWatch: false);
    await _rebuildImagePreview();
    if (!mounted) return;
    messenger.hideCurrentSnackBar();
    final enc = _encoded;
    final fitHint = enc != null && enc.fitsOnChipHotSwap
        ? '可直入片内 9KB 热交换槽 (${enc.bytes.length} B)'
        : (enc != null ? '大小 ${enc.bytes.length} B · AT24C256 池' : '');
    messenger.showSnackBar(
      SnackBar(
        content: Text('已载入「${picked.name}」（$_srcWidth×$_srcHeight）$fitHint'),
      ),
    );
  }

  Future<void> _rebuildImagePreview() async {
    if (_encoding) {
      _encodePending = true;
      return;
    }
    setState(() => _encoding = true);
    try {
      do {
        _encodePending = false;
        Uint8List rgba;
        final fileBytes = _pickedFileBytes;
        if (fileBytes != null) {
          rgba = await cropImageBytesToRgba(
            fileBytes,
            cropRectNorm: _cropRectNorm,
            brightness: _brightness,
            contrast: _contrast,
          );
        } else {
          rgba = generateBuiltInWallpaperRgba(_wallpaperId);
          if (_brightness.abs() > 0.001 || (_contrast - 1.0).abs() > 0.001) {
            final bShift = _brightness * 255.0;
            for (var i = 0; i < rgba.length; i += 4) {
              rgba[i] = (((rgba[i] - 128) * _contrast + 128 + bShift)
                  .round()
                  .clamp(0, 255));
              rgba[i + 1] = (((rgba[i + 1] - 128) * _contrast + 128 + bShift)
                  .round()
                  .clamp(0, 255));
              rgba[i + 2] = (((rgba[i + 2] - 128) * _contrast + 128 + bShift)
                  .round()
                  .clamp(0, 255));
            }
          }
        }

        final encoded = encodeWatchImageAdaptive(
          rgba,
          format: _format,
          dither: _dither,
          accentArgb: widget.controller.dialAccentColor,
        );
        final profile = analyzeWatchImageProfile(rgba, encoded);
        if (!mounted) return;
        setState(() {
          _encoded = encoded;
          _profile = profile;
        });
        widget.controller.customDialPreviewRgba = encoded.previewRgba;

        final img = await rgbaToUiImage(encoded.previewRgba);
        if (!mounted) {
          img.dispose();
          return;
        }
        final old = _previewImage;
        setState(() {
          _previewImage = img;
        });
        old?.dispose();
      } while (_encodePending && mounted);
    } finally {
      if (mounted) setState(() => _encoding = false);
    }
  }

  Future<void> _pushDialConfig() async {
    final messenger = ScaffoldMessenger.of(context);
    messenger.hideCurrentSnackBar();
    if (!widget.controller.connected) {
      messenger.showSnackBar(
        const SnackBar(content: Text('未连接手表：表盘自定义配置已保存本地，连接后按 App 最高优先级自动覆盖写入 AT24C256。')),
      );
      return;
    }
    final ok = await widget.controller.pushConfigToWatch(saveToI2c: true);
    if (!mounted) return;
    messenger.showSnackBar(
      SnackBar(
        content: Text(ok ? '表盘配置已写入 AT24C256 EEPROM，断电不丢失。' : '下发超时，请检查蓝牙连接。'),
      ),
    );
  }

  Future<void> _uploadPhotoDial() async {
    final messenger = ScaffoldMessenger.of(context);
    final enc = _encoded;
    if (enc == null || _uploading) return;
    messenger.hideCurrentSnackBar();
    if (!enc.fitsFlash) {
      messenger.showSnackBar(
        SnackBar(
          content: Text(
            '当前大小 ${enc.bytes.length} B 超出 AT24C256 图库区上限（$watchExtImgMaxBytes B），请改用「PAL4-RLE 16色抖动」。',
          ),
        ),
      );
      return;
    }
    if (!widget.controller.connected) {
      widget.controller.updateDialConfig(id: 3, syncToWatch: false);
      messenger.showSnackBar(
        const SnackBar(content: Text('未连接手表：请先连接手表后再将压缩图片写入 AT24C256（0x3C00）。')),
      );
      return;
    }
    setState(() {
      _uploading = true;
      _sentChunks = 0;
      _totalChunks = (enc.bytes.length + 127) ~/ 128;
    });
    try {
      final ok = await widget.controller.uploadDialImage(
        width: enc.width,
        height: enc.height,
        formatCode: enc.format.code,
        compressedBytes: enc.bytes,
        previewRgba: enc.previewRgba,
        onProgress: (sent, total) {
          if (mounted) {
            setState(() {
              _sentChunks = sent;
              _totalChunks = total;
            });
          }
        },
      );
      if (!mounted) return;
      messenger.showSnackBar(
        SnackBar(
          content: Text(
            ok
                ? (enc.fitsOnChipHotSwap
                      ? '自定义相册表盘（${enc.bytes.length} B，压缩 ${enc.compressionRatio.toStringAsFixed(1)}×）已直入 AT32 片内热交换槽（XIP 零 I2C 秒刷）！'
                      : '自定义相册表盘（${enc.bytes.length} B，压缩 ${enc.compressionRatio.toStringAsFixed(1)}×）已写入 AT24C256 并立即生效！')
                : '写入失败，请确认手表连接与芯片就绪后重试。',
          ),
        ),
      );
    } finally {
      if (mounted) setState(() => _uploading = false);
    }
  }

  Widget _buildPresetGrid(WatchController controller, double maxWidth) {
    final tiles = <Widget>[
      for (var i = 0; i < _dialPresets.length; i++)
        _PresetTile(
          spec: _dialPresets[i],
          selected: i == controller.dialId,
          onTap: () => controller.updateDialConfig(id: i),
        ),
    ];
    if (maxWidth >= 980) {
      return AppCard(
        padding: const EdgeInsets.all(14),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            const SectionHeader(
              title: '表盘方案切换',
              subtitle: '点击实时切换；手表长按 KEY2 亦可左右切换并持久落盘',
            ),
            const SizedBox(height: 12),
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                for (var i = 0; i < tiles.length; i++) ...[
                  if (i > 0) const SizedBox(width: 10),
                  Expanded(
                    child: ConstrainedBox(
                      constraints: const BoxConstraints(minHeight: 82),
                      child: tiles[i],
                    ),
                  ),
                ],
              ],
            ),
          ],
        ),
      );
    }
    if (maxWidth >= 400) {
      return AppCard(
        padding: const EdgeInsets.all(14),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            const SectionHeader(
              title: '表盘方案切换',
              subtitle: '点击实时切换预览并同步手表',
            ),
            const SizedBox(height: 12),
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Expanded(
                  child: ConstrainedBox(
                    constraints: const BoxConstraints(minHeight: 82),
                    child: tiles[0],
                  ),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: ConstrainedBox(
                    constraints: const BoxConstraints(minHeight: 82),
                    child: tiles[1],
                  ),
                ),
              ],
            ),
            const SizedBox(height: 10),
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Expanded(
                  child: ConstrainedBox(
                    constraints: const BoxConstraints(minHeight: 82),
                    child: tiles[2],
                  ),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: ConstrainedBox(
                    constraints: const BoxConstraints(minHeight: 82),
                    child: tiles[3],
                  ),
                ),
              ],
            ),
          ],
        ),
      );
    }
    return AppCard(
      padding: const EdgeInsets.all(14),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const SectionHeader(title: '表盘方案切换'),
          const SizedBox(height: 10),
          for (var i = 0; i < tiles.length; i++) ...[
            if (i > 0) const SizedBox(height: 10),
            tiles[i],
          ],
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final controller = widget.controller;
    final scheme = Theme.of(context).colorScheme;
    final current = _dialPresets[controller.dialId.clamp(0, _dialPresets.length - 1)];
    final addrHex = controller.extI2cAddr
        .toRadixString(16)
        .toUpperCase()
        .padLeft(2, '0');

    return LayoutBuilder(
      builder: (context, constraints) {
        final screen = MediaQuery.sizeOf(context);
        final pad = screen.width < 560 ? 16.0 : 28.0;
        final portraitPhone = screen.width < 760 && screen.height > screen.width;
        final bottom = portraitPhone ? 112.0 : 20.0;
        final wide = constraints.maxWidth >= 900;
        final innerWidth = math.max(
          0.0,
          math.min(constraints.maxWidth - pad * 2, 1180.0),
        );
        final header = PageHeader(
          title: '表盘工作台',
          subtitle: '9KB 片内 Flash 热交换槽 + AT24C256 32KB 按需动态库 · 导入即见实时预览',
          trailing: StatusPill(
            label: controller.connected
                ? (controller.mcuImgOnChip
                      ? '片内热槽 XIP 生效'
                      : (controller.extFlashPresent ? 'AT24C256 · 0x$addrHex' : '未探测到 EEPROM'))
                : '128×160 实时预览',
            color: controller.connected && (controller.mcuImgOnChip || controller.extFlashPresent)
                ? AppPalette.mint
                : _dialAmber,
          ),
        );
        final preview = _DialStageCard(
              spec: current,
              controller: controller,
              photoImage: _previewImage,
              encoded: _encoded,
              profile: _profile,
              uploading: _uploading,
              sentChunks: _sentChunks,
              totalChunks: _totalChunks,
              onPushConfig: _pushDialConfig,
              onUploadPhoto: _uploadPhotoDial,
              onApplyProfile: _applyRecommendedProfile,
              screenScale: 2,
            );
            final phoneActions = _DialStageCard(
              spec: current,
              controller: controller,
              photoImage: _previewImage,
              encoded: _encoded,
              profile: _profile,
              uploading: _uploading,
              sentChunks: _sentChunks,
              totalChunks: _totalChunks,
              onPushConfig: _pushDialConfig,
              onUploadPhoto: _uploadPhotoDial,
              onApplyProfile: _applyRecommendedProfile,
              showTitle: false,
              showFace: false,
            );
            final imageStudio = _ImageCompressionCard(
              wallpaperId: _wallpaperId,
              pickedImageName: _pickedImageName,
              sourceImage: _sourceUiImage,
              srcWidth: _srcWidth,
              srcHeight: _srcHeight,
              cropRectNorm: _cropRectNorm,
              format: _format,
              dither: _dither,
              brightness: _brightness,
              contrast: _contrast,
              encoded: _encoded,
              profile: _profile,
              encoding: _encoding,
              uploading: _uploading,
              sentChunks: _sentChunks,
              totalChunks: _totalChunks,
              onSelectWallpaper: (id) {
                final oldSrc = _sourceUiImage;
                setState(() {
                  _wallpaperId = id;
                  _pickedFileBytes = null;
                  _pickedImageName = null;
                  _sourceUiImage = null;
                  _srcWidth = watchDisplayWidth;
                  _srcHeight = watchDisplayHeight;
                  _cropRectNorm = const ui.Rect.fromLTWH(0, 0, 1, 1);
                });
                oldSrc?.dispose();
                controller.updateDialConfig(id: 3, syncToWatch: false);
                unawaited(_rebuildImagePreview());
              },
              onPickLocalPhoto: _pickLocalPhoto,
              onCropChanged: _onCropChanged,
              onResetCropPreset: (scale, biasY) {
                final next = _defaultCropRect(_srcWidth, _srcHeight, scale: scale, biasY: biasY);
                _onCropChanged(next, immediate: true);
              },
              onFormatChanged: (fmt) {
                setState(() => _format = fmt);
                unawaited(_rebuildImagePreview());
              },
              onDitherChanged: (v) {
                setState(() => _dither = v);
                unawaited(_rebuildImagePreview());
              },
              onBrightnessChanged: (v) {
                setState(() => _brightness = v);
                unawaited(_rebuildImagePreview());
              },
              onContrastChanged: (v) {
                setState(() => _contrast = v);
                unawaited(_rebuildImagePreview());
              },
              onUploadPhoto: _uploadPhotoDial,
            );
            final customization = _DialCustomizationCard(
              controller: controller,
              onAccentChanged: () {
                if (_format == WatchExtImgFormat.mono1bpp) {
                  unawaited(_rebuildImagePreview());
                }
              },
            );
            final memoryMap = _FlashPartitionCard(scheme: scheme, controller: controller);
            final presets = _buildPresetGrid(controller, innerWidth);
            final Widget workspace;
            if (wide) {
              workspace = Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  header,
                  const SizedBox(height: 16),
                  presets,
                  const SizedBox(height: 16),
                  Expanded(
                    child: Row(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        SizedBox(
                          width: 128 * 2 + 72,
                          child: SingleChildScrollView(
                            primary: false,
                            child: preview,
                          ),
                        ),
                        const SizedBox(width: 16),
                        Expanded(
                          child: SingleChildScrollView(
                            primary: false,
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.stretch,
                              children: [
                                customization,
                                const SizedBox(height: 16),
                                imageStudio,
                                const SizedBox(height: 16),
                                memoryMap,
                              ],
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),
                ],
              );
            } else {
              final columnH = math.max(0.0, constraints.maxHeight - bottom);
              final faceBudget = math.max(148.0, (columnH - 72) * 0.32);
              final phoneScale = math.min(
                ((innerWidth - 36) / 128).clamp(0.78, 1.15),
                ((faceBudget - 48) / 160).clamp(0.78, 1.15),
              );
              final face = _DialStageCard(
                spec: current,
                controller: controller,
                photoImage: _previewImage,
                encoded: _encoded,
                profile: _profile,
                uploading: _uploading,
                sentChunks: _sentChunks,
                totalChunks: _totalChunks,
                onPushConfig: _pushDialConfig,
                onUploadPhoto: _uploadPhotoDial,
                onApplyProfile: _applyRecommendedProfile,
                screenScale: phoneScale,
                showTitle: false,
                showControls: false,
              );
              workspace = Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  header,
                  const SizedBox(height: 8),
                  face,
                  const SizedBox(height: 8),
                  Expanded(
                    child: SingleChildScrollView(
                      primary: false,
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.stretch,
                        children: [
                          presets,
                          const SizedBox(height: 12),
                          phoneActions,
                          const SizedBox(height: 12),
                          customization,
                          const SizedBox(height: 12),
                          imageStudio,
                          const SizedBox(height: 12),
                          memoryMap,
                        ],
                      ),
                    ),
                  ),
                ],
              );
            }

            return Padding(
              padding: EdgeInsets.fromLTRB(pad, 16, pad, bottom),
              child: Align(
                alignment: Alignment.topCenter,
                child: SizedBox(width: innerWidth, child: workspace),
              ),
            );
          },
    );
  }
}

/// 表盘画面按实机 128×160 排好，再整块放大。放大不会改字号和留白。
class _NativeDialScreen extends StatelessWidget {
  const _NativeDialScreen({
    required this.width,
    required this.height,
    required this.bgColor,
    required this.child,
  });

  final double width;
  final double height;
  final Color bgColor;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(8),
      decoration: BoxDecoration(
        color: const Color(0xFF0A0D0C),
        borderRadius: BorderRadius.circular(22),
        border: Border.all(color: AppPalette.line, width: 1.5),
      ),
      child: SizedBox(
        width: width,
        height: height,
        child: FittedBox(
          fit: BoxFit.fill,
          child: SizedBox(
            width: 176,
            height: 220,
            child: ClipRRect(
              borderRadius: BorderRadius.circular(12),
              child: ColoredBox(color: bgColor, child: child),
            ),
          ),
        ),
      ),
    );
  }
}

class _DialStageCard extends StatelessWidget {
  const _DialStageCard({
    required this.spec,
    required this.controller,
    required this.photoImage,
    required this.encoded,
    required this.profile,
    required this.uploading,
    required this.sentChunks,
    required this.totalChunks,
    required this.onPushConfig,
    required this.onUploadPhoto,
    required this.onApplyProfile,
    this.screenScale = 2,
    this.showTitle = true,
    this.showFace = true,
    this.showControls = true,
  });

  final _DialSpec spec;
  final WatchController controller;
  final ui.Image? photoImage;
  final EncodedWatchImage? encoded;
  final WatchImageProfile? profile;
  final bool uploading;
  final int sentChunks;
  final int totalChunks;
  final VoidCallback onPushConfig;
  final VoidCallback onUploadPhoto;
  final VoidCallback onApplyProfile;
  final double screenScale;
  final bool showTitle;
  final bool showFace;
  final bool showControls;

  @override
  Widget build(BuildContext context) {
    final now = controller.now;
    final dispHour = controller.use24Hour
        ? now.hour
        : ((now.hour % 12) == 0 ? 12 : (now.hour % 12));
    final hh = dispHour.toString().padLeft(2, '0');
    final mm = now.minute.toString().padLeft(2, '0');
    final ss = now.second.toString().padLeft(2, '0');
    final pct = controller.stepGoal > 0
        ? (controller.steps / controller.stepGoal).clamp(0.0, 1.0)
        : 0.0;
    final fgColor = Color(controller.dialTextColor);
    final acColor = Color(controller.dialAccentColor);
    final bgColor = Color(controller.dialBgColor);
    final compactFace = showFace && !showTitle && !showControls;

    return AppCard(
      padding: compactFace
          ? const EdgeInsets.fromLTRB(12, 12, 12, 10)
          : const EdgeInsets.all(20),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          if (showTitle) ...[
            SectionHeader(
              title: spec.name,
              subtitle: spec.subtitle,
              trailing: StatusPill(
                label: spec.tag,
                color: spec.id == 3 ? _dialAmber : AppPalette.mint,
              ),
            ),
            const SizedBox(height: 18),
          ],
          if (showFace) ...[
          Center(
            child: _NativeDialScreen(
              width: 128 * screenScale,
              height: 160 * screenScale,
              bgColor: bgColor,
              child: _buildDialCanvas(
                hh,
                mm,
                ss,
                pct,
                fgColor,
                acColor,
                bgColor,
              ),
            ),
          ),
          const SizedBox(height: 12),
          Center(
            child: Text(
              controller.mcuImgReady
                  ? (controller.mcuImgOnChip
                        ? '片内 0xD800 热槽已激活 (${controller.mcuImgBytes} B · XIP 120MHz)'
                        : 'AT24C256 已固化相册图 (${controller.mcuImgBytes} B)')
                  : '128 × 160 · 实机同款参数即时仿真',
              style: TextStyle(
                fontSize: 11.5,
                color: Theme.of(context).colorScheme.onSurfaceVariant,
              ),
            ),
          ),
          ],
          if (showControls && profile != null && spec.id == 3) ...[
            const SizedBox(height: 10),
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
              decoration: BoxDecoration(
                color: Theme.of(context)
                    .colorScheme
                    .surfaceContainerHigh
                    .withValues(alpha: 0.72),
                borderRadius: BorderRadius.circular(12),
                border: Border.all(
                  color: (profile!.fitsOnChipHotSwap
                          ? AppPalette.mint
                          : _dialAmber)
                      .withValues(alpha: 0.35),
                ),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Row(
                    children: [
                      Expanded(
                        child: Text(
                          '平坦率 ${(profile!.flatRatio * 100).round()}% · 推荐${const ['居中', '居顶', '居底'][profile!.recommendedAlign]}',
                          style: const TextStyle(
                            fontSize: 11.5,
                            fontWeight: FontWeight.w700,
                          ),
                        ),
                      ),
                      StatusPill(
                        label: profile!.fitsOnChipHotSwap
                            ? '可直入片内 9KB'
                            : '需 AT24C256 退避',
                        color: profile!.fitsOnChipHotSwap
                            ? AppPalette.mint
                            : _dialAmber,
                      ),
                    ],
                  ),
                  const SizedBox(height: 6),
                  Align(
                    alignment: Alignment.centerLeft,
                    child: TextButton.icon(
                      style: TextButton.styleFrom(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 10,
                          vertical: 4,
                        ),
                        minimumSize: Size.zero,
                        tapTargetSize: MaterialTapTargetSize.shrinkWrap,
                      ),
                      onPressed: onApplyProfile,
                      icon: const Icon(Icons.auto_fix_high_rounded, size: 15),
                      label: const Text(
                        '应用局部刷新最佳时钟位与对比度',
                        style: TextStyle(fontSize: 11.5),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ],
          if (showControls && uploading && totalChunks > 0) ...[
            const SizedBox(height: 12),
            LinearProgressIndicator(value: sentChunks / totalChunks),
            const SizedBox(height: 4),
            Text(
              encoded != null && encoded!.fitsOnChipHotSwap
                  ? '正在写入 AT32 片内热交换槽：$sentChunks / $totalChunks 块'
                  : '正在分块写入 AT24C256：$sentChunks / $totalChunks 块',
              textAlign: TextAlign.center,
              style: const TextStyle(fontSize: 11),
            ),
          ],
          if (showControls) ...[
          const SizedBox(height: 14),
          Wrap(
            spacing: 10,
            runSpacing: 10,
            children: [
              FilledButton.icon(
                onPressed: uploading ? null : onPushConfig,
                icon: const Icon(Icons.save_as_rounded, size: 18),
                label: Text(
                  controller.connected ? '下发配置到手表' : '保存本地（连表覆盖）',
                ),
              ),
              if (spec.id == 3)
                OutlinedButton.icon(
                  onPressed: uploading ? null : onUploadPhoto,
                  icon: const Icon(Icons.upload_file_rounded, size: 18),
                  label: Text(
                    encoded == null
                        ? '写入压缩底图'
                        : (encoded!.fitsOnChipHotSwap
                              ? '写入片内热槽 (${(encoded!.bytes.length / 1024).toStringAsFixed(1)} KB)'
                              : '写入片外 EEPROM (${(encoded!.bytes.length / 1024).toStringAsFixed(1)} KB)'),
                  ),
                ),
            ],
          ),
          ],
        ],
      ),
    );
  }

  String _compText(int comp) {
    final now = controller.now;
    const wd = ['MON', 'TUE', 'WED', 'THU', 'FRI', 'SAT', 'SUN'];
    return switch (comp) {
      0 => '${wd[now.weekday - 1]}  ${now.month.toString().padLeft(2, '0')}.${now.day.toString().padLeft(2, '0')}',
      1 => '${controller.steps}/${controller.stepGoal} 步',
      2 => '${controller.headingDeg.round().toString().padLeft(3, '0')}°',
      3 => controller.batteryMv > 0
          ? '${(controller.batteryMv / 1000).toStringAsFixed(2)}V'
          : 'USB PWR',
      _ => '',
    };
  }

  Widget _buildDialCanvas(
    String hh,
    String mm,
    String ss,
    double pct,
    Color fgColor,
    Color acColor,
    Color bgColor,
  ) {
    final topText = _compText(controller.dialTop);
    final botText = _compText(controller.dialBot);

    switch (controller.dialId) {
      case 0:
        return Padding(
          padding: const EdgeInsets.all(12),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text(
                topText.isEmpty ? 'AT32 WATCH' : topText,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  color: fgColor.withValues(alpha: 0.7),
                  fontSize: 10,
                  letterSpacing: 0.6,
                ),
              ),
              Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text(
                    hh,
                    style: TextStyle(
                      color: fgColor,
                      fontSize: 44,
                      fontWeight: FontWeight.w800,
                      height: 0.96,
                      fontFeatures: const [FontFeature.tabularFigures()],
                    ),
                  ),
                  Text(
                    mm,
                    style: TextStyle(
                      color: acColor,
                      fontSize: 44,
                      fontWeight: FontWeight.w800,
                      height: 0.96,
                      fontFeatures: const [FontFeature.tabularFigures()],
                    ),
                  ),
                ],
              ),
              controller.dialBot == 1
                  ? Column(
                      children: [
                        ClipRRect(
                          borderRadius: BorderRadius.circular(4),
                          child: LinearProgressIndicator(
                            value: pct,
                            minHeight: 5,
                            backgroundColor: acColor.withValues(alpha: 0.18),
                            valueColor: AlwaysStoppedAnimation(acColor),
                          ),
                        ),
                        const SizedBox(height: 4),
                        Row(
                          mainAxisAlignment: MainAxisAlignment.spaceBetween,
                          children: [
                            Text(
                              'STEPS',
                              style: TextStyle(
                                color: fgColor.withValues(alpha: 0.65),
                                fontSize: 9,
                              ),
                            ),
                            Text(
                              '${(pct * 100).round()}%',
                              style: TextStyle(color: fgColor, fontSize: 9.5),
                            ),
                          ],
                        ),
                      ],
                    )
                  : Text(
                      botText,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(color: fgColor, fontSize: 10),
                    ),
            ],
          ),
        );
      case 1:
        return Padding(
          padding: const EdgeInsets.all(10),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text(
                topText.isEmpty ? 'ANALOG DIAL' : topText,
                style: TextStyle(
                  color: fgColor.withValues(alpha: 0.7),
                  fontSize: 9.5,
                ),
              ),
              Expanded(
                child: Padding(
                  padding: const EdgeInsets.all(4),
                  child: Center(
                    child: WatchFace(
                      now: controller.now,
                      use24Hour: controller.use24Hour,
                    ),
                  ),
                ),
              ),
              Text(
                botText,
                style: TextStyle(
                  color: acColor,
                  fontSize: 10,
                  fontFeatures: const [FontFeature.tabularFigures()],
                ),
              ),
            ],
          ),
        );
      case 2:
        final rollTilt = (controller.rollDeg / 90.0).clamp(-0.35, 0.35);
        return Padding(
          padding: const EdgeInsets.all(12),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  Text(
                    '9DOF ${controller.headingDeg.round()}°',
                    style: TextStyle(
                      color: acColor,
                      fontSize: 11.5,
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                  Text(
                    controller.use24Hour ? '24H' : (controller.now.hour >= 12 ? 'PM' : 'AM'),
                    style: TextStyle(color: fgColor.withValues(alpha: 0.6), fontSize: 9.5),
                  ),
                ],
              ),
              Text(
                controller.dialShowSeconds ? '$hh:$mm:$ss' : '$hh:$mm',
                style: TextStyle(
                  color: fgColor,
                  fontSize: controller.dialShowSeconds ? 25 : 32,
                  fontWeight: FontWeight.w800,
                  fontFeatures: const [FontFeature.tabularFigures()],
                ),
              ),
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 7),
                decoration: BoxDecoration(
                  color: acColor.withValues(alpha: 0.1),
                  borderRadius: BorderRadius.circular(10),
                  border: Border.all(color: acColor.withValues(alpha: 0.35)),
                ),
                child: Column(
                  children: [
                    Transform.rotate(
                      angle: rollTilt,
                      child: Container(height: 2, color: acColor),
                    ),
                    const SizedBox(height: 6),
                    Text(
                      'R ${controller.rollDeg.toStringAsFixed(0)}°  P ${controller.pitchDeg.toStringAsFixed(0)}°',
                      style: TextStyle(color: fgColor, fontSize: 9.5),
                    ),
                  ],
                ),
              ),
              Column(
                children: [
                  ClipRRect(
                    borderRadius: BorderRadius.circular(4),
                    child: LinearProgressIndicator(
                      value: pct,
                      minHeight: 5,
                      backgroundColor: fgColor.withValues(alpha: 0.15),
                      valueColor: AlwaysStoppedAnimation(acColor),
                    ),
                  ),
                  const SizedBox(height: 4),
                  Text(
                    '${controller.steps} / ${controller.stepGoal} 步',
                    style: TextStyle(color: acColor, fontSize: 9.5),
                  ),
                ],
              ),
            ],
          ),
        );
      default:
        final align = switch (controller.dialPhotoAlign) {
          1 => Alignment.topCenter,
          2 => Alignment.bottomCenter,
          _ => Alignment.center,
        };
        return Stack(
          fit: StackFit.expand,
          children: [
            if (photoImage != null)
              RawImage(
                image: photoImage,
                fit: BoxFit.cover,
                filterQuality: FilterQuality.none,
              )
            else
              Container(
                decoration: BoxDecoration(
                  gradient: LinearGradient(
                    begin: Alignment.topLeft,
                    end: Alignment.bottomRight,
                    colors: [
                      acColor.withValues(alpha: 0.25),
                      bgColor,
                    ],
                  ),
                ),
              ),
            Padding(
              padding: const EdgeInsets.all(10),
              child: Align(
                alignment: align,
                child: Container(
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                  decoration: controller.dialPhotoScrim
                      ? BoxDecoration(
                          color: Colors.black.withValues(alpha: 0.65),
                          borderRadius: BorderRadius.circular(12),
                        )
                      : null,
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Text(
                        '$hh:$mm',
                        style: TextStyle(
                          color: fgColor,
                          fontSize: 30,
                          fontWeight: FontWeight.w800,
                          height: 1.0,
                          fontFeatures: const [FontFeature.tabularFigures()],
                        ),
                      ),
                      const SizedBox(height: 3),
                      Text(
                        topText.isNotEmpty ? topText : botText,
                        style: TextStyle(color: acColor, fontSize: 10),
                      ),
                    ],
                  ),
                ),
              ),
            ),
          ],
        );
    }
  }
}

class _DialCustomizationCard extends StatelessWidget {
  const _DialCustomizationCard({
    required this.controller,
    required this.onAccentChanged,
  });

  final WatchController controller;
  final VoidCallback onAccentChanged;

  @override
  Widget build(BuildContext context) {
    return AppCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const SectionHeader(
            title: '系列表盘自定义',
            subtitle: '主题配色、自定义 RGB565 色彩、上下信息组件与相册时间排版',
          ),
          const SizedBox(height: 14),
          const Text('预设配色方案', style: TextStyle(fontWeight: FontWeight.w600, fontSize: 13)),
          const SizedBox(height: 8),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              for (final preset in _colorThemes)
                ActionChip(
                  avatar: Container(
                    width: 14,
                    height: 14,
                    decoration: BoxDecoration(
                      color: Color(preset.accent),
                      shape: BoxShape.circle,
                    ),
                  ),
                  label: Text(preset.name),
                  onPressed: () {
                    controller.updateDialConfig(
                      textColor: preset.text,
                      accentColor: preset.accent,
                      bgColor: preset.bg,
                    );
                    onAccentChanged();
                  },
                ),
            ],
          ),
          const SizedBox(height: 14),
          _ColorSwatchRow(
            label: '文字颜色',
            selected: controller.dialTextColor,
            options: _swatchColors,
            onSelected: (c) => controller.updateDialConfig(textColor: c),
          ),
          const SizedBox(height: 10),
          _ColorSwatchRow(
            label: '强调主色',
            selected: controller.dialAccentColor,
            options: _swatchColors,
            onSelected: (c) {
              controller.updateDialConfig(accentColor: c);
              onAccentChanged();
            },
          ),
          const SizedBox(height: 10),
          _ColorSwatchRow(
            label: '背景底色',
            selected: controller.dialBgColor,
            options: _bgSwatches,
            onSelected: (c) => controller.updateDialConfig(bgColor: c),
          ),
          const Divider(height: 24),
          LayoutBuilder(
            builder: (context, c) {
              final dTop = DropdownButtonFormField<int>(
                initialValue: controller.dialTop,
                isExpanded: true,
                decoration: const InputDecoration(labelText: '顶部信息组件'),
                items: List.generate(
                  _compLabels.length,
                  (i) => DropdownMenuItem(value: i, child: Text(_compLabels[i])),
                ),
                onChanged: (v) {
                  if (v != null) controller.updateDialConfig(top: v);
                },
              );
              final dBot = DropdownButtonFormField<int>(
                initialValue: controller.dialBot,
                isExpanded: true,
                decoration: const InputDecoration(labelText: '底部信息组件'),
                items: List.generate(
                  _compLabels.length,
                  (i) => DropdownMenuItem(value: i, child: Text(_compLabels[i])),
                ),
                onChanged: (v) {
                  if (v != null) controller.updateDialConfig(bot: v);
                },
              );
              if (c.maxWidth < 340) {
                return Column(
                  children: [dTop, const SizedBox(height: 12), dBot],
                );
              }
              return Row(
                children: [
                  Expanded(child: dTop),
                  const SizedBox(width: 12),
                  Expanded(child: dBot),
                ],
              );
            },
          ),
          const SizedBox(height: 8),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('显示秒针 / 呼吸冒号'),
            value: controller.dialShowSeconds,
            onChanged: (v) => controller.updateDialConfig(showSeconds: v),
          ),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('24 小时制'),
            value: controller.use24Hour,
            onChanged: controller.setUse24Hour,
          ),
          if (controller.dialId == 3) ...[
            const SizedBox(height: 4),
            Wrap(
              spacing: 12,
              runSpacing: 8,
              crossAxisAlignment: WrapCrossAlignment.center,
              alignment: WrapAlignment.spaceBetween,
              children: [
                const Text('相册时间位置', style: TextStyle(fontWeight: FontWeight.w600)),
                SegmentedButton<int>(
                  segments: const [
                    ButtonSegment(value: 1, label: Text('居顶')),
                    ButtonSegment(value: 0, label: Text('居中')),
                    ButtonSegment(value: 2, label: Text('居底')),
                  ],
                  selected: {controller.dialPhotoAlign},
                  showSelectedIcon: false,
                  onSelectionChanged: (s) =>
                      controller.updateDialConfig(photoAlign: s.first),
                ),
              ],
            ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('半透明暗角时间胶囊'),
              subtitle: const Text('防止明亮背景图遮挡时间读数'),
              value: controller.dialPhotoScrim,
              onChanged: (v) => controller.updateDialConfig(photoScrim: v),
            ),
          ],
        ],
      ),
    );
  }
}

class _ColorSwatchRow extends StatelessWidget {
  const _ColorSwatchRow({
    required this.label,
    required this.selected,
    required this.options,
    required this.onSelected,
  });

  final String label;
  final int selected;
  final List<int> options;
  final ValueChanged<int> onSelected;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Row(
      children: [
        SizedBox(
          width: 72,
          child: Text(label, style: const TextStyle(fontSize: 12.5)),
        ),
        Expanded(
          child: Wrap(
            spacing: 8,
            runSpacing: 6,
            children: [
              for (final hex in options)
                GestureDetector(
                  onTap: () => onSelected(hex),
                  child: Container(
                    width: 24,
                    height: 24,
                    decoration: BoxDecoration(
                      color: Color(hex),
                      shape: BoxShape.circle,
                      border: Border.all(
                        color: selected == hex
                            ? scheme.primary
                            : scheme.outlineVariant,
                        width: selected == hex ? 2.4 : 1.0,
                      ),
                    ),
                  ),
                ),
            ],
          ),
        ),
      ],
    );
  }
}

class _ImageCompressionCard extends StatelessWidget {
  const _ImageCompressionCard({
    required this.wallpaperId,
    required this.pickedImageName,
    required this.sourceImage,
    required this.srcWidth,
    required this.srcHeight,
    required this.cropRectNorm,
    required this.format,
    required this.dither,
    required this.brightness,
    required this.contrast,
    required this.encoded,
    required this.profile,
    required this.encoding,
    required this.uploading,
    required this.sentChunks,
    required this.totalChunks,
    required this.onSelectWallpaper,
    required this.onPickLocalPhoto,
    required this.onCropChanged,
    required this.onResetCropPreset,
    required this.onFormatChanged,
    required this.onDitherChanged,
    required this.onBrightnessChanged,
    required this.onContrastChanged,
    required this.onUploadPhoto,
  });

  final String wallpaperId;
  final String? pickedImageName;
  final ui.Image? sourceImage;
  final int srcWidth;
  final int srcHeight;
  final ui.Rect cropRectNorm;
  final WatchExtImgFormat format;
  final bool dither;
  final double brightness;
  final double contrast;
  final EncodedWatchImage? encoded;
  final WatchImageProfile? profile;
  final bool encoding;
  final bool uploading;
  final int sentChunks;
  final int totalChunks;
  final ValueChanged<String> onSelectWallpaper;
  final VoidCallback onPickLocalPhoto;
  final void Function(ui.Rect next, {bool immediate}) onCropChanged;
  final void Function(double scale, double biasY) onResetCropPreset;
  final ValueChanged<WatchExtImgFormat> onFormatChanged;
  final ValueChanged<bool> onDitherChanged;
  final ValueChanged<double> onBrightnessChanged;
  final ValueChanged<double> onContrastChanged;
  final VoidCallback onUploadPhoto;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final bytes = encoded?.bytes.length ?? 0;
    final raw = watchDisplayWidth * watchDisplayHeight * 2; // 40,960 B
    final fitsChip = encoded?.fitsOnChipHotSwap ?? true;
    final fitsPool = encoded?.fitsFlash ?? true;

    return AppCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SectionHeader(
            title: '128×160 相册裁切选区与抗噪压缩工作台',
            subtitle: pickedImageName != null
                ? '源图 $pickedImageName ($srcWidth×$srcHeight) · 拖拽选框平移、拖拽四角缩放大区域'
                : '选择本地图片后可直接框选任意 4:5 局部区域，表盘预览会跟着刷新',
            trailing: encoding
                ? const StatusPill(label: '编码中…')
                : StatusPill(
                    label: encoded == null
                        ? '--'
                        : '${(bytes / 1024).toStringAsFixed(1)} KB · ${fitsChip ? '片内热槽' : (fitsPool ? '片外池' : '超限')}',
                    color: fitsChip
                        ? AppPalette.mint
                        : (fitsPool ? _dialAmber : const Color(0xFFEF4444)),
                  ),
          ),
          const SizedBox(height: 12),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              FilledButton.tonalIcon(
                onPressed: uploading ? null : onPickLocalPhoto,
                icon: const Icon(Icons.photo_library_outlined, size: 18),
                label: const Text('导入本地图片'),
              ),
              for (final wp in _wallpaperPresets)
                ChoiceChip(
                  label: Text(wp.$2),
                  selected: pickedImageName == null && wallpaperId == wp.$1,
                  onSelected: (_) => onSelectWallpaper(wp.$1),
                ),
            ],
          ),
          if (sourceImage != null) ...[
            const SizedBox(height: 12),
            _InteractiveImageCropper(
              image: sourceImage!,
              srcWidth: srcWidth,
              srcHeight: srcHeight,
              cropRectNorm: cropRectNorm,
              accent: fitsChip ? AppPalette.mint : _dialAmber,
              onChanged: onCropChanged,
            ),
            const SizedBox(height: 8),
            Wrap(
              spacing: 6,
              runSpacing: 6,
              children: [
                ActionChip(
                  label: const Text('居中全幅', style: TextStyle(fontSize: 11.5)),
                  onPressed: () => onResetCropPreset(1.0, 0.5),
                ),
                ActionChip(
                  label: const Text('偏上构图', style: TextStyle(fontSize: 11.5)),
                  onPressed: () => onResetCropPreset(0.95, 0.08),
                ),
                ActionChip(
                  label: const Text('偏下构图', style: TextStyle(fontSize: 11.5)),
                  onPressed: () => onResetCropPreset(0.95, 0.92),
                ),
                ActionChip(
                  label: const Text('中心特写 55%', style: TextStyle(fontSize: 11.5)),
                  onPressed: () => onResetCropPreset(0.55, 0.35),
                ),
              ],
            ),
          ],
          const SizedBox(height: 12),
          SegmentedButton<WatchExtImgFormat>(
            segments: const [
              ButtonSegment(
                value: WatchExtImgFormat.pal4Rle,
                label: Text('16色抗噪 RLE'),
              ),
              ButtonSegment(
                value: WatchExtImgFormat.rgb565Rle,
                label: Text('真彩 RLE'),
              ),
              ButtonSegment(
                value: WatchExtImgFormat.mono1bpp,
                label: Text('黑白 1bpp'),
              ),
            ],
            selected: {format},
            showSelectedIcon: false,
            onSelectionChanged: (s) => onFormatChanged(s.first),
          ),
          if (format != WatchExtImgFormat.rgb565Rle)
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('边缘自适应抗噪抖动（纯白背景零斑点）'),
              subtitle: const Text('锁定纯白/平坦色块并抑制误差溢出，仅在渐变区启用 FS 抖动'),
              value: dither,
              onChanged: onDitherChanged,
            ),
          Row(
            children: [
              const SizedBox(width: 40, child: Text('亮度', style: TextStyle(fontSize: 12))),
              Expanded(
                child: Slider(
                  value: brightness,
                  min: -0.35,
                  max: 0.35,
                  onChanged: onBrightnessChanged,
                ),
              ),
              const SizedBox(width: 40, child: Text('对比', style: TextStyle(fontSize: 12))),
              Expanded(
                child: Slider(
                  value: contrast,
                  min: 0.6,
                  max: 1.6,
                  onChanged: onContrastChanged,
                ),
              ),
            ],
          ),
          const SizedBox(height: 12),
          Container(
            padding: const EdgeInsets.all(12),
            decoration: BoxDecoration(
              color: scheme.surfaceContainerHigh.withValues(alpha: 0.7),
              borderRadius: BorderRadius.circular(12),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Wrap(
                  alignment: WrapAlignment.spaceBetween,
                  spacing: 10,
                  runSpacing: 4,
                  children: [
                    Text(
                      fitsChip
                          ? '片内热槽命中：$bytes / $watchOnChipHotSwapMaxBytes B（XIP 零 I2C）'
                          : '动态池体积：$bytes / $watchExtImgMaxBytes B（超过 9KB 走 AT24C256）',
                      style: TextStyle(
                        fontSize: 12,
                        fontWeight: FontWeight.w700,
                        color: fitsChip
                            ? AppPalette.mint
                            : (fitsPool ? _dialAmber : const Color(0xFFEF4444)),
                      ),
                    ),
                    Text(
                      '压缩比 ${(raw / math.max(1, bytes)).toStringAsFixed(1)}×',
                      style: TextStyle(
                        fontSize: 11,
                        color: scheme.onSurfaceVariant,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 6),
                ClipRRect(
                  borderRadius: BorderRadius.circular(6),
                  child: LinearProgressIndicator(
                    value: fitsChip
                        ? (bytes / watchOnChipHotSwapMaxBytes).clamp(0.0, 1.0)
                        : (bytes / watchExtImgMaxBytes).clamp(0.0, 1.0),
                    minHeight: 6,
                    color: fitsChip
                        ? AppPalette.mint
                        : (fitsPool ? _dialAmber : const Color(0xFFEF4444)),
                  ),
                ),
                const SizedBox(height: 10),
                FilledButton.icon(
                  onPressed: (!fitsPool || uploading) ? null : onUploadPhoto,
                  icon: const Icon(Icons.cloud_upload_outlined, size: 18),
                  label: Text(
                    uploading
                        ? '正在写入 ($sentChunks/$totalChunks)...'
                        : (fitsChip
                              ? '编码并写入 AT32 片内热交换槽设为表盘'
                              : (fitsPool
                                    ? '写入 AT24C256 设为自定义表盘'
                                    : '超出最大容量 · 请改用 16 色 RLE')),
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

enum _CropDragMode { none, pan, tl, tr, bl, br }

class _InteractiveImageCropper extends StatefulWidget {
  const _InteractiveImageCropper({
    required this.image,
    required this.srcWidth,
    required this.srcHeight,
    required this.cropRectNorm,
    required this.accent,
    required this.onChanged,
  });

  final ui.Image image;
  final int srcWidth;
  final int srcHeight;
  final ui.Rect cropRectNorm;
  final Color accent;
  final void Function(ui.Rect next, {bool immediate}) onChanged;

  @override
  State<_InteractiveImageCropper> createState() =>
      _InteractiveImageCropperState();
}

class _InteractiveImageCropperState extends State<_InteractiveImageCropper> {
  _CropDragMode _mode = _CropDragMode.none;
  ui.Rect _startNorm = ui.Rect.zero;
  Offset _startLocal = Offset.zero;
  ui.Rect _renderedImgRect = ui.Rect.zero;

  double get _maxNormW {
    final imgAspect = widget.srcWidth / math.max(1, widget.srcHeight);
    const targetAspect = watchDisplayWidth / watchDisplayHeight; // 0.8
    return imgAspect >= targetAspect
        ? (targetAspect / imgAspect).clamp(0.12, 1.0)
        : 1.0;
  }

  double get _maxNormH {
    final imgAspect = widget.srcWidth / math.max(1, widget.srcHeight);
    const targetAspect = watchDisplayWidth / watchDisplayHeight; // 0.8
    return imgAspect >= targetAspect
        ? 1.0
        : (imgAspect / targetAspect).clamp(0.12, 1.0);
  }

  ui.Rect _rectPx(ui.Rect imgRect) {
    final n = widget.cropRectNorm;
    return ui.Rect.fromLTWH(
      imgRect.left + n.left * imgRect.width,
      imgRect.top + n.top * imgRect.height,
      n.width * imgRect.width,
      n.height * imgRect.height,
    );
  }

  ui.Rect _computeImageRect(Size box) {
    final iw = widget.srcWidth.toDouble();
    final ih = widget.srcHeight.toDouble();
    final scale = math.min(box.width / iw, box.height / ih);
    final w = iw * scale;
    final h = ih * scale;
    return ui.Rect.fromLTWH((box.width - w) / 2, (box.height - h) / 2, w, h);
  }

  void _handleDown(DragStartDetails d) {
    if (_renderedImgRect.isEmpty) return;
    _startLocal = d.localPosition;
    _startNorm = widget.cropRectNorm;
    final cropPx = _rectPx(_renderedImgRect);
    const hitR = 22.0;
    if ((d.localPosition - cropPx.topLeft).distance <= hitR) {
      _mode = _CropDragMode.tl;
    } else if ((d.localPosition - cropPx.topRight).distance <= hitR) {
      _mode = _CropDragMode.tr;
    } else if ((d.localPosition - cropPx.bottomLeft).distance <= hitR) {
      _mode = _CropDragMode.bl;
    } else if ((d.localPosition - cropPx.bottomRight).distance <= hitR) {
      _mode = _CropDragMode.br;
    } else if (cropPx.contains(d.localPosition)) {
      _mode = _CropDragMode.pan;
    } else {
      // 点击空白处直接把选区中心移过去
      final nx = ((d.localPosition.dx - _renderedImgRect.left) /
              _renderedImgRect.width)
          .clamp(0.0, 1.0);
      final ny = ((d.localPosition.dy - _renderedImgRect.top) /
              _renderedImgRect.height)
          .clamp(0.0, 1.0);
      final nl = (nx - _startNorm.width / 2).clamp(0.0, 1.0 - _startNorm.width);
      final nt = (ny - _startNorm.height / 2).clamp(0.0, 1.0 - _startNorm.height);
      _startNorm = ui.Rect.fromLTWH(nl, nt, _startNorm.width, _startNorm.height);
      _mode = _CropDragMode.pan;
      widget.onChanged(_startNorm, immediate: false);
    }
  }

  void _handleUpdate(DragUpdateDetails d) {
    if (_mode == _CropDragMode.none || _renderedImgRect.isEmpty) return;
    final dxN = (d.localPosition.dx - _startLocal.dx) / _renderedImgRect.width;
    final dyN = (d.localPosition.dy - _startLocal.dy) / _renderedImgRect.height;

    if (_mode == _CropDragMode.pan) {
      final nl = (_startNorm.left + dxN).clamp(0.0, 1.0 - _startNorm.width);
      final nt = (_startNorm.top + dyN).clamp(0.0, 1.0 - _startNorm.height);
      widget.onChanged(
        ui.Rect.fromLTWH(nl, nt, _startNorm.width, _startNorm.height),
      );
      return;
    }

    // 四角缩放严格锁定 128:160 (4:5) 物理像素纵横比，锚定对角顶点
    final ratioHPerW = _maxNormH / math.max(0.001, _maxNormW);
    final minW = math.min(0.16, _maxNormW);
    double ax, ay; // anchor
    double rawW, rawH;
    switch (_mode) {
      case _CropDragMode.tl:
        ax = _startNorm.right;
        ay = _startNorm.bottom;
        rawW = (_startNorm.width - dxN);
        rawH = (_startNorm.height - dyN);
      case _CropDragMode.tr:
        ax = _startNorm.left;
        ay = _startNorm.bottom;
        rawW = (_startNorm.width + dxN);
        rawH = (_startNorm.height - dyN);
      case _CropDragMode.bl:
        ax = _startNorm.right;
        ay = _startNorm.top;
        rawW = (_startNorm.width - dxN);
        rawH = (_startNorm.height + dyN);
      case _CropDragMode.br:
        ax = _startNorm.left;
        ay = _startNorm.top;
        rawW = (_startNorm.width + dxN);
        rawH = (_startNorm.height + dyN);
      case _CropDragMode.none:
      case _CropDragMode.pan:
        return;
    }

    final candW = ((rawW + rawH / ratioHPerW) * 0.5).clamp(minW, _maxNormW);
    final maxAvailW = (_mode == _CropDragMode.tl || _mode == _CropDragMode.bl)
        ? ax
        : (1.0 - ax);
    final maxAvailH = (_mode == _CropDragMode.tl || _mode == _CropDragMode.tr)
        ? ay
        : (1.0 - ay);
    final boundedW = math.min(
      candW,
      math.min(maxAvailW, maxAvailH / ratioHPerW),
    );
    final nw = boundedW.clamp(minW, _maxNormW);
    final nh = (nw * ratioHPerW).clamp(minW * ratioHPerW, _maxNormH);

    final nl = (_mode == _CropDragMode.tl || _mode == _CropDragMode.bl)
        ? (ax - nw)
        : ax;
    final nt = (_mode == _CropDragMode.tl || _mode == _CropDragMode.tr)
        ? (ay - nh)
        : ay;
    widget.onChanged(
      ui.Rect.fromLTWH(
        nl.clamp(0.0, 1.0 - nw),
        nt.clamp(0.0, 1.0 - nh),
        nw,
        nh,
      ),
    );
  }

  void _handleScroll(PointerScrollEvent e) {
    final step = e.scrollDelta.dy > 0 ? 0.06 : -0.06;
    final ratioHPerW = _maxNormH / math.max(0.001, _maxNormW);
    final minW = math.min(0.16, _maxNormW);
    final cur = widget.cropRectNorm;
    final nw = (cur.width + step).clamp(minW, _maxNormW);
    final nh = (nw * ratioHPerW).clamp(minW * ratioHPerW, _maxNormH);
    final cx = cur.center.dx;
    final cy = cur.center.dy;
    final nl = (cx - nw / 2).clamp(0.0, 1.0 - nw);
    final nt = (cy - nh / 2).clamp(0.0, 1.0 - nh);
    widget.onChanged(ui.Rect.fromLTWH(nl, nt, nw, nh));
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 224,
      decoration: BoxDecoration(
        color: const Color(0xFF0A0D0C),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: AppPalette.line),
      ),
      clipBehavior: Clip.antiAlias,
      child: LayoutBuilder(
        builder: (context, constraints) {
          final size = Size(constraints.maxWidth, constraints.maxHeight);
          _renderedImgRect = _computeImageRect(size);
          return Listener(
            onPointerSignal: (s) {
              if (s is PointerScrollEvent) _handleScroll(s);
            },
            child: GestureDetector(
              behavior: HitTestBehavior.opaque,
              onPanStart: _handleDown,
              onPanUpdate: _handleUpdate,
              onPanEnd: (_) {
                _mode = _CropDragMode.none;
                widget.onChanged(widget.cropRectNorm, immediate: true);
              },
              child: CustomPaint(
                size: size,
                painter: _CropOverlayPainter(
                  image: widget.image,
                  imgRect: _renderedImgRect,
                  cropRectNorm: widget.cropRectNorm,
                  accent: widget.accent,
                ),
              ),
            ),
          );
        },
      ),
    );
  }
}

class _CropOverlayPainter extends CustomPainter {
  const _CropOverlayPainter({
    required this.image,
    required this.imgRect,
    required this.cropRectNorm,
    required this.accent,
  });

  final ui.Image image;
  final ui.Rect imgRect;
  final ui.Rect cropRectNorm;
  final Color accent;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawImageRect(
      image,
      ui.Rect.fromLTWH(0, 0, image.width.toDouble(), image.height.toDouble()),
      imgRect,
      Paint()..filterQuality = FilterQuality.medium,
    );

    final sel = ui.Rect.fromLTWH(
      imgRect.left + cropRectNorm.left * imgRect.width,
      imgRect.top + cropRectNorm.top * imgRect.height,
      cropRectNorm.width * imgRect.width,
      cropRectNorm.height * imgRect.height,
    );

    final mask = Path()
      ..addRect(imgRect)
      ..addRect(sel)
      ..fillType = PathFillType.evenOdd;
    canvas.drawPath(
      mask,
      Paint()..color = const Color(0x99000000),
    );

    final gridPaint = Paint()
      ..color = Colors.white.withValues(alpha: 0.25)
      ..strokeWidth = 0.8;
    for (var i = 1; i <= 2; i++) {
      final dx = sel.left + sel.width * i / 3;
      final dy = sel.top + sel.height * i / 3;
      canvas.drawLine(Offset(dx, sel.top), Offset(dx, sel.bottom), gridPaint);
      canvas.drawLine(Offset(sel.left, dy), Offset(sel.right, dy), gridPaint);
    }

    canvas.drawRect(
      sel,
      Paint()
        ..color = accent
        ..style = PaintingStyle.stroke
        ..strokeWidth = 2.0,
    );

    final handleFill = Paint()..color = accent;
    final handleStroke = Paint()
      ..color = Colors.black
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.4;
    for (final pt in [
      sel.topLeft,
      sel.topRight,
      sel.bottomLeft,
      sel.bottomRight,
    ]) {
      canvas.drawCircle(pt, 5.5, handleFill);
      canvas.drawCircle(pt, 5.5, handleStroke);
    }
  }

  @override
  bool shouldRepaint(covariant _CropOverlayPainter oldDelegate) {
    return oldDelegate.image != image ||
        oldDelegate.imgRect != imgRect ||
        oldDelegate.cropRectNorm != cropRectNorm ||
        oldDelegate.accent != accent;
  }
}

class _PresetTile extends StatelessWidget {
  const _PresetTile({
    required this.spec,
    required this.selected,
    required this.onTap,
  });

  final _DialSpec spec;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(16),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 180),
        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
        decoration: BoxDecoration(
          color: selected
              ? spec.accent.withValues(alpha: 0.12)
              : scheme.surfaceContainerHigh.withValues(alpha: 0.65),
          borderRadius: BorderRadius.circular(16),
          border: Border.all(
            color: selected
                ? spec.accent.withValues(alpha: 0.65)
                : scheme.outlineVariant.withValues(alpha: 0.6),
            width: selected ? 1.4 : 1,
          ),
        ),
        child: Row(
          children: [
            Container(
              width: 10,
              height: 34,
              decoration: BoxDecoration(
                color: spec.accent,
                borderRadius: BorderRadius.circular(999),
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Wrap(
                    spacing: 6,
                    runSpacing: 4,
                    crossAxisAlignment: WrapCrossAlignment.center,
                    children: [
                      Text(
                        spec.name,
                        style: const TextStyle(
                          fontSize: 14.5,
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                      Container(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 7,
                          vertical: 2,
                        ),
                        decoration: BoxDecoration(
                          color: spec.accent.withValues(alpha: 0.14),
                          borderRadius: BorderRadius.circular(999),
                        ),
                        child: Text(
                          spec.tag,
                          style: TextStyle(
                            color: spec.accent,
                            fontSize: 10,
                            fontWeight: FontWeight.w700,
                          ),
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 3),
                  Text(
                    spec.subtitle,
                    style: TextStyle(
                      color: scheme.onSurfaceVariant,
                      fontSize: 11.5,
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(width: 8),
            Column(
              crossAxisAlignment: CrossAxisAlignment.end,
              children: [
                Text(
                  spec.sizeHint,
                  style: TextStyle(
                    color: scheme.onSurfaceVariant,
                    fontSize: 11,
                    fontWeight: FontWeight.w600,
                  ),
                ),
                const SizedBox(height: 4),
                Icon(
                  selected
                      ? Icons.radio_button_checked_rounded
                      : Icons.radio_button_off_rounded,
                  size: 18,
                  color: selected ? spec.accent : scheme.outline,
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class _FlashPartitionCard extends StatelessWidget {
  const _FlashPartitionCard({required this.scheme, required this.controller});

  final ColorScheme scheme;
  final WatchController controller;

  @override
  Widget build(BuildContext context) {
    final totalPool = controller.extPoolTotal > 0 ? controller.extPoolTotal : 32000;
    final imgB = controller.extPoolState?.imgBytes ??
        (controller.mcuImgReady ? controller.mcuImgBytes : 0);
    final msgB = controller.extPoolMsgBytes;
    final usedOther = (controller.extPoolUsed - imgB - msgB).clamp(0, totalPool);
    final freeB = (totalPool - imgB - msgB - usedOther).clamp(0, totalPool);
    final imgFlex = imgB > 0 ? ((imgB * 24) ~/ totalPool).clamp(1, 20) : 0;
    final msgFlex = msgB > 0 ? ((msgB * 24) ~/ totalPool).clamp(1, 20) : 0;
    final freeFlex = freeB > 0 ? ((freeB * 24) ~/ totalPool).clamp(1, 24) : 1;

    return AppCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SectionHeader(
            title: '32KB 外置 AT24C256 动态 ROM (0x50)',
            subtitle:
                '0x0000 双槽配置 + 0x0100 CRC16 目录 + 0x0300..0x7FFF 32,000B 统一动态池（PackBits 压缩）',
            trailing: StatusPill(
              label: controller.extFlashPresent
                  ? '${controller.extPoolUsed} / $totalPool B'
                  : '32,768 B',
            ),
          ),
          const SizedBox(height: 14),
          ClipRRect(
            borderRadius: BorderRadius.circular(8),
            child: SizedBox(
              height: 12,
              child: Row(
                children: [
                  const Expanded(
                    flex: 1,
                    child: ColoredBox(color: Color(0xFFF5B759)),
                  ),
                  const SizedBox(width: 2),
                  const Expanded(
                    flex: 1,
                    child: ColoredBox(color: Color(0xFF4B6359)),
                  ),
                  if (imgFlex > 0) ...[
                    const SizedBox(width: 2),
                    Expanded(
                      flex: imgFlex,
                      child: const ColoredBox(color: AppPalette.mint),
                    ),
                  ],
                  if (msgFlex > 0) ...[
                    const SizedBox(width: 2),
                    Expanded(
                      flex: msgFlex,
                      child: const ColoredBox(color: Color(0xFF5A8D9E)),
                    ),
                  ],
                  const SizedBox(width: 2),
                  Expanded(
                    flex: freeFlex,
                    child: ColoredBox(
                      color: scheme.surfaceContainerHigh,
                    ),
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(height: 14),
          const _PartitionRow(
            color: Color(0xFFF5B759),
            range: '0x0000 – 0x00FF',
            title: '双槽设置 / 表盘样式 / 磁校准持久化 (CFG)',
            size: '256 B',
          ),
          const SizedBox(height: 8),
          const _PartitionRow(
            color: Color(0xFF4B6359),
            range: '0x0100 – 0x02FF',
            title: 'ATD1 动态块双槽 CRC16 目录（最多 20 条消息索引）',
            size: '512 B',
          ),
          const SizedBox(height: 8),
          _PartitionRow(
            color: AppPalette.mint,
            range: '0x0800DC00 – 0x08010000 (MCU 片内)',
            title: 'AT32 片内自定义表盘极速热槽（≤9KB XIP 120MHz 零 I2C）',
            size: controller.mcuImgOnChip && controller.mcuImgReady
                ? '已激活 (${controller.mcuImgBytes} B)'
                : '9,216 B 可用',
          ),
          const SizedBox(height: 8),
          _PartitionRow(
            color: AppPalette.mint,
            range: '0x0300 – 0x7FFF · 图片回退块',
            title: '超 9KB 大图自动写入 AT24C256（含 SYS1/SHD1/SPR1/ANL1 共 6 项按需包）',
            size: imgB > 0 ? '$imgB B' : '已走片内热槽',
          ),
          const SizedBox(height: 8),
          _PartitionRow(
            color: const Color(0xFF5A8D9E),
            range: '0x0300 – 0x7FFF · 持久化消息',
            title: '手机/PC 通知行+详情 PackBits 压缩（断电不丢失）',
            size: '${controller.extPoolMsgCount}/${controller.extPoolMsgMax} 条 · $msgB B',
          ),
          const SizedBox(height: 8),
          _PartitionRow(
            color: const Color(0xFFF5B759),
            range: '0x0300 · 按需小程序与字库 (0x2F)',
            title: 'SYS1(动画图标) / SHD1(23 枚华文琥珀) / SPR1 / ANL1 / C3D1 / ABT2',
            size: (() {
              final f = controller.extPoolState?.assetFlags ?? 0;
              var ready = 0;
              for (final b in const [0x02, 0x04, 0x08, 0x20, 0x40, 0x80]) {
                if ((f & b) != 0) ready++;
              }
              return '$ready / 6 已就绪';
            })(),
          ),
          const SizedBox(height: 8),
          _PartitionRow(
            color: scheme.outlineVariant,
            range: '最大可用连续块',
            title: '当前剩余动态 ROM 空间（空间不足自动淘汰最旧通知）',
            size: '${controller.extPoolFreeMax} B 空闲',
          ),
          if (controller.connected) ...[
            const SizedBox(height: 10),
            Align(
              alignment: Alignment.centerRight,
              child: OutlinedButton.icon(
                onPressed: () async {
                  final messenger = ScaffoldMessenger.of(context);
                  final ok = await controller.syncExtAssetsNow();
                  if (!context.mounted) return;
                  messenger.showSnackBar(
                    SnackBar(
                      content: Text(
                        ok ? '已全量同步 6 项片外小程序、字库与关于资源 (0x2F)。' : '请先连接手表后再同步。',
                      ),
                    ),
                  );
                },
                icon: const Icon(Icons.sync_rounded, size: 16),
                label: const Text('全量同步片外小程序与字库 (0x2F)'),
              ),
            ),
          ],
        ],
      ),
    );
  }
}

class _PartitionRow extends StatelessWidget {
  const _PartitionRow({
    required this.color,
    required this.range,
    required this.title,
    required this.size,
  });

  final Color color;
  final String range;
  final String title;
  final String size;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Container(
              width: 9,
              height: 9,
              decoration: BoxDecoration(color: color, shape: BoxShape.circle),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: Text(
                range,
                style: const TextStyle(
                  fontSize: 11.5,
                  fontWeight: FontWeight.w700,
                  fontFeatures: [FontFeature.tabularFigures()],
                ),
              ),
            ),
            Text(
              size,
              style: TextStyle(
                fontSize: 11.5,
                fontWeight: FontWeight.w700,
                color: scheme.onSurface,
              ),
            ),
          ],
        ),
        const SizedBox(height: 2),
        Padding(
          padding: const EdgeInsets.only(left: 17),
          child: Text(
            title,
            style: TextStyle(fontSize: 11, color: scheme.onSurfaceVariant),
          ),
        ),
      ],
    );
  }
}
