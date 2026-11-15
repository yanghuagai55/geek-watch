import 'package:material_ui/material_ui.dart';

abstract final class AppPalette {
  static const mint = Color(0xFF73D4AE);
  static const mintSoft = Color(0xFFA5E4C9);
  static const mintDeep = Color(0xFF24795C);
  static const warmWhite = Color(0xFFF0F1E4);
  static const muted = Color(0xFF87978F);
  static const ink = Color(0xFF0A0F10);
  static const darkSurface = Color(0xFF182120);
  static const line = Color(0xFF31413C);
  static const lightSurface = Color(0xFFFCFCF6);
}

abstract final class AppFonts {
  /// 标题与大数字：Playfair Display（子集化后改名为 AT32 Playfair）。
  static const latin = 'AT32 Playfair';

  /// 正文与其余界面文字：思源宋体 SemiBold（子集化后改名为 AT32 Serif SC）。
  static const han = 'AT32 Serif SC';

  /// 正文回退：思源宋体缺字时交给系统字体，避免出现豆腐块。
  /// Windows 用微软雅黑，Android 用 Noto/Sans 系列（系统会再回退到中文）。
  static const systemFallback = [
    'Microsoft YaHei UI',
    'PingFang SC',
    'Noto Sans SC',
    'Noto Sans CJK SC',
    'Roboto',
    'sans-serif',
  ];

  /// 标题回退：Playfair 没有汉字，中文标题落回思源宋体。
  static const headingFallback = [han, ...systemFallback];
}

abstract final class AppTheme {
  static ThemeData dark() => _build(Brightness.dark);

  static ThemeData light() => _build(Brightness.light);

  static ThemeData _build(Brightness brightness) {
    final isDark = brightness == Brightness.dark;
    final primary = isDark ? AppPalette.mint : AppPalette.mintDeep;
    final scheme =
        ColorScheme.fromSeed(
          seedColor: primary,
          brightness: brightness,
        ).copyWith(
          primary: primary,
          onPrimary: isDark ? const Color(0xFF0A1B14) : AppPalette.warmWhite,
          secondary: isDark ? AppPalette.mintSoft : AppPalette.mintDeep,
          onSecondary: isDark ? const Color(0xFF102019) : AppPalette.warmWhite,
          surface: isDark ? AppPalette.darkSurface : AppPalette.lightSurface,
          onSurface: isDark ? AppPalette.warmWhite : const Color(0xFF1B2722),
          onSurfaceVariant: isDark ? AppPalette.muted : const Color(0xFF5C6A60),
          surfaceContainerLow: isDark
              ? const Color(0xFF101716)
              : const Color(0xFFEDF2EA),
          surfaceContainer: isDark
              ? AppPalette.darkSurface
              : AppPalette.lightSurface,
          surfaceContainerHigh: isDark
              ? const Color(0xFF202B28)
              : const Color(0xFFE6EDE4),
          surfaceContainerHighest: isDark
              ? const Color(0xFF27332F)
              : const Color(0xFFD6E2D6),
          outline: isDark ? AppPalette.line : const Color(0xFF738579),
          outlineVariant: isDark
              ? const Color(0xFF26332F)
              : const Color(0xFFC7D5C8),
        );
    final base = ThemeData(brightness: brightness, useMaterial3: true);

    // 正文默认思源宋体；只把标题级字阶切到 Playfair（中文自动落回宋体）。
    final bodyText = base.textTheme.apply(
      fontFamily: AppFonts.han,
      fontFamilyFallback: AppFonts.systemFallback,
      bodyColor: scheme.onSurface,
      displayColor: scheme.onSurface,
    );
    TextStyle? display(TextStyle? style) => style?.copyWith(
      fontFamily: AppFonts.latin,
      fontFamilyFallback: AppFonts.headingFallback,
    );

    return ThemeData(
      useMaterial3: true,
      colorScheme: scheme,
      scaffoldBackgroundColor: isDark
          ? AppPalette.ink
          : const Color(0xFFF3F5EE),
      textTheme: bodyText.copyWith(
        displayLarge: display(bodyText.displayLarge),
        displayMedium: display(bodyText.displayMedium),
        displaySmall: display(bodyText.displaySmall),
        headlineLarge: display(bodyText.headlineLarge),
        headlineMedium: display(bodyText.headlineMedium),
        headlineSmall: display(bodyText.headlineSmall),
        titleLarge: display(bodyText.titleLarge),
        titleMedium: display(bodyText.titleMedium),
      ),
      cardTheme: CardThemeData(
        elevation: 0,
        margin: EdgeInsets.zero,
        color: scheme.surfaceContainer,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(20),
          side: BorderSide(color: scheme.outlineVariant),
        ),
      ),
      dividerTheme: DividerThemeData(
        color: scheme.outlineVariant,
        thickness: 1,
        space: 1,
      ),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: isDark ? const Color(0xFF101716) : const Color(0xFFEFF3EB),
        contentPadding: const EdgeInsets.symmetric(
          horizontal: 16,
          vertical: 14,
        ),
        hintStyle: TextStyle(color: scheme.onSurfaceVariant),
        labelStyle: TextStyle(color: scheme.onSurfaceVariant),
        border: OutlineInputBorder(
          borderRadius: BorderRadius.circular(14),
          borderSide: BorderSide(color: scheme.outlineVariant),
        ),
        enabledBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(14),
          borderSide: BorderSide(color: scheme.outlineVariant),
        ),
        focusedBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(14),
          borderSide: BorderSide(color: scheme.primary, width: 1.5),
        ),
      ),
      filledButtonTheme: FilledButtonThemeData(
        style: FilledButton.styleFrom(
          minimumSize: const Size(0, 44),
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(13),
          ),
          textStyle: const TextStyle(fontWeight: FontWeight.w600),
        ),
      ),
      outlinedButtonTheme: OutlinedButtonThemeData(
        style: OutlinedButton.styleFrom(
          minimumSize: const Size(0, 44),
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
          side: BorderSide(color: scheme.outlineVariant),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(13),
          ),
          textStyle: const TextStyle(fontWeight: FontWeight.w600),
        ),
      ),
      navigationBarTheme: NavigationBarThemeData(
        height: 70,
        backgroundColor: scheme.surfaceContainerLow,
        indicatorColor: scheme.primary.withValues(alpha: 0.14),
        labelTextStyle: const WidgetStatePropertyAll(
          TextStyle(fontSize: 12, fontWeight: FontWeight.w600),
        ),
      ),
      // 宋体字面比原先的无衬线更宽，分段按钮和列表副标题都需要收紧，
      // 否则窄屏下中文会被挤成竖排。
      segmentedButtonTheme: SegmentedButtonThemeData(
        style: ButtonStyle(
          visualDensity: VisualDensity.compact,
          padding: const WidgetStatePropertyAll(
            EdgeInsets.symmetric(horizontal: 10, vertical: 7),
          ),
          textStyle: const WidgetStatePropertyAll(
            TextStyle(fontSize: 12.5, fontWeight: FontWeight.w600),
          ),
          minimumSize: const WidgetStatePropertyAll(Size(0, 36)),
        ),
      ),
      listTileTheme: ListTileThemeData(
        titleTextStyle: TextStyle(
          fontSize: 14.5,
          fontWeight: FontWeight.w600,
          color: scheme.onSurface,
        ),
        subtitleTextStyle: TextStyle(
          fontSize: 11.5,
          height: 1.35,
          color: scheme.onSurfaceVariant,
        ),
      ),
      snackBarTheme: SnackBarThemeData(
        behavior: SnackBarBehavior.floating,
        backgroundColor: scheme.surfaceContainerHighest,
        contentTextStyle: TextStyle(color: scheme.onSurface),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(14)),
      ),
      sliderTheme: SliderThemeData(
        activeTrackColor: scheme.primary,
        thumbColor: scheme.primary,
        inactiveTrackColor: scheme.outlineVariant,
      ),
      progressIndicatorTheme: const ProgressIndicatorThemeData(
        linearTrackColor: Colors.transparent,
      ),
    );
  }
}
