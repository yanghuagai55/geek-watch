# 后端接入交接文档（Flutter 配套应用）

> 面向负责「把界面接上真机」的 agent / 开发者。
> 当前状态：**Windows 经典 SPP 已接入真机**（裸 RFCOMM + Dart FFI，通道 1），性能遥测、消息、歌词、亮度、播放态、抓屏均已跑通；BLE / 串口是明确报错的占位实现。
> 手表固件侧协议与 PC 侧参考实现都在仓库根 `/README.md` 与 `host/ble_tool.py`，**先读那两个**。

最后更新：2026-11-14 · 工作区改动（未提交）

---

## 1. 交付物与验收命令

| 项目 | 值 |
| --- | --- |
| 工程目录 | `app/`（Flutter 3.50 / Dart 3.14，SDK 约束 `^3.14.0`） |
| 目标平台 | Windows（已实测）、Android（可编译，未实机验证） |
| 产物 | `app/build/windows/x64/runner/Release/watch_companion.exe` |
| 依赖 | `material_ui ^1.1.0`（本 SDK 已把 `flutter/material.dart` 迁到这个包）、`prism_ui`（vendored 路径依赖） |

```bat
cd app
flutter pub get
flutter analyze --no-pub                     :: 必须 0 问题
flutter test  --no-pub                       :: 必须 3/3 通过
flutter build windows --release --no-pub     :: 约 25 秒
flutter run -d windows                       :: 开发时用，可看 print 与异常
```

> 注意：本 SDK 里 `package:flutter/material.dart` 已废弃，新增代码请 `import 'package:material_ui/material_ui.dart';`。

---

## 2. 目录结构与职责

```
app/lib/
├── main.dart                     入口；MaterialApp、主题切换、窗口标题栏明暗桥、崩溃日志、启动后自动连接
├── core/crash_log.dart           Dart 侧异常落盘 crash.log（runZonedGuarded + FlutterError + PlatformDispatcher）
├── services/
│   ├── watch_protocol.dart       帧编解码、UTF-8 截断、0x12 遥测解析、0x11 画面 RGB565→RGBA（纯 Dart）
│   ├── watch_transport.dart      传输接口、TransportType、LinkState、TransportException（纯 Dart）
│   ├── windows_rfcomm.dart       Windows 裸 RFCOMM（AF_BTH），接收跑在独立 isolate
│   ├── watch_session.dart        会话层：命令串行化、等 ok、1 Hz 遥测、抓屏、0x0B 点阵
│   ├── text_bitmap.dart          文字 → 128×32 单色点阵
│   ├── text_paginator.dart       长标题/歌词按宽度分页
│   ├── lrc_library.dart          按标题找同名 .lrc，返回当前行与时间窗口
│   ├── media_monitor.dart        Windows 媒体会话（SMTC）Dart 包装
│   ├── system_tray.dart          托盘开关（原生 MethodChannel）
│   └── snapshot_image.dart       WatchSnapshot → dart:ui.Image
├── core/app_settings.dart        %APPDATA%\AT32WatchCompanion\settings.json 持久化
├── state/watch_controller.dart   唯一全局状态（见 §3），ChangeNotifier，1 Hz ticker
├── theme/app_theme.dart          调色板 AppPalette、字体常量 AppFonts、明暗两套 ThemeData
├── pages/
│   ├── home_page.dart            主页：表盘卡 + 三张性能卡 + 快捷功能（含屏幕镜像对话框）
│   ├── messages_page.dart        消息：3 个槽 + UTF-8 字节计数 + 发送
│   ├── music_page.dart           音乐：播放器卡 + 进度条 + 歌词逐行同步 + 手表控制跟随
│   └── settings_page.dart        设置：链路、地址/串口/波特率、主题、亮度、熄屏、24h
└── widgets/
    ├── app_shell.dart            导航外壳：桌面 76→236px 折叠侧栏、竖屏 prism 液态玻璃底栏
    ├── common.dart               AppCard / PageHeader / StatusPill / 连接切换入口
    ├── watch_face.dart           表盘 CustomPainter（吃 now / use24Hour，仿真设备屏幕）
    ├── metric_card.dart          MetricCard（桌面）/ MiniMetricCard（竖屏）+ 迷你曲线
    └── companion_glyphs.dart     底栏图标（改自 FlClash app_glyphs.dart，GPL 来源见 NOTICE）

app/tool/spp_probe.dart           命令行探针：连接/遥测/消息/歌词/抓屏/0x0B 点阵自检
app/windows/runner/
├── media_monitor.cpp/.h          SMTC 后台轮询（MTA 线程），平台线程只读缓存
└── flutter_window.cpp            窗口主题、媒体、托盘三个 MethodChannel
```

**不要动的东西**：`app/packages/prism_ui/`（GPL 上游 vendored 源码，改动要记进 `THIRD_PARTY_NOTICES.md`）、`app/assets/fonts/`（见 §9）。

---

## 3. 状态模型 `WatchController`

`app/lib/state/watch_controller.dart`。`ChangeNotifier`，页面通过 `ListenableBuilder` 订阅。

| 字段 | 类型 | 现在谁在用 |
| --- | --- | --- |
| `session` | `WatchSession?`（构造注入） | 为空 = 纯界面预览（测试）；非空 = 真机链路 |
| `transport` | `TransportType{spp, ble, serial}` | 设置页分段按钮；`transportLabel`→`SPP/BLE/COM`，`transportName`→`经典 SPP/蓝牙 BLE/串口` |
| `deviceAddress` / `serialPort` / `baudRate` | `String` | 设置页输入框；切换 `transport` 时自动换地址预设（SPP `04:26:01:08:02:0C`、BLE `C4:26:01:08:02:0C`） |
| `linkState` | `LinkState{idle,connecting,connected,failed}` | 由 `session.states` 回填；`connected` / `connecting` / `connectionLabel` 都从它派生 |
| `previewConnected` | `bool` | 仅在没有 `session` 时作为预览开关（测试仍用） |
| `_liveTelemetry` | `bool` | 收到第一帧 0x12 后为真；主页标签 `实时` / `LIVE · 演示` |
| `ramUsed` / `ramTotal` / `cpu` / `fps` | `double/int` | 三张性能卡；真机值来自 0x12，断线后自动回落到演示正弦波 |
| `ramHistory` / `cpuHistory` / `fpsHistory` | `List<double>`（28 点） | 卡片里的迷你曲线 |
| `playing` / `lastControlAction` / `controlSeq` | `bool/int/int` | 手表 0x10 控制帧驱动音乐页状态 |
| `messageSlots` | `List<String>`（3） | 消息页三槽，内存态 |
| `themeMode` / `animations` / `liquidGlass` / `use24Hour` / `brightness` / `screenTimeout` / `autoConnect` / `autoSyncTime` | 混合 | 设置页与主题 |

业务方法：`connect()` / `disconnect()`、`sendMessage()`、`syncLyrics()`、`setPlayState()`、`applyBrightness()`、`syncTime()`、`requestSnapshot()`、`setMessageSlot()`。

**关键**：有 `session` 时，`_tick()` 只更新时间，不再伪造性能；0x12 到达后写入 `ramUsed/cpu/fps` 并 `_append(...)`。断线后 `_liveTelemetry=false`，演示数据自动恢复。`resetSettings()` 仍只重置 UI 状态。

---

## 4. 设备动作现状

### 4.1 已接真机

| 位置 | 动作 | 现在 | 协议 |
| --- | --- | --- | --- |
| 主页 Hero / 竖屏表盘卡 / 侧栏连接卡 | 连接 / 断开 | 真实建链；失败走 SnackBar，不卡界面 | 按 `transport` 建链 |
| 主页快捷功能「屏幕镜像」 | 抓屏 | 弹窗显示真机 128×160 画面（最近邻放大） | `06` 请求 → 收 `11` |
| 消息页「发送到手表」 | 写消息槽 | 成功/失败提示；按字符边界截到 23 字节 | `02` 槽号 + UTF-8 |
| 音乐页「同步到手表」 | 逐行歌词 + 播放态 | 每行一帧，间隔 350ms；返回成功行数 | `03` ≤27 字节、`04` |
| 音乐页播放/暂停 | 播放态 | 本地状态立即切换，同时下行 `04` | `04` |
| 设置页亮度滑杆 / 保存设置 | 亮度 | 松手与保存时写设备，0/1/2 三档 | `05` |
| 连接成功 | 自动校时 | 后台发送，不阻塞「已连接」提示 | `01` |
| 手表按键 | 反向控制 | `10` 帧直接调用 Windows 媒体 play/pause/next/prev（SMTC），同时更新音乐页状态 | `10` |
| 手表音乐页 | 未连接状态 | 4 秒收不到合法帧显示「无连接」；连接后显示 PC 下发的标题/歌词 | `0B` |
| 设置页「跟随系统媒体」 | Windows 媒体跟随 | SMTC 读标题/艺人/播放态；标题长时分页轮播，有 `.lrc` 时按时间轴推送 | `0B` + `04` |
| 设置页「关闭窗口后驻留托盘」 | 后台驻留 | 关闭窗口只隐藏，托盘双击恢复、右键退出 | 原生 `watch_companion/tray` |

### 4.2 仍是本地状态 / 尚未实现

- **消息槽持久化**：已搬进 `WatchController.messageSlots`，但仍只在内存，重启即丢。
- **播放器**：`晚风手记` 仍是写死的演示曲目；PC 端切歌只是 UI 演示，协议只有播放态。
- **BLE / 串口**：设置页保留入口，但 `WatchSession` 会返回明确错误。
- **表盘**：`WatchFace` 仍是按 `now` 画的仿真图，抓屏画面只在「屏幕镜像」弹窗里显示。
- **歌词来源**：SMTC 本身不提供歌词，需要把同名 `.lrc` 放到 `%APPDATA%\AT32WatchCompanion\lyrics`（设置页有「打开歌词目录」）。

---

## 5. 已实现架构与关键细节

```
services/
├── watch_protocol.dart      纯 Dart：buildWatchFrame / fitUtf8 / WatchFrameParser / WatchStats / WatchSnapshot
├── watch_transport.dart     纯 Dart：WatchTransport 接口、TransportType、LinkState、TransportException
├── windows_rfcomm.dart      Windows 裸 RFCOMM，接收循环在独立 isolate
├── watch_session.dart       会话层：命令串行化、等 ok、1 Hz 遥测、抓屏、日志流
└── snapshot_image.dart      WatchSnapshot → dart:ui.Image
```

`WatchSession` API：

```dart
Future<void> connect({required TransportType type, required String address, int baud = 115200});
Future<void> disconnect();
Future<bool> syncTime(DateTime now);          // 01
Future<bool> sendMessage(int slot, String t); // 02，按 UTF-8 字符边界截到 23 字节
Future<bool> sendLyric(String text);          // 03，≤27 字节
Future<bool> setPlayState(bool playing);      // 04
Future<bool> setBrightness(int level);        // 05，0/1/2
Future<WatchSnapshot?> requestSnapshot();     // 06 → 收 11
Stream<WatchStats> get stats;                 // 12
Stream<int> get controls;                     // 10
Stream<LinkState> get states;
Stream<String> get logs;
```

**SPP 实现要点（踩过坑，别重写错）**：

1. `SOCKADDR_BTH` 在 Windows 头文件里按 **1 字节对齐**，共 **30 字节**：family@0（u16）、btAddr@2（u64）、serviceClassId@10（16 字节全 0）、port@26（u32）。通道号写 1。
2. 地址字符串先去掉冒号解析成十六进制整数（`04260108020C`），再**小端序**写入偏移 2。不要按字符串顺序直接写。
3. 接收 **不要用 `SO_RCVTIMEO` + `WSAGetLastError` 判超时**：`WSAGetLastError` 是线程局部的，Dart isolate 的两次 FFI 调用可能落在不同 OS 线程，超时会被误读成 0 并断开链路。现在用 `select()`（20ms）+ 可读后再 `recv()`，控制流不依赖错误码。
4. isolate 只负责 `select/recv`；发送由主 isolate 直接写同一个 socket（帧很小），避免每帧一次端口往返。`closesocket` 也能从主 isolate 打断阻塞的接收。
5. 连接失败重试 3 次、间隔 400ms；HC-04 同一时刻只支持一个无线连接，重复快速连接偶尔需要等一下。

**0x0B 点阵（解决歌词方框）**：

- 固件字库只有约 500 字（subset 8.7KB；full 表也装不进 64KB Flash），任意歌词/视频标题必然缺字。
- App 用系统字体把文字栅格化成 128×32 1bpp（512B），按 4 块、每块 129B 下发；固件展开成 4bpp 覆盖位图，用 `ui_mask_draw` 贴图。收到第 0 块会自动打开音乐页。
- 长文本由 `paginateWatchText` 按 128px 分页，每页 ≤2 行；有 LRC 时按当前行到下一行的时间窗口分配每页时长（1.2–8s），没有时间轴时每 3 秒轮播。
- LRC 位置：`%APPDATA%\AT32WatchCompanion\lyrics\<媒体标题>.lrc`。

**Windows 媒体 / 托盘**：

- `media_monitor.cpp` 在独立 MTA 线程轮询 SMTC，`watch_companion/media.nowPlaying` 只返回缓存 map，不阻塞 UI。
- `watch_companion/tray` 控制 `setEnabled` / `hide` / `show`；开启后 `WM_CLOSE` 只隐藏窗口，托盘双击恢复，右键菜单“退出”才真正结束进程。
- 设置通过 `core/app_settings.dart` 防抖写入 `%APPDATA%\AT32WatchCompanion\settings.json`。

**UI 刷新要点**：`AppShell._buildContent` 里的页面必须在 `ListenableBuilder` 的 builder **内部**构造。如果页面 widget 实例在 builder 外创建，Element 的 `identical` 优化会直接复用旧 widget，controller 每秒通知也不会让页面重新 build（表现为「连上了但界面还是演示数据」）。

---

## 6. 协议速查（完整版见根 `README.md`）

```
帧 = A5 命令 长度 数据... 异或        异或从 A5 一直算到数据末尾
合法帧回 "ok"（ASCII），校验错回 "bad"
```

| 命令 | 方向 | 载荷 |
| --- | --- | --- |
| `01` 校时 | PC→表 | 年低 年高 月 日 时 分 秒 |
| `02` 消息 | PC→表 | 槽号 0..2 + UTF-8（≤23 字节，按字符边界截断） |
| `03` 歌词 | PC→表 | UTF-8（≤27 字节） |
| `04` 播放态 | PC→表 | `0` 暂停 / `1` 播放 |
| `05` 亮度 | PC→表 | `0`/`1`/`2` |
| `06` 要画面 | PC→表 | 无数据；下一帧回一张 128×160 RGB565（40,964 字节） |
| `07` 按键 | PC→表 | 位掩码 1=上一项 2=确定 4=下一项，**按下后必须发 `0` 松开** |
| `08` 调试 | PC→表 | `1` 开 / `0` 关（按键注入前置） |
| `09` 要性能 | PC→表 | 无数据；下一帧回 `12`，App 每 1 秒发一次 |
| `0A` 回主页 | PC→表 | 无数据 |
| `0B` 点阵 | PC→表 | 块号 0..3 + 128 字节 1bpp（128×32，共 512B）；收到块 0 自动打开音乐页 |
| `10` 控制 | 表→PC | 1 播放 / 2 暂停 / 3 下一首 / 4 上一首 |
| `11` 画面 | 表→PC | `A5 11 FF 长度3 宽2 高2 像素...`，长度 3 字节小端 |
| `12` 性能 | 表→PC | 8×u32；`[6]` 低 16=整片 SRAM 已用（字节）、高 16=CPU×10；`[7]` 低 16=SRAM 总量（字节）、高 16=完整帧 FPS |
| `13` 传感器 | 表→PC | 无数据，回一行 `DBG ...`，**不回 ok** |

> 两处历史不一致已按固件 `ui/src/link.c` 与真机实测确认：
> 1. `04` 播放态确实存在，`1` 播放 / `0` 暂停；
> 2. `10` 控制是「1 播放 / 2 暂停 / 3 下一首 / 4 上一首」，README 的「1 播放/暂停」是旧描述。

---

## 7. 平台差异

- **Windows 经典 SPP（已实现）**：系统分配的 COM 口打不开，走裸 RFCOMM socket（`AF_BTH` + `BTPROTO_RFCOMM`，通道 1），Dart 侧用 `dart:ffi` 调 `ws2_32.dll`。参考实现见 `host/winui/Services/SppTransport.cs` 与 `host/ble_tool.py`。命令行验证：`dart run tool/spp_probe.dart`。
- **Android**：暂未接入。BLE 需要 `flutter_blue_plus` 之类插件；经典 SPP 需要 `BluetoothSocket` + `BLUETOOTH_CONNECT` 运行时权限（Android 12+）。`WatchSession` 的工厂函数是唯一需要改的地方。
- **串口（未实现）**：CH340 直连 PA2/PA3，`115200 8N1`；Windows 需要 `serial_port` 类插件或再写一份 CreateFile/DCB 的 FFI，Android 需 USB Host + OTG。
- 三条链路共用同一套帧，`transport` 只影响建链方式。
- **HC-04 同一时刻只允许一个无线连接**：调试探针与 App 不要同时连；手机连着时电脑也连不上。

---

## 8. 尚未实现、但一定会被问到

| 项 | 现状 | 建议 |
| --- | --- | --- |
| 消息槽持久化 | 内存态 | 仿 `AppSettings` 写进 settings.json |
| 自动重连 | `autoConnect` 只控制启动时连一次 | 会话层加指数退避重连，UI 只读 `LinkState` |
| 断线提示 | 状态会回未连接；没有全局 SnackBar | 在 `WatchController` 监听 `failed` 后回调 UI |
| 日志页 | Flutter 版没有 | `WatchSession.logs` 已经可用，按 `[SPP 收] A5 12 ...` 格式展示即可 |
| 多设备 | 单一地址 | 保持单设备，别提前抽象 |
| 按键注入 / 回主页 | 会话层没暴露 | 需要时加 `pressKey()`（先 `08 1`，再 `07 mask`，90ms 后 `07 0`）与 `goHome()` |
| 自动歌词来源 | 仅同名 `.lrc` | 可接在线歌词 API 或音乐 App 的播放位置；SMTC 本身没有歌词字段 |

---

## 9. 约束与坑（踩过的）

1. **中文字体是子集**：`assets/fonts/AT32SerifSC-SemiBold.otf` 只含 411 个汉字。**新增 UI 文案如果用了没收录的字，会显示豆腐块**。把新字加进 `assets/fonts/subset_chars.txt` 后重跑：
   ```bat
   python -m fontTools.subset <原版思源宋体.otf> --text-file=subset_chars.txt --no-hinting --output-file=assets/fonts/AT32SerifSC-SemiBold.otf
   ```
   （字体已按 OFL 要求改名，别改回原名。）
2. **字体分工**：正文/标签 = 思源宋体（`AppFonts.han`），标题与大数字 = Playfair（`AppFonts.latin`）。新增文本不用写 `fontFamily`，跟随主题即可；只有"大数字"需要显式指定。
3. **许可证**：`app/` 整体 **GPL-3.0-or-later**（含 FlClash `prism_ui` 派生代码）。新增文件保留 GPL 头，改动 `prism_ui` 要更新 `THIRD_PARTY_NOTICES.md`。
4. **1 Hz 刷新是有意收窄的**：只有页面内容订阅 controller（`ListenableBuilder` 包在 `app_shell.dart` 的内容区）。**别把 `AnimatedBuilder` 加回根部** —— 之前整树每秒重建会把液态玻璃 shader 反复重建，曾触发引擎侧崩溃（见下条）。
5. **崩溃取证已内置**：闪退后 exe 同目录会留 `crash.log`（Dart 异常+堆栈）、`native_crash.log`（异常码/模块偏移）、`crash_*.dmp`。历史问题：`flutter_windows.dll` 引用计数崩溃（`0xc000041d` @ `0x102DA`），未能稳定复现；如果接入后复现，把这三个文件一起看。设置里可关「液态玻璃底栏」做 A/B。
6. **写测试**：`app/test/widget_test.dart` 会 mock `watch_companion/window_theme` 通道；`WatchController(startTicker: false)` 用来关掉 1 Hz ticker。新增带 Timer/长连接的 service 时，务必给测试留注入点，否则 `pumpAndSettle` 会超时。
7. **截图脚本**（验收用）：`SetProcessDpiAwareness(2)` 后再抓，且用 `GetClientRect` + `ClientToScreen` 只截客户区（窗口矩形会把 11px 的 DWM 边框带进来，边缘出现像残影的东西）。窗口用 `EnumWindows` + `GetWindowThreadProcessId` 按 pid 找，别用 `FindWindowW` 按标题找（会抓到别的窗口）。
8. **不要用 `WSAGetLastError` 判断 FFI 超时**：它是线程局部的，Dart isolate 的两次 FFI 调用可能跑在不同 OS 线程，超时会被读成 0。接收循环已改成 `select()` + 可读后 `recv()`。
9. **页面必须在 `ListenableBuilder` 的 builder 内构造**：widget 实例被复用时 Element 会跳过重建，表现为「链路明明连上了，界面还停在演示数据」。这个坑已修，改 `app_shell.dart` 时别再挪出去。
10. **`SOCKADDR_BTH` 是 30 字节紧凑布局**（1 字节对齐），不是 C 默认的 8 字节对齐；地址要先解析成整数再小端写入。
11. **C++/WinRT 在 VS 18 需要宏**：Windows SDK 的 C++/WinRT 头仍用实验性 coroutine，必须给 runner 加 `_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS`，否则编译直接报 STL1011。
12. **托盘关闭不是退出**：`WM_CLOSE` 在托盘启用时被拦截成 `SW_HIDE`；托盘菜单“退出”会先置 `force_exit_` 再发 `WM_CLOSE`。改这段时别把 `force_exit_` 逻辑删掉。
13. **音乐页未连接文案**：固件字库缺「备」「未」，所以写「设备未连接」会出方框；当前用「无连接」（无/连/接都在子集里）。要改回完整文案，需要重新生成字模表。

---

## 10. 验收清单

接完一段就过一遍，全绿再提交：

- [x] `flutter analyze --no-pub` → 0 问题
- [x] `flutter test --no-pub` → 20/20（3 个 widget + 6 个协议 + 5 个会话 + 6 个媒体/分页/控制通道）
- [x] `flutter build windows --release --no-pub` 通过，exe 能启动、不闪退
- [x] 未配对/链路被占用时点「连接设备」有明确失败提示，不卡死界面
- [x] 连上后：性能卡数值变成真机数据（不是正弦波）、标签变成「实时」
- [x] 发中文消息不出现半个汉字（`tool/spp_probe.dart` 真机发送「你好，手表」；23 字节截断有单测）
- [ ] 手表按键 → 电脑音乐卡状态跟着变（`10` 控制帧解析已有单测，需要人工按一下手表按键确认）
- [x] 断开后界面回到未连接态，重连可用
- [x] 抓屏：`06` → `11`，弹窗显示 128×160 RGB565 画面
- [x] 歌词/标题点阵：0x0B 真机显示中文，不再出现方框
- [x] 媒体跟随：SMTC 读取哔哩哔哩标题并下发到手表音乐页
- [x] 托盘：开启后关闭窗口进程存活、双击托盘恢复（已用 WM_CLOSE/托盘回调实测）
- [x] 新增中文文案无豆腐块（字体回退到 Microsoft YaHei UI）
- [x] `git status` 里没有 `build/`、`*.dmp`、`crash.log` 之类产物
