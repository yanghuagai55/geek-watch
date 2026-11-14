#ifndef RUNNER_FLUTTER_WINDOW_H_
#define RUNNER_FLUTTER_WINDOW_H_

#include <flutter/dart_project.h>
#include <flutter/encodable_value.h>
#include <flutter/flutter_view_controller.h>
#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>
#include <shellapi.h>

#include <memory>

#include "media_monitor.h"
#include "win32_window.h"

// A window that does nothing but host a Flutter view.
class FlutterWindow : public Win32Window {
 public:
  // Creates a new FlutterWindow hosting a Flutter view running |project|.
  explicit FlutterWindow(const flutter::DartProject& project);
  virtual ~FlutterWindow();

 protected:
  // Win32Window:
  bool OnCreate() override;
  void OnDestroy() override;
  LRESULT MessageHandler(HWND window, UINT const message, WPARAM const wparam,
                         LPARAM const lparam) noexcept override;

 private:
  void SetTrayEnabled(bool enabled);
  void RemoveTrayIcon();
  void ShowTrayMenu(HWND window);

  // The project to run.
  flutter::DartProject project_;

  // The Flutter instance hosted by this window.
  std::unique_ptr<flutter::FlutterViewController> flutter_controller_;

  // Channel used by Flutter to synchronize the native title bar theme.
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>>
      window_theme_channel_;

  // Windows 媒体会话（标题 / 艺人 / 播放状态）。
  MediaMonitor media_monitor_;
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>>
      media_channel_;

  // 托盘与“关闭后驻留”。
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>>
      tray_channel_;
  NOTIFYICONDATAW tray_data_{};
  bool tray_enabled_ = false;
  bool force_exit_ = false;
};

#endif  // RUNNER_FLUTTER_WINDOW_H_
