# AT32 手表 UI · 可移植动画核心

纯 C99 的嵌入式 UI / 动画核心 + 一个 **AT32F421K8T7** 参考端口，驱动 1.8" ST7735S
128×160 RGB565 彩屏。仿 Apple 手表风格：无界翻页、缩放进入、缓动动画、彩色矢量图标。
核心与硬件完全解耦（`ui/` 只依赖 `ui/hal.h`），可整包移植到其它 MCU / 屏幕。
PC 端提供 **WinUI 3 调试启动器**（`host/winui/`）和命令行 `host/ble_tool.py`。

![target](https://img.shields.io/badge/target-AT32F421K8T7-blue) ![ram](https://img.shields.io/badge/RAM-10.0%2F16KB-green) ![flash](https://img.shields.io/badge/flash-61.1%2F64KB-orange) ![fps](https://img.shields.io/badge/128%C3%97160-61fps-brightgreen)

## 特性

- **低内存局部刷新**：全宽 4 行脏条带 + 双缓冲 + 历史签名，静止时零传输、收敛自愈一帧。
- **异步 SPI DMA**（15MHz，DMA1 Ch3），完成门控 TC+TXE+!BSY；空闲 WFI，cooperative service hook。
- **可移植核心**：`ui/` 无芯片头文件、无 malloc、纯整数；编译为 `libui.a` 直接复用。
- **动画系统**：预设 slide/fade/zoom/stack + 可复用缓动 `ui_ease_in_out`（LVGL MIT 整数移植）。
- **彩色矢量图标**：`ui_svg_cmd_t`（线段+圆盘）+ 每命令调色板索引 `ci` + 粗线圆帽；
  跑步 / 圆角信封（开盖→信纸弹出落最顶层→倒放）/ 旋转齿轮。
- **字体按需打包**：消息字模默认子集（`full` 全量）；华文琥珀标题/时钟字可选（`nosong` 回退小字模）。
- **调试 overlay**：整片 SRAM / CPU / 帧数单行角标，覆盖式绘制，不推动任何页面布局。
- **蓝牙调试协议**：`A5 命令 长度 数据 异或`，校时 / 消息 / 歌词 / 按键 / 亮度 / 截屏回传 / 回主页 / 性能遥测 / 传感器快照。
- **WinUI 3 调试台**：默认经典 SPP 自动连接，连接后自动校时、回表盘主页并抓屏；RAM / CPU / FPS 1 Hz 环形仪表；BLE / SPP / 串口 / 演示四通道；抓屏保存 PNG、传感器快照、日志与设置。
- **运动与传感器**：计步、抬腕、运动柱状图；“传感器数值”页 10Hz 显示 MPU6050 加速度/陀螺/温度与 QMC5883L/P 三轴磁场，并每秒重扫 I2C。
- **电源与状态**：PA1 电池检测 + 右上角常驻电量图标；状态灯常亮/闪烁/关闭；熄屏 30s/1m/2m/5m，按键或抬腕唤醒。

## 硬件

| 项 | 说明 |
| --- | --- |
| MCU | AT32F421K8T7，LQFP-32，Cortex-M4 120MHz，64KB Flash / 16KB SRAM |
| 时钟 | 外部 8MHz 晶振 ×15 = 120MHz；晶振不起振时回退内部 HICK（48/2×5） |
| 屏幕 | 1.8" ST7735S，128×160，SPI1 + DMA1 通道 3，模块内部 CS 接地 |
| 接线 | SCK PA5 / MOSI PA7 / RES PA4 / DC PA6 / 背光 PB0 |
| 串口 | USART2，TX PA2 / RX PA3，115200 8N1（接 HC-04 / HC-08 透传） |
| 按键 | PA11 / PA12 / PA15，上拉输入，按下为低 |
| 电池检测 | PA1 / ADC1_IN1，R1=R2=100k 分压（×2），100nF 滤波 |
| I2C 传感器 | PA9/PA10：MPU6050 `0x68`、QMC5883P `0x2C`（兼容 QMC5883L `0x0D`）、BMP580 `0x46` |
| 状态灯 | PB3，高电平点亮；PB1 为 BLE 状态输入 |

实测：**61 fps**（16ms 帧节拍封顶），Flash **61.1/64 KB**，静态 RAM **10.0/16 KB**
（调试角标含当前栈，通常显示 `R9/16K`）。

## 目录

```
ui/            可移植核心（hal.h 为唯一硬件边界）
  ui.h         公共 API（分组 + 集成示例）
  hal.h        硬件抽象：每个板子写一份同名实现
  ui_icons.h   动画图标接口
  ICON_SVG_PROMPT.md  外部 AI 生成图标数据的格式规格
  src/         核心实现 + 字模表（font_data*.c / song_data*.c）
at32/          AT32F421K8T7 参考端口（port.c / build.bat / 链接脚本 / 启动文件）
host/          PC 模拟器 / 回归 / 预览 / 字模工具 / 蓝牙调试台
  winui/       WinUI 3 调试启动器（默认 SPP、自动校时、实时遥测）
docs/          开发日志 / 蓝牙调试记录
tools/         OpenOCD 等（不入库）
```

## 固件构建

需要 `arm-none-eabi-gcc`（默认 `D:\dev\arm-none-eabi-gcc\bin`，可用 `ARM_GCC_BIN` 覆盖）。

```bat
at32\build.bat                 :: 默认：子集字模 + 华文琥珀   (~61.1KB/64KB)
at32\build.bat nosong          :: 丢华文琥珀，回退小字模      (~45.5KB/64KB)
```

> `full`（完整消息字模）当前会超出 64KB 片内 Flash，需要外部字库或压缩后再用。

产物：`at32/obj/watch.elf` / `watch.bin`，以及可复用归档 `at32/obj/libui.a`。

## 烧录

用 pyOCD + CMSIS-DAP（PWLINK2 / DAPLink 均可）：

```bash
pyocd flash --pack at32/packs/ArteryTek.AT32F421_DFP.2.2.0.pack \
    -t _at32f421k8t7 -u <探针UID> at32/obj/watch.bin
```

> 官方 DFP 里 `Dname` 带前导 `-`，pyOCD 会把目标名解析成 `_at32f421k8t7`；
> 用这个带下划线的名字即可直接烧录，不需要重新打包。

SWD 只接 PA13(SWDIO) / PA14(SWCLK) / GND / 3V3，探针同时给板子供电。

## 调试工具（PC 端）

### WinUI 3 调试启动器（推荐）

`host/winui/At32Debugger.csproj`，需要 .NET 8 SDK 与 Windows App SDK。

```bat
cd host\winui
dotnet build At32Debugger.csproj -c Debug -p:Platform=x64
bin\x64\Debug\net8.0-windows10.0.19041.0\win-x64\At32Debugger.exe
```

- 启动默认经典 SPP 自动连接；连接后自动校时（`01`）、回表盘主页（`0A`）并抓屏显示在主页右侧。
- 主页实时遥测：RAM / CPU / FPS 环形仪表，1 Hz 从 `12` 性能帧读取；断开自动清空。
- 页面：主页 / 调试 / 屏幕 / 日志 / 设置 / 关于；支持 BLE、经典 SPP、串口和演示四种链路。
- 支持抓屏保存 PNG、传感器快照、消息 / 歌词、按键、亮度、原始命令、监听口、解除 BLE 配对。
- 无系统标题栏（Win11 圆角），标题栏与应用图标使用 `Assets/app.ico` / `app.png`。

### 命令行 ble_tool.py

`host/ble_tool.py`（三种链路 + 旁路 COM 监听）。

- **主链路**：默认经典 SPP（`04:26:01:08:02:0C` 通道 1）；`--ble` 走 BLE；`--port COM7` 走串口。
- **旁路监听口**：`--mon-port COM7` 额外打开一个 COM 口，用来观察模块 UART 或从 UART 发数据，和主链路互不影响。
- **日志**：`mon` 输出 `[SPP 收] 41 54 0D 0A  |AT..|` 这种 hex + ASCII 双格式，前缀标明通道和方向。
- 抓屏实测：**HC-04 BLE 3.7 秒/帧**（≈11KB/s），经典 SPP 约 **4 秒/帧**；HC-08 只有 151B/s，不建议。

```bash
# 经典 SPP（默认链路，实测最稳）
python host/ble_tool.py ping
python host/ble_tool.py time
python host/ble_tool.py msg 0 "HELLO WATCH"
python host/ble_tool.py music "DEMO LYRIC" 1
python host/ble_tool.py snap host/obj/shot.bmp
python host/ble_tool.py demo

# 串口：CH340 直连手表 PA2/PA3，或模块测试架
python host/ble_tool.py --port COM7 ping

# BLE
python host/ble_tool.py --ble ping

# 测试架：PC 走经典连模块，同时监听/发送模块 UART（COM7）
python host/ble_tool.py --mon-port COM7 mon
python host/ble_tool.py --crlf --mon-port COM7 comsend "456"
```

- 抓屏：128×160 RGB565，40,964 字节，经典 SPP 约 **3.8~4.5 秒/帧**。
- 消息：UTF-8 按字符截断到 23 字节，自动避免半个汉字。
- 音乐：`lyric` 歌词、`state 0|1` 播放态；手表按键会反向发 `0x10` 播放/暂停/上一首/下一首。
- 蓝牙按键注入只在调试模式生效，`key` / `longkey` 会自动先发 `debug 1`；`longkey 2` 用于从子页返回。

### 协议

```
帧 = A5 命令 长度 数据... 异或        异或从 A5 算到数据末尾
合法帧回 "ok"，校验错回 "bad"

01 校时   年低 年高 月 日 时 分 秒
02 消息   槽号 0..2 + UTF-8
03 歌词   UTF-8
05 亮度   0/1/2
06 要画面 无数据，下一帧回一张 128×160 RGB565
07 按键   位掩码 1=上一项 2=确定 4=下一项（需边沿：按下后必须发 0 松开）
08 调试   1 开 0 关
0A 回主页 无数据，关闭子页/菜单并回到表盘
10 控制   手表→PC：1 播放/暂停 2 上一首 3 下一首
11 画面   A5 11 FF 长度3 宽2 高2 像素... 异或
12 性能   手表→PC：8×u32；[6] 低16=整片 SRAM 已用、高16=CPU×10，[7] 低16=SRAM 总量、高16=实测 FPS
13 传感器 无数据，回一行 DBG ...（I2C 扫描 / QMC 寄存器 / 电池），不回 ok
```

## 快速开始（PC 模拟，无需硬件）

需要 MSVC BuildTools（`cl`）与 Python（Pillow 用于拼预览图）。

**首次克隆先自举字模**（字模表不入库，见「许可与字体」）：

```bat
host\build.bat regen          :: 光栅字模（需本地字体）
python host\subset_font.py    :: 生成默认子集（完整表留 font_data_full.c）
```

然后：

```bat
host\build.bat                 :: 构建 128x160 与 240x240 模拟器
host\watch_ui.exe              :: 交互窗口：左右翻页 / 回车进入 / 长按返回
host\watch_ui.exe --check      :: 行为 + 渲染回归（含脏/全量等价）
host\watch_ui.exe --snapshot out.bmp r   :: 无头截屏（r=右翻一页）
```

## 移植到其它板子

1. 照 `ui/hal.h` 写一份端口（tick / idle / clock / link / key / display flush+busy / meter / pace）。
2. 链接 `libui.a`（或 `ui/src/*.c`），`ui_init(); for(;;) ui_frame();`。
3. 自定义页不用 demo：`ui_core_init();` 循环里 `ui_draw_begin(); … ui_draw_flush();`。
   示例见 `ui/ui.h` 顶部注释。屏幕尺寸用 `HAL_LCD_W/H`，布局用千分比 `UI_MX/UI_MY`。

约束：C99、无 malloc、纯整数、`ui/` 不 include 芯片头；SVG/像素缓冲须静态存活到帧刷完。

## 图标 / 动画协作流程

1. 用 `ui/ICON_SVG_PROMPT.md` 的规格喂给任意强 AI 产出 C 数据。
2. 放进 `host/gemini_icons.h`，跑 `host/icon_preview.c` 用**真实渲染器**出预览图验收。
3. 只集成被认可的部分到 `ui/src/ui_app.c` / `ui_icons.c`，跑回归后烧录肉眼确认。

## 测试

```bat
host\watch_ui.exe --check & host\watch_ui_240.exe --check
host\build.bat --odd & host\obj\watch_ui_odd.exe --check   :: 128x163 奇数高
cd host && python benchmark_device.py --port COM5          :: 实机性能/截屏
```

## 已知问题与绕开办法

### 1. HC-04 的经典蓝牙 SPP 串口打不开（COM 端口 5 秒超时）

现象：`COM8` 打开 5.2 秒后报「信号灯超时时间已到」，`COM9` 直接卡死不返回。

实测排查（2026-11-13，Intel 无线网卡 + Win11 + HC-04D）：

| 检查项 | 结果 |
| --- | --- |
| 设备管理器 `Bluetooth Device (RFCOMM Protocol TDI)` | Status=OK，ProblemCode=0（驱动正常） |
| 配对记录 | 经典（`04260108020c`）与 BLE（`c4260108020c`）各一条，均在 |
| WinRT 强制 RFCOMM 连接 | `connection_status` 从 0 变 1，链路能建立 |
| 重启蓝牙无线电（`Radio.set_state_async`） | 无效 |
| 打开 SPP COM 口 | 仍然 5.2s 超时 / 卡死 |

结论：**这不是驱动坏了，是 SPP 通道本身不通**。HC-04 是双模模块，实测 BLE 与 SPP
不能同时稳定工作；而 HC-PC 直接建 socket 会抛 `SocketException 0x80004005
提供了一个无效的参数`（InTheHand 绕过系统配对），所以也别用它当桥。

**绕开办法（按推荐顺序）**

1. **走 BLE**：`python host/ble_tool.py --ble ping`，WinRT 直连，抓屏 3.7 秒/帧。
2. **走 CH340 有线**：`CH340 TX→PA3`、`CH340 RX→PA2`、`GND`，
   `python host/ble_tool.py --port COMx ping`（115200），抓屏 1~2 秒/帧，不受蓝牙任何问题影响。
3. 不要试图在「设置」里反复配对 —— 配对记录已经是最新的，问题不在这。

### 2. BLE 能连上但收不到数据（`start notify ... Unreachable`）

Windows 缓存了旧的 GATT 句柄（句柄号常见为 `0035`）。

**真正有效的做法：解除系统配对 —— 不需要管理员权限**

```bash
python host/ble_tool.py --addr C4:26:01:08:02:0C unpair
```

实测对比（2026-11-13）：

| 做法 | 结果 |
| --- | --- |
| 板子断电 3 秒再上电 | ❌ 仍然 `Unreachable` |
| 重启蓝牙无线电（`Radio.set_state_async` OFF→ON） | ❌ 无效 |
| **`unpair_async()` 解除 BLE 配对** | ✅ **立刻恢复，ping 108 ms** |
| 管理员删注册表 bond | ✅ 有效，但需要提权，现在没必要 |

旧 GUI 已移除；WinUI 启动器启动时会自动连接一次，遇到 `Unreachable` 仍先执行上面的 `unpair`。

仍然不行再按顺序兜底：断电重启 → 关开蓝牙 → 管理员删 bond：

```powershell
reg delete "HKLM\SYSTEM\CurrentControlSet\Services\BTHPORT\Parameters\Devices\c4260108020c" /f
```

最后兜底：换 CH340 有线。

### 3. 按键点一下没反应

协议里的按键是**边沿触发**：只发「按下」不生效第二次，必须先按下再松开。

```
A5 07 01 04 a7     ← 按下“下一项”
A5 07 01 00 a3     ← 90ms 后松开
```

`host/ble_tool.py key 4`（或 `longkey 2`）已经按这个实现（按下 + 90ms 松开）；
自己写脚本时别只发按下那一帧。

### 4. 调试角标会遮住页面点

角标是**覆盖式**单行条（`R11/16K C0% 60`），压在屏幕最底部，不参与布局计算，
所以 0 级界面不会因为开调试而整体上移；代价是调试模式下面点数被它挡住。
`R` 是整片 SRAM 已用/总量（含当前栈），不再是 UI 内存池的 6.6KB。

### 5. BLE 能连上但收不到 UART 数据（经典 SPP 已通）

现象：`ble_tool.py ping` 能连上 HC-04BLE，`start_notify` 成功，但收不到 `ok`；
`mon` 监听时复位手表也收不到 `boot`。

实测（2026-11-13）：

| 检查 | 结果 |
| --- | --- |
| 固件 USART2 115200 | 无数据 |
| 固件 USART2 9600 | 无数据 |
| `unpair` 后重连 | 无数据 |
| 复位时 `mon` | 无数据 |
| SWD 读 `g_sensor_ok` / `g_i2c_found` | 正常，手表在跑 |
| **经典 SPP（`--spp`）** | **ping / 校时 / 消息 / 音乐 / 抓屏 / 传感器全部通过** |

结论：**经典 SPP 链路已经可用**，优先用 `--spp 04:26:01:08:02:0C`；
BLE 侧仍然收不到 UART 数据，问题在模块 UART→FFE1 的转发/接线，后续再查。
SPP 抓屏 40,964 字节约 4 秒，消息页、音乐页均已用实机截图确认。

## 存储扩展（待接）

- 计划：**AT24C256**（32KB，I2C，挂 PA9/PA10，地址 `0x50~0x57`），模块未到货。
- 优先存：设置持久化、指南针校准、步数历史、日志；I2C 400kHz 约 40KB/s，不适合每帧读字库。
- 当前链接进固件的字库约 **24KB**（`song_data` 15.3KB + `font_data` 8.7KB）。
  32KB EEPROM 未压缩基本会被字库占满，设置/日志空间很少；字库仍需压缩或等 SPI NOR。
- 可选压缩：逐字形 RLE（省 5~9KB）或 heatshrink 分块（省 7~12KB），需要块索引/缓存。

## 许可与字体

- **代码：MIT**，见 [LICENSE](LICENSE)。`ui_ease_in_out` 为 LVGL（MIT）缓动的整数移植，已注明出处。
- **字模表不入库**：`ui/src/font_data.c` / `font_data_full.c` / `song_data.c` 已 gitignore。
  历史表光栅自商业字体（华文琥珀 STHupo、SimSun/Consolas），不可公开传播；克隆后按上文自举生成。
- **推荐开源字体（自举/替换用，均允许光栅再分发）**：
  - 标题/时钟显示字：**ZCOOL QingKe HuangYou（站酷庆科黄油体，OFL）**。
  - 小字中文：**Noto Sans SC / Source Han Sans（OFL）**。
  - 小字 ASCII：**Inter（OFL）**，仓库已带 `host/fonts/Inter-Medium.ttf`。
  用这些字体重新光栅后，字模表即可安全入库。工具：
  `host/gen_font.c`（点阵）、`host/gen_clock.c`（显示字）、`host/subset_font.py`（子集）。

## 相关文档

- 开发日志：`docs/开发日志-2026-11-13.md`
- 图标格式：`ui/ICON_SVG_PROMPT.md`
