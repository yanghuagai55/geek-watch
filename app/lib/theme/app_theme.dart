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

/// 语义化明暗自适应扩展色板：所有页面和组件必须通过 `context.appColors` 取色，
/// 杜绝浅色模式下直接硬编码亮薄荷/暗绿底色或低对比琥珀色。
@immutable
class AppThemeColors extends ThemeExtension<AppThemeColors> {
  const AppThemeColors({
    required this.accent,
    required this.accentSoft,
    required this.accentContainer,
    required this.onAccentContainer,
    required this.warning,
    required this.warningContainer,
    required this.info,
    required this.infoContainer,
    required this.danger,
    required this.dangerContainer,
    required this.heroGradient,
    required this.heroTextPrimary,
    required this.heroTextSecondary,
    required this.heroInsetBg,
    required this.insetSurface,
    required this.elevatedSurface,
    required this.albumArtGradient,
    required this.hardwareBezel,
    required this.hardwareBezelBorder,
  });

  factory AppThemeColors.fromBrightness(Brightness brightness) {
    if (brightness == Brightness.dark) {
      return const AppThemeColors(
        accent: AppPalette.mint,
        accentSoft: AppPalette.mintSoft,
        accentContainer: Color(0xFF19352B),
        onAccentContainer: Color(0xFFC5F2DF),
        warning: Color(0xFFF5B759),
        warningContainer: Color(0xFF332512),
        info: Color(0xFF7CC6E8),
        infoContainer: Color(0xFF152C37),
        danger: Color(0xFFF27B7B),
        dangerContainer: Color(0xFF3B1B1B),
        heroGradient: [
          Color(0xFF1B2B26),
          Color(0xFF131D1A),
          Color(0xFF0E1514),
        ],
        heroTextPrimary: AppPalette.warmWhite,
        heroTextSecondary: AppPalette.muted,
        heroInsetBg: Color(0xC70E1615),
        insetSurface: Color(0xFF111917),
        elevatedSurface: Color(0xFF202B28),
        albumArtGradient: [
          Color(0xFF2B463B),
          Color(0xFF17221E),
          Color(0xFF0E1413),
        ],
        hardwareBezel: Color(0xFF080B0A),
        hardwareBezelBorder: Color(0xFF31413C),
      );
    }
    return const AppThemeColors(
      accent: AppPalette.mintDeep,
      accentSoft: Color(0xFF2D8B6A),
      accentContainer: Color(0xFFE0F1E7),
      onAccentContainer: Color(0xFF124634),
      warning: Color(0xFFB4690E),
      warningContainer: Color(0xFFFCEFD9),
      info: Color(0xFF1B6E94),
      infoContainer: Color(0xFFE1F1F8),
      danger: Color(0xFFC63A3A),
      dangerContainer: Color(0xFFFCE8E8),
      heroGradient: [
        Color(0xFFDDEEE3),
        Color(0xFFEAF3E8),
        Color(0xFFF6F7EE),
      ],
      heroTextPrimary: Color(0xFF17241F),
      heroTextSecondary: Color(0xFF4F6158),
      heroInsetBg: Color(0xDCF8FAF4),
      insetSurface: Color(0xFFEEF3EB),
      elevatedSurface: Color(0xFFE3ECE2),
      albumArtGradient: [
        Color(0xFFD3E9DC),
        Color(0xFFE5F1E8),
        Color(0xFFF3F6EE),
      ],
      hardwareBezel: Color(0xFF0B0E0D),
      hardwareBezelBorder: Color(0xFF3A4B45),
    );
  }

  final Color accent;
  final Color accentSoft;
  final Color accentContainer;
  final Color onAccentContainer;
  final Color warning;
  final Color warningContainer;
  final Color info;
  final Color infoContainer;
  final Color danger;
  final Color dangerContainer;
  final List<Color> heroGradient;
  final Color heroTextPrimary;
  final Color heroTextSecondary;
  final Color heroInsetBg;
  final Color insetSurface;
  final Color elevatedSurface;
  final List<Color> albumArtGradient;
  final Color hardwareBezel;
  final Color hardwareBezelBorder;

  static AppThemeColors of(BuildContext context) {
    return Theme.of(context).extension<AppThemeColors>() ??
        AppThemeColors.fromBrightness(Theme.of(context).brightness);
  }

  @override
  AppThemeColors copyWith({
    Color? accent,
    Color? accentSoft,
    Color? accentContainer,
    Color? onAccentContainer,
    Color? warning,
    Color? warningContainer,
    Color? info,
    Color? infoContainer,
    Color? danger,
    Color? dangerContainer,
    List<Color>? heroGradient,
    Color? heroTextPrimary,
    Color? heroTextSecondary,
    Color? heroInsetBg,
    Color? insetSurface,
    Color? elevatedSurface,
    List<Color>? albumArtGradient,
    Color? hardwareBezel,
    Color? hardwareBezelBorder,
  }) {
    return AppThemeColors(
      accent: accent ?? this.accent,
      accentSoft: accentSoft ?? this.accentSoft,
      accentContainer: accentContainer ?? this.accentContainer,
      onAccentContainer: onAccentContainer ?? this.onAccentContainer,
      warning: warning ?? this.warning,
      warningContainer: warningContainer ?? this.warningContainer,
      info: info ?? this.info,
      infoContainer: infoContainer ?? this.infoContainer,
      danger: danger ?? this.danger,
      dangerContainer: dangerContainer ?? this.dangerContainer,
      heroGradient: heroGradient ?? this.heroGradient,
      heroTextPrimary: heroTextPrimary ?? this.heroTextPrimary,
      heroTextSecondary: heroTextSecondary ?? this.heroTextSecondary,
      heroInsetBg: heroInsetBg ?? this.heroInsetBg,
      insetSurface: insetSurface ?? this.insetSurface,
      elevatedSurface: elevatedSurface ?? this.elevatedSurface,
      albumArtGradient: albumArtGradient ?? this.albumArtGradient,
      hardwareBezel: hardwareBezel ?? this.hardwareBezel,
      hardwareBezelBorder: hardwareBezelBorder ?? this.hardwareBezelBorder,
    );
  }

  @override
  AppThemeColors lerp(covariant ThemeExtension<AppThemeColors>? other, double t) {
    if (other is! AppThemeColors) return this;
    List<Color> lerpList(List<Color> a, List<Color> b) {
      final len = a.length < b.length ? a.length : b.length;
      return [
        for (var i = 0; i < len; i++) Color.lerp(a[i], b[i], t) ?? a[i],
      ];
    }

    return AppThemeColors(
      accent: Color.lerp(accent, other.accent, t) ?? accent,
      accentSoft: Color.lerp(accentSoft, other.accentSoft, t) ?? accentSoft,
      accentContainer:
          Color.lerp(accentContainer, other.accentContainer, t) ??
          accentContainer,
      onAccentContainer:
          Color.lerp(onAccentContainer, other.onAccentContainer, t) ??
          onAccentContainer,
      warning: Color.lerp(warning, other.warning, t) ?? warning,
      warningContainer:
          Color.lerp(warningContainer, other.warningContainer, t) ??
          warningContainer,
      info: Color.lerp(info, other.info, t) ?? info,
      infoContainer:
          Color.lerp(infoContainer, other.infoContainer, t) ?? infoContainer,
      danger: Color.lerp(danger, other.danger, t) ?? danger,
      dangerContainer:
          Color.lerp(dangerContainer, other.dangerContainer, t) ??
          dangerContainer,
      heroGradient: lerpList(heroGradient, other.heroGradient),
      heroTextPrimary:
          Color.lerp(heroTextPrimary, other.heroTextPrimary, t) ??
          heroTextPrimary,
      heroTextSecondary:
          Color.lerp(heroTextSecondary, other.heroTextSecondary, t) ??
          heroTextSecondary,
      heroInsetBg: Color.lerp(heroInsetBg, other.heroInsetBg, t) ?? heroInsetBg,
      insetSurface:
          Color.lerp(insetSurface, other.insetSurface, t) ?? insetSurface,
      elevatedSurface:
          Color.lerp(elevatedSurface, other.elevatedSurface, t) ??
          elevatedSurface,
      albumArtGradient: lerpList(albumArtGradient, other.albumArtGradient),
      hardwareBezel:
          Color.lerp(hardwareBezel, other.hardwareBezel, t) ?? hardwareBezel,
      hardwareBezelBorder:
          Color.lerp(hardwareBezelBorder, other.hardwareBezelBorder, t) ??
          hardwareBezelBorder,
    );
  }
}

extension AppThemeContext on BuildContext {
  AppThemeColors get appColors => AppThemeColors.of(this);
  ColorScheme get colorScheme => Theme.of(this).colorScheme;
}

abstract final class AppTheme {
  static ThemeData dark() => _build(Brightness.dark);

  static ThemeData light() => _build(Brightness.light);

  static ThemeData _build(Brightness brightness) {
    final isDark = brightness == Brightness.dark;
    final primary = isDark ? AppPalette.mint : AppPalette.mintDeep;
    final appColors = AppThemeColors.fromBrightness(brightness);
    final scheme =
        ColorScheme.fromSeed(
          seedColor: primary,
          brightness: brightness,
        ).copyWith(
          primary: primary,
          onPrimary: isDark ? const Color(0xFF0A1B14) : AppPalette.warmWhite,
          primaryContainer: appColors.accentContainer,
          onPrimaryContainer: appColors.onAccentContainer,
          secondary: isDark ? AppPalette.mintSoft : AppPalette.mintDeep,
          onSecondary: isDark ? const Color(0xFF102019) : AppPalette.warmWhite,
          secondaryContainer: isDark
              ? const Color(0xFF1E2D28)
              : const Color(0xFFE3EFE7),
          onSecondaryContainer: isDark
              ? AppPalette.warmWhite
              : const Color(0xFF163227),
          surface: isDark ? AppPalette.darkSurface : AppPalette.lightSurface,
          onSurface: isDark ? AppPalette.warmWhite : const Color(0xFF192520),
          onSurfaceVariant: isDark ? AppPalette.muted : const Color(0xFF56675D),
          surfaceContainerLow: isDark
              ? const Color(0xFF101716)
              : const Color(0xFFEDF2EA),
          surfaceContainer: isDark
              ? AppPalette.darkSurface
              : AppPalette.lightSurface,
          surfaceContainerHigh: isDark
              ? const Color(0xFF202B28)
              : const Color(0xFFE5EDE3),
          surfaceContainerHighest: isDark
              ? const Color(0xFF27332F)
              : const Color(0xFFD6E2D6),
          outline: isDark ? AppPalette.line : const Color(0xFF738579),
          outlineVariant: isDark
              ? const Color(0xFF26332F)
              : const Color(0xFFC7D5C8),
          error: appColors.danger,
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
      extensions: [appColors],
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
      dialogTheme: DialogThemeData(
        elevation: 0,
        backgroundColor: scheme.surfaceContainer,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(22),
          side: BorderSide(color: scheme.outlineVariant),
        ),
      ),
      chipTheme: ChipThemeData(
        backgroundColor: scheme.surfaceContainerLow,
        selectedColor: appColors.accentContainer,
        secondarySelectedColor: appColors.accentContainer,
        disabledColor: scheme.surfaceContainerLow.withValues(alpha: 0.5),
        side: BorderSide(color: scheme.outlineVariant),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
        labelStyle: TextStyle(
          fontSize: 12,
          fontWeight: FontWeight.w600,
          color: scheme.onSurface,
        ),
        secondaryLabelStyle: TextStyle(
          fontSize: 12,
          fontWeight: FontWeight.w700,
          color: appColors.onAccentContainer,
        ),
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
      ),
      switchTheme: SwitchThemeData(
        thumbColor: WidgetStateProperty.resolveWith((states) {
          if (states.contains(WidgetState.disabled)) {
            return scheme.onSurface.withValues(alpha: 0.35);
          }
          if (states.contains(WidgetState.selected)) {
            return scheme.onPrimary;
          }
          return scheme.onSurfaceVariant;
        }),
        trackColor: WidgetStateProperty.resolveWith((states) {
          if (states.contains(WidgetState.disabled)) {
            return scheme.surfaceContainerHighest.withValues(alpha: 0.5);
          }
          if (states.contains(WidgetState.selected)) {
            return scheme.primary;
          }
          return scheme.surfaceContainerHigh;
        }),
        trackOutlineColor: WidgetStateProperty.resolveWith((states) {
          if (states.contains(WidgetState.selected)) {
            return Colors.transparent;
          }
          return scheme.outlineVariant;
        }),
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
          minimumSize: const Size(0, 42),
          padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 11),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(13),
          ),
          textStyle: const TextStyle(fontWeight: FontWeight.w600),
        ),
      ),
      outlinedButtonTheme: OutlinedButtonThemeData(
        style: OutlinedButton.styleFrom(
          minimumSize: const Size(0, 42),
          padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 11),
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
          backgroundColor: WidgetStateProperty.resolveWith((states) {
            if (states.contains(WidgetState.selected)) {
              return appColors.accentContainer;
            }
            return scheme.surfaceContainerLow;
          }),
          foregroundColor: WidgetStateProperty.resolveWith((states) {
            if (states.contains(WidgetState.selected)) {
              return appColors.onAccentContainer;
            }
            return scheme.onSurfaceVariant;
          }),
          side: WidgetStatePropertyAll(
            BorderSide(color: scheme.outlineVariant),
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
      progressIndicatorTheme: ProgressIndicatorThemeData(
        linearTrackColor: scheme.outlineVariant.withValues(alpha: 0.35),
      ),
    );
  }
}
