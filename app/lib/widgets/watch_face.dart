import 'dart:math' as math;

import 'package:material_ui/material_ui.dart';

import '../theme/app_theme.dart';

class WatchFace extends StatelessWidget {
  const WatchFace({super.key, required this.now, this.use24Hour = true});

  final DateTime now;
  final bool use24Hour;

  @override
  Widget build(BuildContext context) {
    return AspectRatio(
      aspectRatio: 128 / 160,
      child: Container(
        decoration: BoxDecoration(
          color: const Color(0xFF080A09),
          borderRadius: BorderRadius.circular(25),
          border: Border.all(color: AppPalette.line, width: 1.2),
          boxShadow: [
            BoxShadow(
              color: AppPalette.mint.withValues(alpha: 0.12),
              blurRadius: 32,
              spreadRadius: 2,
            ),
          ],
        ),
        padding: const EdgeInsets.all(7),
        child: ClipRRect(
          borderRadius: BorderRadius.circular(19),
          child: RepaintBoundary(
            child: SizedBox.expand(
              child: CustomPaint(
                painter: _WatchFacePainter(now: now, use24Hour: use24Hour),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _WatchFacePainter extends CustomPainter {
  _WatchFacePainter({required this.now, required this.use24Hour});

  final DateTime now;
  final bool use24Hour;

  @override
  void paint(Canvas canvas, Size size) {
    final w = size.width;
    final h = size.height;
    final background = Paint()
      ..shader = const RadialGradient(
        center: Alignment(0, -0.35),
        radius: 1.2,
        colors: [Color(0xFF1B231E), Color(0xFF090B0A)],
      ).createShader(Offset.zero & size);
    canvas.drawRect(Offset.zero & size, background);

    _text(
      canvas,
      '${const ['MON', 'TUE', 'WED', 'THU', 'FRI', 'SAT', 'SUN'][now.weekday - 1]} ${now.day.toString().padLeft(2, '0')}',
      Offset(w * 0.09, h * 0.045),
      w * 0.055,
      const Color(0xFF89928B),
      weight: FontWeight.w600,
    );
    _drawBattery(canvas, Offset(w * 0.79, h * 0.05), w * 0.12);

    final center = Offset(w * 0.5, h * 0.405);
    final radius = math.min(w * 0.39, h * 0.285);
    final outer = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = math.max(1, w * 0.007)
      ..color = AppPalette.mint.withValues(alpha: 0.8);
    canvas.drawCircle(center, radius, outer);

    final activity = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = math.max(2, w * 0.018)
      ..strokeCap = StrokeCap.round
      ..color = AppPalette.mint;
    canvas.drawArc(
      Rect.fromCircle(center: center, radius: radius - w * 0.04),
      -math.pi / 2,
      math.pi * 1.25,
      false,
      activity,
    );

    for (var i = 0; i < 60; i++) {
      final angle = i * math.pi / 30 - math.pi / 2;
      final major = i % 5 == 0;
      final inner = radius - (major ? w * 0.082 : w * 0.055);
      final tickRadius = radius - w * 0.023;
      final paint = Paint()
        ..strokeWidth = major ? w * 0.013 : w * 0.005
        ..strokeCap = StrokeCap.round
        ..color = major ? const Color(0xFFE6E6DE) : const Color(0xFF626A63);
      canvas.drawLine(
        center + Offset(math.cos(angle), math.sin(angle)) * inner,
        center + Offset(math.cos(angle), math.sin(angle)) * tickRadius,
        paint,
      );
    }

    final second = now.second + now.millisecond / 1000;
    final minute = now.minute + second / 60;
    final hour = now.hour % 12 + minute / 60;
    _hand(
      canvas,
      center,
      hour * math.pi / 6 - math.pi / 2,
      radius * 0.48,
      w * 0.026,
      const Color(0xFFF3F0E8),
    );
    _hand(
      canvas,
      center,
      minute * math.pi / 30 - math.pi / 2,
      radius * 0.70,
      w * 0.018,
      const Color(0xFFF3F0E8),
    );
    _hand(
      canvas,
      center,
      second * math.pi / 30 - math.pi / 2,
      radius * 0.78,
      w * 0.007,
      AppPalette.mint,
    );
    canvas.drawCircle(center, w * 0.025, Paint()..color = AppPalette.mint);
    canvas.drawCircle(
      center,
      w * 0.010,
      Paint()..color = const Color(0xFF191B18),
    );

    final hourText = use24Hour
        ? now.hour
        : (now.hour % 12 == 0 ? 12 : now.hour % 12);
    final time =
        '${hourText.toString().padLeft(2, '0')}:${now.minute.toString().padLeft(2, '0')}';
    _text(
      canvas,
      time,
      Offset(w * 0.5, h * 0.745),
      w * 0.16,
      const Color(0xFFF4F0E6),
      centered: true,
      weight: FontWeight.w700,
      mono: true,
    );
    _text(
      canvas,
      '${now.month}月${now.day}日  ·  界面预览',
      Offset(w * 0.5, h * 0.842),
      w * 0.047,
      const Color(0xFF9AA29A),
      centered: true,
    );

    for (var i = 0; i < 5; i++) {
      canvas.drawCircle(
        Offset(w * 0.5 + (i - 2) * w * 0.072, h * 0.936),
        i == 0 ? w * 0.017 : w * 0.011,
        Paint()..color = i == 0 ? AppPalette.mint : const Color(0xFF515950),
      );
    }
  }

  void _hand(
    Canvas canvas,
    Offset center,
    double angle,
    double length,
    double width,
    Color color,
  ) {
    final paint = Paint()
      ..color = color
      ..strokeWidth = width
      ..strokeCap = StrokeCap.round;
    canvas.drawLine(
      center - Offset(math.cos(angle), math.sin(angle)) * length * 0.12,
      center + Offset(math.cos(angle), math.sin(angle)) * length,
      paint,
    );
  }

  void _drawBattery(Canvas canvas, Offset origin, double width) {
    final height = width * 0.46;
    final rect = origin & Size(width, height);
    final paint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1
      ..color = const Color(0xFF89928B);
    canvas.drawRRect(
      RRect.fromRectAndRadius(rect, const Radius.circular(2)),
      paint,
    );
    canvas.drawRect(
      Rect.fromLTWH(origin.dx + 2, origin.dy + 2, width * 0.72, height - 4),
      Paint()..color = AppPalette.mint,
    );
    canvas.drawRect(
      Rect.fromLTWH(
        origin.dx + width + 1,
        origin.dy + height * 0.3,
        2,
        height * 0.4,
      ),
      Paint()..color = const Color(0xFF89928B),
    );
  }

  void _text(
    Canvas canvas,
    String value,
    Offset offset,
    double fontSize,
    Color color, {
    bool centered = false,
    FontWeight weight = FontWeight.w400,
    bool mono = false,
  }) {
    final painter = TextPainter(
      text: TextSpan(
        text: value,
        style: TextStyle(
          color: color,
          fontSize: fontSize,
          fontWeight: weight,
          fontFamily: mono ? 'Consolas' : 'Segoe UI',
          fontFamilyFallback: const ['Microsoft YaHei UI'],
          fontFeatures: mono ? const [] : null,
        ),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    painter.paint(
      canvas,
      centered ? offset - Offset(painter.width / 2, 0) : offset,
    );
  }

  @override
  bool shouldRepaint(covariant _WatchFacePainter oldDelegate) {
    return oldDelegate.now != now || oldDelegate.use24Hour != use24Hour;
  }
}
