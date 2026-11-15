# Third-Party Notices

This Flutter companion app is licensed under the **GNU General Public License,
version 3 or (at your option) any later version** (`GPL-3.0-or-later`).
See [`LICENSE`](LICENSE).

## FlClash / prism_ui

The portrait navigation dock and liquid-glass rendering are derived from
[`prism_ui`](https://github.com/chen08209/prism_ui), the UI package used by
[`FlClash`](https://github.com/chen08209/FlClash).

- Upstream FlClash revision: `be84736605527034689c95bf27828ff27d912c5e`
  (v0.9.3).
- Upstream prism_ui revision: `63450c0ac0e8e1b2c0f3ed4ad7926dc94e083185`
  (the revision pinned by FlClash's `plugins/prism_ui` submodule).
- License: GNU General Public License, version 3 or later.
- Vendored source: [`packages/prism_ui`](packages/prism_ui).
- Upstream copyright and attribution notices are retained in
  [`packages/prism_ui/NOTICE`](packages/prism_ui/NOTICE).

Local integration changes:

- Added the companion app's four destinations and labels.
- Connected dock selection to the app's existing page navigation.
- Added companion-specific glyphs adapted from FlClash's
  `lib/ui/icons/app_glyphs.dart`.
- Limited the dock to portrait mobile layouts.
- Wrapped the portrait shell in `LiquidGlassScope` and `LiquidGlassHost`.

The corresponding complete source is this `app/` directory. Redistribution
must keep this notice, the GPL license, and the complete corresponding source
available under GPL-3.0-or-later.

## Fonts

Both font families are licensed under the **SIL Open Font License 1.1** and are
bundled as subsets covering only the glyphs this UI uses.

| Family in app | Upstream | Upstream family | Files |
| --- | --- | --- | --- |
| `AT32 Playfair` | [Playfair Display](https://github.com/clauseggers/Playfair-Display) (weight 400 / 600 / 700) | Playfair Display | `assets/fonts/AT32Playfair-*.ttf` |
| `AT32 Serif SC` | [Source Han Serif](https://github.com/adobe-fonts/source-han-serif) CN SemiBold | 思源宋体 / Source Han Serif | `assets/fonts/AT32SerifSC-SemiBold.otf` |

Because subsetting produces a Modified Version, and both upstream families
declare Reserved Font Names ("Playfair Display" and "Source"), the bundled
subsets are renamed to `AT32 Playfair` / `AT32 Serif SC` as required by the
OFL. No outlines were redesigned; only unneeded glyphs and hinting were
removed.

- License texts: [`assets/fonts/LICENSE-PlayfairDisplay.txt`](assets/fonts/LICENSE-PlayfairDisplay.txt),
  [`assets/fonts/LICENSE-SourceHanSerif.txt`](assets/fonts/LICENSE-SourceHanSerif.txt).
- The exact code point set used for the CJK subset is kept in
  [`assets/fonts/subset_chars.txt`](assets/fonts/subset_chars.txt); regenerate
  with `python -m fontTools.subset <original.otf> --text-file=subset_chars.txt
  --no-hinting`. If new Chinese copy is added, extend that file and re-subset,
  otherwise the new characters fall back to the system font.

## Additional notices

`prism_ui` includes code adapted from Material Color Utilities (Apache-2.0),
Material New Shapes (MIT), Super Sliver List (MIT), navigator_resizable (MIT),
and Flutter's ink sparkle shader (BSD-3-Clause). See the retained upstream
`NOTICE` and source headers for details.
