#include <flutter/dart_project.h>
#include <flutter/flutter_view_controller.h>
#include <windows.h>

#include <dbghelp.h>

#include <string>

#include "flutter_window.h"
#include "utils.h"

namespace {

// 崩溃取证：把原生未处理异常写到 exe 同目录，同时留一份 minidump。
// 之前用户反馈「用一下闪退」时，我们只有事件日志里的模块偏移，
// 拿不到调用栈；有这两个文件才能定位。
std::wstring ExecutableDirectory() {
  wchar_t path[MAX_PATH] = {};
  const DWORD length = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return L".";
  }
  std::wstring value(path, length);
  const size_t slash = value.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : value.substr(0, slash);
}

std::wstring HexValue(unsigned long long value) {
  wchar_t buffer[32] = {};
  swprintf_s(buffer, L"0x%llX", value);
  return buffer;
}

std::wstring DescribeAddress(const void* address) {
  HMODULE module = nullptr;
  const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
  if (!::GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(address),
                            &module)) {
    return L"<未知模块> " + HexValue(reinterpret_cast<uintptr_t>(address));
  }
  wchar_t path[MAX_PATH] = {};
  ::GetModuleFileNameW(module, path, MAX_PATH);
  const auto offset = reinterpret_cast<uintptr_t>(address) -
                      reinterpret_cast<uintptr_t>(module);
  return std::wstring(path) + L" + " + HexValue(offset);
}

void AppendCrashText(const std::wstring& text) {
  const std::wstring path =
      ExecutableDirectory() + L"\\native_crash.log";
  HANDLE file = ::CreateFileW(path.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  ::SetFilePointer(file, 0, nullptr, FILE_END);

  const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                          static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
  if (bytes > 0) {
    std::string utf8(static_cast<size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                          static_cast<int>(text.size()), utf8.data(), bytes,
                          nullptr, nullptr);
    DWORD written = 0;
    ::WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written,
                nullptr);
  }
  ::CloseHandle(file);
}

void WriteMiniDump(EXCEPTION_POINTERS* info, const std::wstring& suffix) {
  const std::wstring path =
      ExecutableDirectory() + L"\\crash_" + suffix + L".dmp";
  HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  MINIDUMP_EXCEPTION_INFORMATION exception_info = {};
  exception_info.ThreadId = ::GetCurrentThreadId();
  exception_info.ExceptionPointers = info;
  exception_info.ClientPointers = FALSE;
  ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), file,
                      static_cast<MINIDUMP_TYPE>(MiniDumpNormal),
                      &exception_info, nullptr, nullptr);
  ::CloseHandle(file);
}

std::wstring NowStamp() {
  SYSTEMTIME time = {};
  ::GetLocalTime(&time);
  wchar_t buffer[64] = {};
  swprintf_s(buffer, L"%04d-%02d-%02d %02d:%02d:%02d", time.wYear, time.wMonth,
             time.wDay, time.wHour, time.wMinute, time.wSecond);
  return buffer;
}

LONG WINAPI CrashHandler(EXCEPTION_POINTERS* info) {
  if (info != nullptr && info->ExceptionRecord != nullptr) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    std::wstring text = L"===== " + NowStamp() + L" =====\r\n";
    text += L"code: " + HexValue(record->ExceptionCode) + L"\r\n";
    text += L"address: ";
    text += DescribeAddress(record->ExceptionAddress);
    text += L"\r\n";
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        record->NumberParameters >= 2) {
      text += record->ExceptionInformation[0] == 0 ? L"access: read @ "
                                                   : L"access: write @ ";
      text += HexValue(record->ExceptionInformation[1]) + L"\r\n";
    }
    text += L"\r\n";

    const std::wstring suffix =
        std::to_wstring(::GetCurrentProcessId()) + L"_" +
        std::to_wstring(::GetTickCount64());
    AppendCrashText(text);
    WriteMiniDump(info, suffix);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE prev,
                      _In_ wchar_t *command_line, _In_ int show_command) {
  ::SetUnhandledExceptionFilter(CrashHandler);

  // 诊断开关：只用于验证崩溃取证链路是否真的写出日志与 dump，
  // 正常启动不会走进来。
  if (command_line != nullptr &&
      ::wcsstr(command_line, L"--crash-test") != nullptr) {
    ::Sleep(1000);
    *reinterpret_cast<volatile int*>(nullptr) = 1;
  }

  // Attach to console when present (e.g., 'flutter run') or create a
  // new console when running with a debugger.
  if (!::AttachConsole(ATTACH_PARENT_PROCESS) && ::IsDebuggerPresent()) {
    CreateAndAttachConsole();
  }

  // Initialize COM, so that it is available for use in the library and/or
  // plugins.
  ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  flutter::DartProject project(L"data");

  std::vector<std::string> command_line_arguments =
      GetCommandLineArguments();

  project.set_dart_entrypoint_arguments(std::move(command_line_arguments));

  FlutterWindow window(project);
  Win32Window::Point origin(10, 10);
  Win32Window::Size size(1280, 800);
  if (!window.Create(L"AT32 手表助手", origin, size)) {
    return EXIT_FAILURE;
  }
  window.SetQuitOnClose(true);

  ::MSG msg;
  while (::GetMessage(&msg, nullptr, 0, 0)) {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }

  ::CoUninitialize();
  return EXIT_SUCCESS;
}
