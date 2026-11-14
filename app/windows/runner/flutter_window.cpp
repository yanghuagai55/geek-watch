#include "flutter_window.h"

#include <cwchar>
#include <optional>
#include <variant>

#include "flutter/generated_plugin_registrant.h"
#include "resource.h"

namespace {

constexpr UINT kTrayCallbackMessage = WM_APP + 101;
constexpr UINT kTrayMenuShow = 1;
constexpr UINT kTrayMenuExit = 2;

bool ReadEnabledArgument(
    const flutter::MethodCall<flutter::EncodableValue>& call) {
  const auto* arguments =
      std::get_if<flutter::EncodableMap>(call.arguments());
  if (arguments == nullptr) return false;
  const auto value = arguments->find(flutter::EncodableValue("enabled"));
  if (value == arguments->end()) return false;
  if (const bool* enabled = std::get_if<bool>(&value->second)) return *enabled;
  return false;
}

std::string ReadStringArgument(
    const flutter::MethodCall<flutter::EncodableValue>& call,
    const char* key) {
  const auto* arguments =
      std::get_if<flutter::EncodableMap>(call.arguments());
  if (arguments == nullptr) return std::string();
  const auto value = arguments->find(flutter::EncodableValue(key));
  if (value == arguments->end()) return std::string();
  if (const std::string* text = std::get_if<std::string>(&value->second)) {
    return *text;
  }
  return std::string();
}

}  // namespace

FlutterWindow::FlutterWindow(const flutter::DartProject& project)
    : project_(project) {}

FlutterWindow::~FlutterWindow() {}

bool FlutterWindow::OnCreate() {
  if (!Win32Window::OnCreate()) {
    return false;
  }

  RECT frame = GetClientArea();

  // The size here must match the window dimensions to avoid unnecessary surface
  // creation / destruction in the startup path.
  flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
      frame.right - frame.left, frame.bottom - frame.top, project_);
  // Ensure that basic setup of the controller was successful.
  if (!flutter_controller_->engine() || !flutter_controller_->view()) {
    return false;
  }
  RegisterPlugins(flutter_controller_->engine());
  SetChildContent(flutter_controller_->view()->GetNativeWindow());

  flutter::BinaryMessenger* messenger =
      flutter_controller_->engine()->messenger();
  window_theme_channel_ =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          messenger, "watch_companion/window_theme",
          &flutter::StandardMethodCodec::GetInstance());
  window_theme_channel_->SetMethodCallHandler(
      [this](
          const flutter::MethodCall<flutter::EncodableValue>& call,
          std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>
              result) {
        if (call.method_name() != "setBrightness") {
          result->NotImplemented();
          return;
        }
        bool dark = true;
        const auto* arguments =
            std::get_if<flutter::EncodableMap>(call.arguments());
        if (arguments != nullptr) {
          const auto value =
              arguments->find(flutter::EncodableValue("dark"));
          if (value != arguments->end()) {
            if (const bool* flag = std::get_if<bool>(&value->second)) {
              dark = *flag;
            }
          }
        }
        ApplyBrandWindowColors(GetHandle(), dark);
        result->Success();
      });

  // Windows 媒体会话（SMTC）：标题 / 艺人 / 播放状态，后台线程轮询。
  media_monitor_.Start();
  media_channel_ =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          messenger, "watch_companion/media",
          &flutter::StandardMethodCodec::GetInstance());
  media_channel_->SetMethodCallHandler(
      [this](
          const flutter::MethodCall<flutter::EncodableValue>& call,
          std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>
              result) {
        if (call.method_name() == "control") {
          media_monitor_.Control(ReadStringArgument(call, "action"));
          result->Success();
          return;
        }
        if (call.method_name() != "nowPlaying") {
          result->NotImplemented();
          return;
        }
        const MediaMonitor::Info info = media_monitor_.Get();
        flutter::EncodableMap map;
        map[flutter::EncodableValue("hasSession")] =
            flutter::EncodableValue(info.has_session);
        map[flutter::EncodableValue("title")] =
            flutter::EncodableValue(info.title);
        map[flutter::EncodableValue("artist")] =
            flutter::EncodableValue(info.artist);
        map[flutter::EncodableValue("appId")] =
            flutter::EncodableValue(info.app_id);
        map[flutter::EncodableValue("status")] =
            flutter::EncodableValue(info.status);
        map[flutter::EncodableValue("position")] =
            flutter::EncodableValue(info.position_ms);
        map[flutter::EncodableValue("duration")] =
            flutter::EncodableValue(info.duration_ms);
        result->Success(flutter::EncodableValue(map));
      });

  // 托盘：关闭驻留、恢复窗口、退出。
  tray_channel_ =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          messenger, "watch_companion/tray",
          &flutter::StandardMethodCodec::GetInstance());
  tray_channel_->SetMethodCallHandler(
      [this](
          const flutter::MethodCall<flutter::EncodableValue>& call,
          std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>
              result) {
        if (call.method_name() == "setEnabled") {
          SetTrayEnabled(ReadEnabledArgument(call));
          result->Success();
          return;
        }
        if (call.method_name() == "hide") {
          ShowWindow(GetHandle(), SW_HIDE);
          result->Success();
          return;
        }
        if (call.method_name() == "show") {
          ShowWindow(GetHandle(), SW_SHOW);
          SetForegroundWindow(GetHandle());
          result->Success();
          return;
        }
        result->NotImplemented();
      });

  flutter_controller_->engine()->SetNextFrameCallback([&]() {
    this->Show();
  });

  // Flutter can complete the first frame before the "show window" callback is
  // registered. The following call ensures a frame is pending to ensure the
  // window is shown. It is a no-op if the first frame hasn't completed yet.
  flutter_controller_->ForceRedraw();

  return true;
}

void FlutterWindow::OnDestroy() {
  media_monitor_.Stop();
  RemoveTrayIcon();
  media_channel_.reset();
  tray_channel_.reset();
  window_theme_channel_.reset();
  if (flutter_controller_) {
    flutter_controller_ = nullptr;
  }

  Win32Window::OnDestroy();
}

void FlutterWindow::SetTrayEnabled(bool enabled) {
  if (enabled == tray_enabled_) return;
  HWND window = GetHandle();
  if (window == nullptr) return;

  if (enabled) {
    tray_data_.cbSize = sizeof(NOTIFYICONDATAW);
    tray_data_.hWnd = window;
    tray_data_.uID = 1;
    tray_data_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray_data_.uCallbackMessage = kTrayCallbackMessage;
    tray_data_.hIcon = LoadIconW(GetModuleHandleW(nullptr),
                                 MAKEINTRESOURCEW(IDI_APP_ICON));
    wcsncpy_s(tray_data_.szTip, ARRAYSIZE(tray_data_.szTip),
              L"AT32 手表助手", _TRUNCATE);
    tray_enabled_ = Shell_NotifyIconW(NIM_ADD, &tray_data_) != FALSE;
  } else {
    RemoveTrayIcon();
  }
}

void FlutterWindow::RemoveTrayIcon() {
  if (!tray_enabled_) return;
  Shell_NotifyIconW(NIM_DELETE, &tray_data_);
  tray_enabled_ = false;
}

void FlutterWindow::ShowTrayMenu(HWND window) {
  POINT point;
  GetCursorPos(&point);
  HMENU menu = CreatePopupMenu();
  if (menu == nullptr) return;
  AppendMenuW(menu, MF_STRING, kTrayMenuShow, L"显示主界面");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kTrayMenuExit, L"退出");
  SetForegroundWindow(window);
  const UINT command =
      TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                     point.x, point.y, 0, window, nullptr);
  DestroyMenu(menu);
  PostMessage(window, WM_NULL, 0, 0);

  if (command == kTrayMenuShow) {
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
  } else if (command == kTrayMenuExit) {
    force_exit_ = true;
    RemoveTrayIcon();
    PostMessage(window, WM_CLOSE, 0, 0);
  }
}

LRESULT
FlutterWindow::MessageHandler(HWND hwnd, UINT const message,
                              WPARAM const wparam,
                              LPARAM const lparam) noexcept {
  if (message == kTrayCallbackMessage) {
    if (lparam == WM_LBUTTONDBLCLK) {
      ShowWindow(hwnd, SW_SHOW);
      SetForegroundWindow(hwnd);
    } else if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
      ShowTrayMenu(hwnd);
    }
    return 0;
  }

  // 开启托盘驻留后，点关闭只是隐藏窗口；托盘菜单“退出”才会真正结束。
  if (message == WM_CLOSE && tray_enabled_ && !force_exit_) {
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }

  // Give Flutter, including plugins, an opportunity to handle window messages.
  if (flutter_controller_) {
    std::optional<LRESULT> result =
        flutter_controller_->HandleTopLevelWindowProc(hwnd, message, wparam,
                                                      lparam);
    if (result) {
      return *result;
    }
  }

  switch (message) {
    case WM_FONTCHANGE:
      flutter_controller_->engine()->ReloadSystemFonts();
      break;
  }

  return Win32Window::MessageHandler(hwnd, message, wparam, lparam);
}
