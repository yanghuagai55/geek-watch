// SPDX-License-Identifier: GPL-3.0-or-later
//
// Custom portrait dock glyphs crafted for prism_ui's GlyphPainter (24x24 grid,
// strokeWidth=1.8). At fill=1, GlyphRole.body forms solid silhouettes,
// GlyphRole.detail carves crisp negative-space cutouts inside bodies, and
// GlyphRole.line draws foreground strokes outside bodies.
// Settings gear outline is adapted from FlClash's app_glyphs.dart (GPL-3.0-or-later).

import 'package:prism_ui/icons/glyph.dart';

abstract final class CompanionGlyphs {
  /// 主页：圆润屋檐与居中拱门镂空，与桌面端 Home 图标语义一致且轮廓辨识度高。
  static const dashboard = Glyph([
    GlyphPolyline([
      GlyphVertex(12, 3.3, 1.5),
      GlyphVertex(20.1, 9.8, 1.7),
      GlyphVertex(20.1, 19.4, 2.5),
      GlyphVertex(3.9, 19.4, 2.5),
      GlyphVertex(3.9, 9.8, 1.7),
    ], closed: true),
    GlyphPath(
      'M9.8 17.7 L9.8 14.0 A2.2 2.2 0 0 1 14.2 14.0 L14.2 17.7 A0.8 0.8 0 0 1 13.4 18.5 L10.6 18.5 A0.8 0.8 0 0 1 9.8 17.7 Z',
      role: GlyphRole.detail,
      solid: true,
    ),
  ]);

  /// 运动：饱满心形主体 + 清晰心电律动负空间镂空，兼顾计步健康与视觉份量均衡。
  static const sport = Glyph([
    GlyphPath(
      'M12.0 6.5 '
      'C10.0 3.7 3.4 4.2 3.4 9.5 '
      'C3.4 14.2 8.1 17.2 12.0 20.0 '
      'C15.9 17.2 20.6 14.2 20.6 9.5 '
      'C20.6 4.2 14.0 3.7 12.0 6.5 Z',
    ),
    GlyphPolyline([
      GlyphVertex(6.4, 11.7),
      GlyphVertex(8.9, 11.7),
      GlyphVertex(10.4, 8.8),
      GlyphVertex(12.2, 14.7),
      GlyphVertex(14.0, 10.6),
      GlyphVertex(15.3, 11.7),
      GlyphVertex(17.6, 11.7),
    ], role: GlyphRole.detail),
  ]);

  static const messages = Glyph([
    GlyphBox(3.5, 4.5, 20.5, 16.5, 3.5),
    GlyphPolyline([
      GlyphVertex(7.6, 16.0),
      GlyphVertex(7.6, 20.0, 0.5),
      GlyphVertex(12.4, 16.0),
    ], closed: true),
    GlyphCircle(8.3, 10.5, 1.15, role: GlyphRole.detail, solid: true),
    GlyphCircle(12, 10.5, 1.15, role: GlyphRole.detail, solid: true),
    GlyphCircle(15.7, 10.5, 1.15, role: GlyphRole.detail, solid: true),
  ]);

  static const music = Glyph([
    GlyphCircle(7.2, 18, 2.3, role: GlyphRole.line),
    GlyphCircle(17.8, 16, 2.3, role: GlyphRole.line),
    GlyphPolyline([
      GlyphVertex(9.5, 18),
      GlyphVertex(9.5, 6.5),
      GlyphVertex(20.1, 4.5),
      GlyphVertex(20.1, 16),
    ], role: GlyphRole.line),
    GlyphPolyline([
      GlyphVertex(9.5, 8.7),
      GlyphVertex(20.1, 6.7),
    ], role: GlyphRole.line),
  ]);

  /// 媒体：圆形播放徽章 + 光学居中圆角三角镂空，小尺寸下清晰直白且不与方表盘撞型。
  static const media = Glyph([
    GlyphCircle(12, 12, 8.4),
    GlyphPath(
      'M9.7 9.1 '
      'C9.7 8.2 10.7 7.7 11.4 8.1 '
      'L16.3 11.0 '
      'C17.1 11.4 17.1 12.6 16.3 13.0 '
      'L11.4 15.9 '
      'C10.7 16.3 9.7 15.8 9.7 14.9 Z',
      role: GlyphRole.detail,
      solid: true,
    ),
  ]);

  /// 表盘：竖屏圆角智能手表主体 + 收窄上下表带 + 右侧表冠 + 清晰负空间时针分针镂空。
  static const watchFace = Glyph([
    GlyphPolyline([
      GlyphVertex(9.4, 5.6),
      GlyphVertex(9.8, 3.1, 0.7),
      GlyphVertex(14.2, 3.1, 0.7),
      GlyphVertex(14.6, 5.6),
    ], closed: true),
    GlyphPolyline([
      GlyphVertex(9.4, 18.4),
      GlyphVertex(14.6, 18.4),
      GlyphVertex(14.2, 20.9, 0.7),
      GlyphVertex(9.8, 20.9, 0.7),
    ], closed: true),
    GlyphBox(5.5, 5.5, 18.5, 18.5, 3.4),
    GlyphPolyline([
      GlyphVertex(19.7, 9.8),
      GlyphVertex(19.7, 12.6),
    ], role: GlyphRole.line),
    GlyphPolyline([
      GlyphVertex(12, 8.5),
      GlyphVertex(12, 12.1),
      GlyphVertex(15.1, 13.7),
    ], role: GlyphRole.detail),
  ]);

  /// 设置：六齿精密齿轮 + 完整实心圆孔镂空（消除旧版描边镂空残留的中心黑点）。
  static const settings = Glyph([
    GlyphPolyline([
      GlyphVertex(9.87, 5.44, 0.6),
      GlyphVertex(10.09, 3, 0.9),
      GlyphVertex(13.91, 3, 0.9),
      GlyphVertex(14.13, 5.44, 0.6),
      GlyphVertex(16.62, 6.87, 0.6),
      GlyphVertex(18.84, 5.84, 0.9),
      GlyphVertex(20.75, 9.16, 0.9),
      GlyphVertex(18.75, 10.57, 0.6),
      GlyphVertex(18.75, 13.43, 0.6),
      GlyphVertex(20.75, 14.84, 0.9),
      GlyphVertex(18.84, 18.16, 0.9),
      GlyphVertex(16.62, 17.13, 0.6),
      GlyphVertex(14.13, 18.56, 0.6),
      GlyphVertex(13.91, 21, 0.9),
      GlyphVertex(10.09, 21, 0.9),
      GlyphVertex(9.87, 18.56, 0.6),
      GlyphVertex(7.38, 17.13, 0.6),
      GlyphVertex(5.16, 18.16, 0.9),
      GlyphVertex(3.25, 14.84, 0.9),
      GlyphVertex(5.25, 13.43, 0.6),
      GlyphVertex(5.25, 10.57, 0.6),
      GlyphVertex(3.25, 9.16, 0.9),
      GlyphVertex(5.16, 5.84, 0.9),
      GlyphVertex(7.38, 6.87, 0.6),
    ], closed: true),
    GlyphCircle(12, 12, 3.2, role: GlyphRole.detail, solid: true),
  ]);
}
