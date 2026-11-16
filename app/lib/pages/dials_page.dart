import 'dart:async';
import 'dart:typed_data';
import 'dart:ui' as ui;

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
    subtitle: '本地 ROM 大字号数字时间；支持自定义双色与上下信息栏',
    tag: '本地 ROM',
    sizeHint: '省电高对比',
    accent: AppPalette.mint,
  ),
  _DialSpec(
    id: 1,
    name: '墨竹极简指针',
    subtitle: '动态时针/分针/可选秒针与 12 小时刻度，搭配任意主题色',
    tag: '本地 ROM',
    sizeHint: '矢量表盘',
    accent: AppPalette.mintSoft,
  ),
  _DialSpec(
    id: 2,
    name: '九轴运动仪表',
    subtitle: '9-DOF 磁航向 + 实时 Roll/Pitch 地平线倾角条 + 步数进度环',
    tag: '本地 ROM',
    sizeHint: '9 轴实时',
    accent: _dialAmber,
  ),
    _DialSpec(
    id: 3,
    name: '自定义相册表盘',
    subtitle: '将相册或内置壁纸压缩至 128×160 并写入 AT24C256 动态 ROM 池',
    tag: '32KB I2C',
    sizeHint: '动态共享池',
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

  double _zoom = 1.0;
  double _brightness = 0.0;
  double _contrast = 1.0;
  WatchExtImgFormat _format = WatchExtImgFormat.pal4Rle;
  bool _dither = true;

  EncodedWatchImage? _encoded;
  ui.Image? _previewImage;
  bool _encoding = false;
  bool _uploading = false;
  int _sentChunks = 0;
  int _totalChunks = 0;

  @override
  void initState() {
    super.initState();
    unawaited(_rebuildImagePreview());
  }

  @override
  void dispose() {
    _previewImage?.dispose();
    super.dispose();
  }

  Future<void> _pickLocalPhoto() async {
    final messenger = ScaffoldMessenger.of(context);
    final picked = await ImagePickerBridge.pickImage();
    if (!mounted || picked == null) return;
    setState(() {
      _pickedFileBytes = picked.bytes;
      _pickedImageName = picked.name;
      _zoom = 1.0;
      _brightness = 0.0;
      _contrast = 1.0;
    });
    widget.controller.updateDialConfig(id: 3, syncToWatch: false);
    await _rebuildImagePreview();
    if (!mounted) return;
    messenger.hideCurrentSnackBar();
    messenger.showSnackBar(
      SnackBar(
        content: Text('已载入「${picked.name}」并生成 128×160 ${_format.label} 预览。'),
      ),
    );
  }

  Future<void> _rebuildImagePreview() async {
    if (_encoding) return;
    setState(() => _encoding = true);
    try {
      Uint8List rgba;
      final fileBytes = _pickedFileBytes;
      if (fileBytes != null) {
        rgba = await cropImageBytesToRgba(
          fileBytes,
          zoom: _zoom,
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

      final encoded = encodeWatchImage(
        rgba,
        format: _format,
        dither: _dither,
        accentArgb: widget.controller.dialAccentColor,
      );
      if (!mounted) return;
      setState(() {
        _encoded = encoded;
        _encoding = false;
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
    } finally {
      if (mounted && _encoding) setState(() => _encoding = false);
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
                ? '自定义相册表盘（${enc.bytes.length} B，压缩 ${enc.compressionRatio.toStringAsFixed(1)}×）已写入 AT24C256 并立即生效！'
                : '写入 AT24C256 失败，请确认手表端芯片已就绪并重连重试。',
          ),
        ),
      );
    } finally {
      if (mounted) setState(() => _uploading = false);
    }
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

    return PageScroll(
      children: [
        PageHeader(
          title: '表盘',
          subtitle: 'AT24C256 动态 ROM 池 · 长按手表 KEY2 进表盘编辑，KEY1/3 左右选，短按 KEY2 确认',
          trailing: StatusPill(
            label: controller.connected
                ? (controller.extFlashPresent ? 'AT24C256 · 0x$addrHex 在线' : '未探测到 I2C Flash')
                : '128×160 实时预览',
            color: controller.connected && controller.extFlashPresent
                ? AppPalette.mint
                : _dialAmber,
          ),
        ),
        const SizedBox(height: 22),
        LayoutBuilder(
          builder: (context, constraints) {
            final preview = _DialStageCard(
              spec: current,
              controller: controller,
              photoImage: _previewImage,
              encoded: _encoded,
              uploading: _uploading,
              sentChunks: _sentChunks,
              totalChunks: _totalChunks,
              onPushConfig: _pushDialConfig,
              onUploadPhoto: _uploadPhotoDial,
            );
            final rightColumn = Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                AppCard(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      const SectionHeader(
                        title: '表盘方案预览库',
                        subtitle: '点选实时切换预览；手表端长按 KEY2 也可循环切换并写入 AT24C256',
                      ),
                      const SizedBox(height: 14),
                      for (var i = 0; i < _dialPresets.length; i++) ...[
                        if (i > 0) const SizedBox(height: 10),
                        _PresetTile(
                          spec: _dialPresets[i],
                          selected: i == controller.dialId,
                          onTap: () => controller.updateDialConfig(id: i),
                        ),
                      ],
                    ],
                  ),
                ),
                const SizedBox(height: 16),
                _DialCustomizationCard(
                  controller: controller,
                  onAccentChanged: () {
                    if (_format == WatchExtImgFormat.mono1bpp) {
                      unawaited(_rebuildImagePreview());
                    }
                  },
                ),
                const SizedBox(height: 16),
                _ImageCompressionCard(
                  wallpaperId: _wallpaperId,
                  pickedImageName: _pickedImageName,
                  format: _format,
                  dither: _dither,
                  zoom: _zoom,
                  brightness: _brightness,
                  contrast: _contrast,
                  encoded: _encoded,
                  encoding: _encoding,
                  uploading: _uploading,
                  sentChunks: _sentChunks,
                  totalChunks: _totalChunks,
                  onSelectWallpaper: (id) {
                    setState(() {
                      _wallpaperId = id;
                      _pickedFileBytes = null;
                      _pickedImageName = null;
                    });
                    controller.updateDialConfig(id: 3, syncToWatch: false);
                    unawaited(_rebuildImagePreview());
                  },
                  onPickLocalPhoto: _pickLocalPhoto,
                  onFormatChanged: (fmt) {
                    setState(() => _format = fmt);
                    unawaited(_rebuildImagePreview());
                  },
                  onDitherChanged: (v) {
                    setState(() => _dither = v);
                    unawaited(_rebuildImagePreview());
                  },
                  onZoomChanged: (v) {
                    setState(() => _zoom = v);
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
                ),
                const SizedBox(height: 16),
                _FlashPartitionCard(scheme: scheme, controller: controller),
              ],
            );

            if (constraints.maxWidth >= 900) {
              return Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Expanded(flex: 5, child: preview),
                  const SizedBox(width: 16),
                  Expanded(flex: 7, child: rightColumn),
                ],
              );
            }
            return Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                preview,
                const SizedBox(height: 16),
                rightColumn,
              ],
            );
          },
        ),
      ],
    );
  }
}

class _DialStageCard extends StatelessWidget {
  const _DialStageCard({
    required this.spec,
    required this.controller,
    required this.photoImage,
    required this.encoded,
    required this.uploading,
    required this.sentChunks,
    required this.totalChunks,
    required this.onPushConfig,
    required this.onUploadPhoto,
  });

  final _DialSpec spec;
  final WatchController controller;
  final ui.Image? photoImage;
  final EncodedWatchImage? encoded;
  final bool uploading;
  final int sentChunks;
  final int totalChunks;
  final VoidCallback onPushConfig;
  final VoidCallback onUploadPhoto;

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

    return AppCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SectionHeader(
            title: spec.name,
            subtitle: spec.subtitle,
            trailing: StatusPill(
              label: spec.tag,
              color: spec.id == 3 ? _dialAmber : AppPalette.mint,
            ),
          ),
          const SizedBox(height: 18),
          Center(
            child: Container(
              width: 196,
              padding: const EdgeInsets.all(10),
              decoration: BoxDecoration(
                color: const Color(0xFF0A0D0C),
                borderRadius: BorderRadius.circular(28),
                border: Border.all(color: AppPalette.line, width: 1.5),
                boxShadow: [
                  BoxShadow(
                    color: acColor.withValues(alpha: 0.18),
                    blurRadius: 24,
                    offset: const Offset(0, 10),
                  ),
                ],
              ),
              child: AspectRatio(
                aspectRatio: 128 / 160,
                child: ClipRRect(
                  borderRadius: BorderRadius.circular(18),
                  child: Container(
                    color: bgColor,
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
              ),
            ),
          ),
          const SizedBox(height: 12),
          Center(
            child: Text(
              controller.mcuImgReady
                  ? '手表 0x3C00 已固化相册图 (${controller.mcuImgBytes} B)'
                  : '128 × 160 · 实机同款参数即时仿真',
              style: TextStyle(
                fontSize: 11.5,
                color: Theme.of(context).colorScheme.onSurfaceVariant,
              ),
            ),
          ),
          if (uploading && totalChunks > 0) ...[
            const SizedBox(height: 12),
            LinearProgressIndicator(value: sentChunks / totalChunks),
            const SizedBox(height: 4),
            Text(
              '正在分块写入 AT24C256：$sentChunks / $totalChunks 块',
              textAlign: TextAlign.center,
              style: const TextStyle(fontSize: 11),
            ),
          ],
          const SizedBox(height: 14),
          Wrap(
            spacing: 10,
            runSpacing: 10,
            children: [
              FilledButton.icon(
                onPressed: uploading ? null : onPushConfig,
                icon: const Icon(Icons.save_as_rounded, size: 18),
                label: Text(
                  controller.connected ? '下发配置并写入 AT24C256' : '保存本地（连表覆盖）',
                ),
              ),
              if (spec.id == 3)
                OutlinedButton.icon(
                  onPressed: uploading ? null : onUploadPhoto,
                  icon: const Icon(Icons.upload_file_rounded, size: 18),
                  label: Text(
                    encoded == null
                        ? '写入压缩相册底图'
                        : '写入压缩底图 (${(encoded!.bytes.length / 1024).toStringAsFixed(1)} KB)',
                  ),
                ),
            ],
          ),
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
    required this.format,
    required this.dither,
    required this.zoom,
    required this.brightness,
    required this.contrast,
    required this.encoded,
    required this.encoding,
    required this.uploading,
    required this.sentChunks,
    required this.totalChunks,
    required this.onSelectWallpaper,
    required this.onPickLocalPhoto,
    required this.onFormatChanged,
    required this.onDitherChanged,
    required this.onZoomChanged,
    required this.onBrightnessChanged,
    required this.onContrastChanged,
    required this.onUploadPhoto,
  });

  final String wallpaperId;
  final String? pickedImageName;
  final WatchExtImgFormat format;
  final bool dither;
  final double zoom;
  final double brightness;
  final double contrast;
  final EncodedWatchImage? encoded;
  final bool encoding;
  final bool uploading;
  final int sentChunks;
  final int totalChunks;
  final ValueChanged<String> onSelectWallpaper;
  final VoidCallback onPickLocalPhoto;
  final ValueChanged<WatchExtImgFormat> onFormatChanged;
  final ValueChanged<bool> onDitherChanged;
  final ValueChanged<double> onZoomChanged;
  final ValueChanged<double> onBrightnessChanged;
  final ValueChanged<double> onContrastChanged;
  final VoidCallback onUploadPhoto;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final bytes = encoded?.bytes.length ?? 0;
    final raw = watchDisplayWidth * watchDisplayHeight * 2; // 40,960 B
    final fits = encoded?.fitsFlash ?? true;

    return AppCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SectionHeader(
            title: '128×160 相册图片与压缩算法',
            subtitle: pickedImageName != null
                ? '当前图片：$pickedImageName'
                : '支持从本机相册/文件导入或直接选用预设艺术底图',
            trailing: encoding
                ? const StatusPill(label: '编码中…')
                : StatusPill(
                    label: encoded == null
                        ? '--'
                        : '${(bytes / 1024).toStringAsFixed(1)} KB (${encoded!.compressionRatio.toStringAsFixed(1)}×)',
                    color: fits ? AppPalette.mint : const Color(0xFFEF4444),
                  ),
          ),
          const SizedBox(height: 14),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              FilledButton.tonalIcon(
                onPressed: uploading ? null : onPickLocalPhoto,
                icon: const Icon(Icons.photo_library_outlined, size: 18),
                label: const Text('选择本地图片'),
              ),
              for (final wp in _wallpaperPresets)
                ChoiceChip(
                  label: Text(wp.$2),
                  selected: pickedImageName == null && wallpaperId == wp.$1,
                  onSelected: (_) => onSelectWallpaper(wp.$1),
                ),
            ],
          ),
          const SizedBox(height: 14),
          SegmentedButton<WatchExtImgFormat>(
            segments: const [
              ButtonSegment(
                value: WatchExtImgFormat.pal4Rle,
                label: Text('16色RLE'),
              ),
              ButtonSegment(
                value: WatchExtImgFormat.rgb565Rle,
                label: Text('真彩RLE'),
              ),
              ButtonSegment(
                value: WatchExtImgFormat.mono1bpp,
                label: Text('黑白1bpp'),
              ),
            ],
            selected: {format},
            showSelectedIcon: false,
            onSelectionChanged: (s) => onFormatChanged(s.first),
          ),
          if (format != WatchExtImgFormat.rgb565Rle)
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('Floyd-Steinberg 误差扩散抖动'),
              subtitle: const Text('显著消除 16 色 / 黑白渐变断层'),
              value: dither,
              onChanged: onDitherChanged,
            ),
          if (pickedImageName != null) ...[
            Row(
              children: [
                const Text('裁切缩放', style: TextStyle(fontSize: 12)),
                Expanded(
                  child: Slider(
                    value: zoom,
                    min: 1.0,
                    max: 2.5,
                    divisions: 15,
                    label: '${zoom.toStringAsFixed(1)}×',
                    onChanged: onZoomChanged,
                  ),
                ),
              ],
            ),
          ],
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
            ],
          ),
          Row(
            children: [
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
                      '压缩体积：$bytes / $watchExtImgMaxBytes B',
                      style: TextStyle(
                        fontSize: 12,
                        fontWeight: FontWeight.w700,
                        color: fits ? scheme.primary : const Color(0xFFEF4444),
                      ),
                    ),
                    Text(
                      '原始 RGB565：$raw B',
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
                    value: (bytes / watchExtImgMaxBytes).clamp(0.0, 1.0),
                    minHeight: 6,
                    color: fits ? scheme.primary : const Color(0xFFEF4444),
                  ),
                ),
                const SizedBox(height: 10),
                FilledButton.icon(
                  onPressed: (!fits || uploading) ? null : onUploadPhoto,
                  icon: const Icon(Icons.cloud_upload_outlined, size: 18),
                  label: Text(
                    uploading
                        ? '正在写入 ($sentChunks/$totalChunks)...'
                        : (fits ? '压缩并写入 AT24C256 设为表盘' : '超出 17KB 上限 · 请改用 PAL4-RLE'),
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
            range: '0x0300 – 0x7FFF · 图片块',
            title: '压缩表盘底图（与消息动态共享 32,000 B Pool）',
            size: imgB > 0 ? '$imgB B' : '未写入',
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
            color: scheme.outlineVariant,
            range: '最大可用连续块',
            title: '当前剩余动态 ROM 空间（空间不足自动淘汰最旧通知）',
            size: '${controller.extPoolFreeMax} B 空闲',
          ),
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
