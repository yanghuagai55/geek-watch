# 后端接入交接文档（Flutter 配套应用）

> 面向负责「把界面接上真机」的 agent / 开发者。
> 当前状态：**纯 UI，零硬件连接**。所有设备动作都是占位提示，没有任何 service 层、没有持久化。
> 手表固件侧协议与 PC 侧参考实现都在仓库根 `/README.md` 与 `host/ble_tool.py`，**先读那两个**。

最后更新：2026-11-14 · 对应提交 `4f373b0`

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
├── main.dart                     入口；MaterialApp、主题切换、窗口标题栏明暗桥、崩溃日志安装
├── core/crash_log.dart           Dart 侧异常落盘 crash.log（runZonedGuarded + FlutterError + PlatformDispatcher）
├── state/watch_controller.dart   唯一全局状态（见 §3），ChangeNotifier，1 Hz ticker
├── theme/app_theme.dart          调色板 AppPalette、字体常量 AppFonts、明暗两套 ThemeData
├── pages/
│   ├── home_page.dart            主页：表盘卡 + 三张性能卡 + 快捷功能（桌面/竖屏两套布局）
│   ├── messages_page.dart        消息：3 个槽 + UTF-8 字节计数 + 发送
│   ├── music_page.dart           音乐：播放器卡 + 进度条 + 歌词编辑 + 同步
│   └── settings_page.dart        设置：链路、地址/串口/波特率、主题、亮度、熄屏、24h
└── widgets/
    ├── app_shell.dart            导航外壳：桌面 76→236px 折叠侧栏、竖屏 prism 液态玻璃底栏
    ├── common.dart               AppCard / PageHeader / StatusPill / AppBrand / showPrototypeMessage
    ├── watch_face.dart           表盘 CustomPainter（吃 now / use24Hour，仿真设备屏幕）
    ├── metric_card.dart          MetricCard（桌面）/ MiniMetricCard（竖屏）+ 迷你曲线
    └── companion_glyphs.dart     底栏图标（改自 FlClash app_glyphs.dart，GPL 来源见 NOTICE）
```

**不要动的东西**：`app/packages/prism_ui/`（GPL 上游 vendored 源码，改动要记进 `THIRD_PARTY_NOTICES.md`）、`app/assets/fonts/`（见 §9）。

---

## 3. 状态模型 `WatchController`

`app/lib/state/watch_controller.dart`（146 行）。`ChangeNotifier`，页面通过 `ListenableBuilder` 订阅。

| 字段 | 类型 | 现在谁在用 |
| --- | --- | --- |
| `transport` | `TransportType{spp, ble, serial}` | 设置页分段按钮；`transportLabel`→`SPP/BLE/COM`，`transportName`→`经典 SPP/蓝牙 BLE/串口` |
| `deviceAddress` / `serialPort` / `baudRate` | `String` | 设置页输入框；切换 `transport` 时自动换地址预设（SPP `04:26:01:08:02:0C`、BLE `C4:26:01:08:02:0C`） |
| `previewConnected` | `bool` | 侧栏连接卡、主页表盘卡、底部状态徽章。**目前纯 UI 开关** |
| `themeMode` | `ThemeMode` | 全应用主题（含 Win32 标题栏颜色，走 `watch_companion/window_theme` 通道） |
| `animations` | `bool` | 侧栏展开/收起是否走动画 |
| `liquidGlass` | `bool` | 竖屏底栏用 prism shader 还是退回 Material `NavigationBar` |
| `demoStreaming` | `bool` | 主页「LIVE · 演示 / 已暂停」按钮 |
| `ramUsed` / `ramTotal` / `cpu` / `fps` | `double/int` | 三张性能卡数值 |
| `ramHistory` / `cpuHistory` / `fpsHistory` | `List<double>`（28 点） | 卡片里的迷你曲线 |
| `brightness` / `screenTimeout` / `use24Hour` / `autoConnect` / `autoSyncTime` | 混合 | 设置页控件；`use24Hour` 还影响表盘绘制 |

**关键**：`_tick()` 每秒用正弦波伪造遥测，然后 `notifyListeners()`。接真机时把解析出来的 `12` 性能帧写进 `ramUsed/cpu/fps`，再 `_append(...)` 到三条 history 即可，UI 不用改。

`resetSettings()` 是"恢复默认"，接后端后仍只重置 UI 状态。

---

## 4. 待接入清单

### 4.1 占位按钮（`grep -rn showPrototypeMessage app/lib` 可全部列出）

| 位置 | 按钮 | 现在 | 应发出的协议 |
| --- | --- | --- | --- |
| `home_page.dart:180`（桌面 Hero） | 连接设备 | 弹「将在传输层接入后开放」 | 按 `transport` 建链 |
| `home_page.dart:387`（竖屏表盘卡） | 连接设备 | 同上 | 同上 |
| `home_page.dart:588`（快捷功能） | 屏幕镜像 | 同上 | `06` 要画面 → 收 `11` 画面（RGB565） |
| `messages_page.dart:139` | 发送到手表 | 同上 | `02` 槽号 + UTF-8（≤23 字节） |
| `music_page.dart:98` | 同步歌词 | 同上 | `03` 歌词（≤27 字节）+ `04` 播放态 |

### 4.2 还没长功能的交互

- **消息槽**：`_MessagesPageState._slots` 是三个硬编码字符串，**没有持久化、不在 controller 里**。接后端建议整体搬进 controller 或新的 `MessageStore`。
- **播放器**：`_playing` / `_position` 是页面本地 state，`晚风手记` 是写死的演示曲目；进度条只是 UI。反向控制（手表按键 → PC）要监听 `10` 控制帧。
- **设置页「保存设置」**：只弹提示，没有落盘（`shared_preferences` 也没接）。
- **表盘**：`WatchFace` 是按 `now` 画的仿真图，不是设备回传画面。

---

## 5. 建议架构（可直接照这个写）

新建 `app/lib/services/`，与 UI 保持单向依赖：

```
services/
├── watch_transport.dart      抽象接口 + 状态枚举 + 帧编解码器（纯 Dart，可单测）
├── spp_transport.dart        Windows/RFCOMM + Android BluetoothSocket
├── ble_transport.dart        BLE（Android 必做，Windows 可选）
├── serial_transport.dart     串口（CH340 直连 PA2/PA3）
└── watch_session.dart        会话层：持有 transport、解析帧、写回 controller、暴露业务方法
```

接口草案（**帧编解码一定要单独可测**，这是最容易出错的地方）：

```dart
abstract interface class WatchTransport {
  /// 建立链路；失败抛 TransportException，由会话层转成 UI 提示。
  Future<void> connect();

  Future<void> close();

  /// 已按 MARK 拆好、异或校验通过的帧：cmd + data。
  Stream<WatchFrame> get frames;

  /// 组帧发送并等待 "ok"；超时/校验失败返回 false。
  Future<bool> send(int cmd, List<int> data);

  ValueListenable<LinkState> get state; // idle/connecting/connected/failed
}
```

`WatchSession` 负责的业务方法（与 §6 命令一一对应）：

```dart
Future<bool> syncTime(DateTime now);          // 01
Future<bool> sendMessage(int slot, String t); // 02，按 UTF-8 字符边界截到 23 字节
Future<bool> sendLyric(String text);          // 03，≤27 字节
Future<bool> setPlayState(bool playing);      // 04
Future<bool> setBrightness(int level);        // 05，0/1/2
Future<bool> requestSnapshot();               // 06 → 收 11
Future<bool> pressKey(int mask);              // 07，必须先发 0 松开
Future<bool> goHome();                        // 0A
```

接进 UI 的最短路径：

1. `WatchController` 构造函数新增可选 `WatchSession? session`（测试里传 null，保持现在 3 个测试可跑）；
2. 把 `setPreviewConnected(bool)` 改造为「有 session 时走真连接，无 session 时保持现在行为」，或新增 `connect()/disconnect()` 并把 `previewConnected` 换成只读的 `connected`；
3. 页面里把 `showPrototypeMessage(...)` 换成 `await session.sendMessage(...)` + 失败提示；
4. 收到 `12` 帧时写 `controller.applyTelemetry(...)`（建议在 controller 上加这个方法，别让页面解析字节）。

错误提示沿用现有风格：`ScaffoldMessenger` + 浮动 SnackBar（`AppTheme` 里已配好 `snackBarTheme`），不要弹对话框。

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
| `0A` 回主页 | PC→表 | 无数据 |
| `10` 控制 | 表→PC | 1 播放 / 2 暂停 / 3 下一首 / 4 上一首 |
| `11` 画面 | 表→PC | `A5 11 FF 长度3 宽2 高2 像素...`，长度 3 字节小端 |
| `12` 性能 | 表→PC | 8×u32；`[6]` 低 16=整片 SRAM 已用、高 16=CPU×10；`[7]` 低 16=SRAM 总量、高 16=实测 FPS |
| `13` 传感器 | 表→PC | 无数据，回一行 `DBG ...`，**不回 ok** |

> ⚠️ 两处**不一致，接之前先实测确认**：
> 1. `04` 播放态在根 README 的协议表里没列，命令号来自 `host/ble_tool.py` 的 `CMD_STATE`；
> 2. `10` 控制的语义，README 写「1 播放/暂停 2 上一首 3 下一首」，`ble_tool.py` 的 `CTRL_NAMES` 是「1 播放 2 暂停 3 下一首 4 上一首」。以实测为准，写进代码前先跑一次。

---

## 7. 平台差异

- **Windows 经典 SPP**：系统分配的 COM 口打不开（5.2 秒超时 / 卡死），**必须用裸 RFCOMM socket**（`AF_BTH` + `BTPROTO_RFCOMM`，通道 1）。仓库里 `host/ble_tool.py` 与 `host/winui/` 都是这么做的，是实现参考。Dart 侧没有现成插件，需要 `dart:ffi`（Winsock2）或加一道原生 MethodChannel —— 现成的 channel 写法见 `app/windows/runner/flutter_window.cpp`（`watch_companion/window_theme`）。
- **Android**：BLE 用 `flutter_blue_plus` 之类；经典 SPP 用 `BluetoothSocket`（需 `BLUETOOTH_CONNECT` 运行时权限，Android 12+）。
- **串口（可选）**：CH340 直连 PA2/PA3，`115200 8N1`；Windows 用 `serial_port` 类插件，Android 需 USB Host + OTG。
- 三条链路共用同一套帧，`transport` 只影响建链方式。

---

## 8. 尚未实现、但一定会被问到

| 项 | 现状 | 建议 |
| --- | --- | --- |
| 设置持久化 | 内存态，重启即丢 | `shared_preferences`，在 `WatchController` 里加载/保存 |
| 自动重连 | `autoConnect` 只是个开关 | 会话层加指数退避重连，UI 只读 `LinkState` |
| 断线表现 | 没做 | 底部徽章变灰 + SnackBar「链路已断开」 |
| 日志页 | WinUI 调试台有，Flutter 版没有 | 若要，仿 `host/winui` 的 `[SPP 收] A5 12 ...` hex+ASCII 格式 |
| 多设备 | 单一地址 | 保持单设备，别提前抽象 |

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

---

## 10. 验收清单

接完一段就过一遍，全绿再提交：

- [ ] `flutter analyze --no-pub` → 0 问题
- [ ] `flutter test --no-pub` → 3/3（新增 service 的单测另计）
- [ ] `flutter build windows --release --no-pub` 通过，exe 能启动、不闪退
- [ ] 断网/未配对时点「连接设备」有明确失败提示，不卡死界面
- [ ] 连上后：性能卡数值变成真机数据（不是正弦波）、`LIVE · 演示` 标签改成「实时」
- [ ] 发一条中文消息（含汉字）不出现半个汉字，`02` 帧长度 ≤23
- [ ] 手表按键 → 电脑音乐卡状态跟着变（`10` 控制帧）
- [ ] 断开后界面回到未连接态，重连可用
- [ ] 新增中文文案无豆腐块（或已重跑字体子集化）
- [ ] `git status` 里没有 `build/`、`*.dmp`、`crash.log` 之类产物
