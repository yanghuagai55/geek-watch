// AT32 手表助手 · GPL-3.0-or-later

import 'package:material_ui/material_ui.dart';

import '../state/watch_controller.dart';
import '../theme/app_theme.dart';
import 'watch_face.dart';

/// 主页预览：未连接时画仿真表盘；连接后显示真机抓屏，点击可刷新。
class WatchPreview extends StatelessWidget {
  const WatchPreview({
    super.key,
    required this.controller,
    required this.width,
  });

  final WatchController controller;
  final double width;

  @override
  Widget build(BuildContext context) {
    final image = controller.deviceScreen;
    if (image == null) {
      return RepaintBoundary(
        child: SizedBox(
          width: width,
          child: Stack(
            alignment: Alignment.center,
            children: [
              ListenableBuilder(
                listenable: controller.clockListenable,
                builder: (context, _) => WatchFace(
                  now: controller.now,
                  use24Hour: controller.use24Hour,
                ),
              ),
              if (controller.capturingPreview)
                const SizedBox(
                  width: 20,
                  height: 20,
                  child: CircularProgressIndicator(strokeWidth: 2),
                ),
            ],
          ),
        ),
      );
    }

    final height = width * 160 / 128;
    return GestureDetector(
      onTap: controller.capturePreview,
      child: Tooltip(
        message: '点击刷新手表画面',
        child: SizedBox(
          width: width,
          height: height,
          child: Stack(
            fit: StackFit.expand,
            children: [
              Container(
                decoration: BoxDecoration(
                  color: Colors.black,
                  borderRadius: BorderRadius.circular(width * 0.1),
                  border: Border.all(color: AppPalette.line, width: 2),
                  boxShadow: [
                    BoxShadow(
                      color: Colors.black.withValues(alpha: 0.28),
                      blurRadius: 16,
                    ),
                  ],
                ),
                clipBehavior: Clip.antiAlias,
                child: RawImage(
                  image: image,
                  fit: BoxFit.contain,
                  filterQuality: FilterQuality.none,
                ),
              ),
              Positioned(
                right: 6,
                bottom: 6,
                child: Container(
                  padding: const EdgeInsets.all(4),
                  decoration: BoxDecoration(
                    color: Colors.black.withValues(alpha: 0.5),
                    shape: BoxShape.circle,
                  ),
                  child: const Icon(
                    Icons.refresh_rounded,
                    size: 14,
                    color: AppPalette.mintSoft,
                  ),
                ),
              ),
              if (controller.capturingPreview)
                ColoredBox(
                  color: Colors.black.withValues(alpha: 0.35),
                  child: const Center(
                    child: SizedBox(
                      width: 22,
                      height: 22,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    ),
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }
}
