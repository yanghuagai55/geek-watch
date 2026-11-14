import 'dart:math' as math;

import 'package:material_ui/material_ui.dart';

import '../theme/app_theme.dart';
import 'common.dart';

class MetricCard extends StatelessWidget {
  const MetricCard({
    super.key,
    required this.label,
    required this.value,
    required this.unit,
    required this.detail,
    required this.progress,
    required this.centerText,
    required this.color,
    required this.icon,
    required this.history,
  });

  final String label;
  final String value;
  final String unit;
  final String detail;
  final double progress;
  final String centerText;
  final Color color;
  final IconData icon;
  final List<double> history;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return AppCard(
      padding: const EdgeInsets.all(18),
      child: SizedBox(
        height: 148,
        child: Column(
          children: [
            Row(
              children: [
                SizedBox(
                  width: 78,
                  height: 78,
                  child: Stack(
                    alignment: Alignment.center,
                    children: [
                      SizedBox.expand(
                        child: CircularProgressIndicator(
                          value: 1,
                          strokeWidth: 6,
                          color: scheme.outlineVariant.withValues(alpha: 0.65),
                        ),
                      ),
                      TweenAnimationBuilder<double>(
                        tween: Tween(end: progress.clamp(0.0, 1.0)),
                        duration: const Duration(milliseconds: 500),
                        builder: (context, value, _) => SizedBox.expand(
                          child: CircularProgressIndicator(
                            value: value,
                            strokeWidth: 6,
                            strokeCap: StrokeCap.round,
                            color: color,
                            backgroundColor: Colors.transparent,
                          ),
                        ),
                      ),
                      Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Icon(icon, size: 15, color: color),
                          const SizedBox(height: 2),
                          Text(
                            centerText,
                            style: TextStyle(
                              color: color,
                              fontSize: 12,
                              fontWeight: FontWeight.w700,
                            ),
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
                const SizedBox(width: 16),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        label,
                        style: TextStyle(
                          color: scheme.onSurfaceVariant,
                          fontFamily: AppFonts.latin,
                          fontFamilyFallback: AppFonts.headingFallback,
                          fontSize: 17,
                          fontWeight: FontWeight.w700,
                          letterSpacing: 0.5,
                        ),
                      ),
                      const SizedBox(height: 4),
                      Row(
                        crossAxisAlignment: CrossAxisAlignment.baseline,
                        textBaseline: TextBaseline.alphabetic,
                        children: [
                          Flexible(
                            child: Text(
                              value,
                              overflow: TextOverflow.ellipsis,
                              style: const TextStyle(
                                fontSize: 25,
                                height: 1.05,
                                fontWeight: FontWeight.w700,
                                fontFamily: AppFonts.latin,
                                fontFamilyFallback: AppFonts.headingFallback,
                                fontFeatures: [FontFeature.tabularFigures()],
                              ),
                            ),
                          ),
                          const SizedBox(width: 5),
                          Flexible(
                            child: Text(
                              unit,
                              overflow: TextOverflow.ellipsis,
                              style: TextStyle(
                                color: scheme.onSurfaceVariant,
                                fontSize: 11,
                              ),
                            ),
                          ),
                        ],
                      ),
                      const SizedBox(height: 5),
                      Text(
                        detail,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(
                          color: scheme.onSurfaceVariant,
                          fontSize: 11,
                        ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
            const Spacer(),
            SizedBox(
              height: 34,
              width: double.infinity,
              child: CustomPaint(
                painter: _SparklinePainter(values: history, color: color),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class MiniMetricCard extends StatelessWidget {
  const MiniMetricCard({
    super.key,
    required this.label,
    required this.value,
    required this.unit,
    required this.color,
    required this.icon,
    required this.history,
  });

  final String label;
  final String value;
  final String unit;
  final Color color;
  final IconData icon;
  final List<double> history;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return LayoutBuilder(
      builder: (context, constraints) {
        // 极窄窗口（桌面竖屏拉很细）优先保住中文说明，必要时让出迷你曲线。
        final compact = constraints.maxWidth < 340;
        return AppCard(
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
          child: SizedBox(
            height: 50,
            child: Row(
              children: [
                Container(
                  width: 32,
                  height: 32,
                  decoration: BoxDecoration(
                    color: color.withValues(alpha: 0.1),
                    borderRadius: BorderRadius.circular(10),
                  ),
                  child: Icon(icon, size: 16, color: color),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.center,
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        label,
                        style: TextStyle(
                          color: scheme.onSurfaceVariant,
                          fontFamily: AppFonts.latin,
                          fontFamilyFallback: AppFonts.headingFallback,
                          fontSize: 15,
                          fontWeight: FontWeight.w700,
                          letterSpacing: 0.4,
                        ),
                      ),
                      const SizedBox(height: 3),
                      Text(
                        unit,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(
                          color: scheme.onSurfaceVariant,
                          fontSize: 10,
                        ),
                      ),
                    ],
                  ),
                ),
                if (!compact) ...[
                  const SizedBox(width: 8),
                  SizedBox(
                    width: 76,
                    height: 26,
                    child: CustomPaint(
                      painter: _SparklinePainter(values: history, color: color),
                    ),
                  ),
                ],
                const SizedBox(width: 10),
                // 固定宽度并右对齐：数字位数变化时曲线和文字都不再左右跳。
                SizedBox(
                  width: 80,
                  child: Text(
                    value,
                    textAlign: TextAlign.right,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      fontSize: compact ? 22 : 24,
                      height: 1,
                      fontWeight: FontWeight.w700,
                      fontFamily: AppFonts.latin,
                      fontFamilyFallback: AppFonts.headingFallback,
                      fontFeatures: const [FontFeature.tabularFigures()],
                    ),
                  ),
                ),
              ],
            ),
          ),
        );
      },
    );
  }
}

class _SparklinePainter extends CustomPainter {
  _SparklinePainter({required this.values, required this.color});

  final List<double> values;
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    if (values.length < 2) return;
    var low = values.reduce(math.min);
    var high = values.reduce(math.max);
    if (high - low < 1e-6) {
      // 全是同一个值（例如 FPS=0）时把线放中间，不要贴底。
      low -= 0.5;
      high += 0.5;
    } else {
      final padding = (high - low) * 0.12;
      low -= padding;
      high += padding;
    }
    final span = high - low;
    final points = <Offset>[];
    for (var i = 0; i < values.length; i++) {
      final x = i / (values.length - 1) * size.width;
      final ratio = ((values[i] - low) / span).clamp(0.0, 1.0);
      final y = size.height - 2 - ratio * (size.height - 4);
      points.add(Offset(x, y));
    }
    final line = Path()..moveTo(points.first.dx, points.first.dy);
    for (var i = 1; i < points.length; i++) {
      final previous = points[i - 1];
      final current = points[i];
      final midX = (previous.dx + current.dx) / 2;
      line.cubicTo(midX, previous.dy, midX, current.dy, current.dx, current.dy);
    }
    final fill = Path.from(line)
      ..lineTo(size.width, size.height)
      ..lineTo(0, size.height)
      ..close();
    canvas.drawPath(
      fill,
      Paint()
        ..shader = LinearGradient(
          begin: Alignment.topCenter,
          end: Alignment.bottomCenter,
          colors: [color.withValues(alpha: 0.18), color.withValues(alpha: 0)],
        ).createShader(Offset.zero & size),
    );
    canvas.drawPath(
      line,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.6
        ..strokeCap = StrokeCap.round
        ..color = color,
    );
  }

  @override
  bool shouldRepaint(covariant _SparklinePainter oldDelegate) {
    // history 是原地 removeAt/add 的可变 List，引用永远相同，
    // 必须无条件重绘，否则曲线会一直停住。
    return true;
  }
}
