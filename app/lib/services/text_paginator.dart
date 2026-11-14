// AT32 手表助手 · GPL-3.0-or-later
//
// 把长标题/长歌词行按 128px 宽度切成多页，每页最多两行。
// 有 LRC 时间轴时由控制器按时间轴推送；没有时间轴时按固定间隔轮播。

import 'dart:ui' as ui;

import 'package:flutter/painting.dart';

const double watchMediaFontSize = 14;
const int watchMediaMaxLines = 2;
const int watchMediaMaxWidth = 128;

/// 返回分页后的行列表：每页 1..2 行，单行不超过 128px。
List<List<String>> paginateWatchText(
  String text, {
  double fontSize = watchMediaFontSize,
  int maxWidth = watchMediaMaxWidth,
  int maxLines = watchMediaMaxLines,
}) {
  final normalized = text.replaceAll(RegExp(r'\s+'), ' ').trim();
  if (normalized.isEmpty) return const <List<String>>[];

  final units = normalized.runes
      .map((rune) => String.fromCharCodes(<int>[rune]))
      .toList();
  final lines = <String>[];
  var current = '';

  double measure(String value) {
    final painter = TextPainter(
      text: TextSpan(
        text: value,
        style: TextStyle(
          fontSize: fontSize,
          height: 1.05,
          fontFamily: 'Microsoft YaHei UI',
          fontFamilyFallback: const ['Microsoft YaHei', 'SimSun', 'Segoe UI'],
        ),
      ),
      textDirection: ui.TextDirection.ltr,
      maxLines: 1,
    )..layout();
    final width = painter.width;
    painter.dispose();
    return width;
  }

  for (final unit in units) {
    final candidate = '$current$unit';
    if (current.isEmpty || measure(candidate) <= maxWidth) {
      current = candidate;
      continue;
    }
    final space = current.lastIndexOf(' ');
    if (space > 0 && space >= current.length - 16) {
      lines.add(current.substring(0, space).trimRight());
      current = '${current.substring(space + 1).trimLeft()}$unit';
    } else {
      lines.add(current);
      current = unit;
    }
  }
  if (current.isNotEmpty) lines.add(current);

  final pages = <List<String>>[];
  for (var i = 0; i < lines.length; i += maxLines) {
    final end = i + maxLines < lines.length ? i + maxLines : lines.length;
    pages.add(lines.sublist(i, end));
  }
  return pages;
}
