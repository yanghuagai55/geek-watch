// AT32 手表助手 · GPL-3.0-or-later
//
// 交互式 3D 手表姿态实时视口（Windows & Android 共用纯 Flutter GPU Vertices 管线）：
// - 加载 `assets/models/at32_watch.glb` 真实 PCB/屏幕/IMU 装配体；
// - 根据 9 轴融合角 (Roll / Pitch / Heading) 按固件 Body->ENU 坐标系实时旋转；
// - 将真机抓屏或合成表盘直接投影到 3D `TFT_GLASS` 玻璃表面；
// - 支持手势拖拽 360° 轨道观察（含背面 HC-08 / MCU）、滚轮/按键缩放、KEY2 校准光环。

import 'dart:async';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/gestures.dart';
import 'package:material_ui/material_ui.dart';

import '../services/watch_glb_loader.dart';
import '../state/watch_controller.dart';
import '../theme/app_theme.dart';

/// 3D 视口展示尺寸与交互预设。
enum Watch3DMode {
  /// 首页紧凑卡片模式：适配窄竖屏与桌面 Hero 卡片。
  heroCompact,

  /// 运动页/弹窗大视图模式：带 ENU 罗盘地台、坐标系三轴、KEY2 校准指引环与缩放控制。
  stageLarge,
}

class Watch3DView extends StatefulWidget {
  const Watch3DView({
    super.key,
    required this.controller,
    this.mode = Watch3DMode.heroCompact,
    this.height = 176,
    this.width,
    this.showHudOverlay = true,
    this.showAxesTriad = true,
    this.interactive = true,
    this.onTapExpand,
  });

  final WatchController controller;
  final Watch3DMode mode;
  final double height;
  final double? width;
  final bool showHudOverlay;
  final bool showAxesTriad;
  final bool interactive;
  final VoidCallback? onTapExpand;

  @override
  State<Watch3DView> createState() => _Watch3DViewState();
}

class _Watch3DViewState extends State<Watch3DView>
    with SingleTickerProviderStateMixin {
  WatchGlbModel? _model;
  bool _loadFailed = false;

  late final AnimationController _ticker;
  DateTime _lastFrame = DateTime.now();
  double _demoTimeSec = 0;

  // 平滑后的手表实时姿态角（度）。
  double _smoothRoll = 0;
  double _smoothPitch = 0;
  double _smoothHeading = 0;
  bool _poseSeeded = false;

  // 相机轨道参数：默认从正南偏上约 40° 俯视平放在桌面朝北的手表。
  double _orbitYawDeg = 22.0;
  double _orbitPitchDeg = 46.0;
  double _zoom = 1.0;

  final _Watch3DRenderScratch _scratch = _Watch3DRenderScratch();

  @override
  void initState() {
    super.initState();
    if (widget.mode == Watch3DMode.stageLarge) {
      _orbitYawDeg = 26.0;
      _orbitPitchDeg = 44.0;
      widget.controller.acquireSensorBoost();
    }
    _ticker = AnimationController(
      vsync: this,
      duration: const Duration(days: 1),
    )..addListener(_onTick);
    widget.controller.addListener(_onControllerChanged);
    _syncTickerState();
    _loadModel();
  }

  void _onControllerChanged() {
    if (!mounted) return;
    _syncTickerState();
    if (!_ticker.isAnimating) {
      setState(() {});
    }
  }

  Future<void> _loadModel() async {
    try {
      final loaded = await WatchGlbModel.loadDefault();
      if (!mounted) return;
      setState(() => _model = loaded);
    } catch (_) {
      if (!mounted) return;
      setState(() => _loadFailed = true);
    }
  }

  @override
  void didUpdateWidget(covariant Watch3DView oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.controller != widget.controller) {
      oldWidget.controller.removeListener(_onControllerChanged);
      widget.controller.addListener(_onControllerChanged);
    }
    if (oldWidget.mode != widget.mode) {
      if (widget.mode == Watch3DMode.stageLarge) {
        widget.controller.acquireSensorBoost();
      } else {
        widget.controller.releaseSensorBoost();
      }
    }
    _syncTickerState();
  }

  bool _shouldAmbientAnimate(WatchController c) {
    return c.animations &&
        c.ambientTickEnabled &&
        !c.liveSensor &&
        (c.demoStreaming ||
            c.magCalPhase == MagCalPhase.calibrating ||
            c.magCalPhase == MagCalPhase.readyToFinish);
  }

  void _syncTickerState() {
    final c = widget.controller;
    if (!c.animations || !c.ambientTickEnabled) {
      _ticker.stop();
      final (tr, tp, th) = _targetEulerDegrees(c);
      _smoothRoll = tr;
      _smoothPitch = tp;
      _smoothHeading = th;
      _poseSeeded = true;
      return;
    }
    if (_shouldAmbientAnimate(c)) {
      if (!_ticker.isAnimating) {
        _lastFrame = DateTime.now();
        unawaited(_ticker.repeat());
      }
      return;
    }
    final (tr, tp, th) = _targetEulerDegrees(c);
    final dHead = ((th - _smoothHeading + 540.0) % 360.0) - 180.0;
    final settled =
        _poseSeeded &&
        (tr - _smoothRoll).abs() + (tp - _smoothPitch).abs() + dHead.abs() <
            0.05;
    if (!settled && !_ticker.isAnimating) {
      _lastFrame = DateTime.now();
      unawaited(_ticker.repeat());
    }
  }

  void _onTick() {
    final now = DateTime.now();
    final dt = (now.difference(_lastFrame).inMicroseconds / 1e6).clamp(
      0.001,
      0.05,
    );
    _lastFrame = now;
    _demoTimeSec += dt;

    final c = widget.controller;
    final (targetRoll, targetPitch, targetHeading) = _targetEulerDegrees(c);
    var settled = false;
    if (!_poseSeeded) {
      _smoothRoll = targetRoll;
      _smoothPitch = targetPitch;
      _smoothHeading = targetHeading;
      _poseSeeded = true;
      settled = !_shouldAmbientAnimate(c);
    } else {
      final alpha = 1.0 - math.exp(-dt * 12.0);
      _smoothRoll += (targetRoll - _smoothRoll) * alpha;
      _smoothPitch += (targetPitch - _smoothPitch) * alpha;
      final dHead = ((targetHeading - _smoothHeading + 540.0) % 360.0) - 180.0;
      _smoothHeading = (_smoothHeading + dHead * alpha + 360.0) % 360.0;
      if (!_shouldAmbientAnimate(c) &&
          (targetRoll - _smoothRoll).abs() +
                  (targetPitch - _smoothPitch).abs() +
                  dHead.abs() <
              0.05) {
        _smoothRoll = targetRoll;
        _smoothPitch = targetPitch;
        _smoothHeading = targetHeading;
        settled = true;
      }
    }
    if (settled) {
      _ticker.stop();
    }
    if (mounted) setState(() {});
  }

  (double, double, double) _targetEulerDegrees(WatchController c) {
    if (c.liveSensor) {
      return (c.rollDeg, c.pitchDeg, c.headingDeg);
    }
    if (c.ambientTickEnabled &&
        c.animations &&
        (c.magCalPhase == MagCalPhase.calibrating ||
            c.magCalPhase == MagCalPhase.readyToFinish)) {
      // 校准引导模式未连实机时，温和演示 8 字旋转轨迹。
      return (
        28.0 * math.sin(_demoTimeSec * 1.4),
        24.0 * math.cos(_demoTimeSec * 1.1),
        (_demoTimeSec * 48.0) % 360.0,
      );
    }
    if (c.ambientTickEnabled && c.demoStreaming && c.animations) {
      return (
        9.0 * math.sin(_demoTimeSec * 0.85),
        7.5 * math.cos(_demoTimeSec * 0.65),
        (16.0 * math.sin(_demoTimeSec * 0.35) + 360.0) % 360.0,
      );
    }
    return (0.0, 0.0, 0.0);
  }

  void _resetCamera() {
    setState(() {
      _orbitYawDeg = widget.mode == Watch3DMode.stageLarge ? 26.0 : 22.0;
      _orbitPitchDeg = widget.mode == Watch3DMode.stageLarge ? 44.0 : 46.0;
      _zoom = 1.0;
    });
  }

  @override
  void dispose() {
    widget.controller.removeListener(_onControllerChanged);
    if (widget.mode == Watch3DMode.stageLarge) {
      widget.controller.releaseSensorBoost();
    }
    _ticker.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final c = widget.controller;
    final colors = context.appColors;
    final dark = Theme.of(context).brightness == Brightness.dark;
    final isCalibrating =
        c.magCalPhase == MagCalPhase.calibrating ||
        c.magCalPhase == MagCalPhase.readyToFinish;
    final (dispRoll, dispPitch, dispHeading) = _poseSeeded
        ? (_smoothRoll, _smoothPitch, _smoothHeading)
        : _targetEulerDegrees(c);

    Widget canvasStage = ClipRRect(
      borderRadius: BorderRadius.circular(
        widget.mode == Watch3DMode.stageLarge ? 20 : 18,
      ),
      child: DecoratedBox(
        decoration: BoxDecoration(
          gradient: RadialGradient(
            center: const Alignment(0, -0.05),
            radius: 0.92,
            colors: dark
                ? <Color>[
                    (isCalibrating ? const Color(0xFF1B3A32) : AppPalette.mintDeep)
                        .withValues(alpha: 0.34),
                    const Color(0xFF0E1615),
                    const Color(0xFF0A0F10),
                  ]
                : <Color>[
                    (isCalibrating ? colors.warning : AppPalette.mintSoft)
                        .withValues(alpha: isCalibrating ? 0.16 : 0.28),
                    const Color(0xFFF3F5EA),
                    const Color(0xFFE7ECDF),
                  ],
          ),
          border: Border.all(
            color: isCalibrating
                ? (c.magCalPhase == MagCalPhase.readyToFinish
                      ? colors.accent
                      : colors.warning)
                : Theme.of(context).colorScheme.outlineVariant,
          ),
          borderRadius: BorderRadius.circular(
            widget.mode == Watch3DMode.stageLarge ? 20 : 18,
          ),
        ),
        child: Stack(
          fit: StackFit.expand,
          children: [
            CustomPaint(
              painter: _Watch3DPainter(
                model: _model,
                loadFailed: _loadFailed,
                dark: dark,
                compact: widget.mode == Watch3DMode.heroCompact,
                rollDeg: dispRoll,
                pitchDeg: dispPitch,
                headingDeg: dispHeading,
                orbitYawDeg: _orbitYawDeg,
                orbitPitchDeg: _orbitPitchDeg,
                zoom: _zoom,
                calPhase: c.magCalPhase,
                pulsePhase: _demoTimeSec,
                liveSensor: c.liveSensor,
                fusedAxes: c.fusedAxes,
                deviceScreen: c.deviceScreen,
                timeText: _formatClock(c.now, c.use24Hour),
                showAxesTriad:
                    widget.showAxesTriad &&
                    widget.mode == Watch3DMode.stageLarge,
                scratch: _scratch,
              ),
            ),
            if (widget.showHudOverlay)
              _buildHudOverlay(
                context,
                c,
                dark,
                dispRoll,
                dispPitch,
                dispHeading,
              ),
          ],
        ),
      ),
    );

    if (widget.interactive) {
      canvasStage = Listener(
        onPointerSignal: (event) {
          if (event is PointerScrollEvent) {
            setState(() {
              _zoom = (_zoom - event.scrollDelta.dy * 0.0012).clamp(0.75, 2.2);
            });
          }
        },
        child: GestureDetector(
          behavior: HitTestBehavior.opaque,
          onPanUpdate: (details) {
            setState(() {
              _orbitYawDeg = (_orbitYawDeg + details.delta.dx * 0.55) % 360.0;
              _orbitPitchDeg = (_orbitPitchDeg - details.delta.dy * 0.55).clamp(
                -88.0,
                88.0,
              );
            });
          },
          onDoubleTap: _resetCamera,
          child: canvasStage,
        ),
      );
    }

    return SizedBox(
      width: widget.width,
      height: widget.height,
      child: canvasStage,
    );
  }

  Widget _buildHudOverlay(
    BuildContext context,
    WatchController c,
    bool dark,
    double roll,
    double pitch,
    double heading,
  ) {
    final colors = context.appColors;
    final scheme = Theme.of(context).colorScheme;
    final badgeBg = (dark ? const Color(0xFF0A0F10) : const Color(0xFFFCFCF6))
        .withValues(alpha: 0.84);
    final subtleBorder = scheme.outlineVariant;
    final textPrimary = colors.heroTextPrimary;
    final textSecondary = colors.heroTextSecondary;
    final accent = colors.accent;

    if (widget.mode == Watch3DMode.heroCompact) {
      return Padding(
        padding: const EdgeInsets.all(7),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                _HudPill(
                  bg: badgeBg,
                  border: subtleBorder,
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Container(
                        width: 6,
                        height: 6,
                        decoration: BoxDecoration(
                          shape: BoxShape.circle,
                          color: c.liveSensor ? accent : textSecondary,
                        ),
                      ),
                      const SizedBox(width: 4),
                      Text(
                        c.dofBadge,
                        style: TextStyle(
                          fontSize: 9.5,
                          fontWeight: FontWeight.w600,
                          color: textPrimary,
                        ),
                      ),
                    ],
                  ),
                ),
                if (widget.onTapExpand != null)
                  GestureDetector(
                    onTap: widget.onTapExpand,
                    child: Tooltip(
                      message: '打开运动页 3D 大视图',
                      child: _HudPill(
                        bg: badgeBg,
                        border: subtleBorder,
                        child: Icon(
                          Icons.open_in_full_rounded,
                          size: 12,
                          color: accent,
                        ),
                      ),
                    ),
                  ),
              ],
            ),
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Flexible(
                  child: _HudPill(
                    bg: badgeBg,
                    border: subtleBorder,
                    child: Text(
                      'H ${heading.round()}° · P ${pitch.round()}°',
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                        fontSize: 9.5,
                        fontWeight: FontWeight.w600,
                        color: textPrimary,
                      ),
                    ),
                  ),
                ),
              ],
            ),
          ],
        ),
      );
    }

    // stageLarge HUD
    return Padding(
      padding: const EdgeInsets.all(12),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Row(
            children: [
              Expanded(
                child: Align(
                  alignment: Alignment.centerLeft,
                  child: _HudPill(
                    bg: badgeBg,
                    border: subtleBorder,
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Container(
                          width: 7,
                          height: 7,
                          decoration: BoxDecoration(
                            shape: BoxShape.circle,
                            color: c.liveSensor
                                ? accent
                                : (c.demoStreaming
                                      ? colors.warning
                                      : textSecondary),
                          ),
                        ),
                        const SizedBox(width: 6),
                        Flexible(
                          child: Text(
                            c.liveSensor
                                ? '${c.dofBadge} · 实机融合'
                                : '${c.dofBadge} · 拖拽旋转',
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: TextStyle(
                              fontSize: 11,
                              fontWeight: FontWeight.w600,
                              color: textPrimary,
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              ),
              const SizedBox(width: 8),
              _HudIconBtn(
                bg: badgeBg,
                border: subtleBorder,
                icon: Icons.zoom_in_rounded,
                color: textPrimary,
                tooltip: '放大模型',
                onTap: () =>
                    setState(() => _zoom = (_zoom + 0.18).clamp(0.75, 2.2)),
              ),
              const SizedBox(width: 6),
              _HudIconBtn(
                bg: badgeBg,
                border: subtleBorder,
                icon: Icons.zoom_out_rounded,
                color: textPrimary,
                tooltip: '缩小模型',
                onTap: () =>
                    setState(() => _zoom = (_zoom - 0.18).clamp(0.75, 2.2)),
              ),
              const SizedBox(width: 6),
              _HudIconBtn(
                bg: badgeBg,
                border: subtleBorder,
                icon: Icons.center_focus_strong_rounded,
                color: accent,
                tooltip: '回正视角',
                onTap: _resetCamera,
              ),
            ],
          ),
          Row(
            children: [
              Expanded(
                child: Wrap(
                  spacing: 6,
                  runSpacing: 6,
                  children: [
                    _AngleChip(
                      label: 'R',
                      value: '${roll.toStringAsFixed(0)}°',
                      bg: badgeBg,
                      border: subtleBorder,
                      accent: accent,
                      fg: textPrimary,
                    ),
                    _AngleChip(
                      label: 'P',
                      value: '${pitch.toStringAsFixed(0)}°',
                      bg: badgeBg,
                      border: subtleBorder,
                      accent: accent,
                      fg: textPrimary,
                    ),
                    _AngleChip(
                      label: 'H',
                      value: '${heading.toStringAsFixed(0)}°',
                      bg: badgeBg,
                      border: subtleBorder,
                      accent: accent,
                      fg: textPrimary,
                    ),
                  ],
                ),
              ),
              if (widget.onTapExpand != null)
                GestureDetector(
                  onTap: widget.onTapExpand,
                  child: _HudPill(
                    bg: badgeBg,
                    border: subtleBorder,
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Icon(Icons.fullscreen_rounded, size: 15, color: accent),
                        const SizedBox(width: 4),
                        Text(
                          '全屏检视',
                          style: TextStyle(
                            fontSize: 11,
                            fontWeight: FontWeight.w600,
                            color: textPrimary,
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
            ],
          ),
        ],
      ),
    );
  }

  static String _formatClock(DateTime dt, bool use24) {
    final h = use24 ? dt.hour : (dt.hour % 12 == 0 ? 12 : dt.hour % 12);
    return '${h.toString().padLeft(2, '0')}:${dt.minute.toString().padLeft(2, '0')}';
  }
}

class _HudPill extends StatelessWidget {
  const _HudPill({required this.bg, required this.border, required this.child});
  final Color bg;
  final Color border;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 3.5),
      decoration: BoxDecoration(
        color: bg,
        borderRadius: BorderRadius.circular(999),
        border: Border.all(color: border),
      ),
      child: child,
    );
  }
}

class _HudIconBtn extends StatelessWidget {
  const _HudIconBtn({
    required this.bg,
    required this.border,
    required this.icon,
    required this.color,
    required this.tooltip,
    required this.onTap,
  });
  final Color bg;
  final Color border;
  final IconData icon;
  final Color color;
  final String tooltip;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: tooltip,
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(999),
        child: Container(
          padding: const EdgeInsets.all(6),
          decoration: BoxDecoration(
            color: bg,
            borderRadius: BorderRadius.circular(999),
            border: Border.all(color: border),
          ),
          child: Icon(icon, size: 15, color: color),
        ),
      ),
    );
  }
}

class _AngleChip extends StatelessWidget {
  const _AngleChip({
    required this.label,
    required this.value,
    required this.bg,
    required this.border,
    required this.accent,
    required this.fg,
  });
  final String label;
  final String value;
  final Color bg;
  final Color border;
  final Color accent;
  final Color fg;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      decoration: BoxDecoration(
        color: bg,
        borderRadius: BorderRadius.circular(999),
        border: Border.all(color: border),
      ),
      child: RichText(
        text: TextSpan(
          children: [
            TextSpan(
              text: '$label ',
              style: TextStyle(
                fontSize: 10.5,
                fontWeight: FontWeight.w700,
                color: accent,
              ),
            ),
            TextSpan(
              text: value,
              style: TextStyle(
                fontSize: 11,
                fontWeight: FontWeight.w600,
                color: fg,
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// 预分配的高速可复用三角面排序与顶点缓冲，避免 60FPS GC 抖动。
class _Watch3DRenderScratch {
  static const int bucketCount = 1024;
  Float32List transformed = Float32List(0);
  Int32List bucketHead = Int32List(bucketCount);
  Int32List nextTri = Int32List(0);
  Int32List triIndexList = Int32List(0);
  Int32List triColorList = Int32List(0);
  Float32List drawPos = Float32List(0);
  Int32List drawCol = Int32List(0);

  void ensureCapacity(int vertexCount, int maxTris) {
    if (transformed.length < vertexCount * 3) {
      transformed = Float32List(vertexCount * 3);
    }
    if (nextTri.length < maxTris) {
      nextTri = Int32List(maxTris);
      triIndexList = Int32List(maxTris * 3);
      triColorList = Int32List(maxTris);
    }
    if (drawPos.length < maxTris * 6) {
      drawPos = Float32List(maxTris * 6);
      drawCol = Int32List(maxTris * 3);
    }
  }
}

class _Watch3DPainter extends CustomPainter {
  const _Watch3DPainter({
    required this.model,
    required this.loadFailed,
    required this.dark,
    required this.compact,
    required this.rollDeg,
    required this.pitchDeg,
    required this.headingDeg,
    required this.orbitYawDeg,
    required this.orbitPitchDeg,
    required this.zoom,
    required this.calPhase,
    required this.pulsePhase,
    required this.liveSensor,
    required this.fusedAxes,
    required this.deviceScreen,
    required this.timeText,
    required this.showAxesTriad,
    required this.scratch,
  });

  final WatchGlbModel? model;
  final bool loadFailed;
  final bool dark;
  final bool compact;
  final double rollDeg;
  final double pitchDeg;
  final double headingDeg;
  final double orbitYawDeg;
  final double orbitPitchDeg;
  final double zoom;
  final MagCalPhase calPhase;
  final double pulsePhase;
  final bool liveSensor;
  final int fusedAxes;
  final ui.Image? deviceScreen;
  final String timeText;
  final bool showAxesTriad;
  final _Watch3DRenderScratch scratch;

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width * 0.5;
    final cy = size.height * 0.5;
    final minDim = math.min(size.width, size.height);
    final radiusRef = model?.radius ?? 0.056;
    final baseScale = (minDim * (compact ? 0.44 : 0.39) * zoom) / radiusRef;

    // 1. 构造相机视图旋转矩阵 V（仅世界轨道相机 -> 相机空间），用于绘制固定大地罗盘环。
    final vMat = _buildViewMatrix(orbitYawDeg, orbitPitchDeg);

    // 绘制 ENU 大地罗盘参考圆盘与 N/E/S/W 方位刻线。
    _drawEnuCompassRing(canvas, cx, cy, baseScale, vMat);

    // 若模型仍在加载或失败，绘制优雅的线框占位而不闪白或崩溃。
    final m = model;
    if (m == null) {
      _drawPlaceholderCube(canvas, cx, cy, baseScale, vMat, loadFailed);
      return;
    }

    // 2. 构造完整姿态矩阵 M = V * R_z(-heading) * R_y(roll) * R_x(pitch)（Body -> Camera）。
    final bodyToWorld = _buildBodyToEnuMatrix(rollDeg, pitchDeg, headingDeg);
    final fullMat = _mul3(vMat, bodyToWorld);

    // 3. 当正在进行 KEY2 校准时，在表体周围绘制 3D 8 字/球面引导光环。
    if (calPhase == MagCalPhase.calibrating ||
        calPhase == MagCalPhase.readyToFinish) {
      _drawCalibrationRings(canvas, cx, cy, baseScale, vMat);
    }

    // 4. 变换所有 3D 顶点，并记录深度范围用于 O(N) 桶排序。
    var maxTris = 0;
    for (final p in m.parts) {
      maxTris += p.indices.length ~/ 3;
    }
    scratch.ensureCapacity(m.vertexCount, maxTris);

    final vb = m.verticesBody;
    final tv = scratch.transformed;
    final m00 = fullMat[0], m01 = fullMat[1], m02 = fullMat[2];
    final m10 = fullMat[3], m11 = fullMat[4], m12 = fullMat[5];
    final m20 = fullMat[6], m21 = fullMat[7], m22 = fullMat[8];

    final vc = m.vertexCount;
    for (var i = 0; i < vc; i++) {
      final si = i * 3;
      final bx = vb[si];
      final by = vb[si + 1];
      final bz = vb[si + 2];
      tv[si] = m00 * bx + m01 * by + m02 * bz;
      tv[si + 1] = m10 * bx + m11 * by + m12 * bz;
      tv[si + 2] = m20 * bx + m21 * by + m22 * bz;
    }

    // 光照向量（已归一化）：主光来自左上方前侧，补光来自底部边缘照亮 HC-08。
    const lx = -0.36, ly = 0.56, lz = 0.745;
    const rx = 0.45, ry = -0.58, rz = -0.68;
    const hx = -0.19, hy = 0.30, hz = 0.935; // 半角向量近似高光

    const minDepth = -0.10;
    const maxDepth = 0.10;
    const invRange = 1.0 / (maxDepth - minDepth);
    final buckets = scratch.bucketHead;
    buckets.fillRange(0, _Watch3DRenderScratch.bucketCount, -1);

    final nextTri = scratch.nextTri;
    final triIdx = scratch.triIndexList;
    final triCol = scratch.triColorList;
    var acceptedTris = 0;

    for (final part in m.parts) {
      final idx = part.indices;
      final baseRgb = part.colorRgb;
      final br = (baseRgb >> 16) & 0xFF;
      final bg = (baseRgb >> 8) & 0xFF;
      final bb = baseRgb & 0xFF;
      final specGain = part.metallic ? 78.0 : (part.isGlass ? 54.0 : 30.0);
      final bias = part.layerBias;
      final doubleSided = part.doubleSided;
      final isGlass = part.isGlass;

      for (var k = 0; k < idx.length; k += 3) {
        final i0 = idx[k];
        final i1 = idx[k + 1];
        final i2 = idx[k + 2];
        final o0 = i0 * 3;
        final o1 = i1 * 3;
        final o2 = i2 * 3;

        final ax = tv[o0], ay = tv[o0 + 1], az = tv[o0 + 2];
        final bx = tv[o1], by = tv[o1 + 1], bz = tv[o1 + 2];
        final cx3 = tv[o2], cy3 = tv[o2 + 1], cz = tv[o2 + 2];

        final ux = bx - ax, uy = by - ay, uz = bz - az;
        final vx = cx3 - ax, vy = cy3 - ay, vz = cz - az;
        var nx = uy * vz - uz * vy;
        var ny = uz * vx - ux * vz;
        var nz = ux * vy - uy * vx;
        final len2 = nx * nx + ny * ny + nz * nz;
        if (len2 < 1e-16) continue;
        if (!doubleSided && nz <= 0) continue;

        final invLen = 1.0 / math.sqrt(len2);
        nx *= invLen;
        ny *= invLen;
        nz *= invLen;
        if (nz < 0) {
          nx = -nx;
          ny = -ny;
          nz = -nz;
        }

        final diff1 = math.max(0.0, nx * lx + ny * ly + nz * lz);
        final diff2 = math.max(0.0, nx * rx + ny * ry + nz * rz);
        final ndh = math.max(0.0, nx * hx + ny * hy + nz * hz);
        final spec = math.pow(ndh, 18) * specGain;

        int r, g, b;
        if (isGlass) {
          // 屏幕玻璃底层渲染深曜黑底板，稍后在上方叠绘透视实时表盘/截图。
          final fres = (1.0 - nz).clamp(0.0, 1.0);
          r = (14 + diff1 * 14 + spec * 0.6 + fres * 12).round().clamp(0, 255);
          g = (26 + diff1 * 22 + spec * 0.7 + fres * 26).round().clamp(0, 255);
          b = (24 + diff1 * 20 + spec * 0.6 + fres * 22).round().clamp(0, 255);
        } else {
          final light = 0.34 + 0.66 * diff1 + 0.22 * diff2;
          r = (br * light + spec).round().clamp(0, 255);
          g = (bg * light + spec * 1.04).round().clamp(0, 255);
          b = (bb * light + spec).round().clamp(0, 255);
        }

        final zMid = (az + bz + cz) / 3.0 + bias;
        var bIdx = ((zMid - minDepth) * invRange * (_Watch3DRenderScratch.bucketCount - 1))
            .round();
        if (bIdx < 0) bIdx = 0;
        if (bIdx >= _Watch3DRenderScratch.bucketCount) {
          bIdx = _Watch3DRenderScratch.bucketCount - 1;
        }

        final slot = acceptedTris++;
        final t3 = slot * 3;
        triIdx[t3] = i0;
        triIdx[t3 + 1] = i1;
        triIdx[t3 + 2] = i2;
        triCol[slot] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        nextTri[slot] = buckets[bIdx];
        buckets[bIdx] = slot;
      }
    }

    if (acceptedTris > 0) {
      final posOut = scratch.drawPos;
      final colOut = scratch.drawCol;
      var cursor = 0;
      const perspK = 2.4;

      for (var bIdx = 0; bIdx < _Watch3DRenderScratch.bucketCount; bIdx++) {
        var slot = buckets[bIdx];
        while (slot != -1) {
          final t3 = slot * 3;
          final cArgb = triCol[slot];
          final v0 = triIdx[t3] * 3;
          final v1 = triIdx[t3 + 1] * 3;
          final v2 = triIdx[t3 + 2] * 3;

          final k0 = baseScale / (1.0 - tv[v0 + 2] * perspK);
          final k1 = baseScale / (1.0 - tv[v1 + 2] * perspK);
          final k2 = baseScale / (1.0 - tv[v2 + 2] * perspK);

          final pOff = cursor * 6;
          posOut[pOff] = cx + tv[v0] * k0;
          posOut[pOff + 1] = cy - tv[v0 + 1] * k0;
          posOut[pOff + 2] = cx + tv[v1] * k1;
          posOut[pOff + 3] = cy - tv[v1 + 1] * k1;
          posOut[pOff + 4] = cx + tv[v2] * k2;
          posOut[pOff + 5] = cy - tv[v2 + 1] * k2;

          final cOff = cursor * 3;
          colOut[cOff] = cArgb;
          colOut[cOff + 1] = cArgb;
          colOut[cOff + 2] = cArgb;

          cursor++;
          slot = nextTri[slot];
        }
      }

      final vertices = ui.Vertices.raw(
        ui.VertexMode.triangles,
        Float32List.sublistView(posOut, 0, cursor * 6),
        colors: Int32List.sublistView(colOut, 0, cursor * 3),
      );
      canvas.drawVertices(vertices, ui.BlendMode.srcOver, Paint());
    }

    // 5. 若 `TFT_GLASS` 朝向摄像机，将实时手表界面/数字时钟透视绘制在 3D 玻璃上！
    _drawProjectedGlassScreen(canvas, cx, cy, baseScale, fullMat, m);

    // 6. 大视图可选绘制手表本体坐标系 (+X_b/+Y_b/+Z_b) 三轴箭头。
    if (showAxesTriad) {
      _drawBodyAxes(canvas, cx, cy, baseScale, fullMat);
    }
  }

  void _drawEnuCompassRing(
    Canvas canvas,
    double cx,
    double cy,
    double scale,
    List<double> vMat,
  ) {
    const ringR = 0.064;
    const ringZ = -0.014;
    const perspK = 2.4;

    Offset proj(double ex, double ey, double ez) {
      final vx = vMat[0] * ex + vMat[1] * ey + vMat[2] * ez;
      final vy = vMat[3] * ex + vMat[4] * ey + vMat[5] * ez;
      final vz = vMat[6] * ex + vMat[7] * ey + vMat[8] * ez;
      final k = scale / (1.0 - vz * perspK);
      return Offset(cx + vx * k, cy - vy * k);
    }

    final warnColor = dark ? const Color(0xFFF5B759) : const Color(0xFFB4690E);
    final ringColor =
        (calPhase == MagCalPhase.calibrating
                ? warnColor
                : (dark ? AppPalette.mint : AppPalette.mintDeep))
            .withValues(alpha: dark ? 0.30 : 0.25);
    final ringPaint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = compact ? 1.1 : 1.4
      ..color = ringColor;

    final path = Path();
    const steps = 48;
    for (var i = 0; i <= steps; i++) {
      final a = (i / steps) * math.pi * 2.0;
      final pt = proj(ringR * math.sin(a), ringR * math.cos(a), ringZ);
      if (i == 0) {
        path.moveTo(pt.dx, pt.dy);
      } else {
        path.lineTo(pt.dx, pt.dy);
      }
    }
    canvas.drawPath(path, ringPaint);

    // 方位刻度 + N/E/S/W 字标
    final cardinals = <(double deg, String ch, bool isNorth)>[
      (0, 'N', true),
      (90, 'E', false),
      (180, 'S', false),
      (270, 'W', false),
    ];
    for (var deg = 0; deg < 360; deg += 30) {
      final rad = deg * math.pi / 180.0;
      final inner = deg % 90 == 0 ? 0.90 : 0.95;
      final p0 = proj(
        ringR * inner * math.sin(rad),
        ringR * inner * math.cos(rad),
        ringZ,
      );
      final p1 = proj(ringR * math.sin(rad), ringR * math.cos(rad), ringZ);
      canvas.drawLine(
        p0,
        p1,
        Paint()
          ..strokeWidth = deg == 0 ? 2.0 : 1.0
          ..color = (deg == 0
              ? (dark ? AppPalette.mint : AppPalette.mintDeep)
              : ringColor),
      );
    }

    if (!compact) {
      for (final (deg, ch, isN) in cardinals) {
        final rad = deg * math.pi / 180.0;
        final pos = proj(
          ringR * 1.12 * math.sin(rad),
          ringR * 1.12 * math.cos(rad),
          ringZ,
        );
        final tp = TextPainter(
          text: TextSpan(
            text: ch,
            style: TextStyle(
              fontSize: isN ? 11.5 : 10,
              fontWeight: FontWeight.w700,
              color: isN
                  ? (dark ? AppPalette.mint : AppPalette.mintDeep)
                  : (dark ? AppPalette.muted : const Color(0xFF54635C)),
            ),
          ),
          textDirection: TextDirection.ltr,
        )..layout();
        tp.paint(canvas, pos - Offset(tp.width * 0.5, tp.height * 0.5));
      }
    }
  }

  void _drawCalibrationRings(
    Canvas canvas,
    double cx,
    double cy,
    double scale,
    List<double> vMat,
  ) {
    final ready = calPhase == MagCalPhase.readyToFinish;
    final pulse = 0.55 + 0.45 * math.sin(pulsePhase * 4.2);
    final accentColor = dark ? AppPalette.mint : AppPalette.mintDeep;
    final warnColor = dark ? const Color(0xFFF5B759) : const Color(0xFFB4690E);
    final ringColor = (ready ? accentColor : warnColor)
        .withValues(alpha: 0.35 + 0.35 * pulse);
    const radius = 0.058;
    const perspK = 2.4;

    Offset proj(double ex, double ey, double ez) {
      final vx = vMat[0] * ex + vMat[1] * ey + vMat[2] * ez;
      final vy = vMat[3] * ex + vMat[4] * ey + vMat[5] * ez;
      final vz = vMat[6] * ex + vMat[7] * ey + vMat[8] * ez;
      final k = scale / (1.0 - vz * perspK);
      return Offset(cx + vx * k, cy - vy * k);
    }

    final paint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.5
      ..color = ringColor;

    // 两个正交大圆提示用户需绕水平+俯仰做全空间 360° 采集
    for (final tiltY in const [true, false]) {
      final path = Path();
      for (var i = 0; i <= 40; i++) {
        final a = (i / 40.0) * math.pi * 2.0 + pulsePhase * 0.4;
        final pt = tiltY
            ? proj(radius * math.cos(a), 0, radius * math.sin(a))
            : proj(0, radius * math.cos(a), radius * math.sin(a));
        if (i == 0) {
          path.moveTo(pt.dx, pt.dy);
        } else {
          path.lineTo(pt.dx, pt.dy);
        }
      }
      canvas.drawPath(path, paint);
    }
  }

  void _drawProjectedGlassScreen(
    Canvas canvas,
    double cx,
    double cy,
    double scale,
    List<double> fullMat,
    WatchGlbModel m,
  ) {
    // 屏幕外法线 Body +Z = (0, 0, 1)，经 fullMat 变换后得相机空间法线 Z 分量 m[8]。
    final screenFaceZ = fullMat[8];
    if (screenFaceZ <= 0.08) return;

    final b = m.glassBoundsBody;
    final gx0 = b[0] + 0.0008;
    final gx1 = b[1] - 0.0008;
    final gy0 = b[2] + 0.0008;
    final gy1 = b[3] - 0.0008;
    final gz = b[4] + 0.00035;
    const perspK = 2.4;

    Offset projBody(double bx, double by, double bz) {
      final vx = fullMat[0] * bx + fullMat[1] * by + fullMat[2] * bz;
      final vy = fullMat[3] * bx + fullMat[4] * by + fullMat[5] * bz;
      final vz = fullMat[6] * bx + fullMat[7] * by + fullMat[8] * bz;
      final k = scale / (1.0 - vz * perspK);
      return Offset(cx + vx * k, cy - vy * k);
    }

    final pTL = projBody(gx0, gy1, gz);
    final pTR = projBody(gx1, gy1, gz);
    final pBR = projBody(gx1, gy0, gz);
    final pBL = projBody(gx0, gy0, gz);

    final clipPath = Path()..addPolygon([pTL, pTR, pBR, pBL], true);

    canvas.save();
    canvas.clipPath(clipPath);

    const w = 128.0;
    const h = 160.0;
    final u = pTR - pTL;
    final v = pBL - pTL;
    final xform = Float64List.fromList([
      u.dx / w,
      u.dy / w,
      0,
      0,
      v.dx / h,
      v.dy / h,
      0,
      0,
      0,
      0,
      1,
      0,
      pTL.dx,
      pTL.dy,
      0,
      1,
    ]);
    canvas.transform(xform);

    // 绘制真实的手表 128×160 截屏，若暂无截屏则合成动态 AT32 炫酷数字表盘。
    final snap = deviceScreen;
    if (snap != null) {
      paintImage(
        canvas: canvas,
        rect: const Rect.fromLTWH(0, 0, w, h),
        image: snap,
        fit: BoxFit.cover,
        filterQuality: FilterQuality.medium,
      );
    } else {
      canvas.drawRect(
        const Rect.fromLTWH(0, 0, w, h),
        Paint()..color = const Color(0xFF0A1110),
      );
      // 合成微缩 9 轴人工地平线 + 数字时间
      final pitchShift = (-pitchDeg * 0.45).clamp(-22.0, 22.0);
      final rollRad = rollDeg * math.pi / 180.0;
      canvas.save();
      canvas.translate(w * 0.5, 92 + pitchShift);
      canvas.rotate(rollRad);
      canvas.drawRect(
        const Rect.fromLTWH(-90, 0, 180, 90),
        Paint()..color = const Color(0xFF132D25),
      );
      canvas.drawLine(
        const Offset(-90, 0),
        const Offset(90, 0),
        Paint()
          ..strokeWidth = 1.6
          ..color = AppPalette.mint,
      );
      canvas.restore();

      final tpTime = TextPainter(
        text: TextSpan(
          text: timeText,
          style: const TextStyle(
            fontSize: 28,
            fontWeight: FontWeight.w700,
            color: AppPalette.warmWhite,
            letterSpacing: -0.5,
          ),
        ),
        textDirection: TextDirection.ltr,
      )..layout();
      tpTime.paint(canvas, Offset((w - tpTime.width) * 0.5, 22));

      final sub = liveSensor
          ? '${fusedAxes}DOF · ${headingDeg.round()}°'
          : 'AT32 IMU9';
      final tpSub = TextPainter(
        text: TextSpan(
          text: sub,
          style: const TextStyle(
            fontSize: 10,
            fontWeight: FontWeight.w600,
            color: AppPalette.mint,
          ),
        ),
        textDirection: TextDirection.ltr,
      )..layout();
      tpSub.paint(canvas, Offset((w - tpSub.width) * 0.5, 134));
    }

    // 玻璃斜向高光镀膜反射条
    canvas.drawPath(
      Path()
        ..moveTo(0, 0)
        ..lineTo(74, 0)
        ..lineTo(18, 160)
        ..lineTo(0, 160)
        ..close(),
      Paint()
        ..shader = ui.Gradient.linear(
          const Offset(0, 0),
          const Offset(74, 160),
          <Color>[
            Colors.white.withValues(alpha: 0.14),
            Colors.white.withValues(alpha: 0.0),
          ],
        ),
    );
    canvas.restore();

    canvas.drawPath(
      clipPath,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 0.8
        ..color = AppPalette.mint.withValues(alpha: 0.35 * screenFaceZ),
    );
  }

  void _drawBodyAxes(
    Canvas canvas,
    double cx,
    double cy,
    double scale,
    List<double> fullMat,
  ) {
    const perspK = 2.4;

    Offset proj(double bx, double by, double bz) {
      final vx = fullMat[0] * bx + fullMat[1] * by + fullMat[2] * bz;
      final vy = fullMat[3] * bx + fullMat[4] * by + fullMat[5] * bz;
      final vz = fullMat[6] * bx + fullMat[7] * by + fullMat[8] * bz;
      final k = scale / (1.0 - vz * perspK);
      return Offset(cx + vx * k, cy - vy * k);
    }

    // 仅从表壳外缘向外引出 X/Y/Z 轴指示，避免遮挡中央 TFT 实时画面。
    final axes = <(double, double, double, double, double, double, Color, String)>[
      (0.035, 0, 0, 0.056, 0, 0, const Color(0xFFF06A6A), 'X'),
      (0, 0.035, 0, 0, 0.056, 0, AppPalette.mint, 'Y'),
      (0.032, 0.028, 0.004, 0.032, 0.028, 0.028, const Color(0xFF6AB8F7), 'Z'),
    ];
    for (final (sx, sy, sz, ex, ey, ez, col, lbl) in axes) {
      final p0 = proj(sx, sy, sz);
      final p1 = proj(ex, ey, ez);
      final pLbl = proj(
        ex + (ex - sx) * 0.24,
        ey + (ey - sy) * 0.24,
        ez + (ez - sz) * 0.24,
      );
      canvas.drawCircle(p0, 2.0, Paint()..color = col.withValues(alpha: 0.75));
      canvas.drawLine(
        p0,
        p1,
        Paint()
          ..strokeWidth = 1.6
          ..strokeCap = StrokeCap.round
          ..color = col.withValues(alpha: 0.85),
      );
      canvas.drawCircle(p1, 2.6, Paint()..color = col);
      final tp = TextPainter(
        text: TextSpan(
          text: lbl,
          style: TextStyle(
            fontSize: 9.5,
            fontWeight: FontWeight.w700,
            color: col,
          ),
        ),
        textDirection: TextDirection.ltr,
      )..layout();
      tp.paint(canvas, pLbl - Offset(tp.width * 0.5, tp.height * 0.5));
    }
  }

  void _drawPlaceholderCube(
    Canvas canvas,
    double cx,
    double cy,
    double scale,
    List<double> vMat,
    bool failed,
  ) {
    final tp = TextPainter(
      text: TextSpan(
        text: failed ? '3D 模型加载失败' : '载入 3D 装配体...',
        style: TextStyle(
          fontSize: 11,
          color: dark ? AppPalette.muted : const Color(0xFF54635C),
        ),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    tp.paint(canvas, Offset(cx - tp.width * 0.5, cy - tp.height * 0.5));
  }

  // Body B -> ENU: R_z(-heading) * R_y(roll) * R_x(pitch)
  // Row-major 3x3 list [m00, m01, m02, m10, ...].
  static List<double> _buildBodyToEnuMatrix(
    double rollDeg,
    double pitchDeg,
    double headingDeg,
  ) {
    final r = rollDeg * math.pi / 180.0;
    final p = pitchDeg * math.pi / 180.0;
    final h = headingDeg * math.pi / 180.0;

    final cr = math.cos(r), sr = math.sin(r);
    final cp = math.cos(p), sp = math.sin(p);
    // R_z(-heading) -> cw = cos(h), sw = -sin(h)
    final ch = math.cos(h), sh = math.sin(h);

    // M_tilt = R_y(roll) * R_x(pitch)
    // R_y(r) = [cr, 0, sr; 0, 1, 0; -sr, 0, cr]
    // R_x(p) = [1, 0, 0; 0, cp, -sp; 0, sp, cp]
    final t00 = cr;
    final t01 = sr * sp;
    final t02 = sr * cp;
    const t10 = 0.0;
    final t11 = cp;
    final t12 = -sp;
    final t20 = -sr;
    final t21 = cr * sp;
    final t22 = cr * cp;

    // R_z(-h) = [ch, sh, 0; -sh, ch, 0; 0, 0, 1]
    return <double>[
      ch * t00 + sh * t10,
      ch * t01 + sh * t11,
      ch * t02 + sh * t12,
      -sh * t00 + ch * t10,
      -sh * t01 + ch * t11,
      -sh * t02 + ch * t12,
      t20,
      t21,
      t22,
    ];
  }

  // ENU -> Camera Space (+X right, +Y up, +Z toward viewer):
  // 先绕 ENU Z 顺时针/逆时针轨道旋转 yaw，再绕相机 X 向前倾倒 (90 - pitch) 使俯视角看到表盘。
  static List<double> _buildViewMatrix(double yawDeg, double pitchDeg) {
    final y = yawDeg * math.pi / 180.0;
    final p = (pitchDeg - 90.0) * math.pi / 180.0;
    final cy = math.cos(y), sy = math.sin(y);
    final cp = math.cos(p), sp = math.sin(p);

    // V = R_x(p) * R_z(y)
    // R_z(y) = [cy, -sy, 0; sy, cy, 0; 0, 0, 1]
    // R_x(p) = [1, 0, 0; 0, cp, -sp; 0, sp, cp]
    return <double>[
      cy,
      -sy,
      0.0,
      cp * sy,
      cp * cy,
      -sp,
      sp * sy,
      sp * cy,
      cp,
    ];
  }

  static List<double> _mul3(List<double> a, List<double> b) {
    return <double>[
      a[0] * b[0] + a[1] * b[3] + a[2] * b[6],
      a[0] * b[1] + a[1] * b[4] + a[2] * b[7],
      a[0] * b[2] + a[1] * b[5] + a[2] * b[8],
      a[3] * b[0] + a[4] * b[3] + a[5] * b[6],
      a[3] * b[1] + a[4] * b[4] + a[5] * b[7],
      a[3] * b[2] + a[4] * b[5] + a[5] * b[8],
      a[6] * b[0] + a[7] * b[3] + a[8] * b[6],
      a[6] * b[1] + a[7] * b[4] + a[8] * b[7],
      a[6] * b[2] + a[7] * b[5] + a[8] * b[8],
    ];
  }

  @override
  bool shouldRepaint(covariant _Watch3DPainter oldDelegate) {
    return oldDelegate.model != model ||
        oldDelegate.rollDeg != rollDeg ||
        oldDelegate.pitchDeg != pitchDeg ||
        oldDelegate.headingDeg != headingDeg ||
        oldDelegate.orbitYawDeg != orbitYawDeg ||
        oldDelegate.orbitPitchDeg != orbitPitchDeg ||
        oldDelegate.zoom != zoom ||
        oldDelegate.dark != dark ||
        oldDelegate.calPhase != calPhase ||
        oldDelegate.pulsePhase != pulsePhase ||
        oldDelegate.deviceScreen != deviceScreen ||
        oldDelegate.timeText != timeText ||
        oldDelegate.liveSensor != liveSensor ||
        oldDelegate.fusedAxes != fusedAxes;
  }
}
