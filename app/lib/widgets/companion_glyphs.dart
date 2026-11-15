// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dashboard and settings glyphs are adapted from FlClash's
// lib/ui/icons/app_glyphs.dart (GPL-3.0-or-later), revision be84736.
// Message and music glyphs are companion-specific additions.

import 'package:prism_ui/icons/glyph.dart';

abstract final class CompanionGlyphs {
  static const dashboard = Glyph([
    GlyphBox(3.5, 3.5, 10.5, 11.5, 2),
    GlyphBox(13.5, 3.5, 20.5, 9.5, 2),
    GlyphBox(3.5, 14.5, 10.5, 20.5, 2),
    GlyphBox(13.5, 12.5, 20.5, 20.5, 2),
  ]);

  static const sport = Glyph([
    GlyphPolyline([
      GlyphVertex(3, 12),
      GlyphVertex(7, 12),
      GlyphVertex(9, 7),
      GlyphVertex(12, 17),
      GlyphVertex(15, 10),
      GlyphVertex(17, 12),
      GlyphVertex(21, 12),
    ], role: GlyphRole.line),
  ]);

  static const messages = Glyph([
    GlyphBox(3.5, 4.5, 20.5, 16.5, 3.5),
    GlyphPolyline([
      GlyphVertex(8, 16.5),
      GlyphVertex(8, 20.2),
      GlyphVertex(12.4, 16.5),
    ], role: GlyphRole.detail),
    GlyphCircle(8.3, 10.5, 1.1, role: GlyphRole.detail, solid: true),
    GlyphCircle(12, 10.5, 1.1, role: GlyphRole.detail, solid: true),
    GlyphCircle(15.7, 10.5, 1.1, role: GlyphRole.detail, solid: true),
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
    ], role: GlyphRole.detail),
  ]);

  static const media = Glyph([
    GlyphBox(3.5, 4.5, 20.5, 19.5, 4),
    GlyphPolyline([
      GlyphVertex(8.5, 10.5),
      GlyphVertex(8.5, 13.5),
    ], role: GlyphRole.line),
    GlyphPolyline([
      GlyphVertex(12, 8),
      GlyphVertex(12, 16),
    ], role: GlyphRole.detail),
    GlyphPolyline([
      GlyphVertex(15.5, 9.5),
      GlyphVertex(15.5, 14.5),
    ], role: GlyphRole.line),
  ]);

  static const watchFace = Glyph([
    GlyphBox(5, 5.5, 19, 18.5, 4),
    GlyphPolyline([
      GlyphVertex(9, 2.5),
      GlyphVertex(15, 2.5),
    ], role: GlyphRole.detail),
    GlyphPolyline([
      GlyphVertex(9, 21.5),
      GlyphVertex(15, 21.5),
    ], role: GlyphRole.detail),
    GlyphPolyline([
      GlyphVertex(12, 9),
      GlyphVertex(12, 12),
      GlyphVertex(15, 13.5),
    ], role: GlyphRole.line),
    GlyphCircle(12, 12, 1.1, role: GlyphRole.detail, solid: true),
  ]);

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
    GlyphCircle(12, 12, 3, role: GlyphRole.detail),
  ]);
}
