# STM32 手表 UI · 可移植动画核心

纯 C99 的嵌入式 UI / 动画核心 + 一个 STM32F103C8 参考端口，驱动 1.8" ST7735S
128×160 RGB565 彩屏。仿 Apple 手表风格：无界翻页、缩放进入、缓动动画、彩色矢量图标。
核心与硬件完全解耦（`ui/` 只依赖 `ui/hal.h`），可整包移植到其它 MCU / 屏幕。

![status](https://img.shields.io/badge/target-STM32F103C8-blue) ![ram](https://img.shields.io/badge/portable%20RAM-16KB-green) ![flash](https://img.shields.io/badge/flash-~40KB%2F64KB-orange)

## 特性

- **低内存局部刷新**：全宽 4 行脏条带 + 双缓冲 + 历史签名，静止时零传输、收敛自愈一帧。
- **异步 SPI DMA**（18MHz，DMA1 Ch3），完成门控 TC+TXE+!BSY；空闲 WFI， cooperative service hook。
- **可移植核心**：`ui/` 无芯片头文件、无 malloc、纯整数；编译为 `libui.a` 直接复用。
- **动画系统**：预设 slide/fade/zoom/stack + 可复用缓动 `ui_ease_in_out`（LVGL MIT 整数移植）。
- **彩色矢量图标**：`ui_svg_cmd_t`（线段+圆盘）+ 每命令调色板索引 `ci` + 粗线圆帽；
  跑步 / 圆角信封（开盖→信纸弹出落最顶层→倒放）/ 旋转齿轮。
- **字体按需打包**：消息字模默认子集（`full` 全量）；华文琥珀标题/时钟字可选（`nosong` 回退小字模）。
- **调试 overlay**：RAM / CPU / 右下角两位数帧数，宽度自适应。
- **蓝牙串口协议**：校时 / 消息 / 歌词 / 按键 / 截屏回传（A5 帧）。

## 目录

```
ui/            可移植核心（hal.h 为唯一硬件边界）
  ui.h         公共 API（分组 + 集成示例）
  hal.h        硬件抽象：每个板子写一份同名实现
  ui_icons.h   动画图标接口
  ICON_SVG_PROMPT.md  外部 AI 生成图标数据的格式规格
  src/         核心实现 + 字模表（font_data*.c / song_data*.c）
host/          PC 模拟器 / 回归 / 预览 / 字模生成与子集工具
stm32/         STM32F103C8 参考端口（port.c / startup / 链接脚本 / build.bat）
at32/          未来 AT32 端口预留（16KB SRAM 约束），暂不改动
tools/         OpenOCD 等（不入库）
```

## 快速开始（PC 模拟，无需硬件）

需要 MSVC BuildTools（`cl`）与 Python（Pillow 用于拼预览图）。

**首次克隆先自举字模**（字模表不入库，见下文「许可与字体」）：

```bat
host\build.bat regen          :: 光栅字模（需本地字体，推荐开源字体见下文「许可与字体」）
python host\subset_font.py    :: 生成默认子集（完整表留 font_data_full.c）
```

然后：

```bat
host\build.bat                 :: 构建 128x160 与 240x240 模拟器
host\watch_ui.exe              :: 交互窗口：左右翻页 / 回车进入 / 长按返回
host\watch_ui.exe --check      :: 行为 + 渲染回归（含脏/全量等价）
python host\preview.py         :: 浏览器实时预览（真实 C 帧缓冲）
host\watch_ui.exe --snapshot out.bmp r   :: 无头截屏（r=右翻一页）
```

## STM32 构建与烧录

需要 `arm-none-eabi-gcc`（默认 `D:\dev\arm-none-eabi-gcc\bin`，可用 `ARM_GCC_BIN` 覆盖）
与 OpenOCD + ST-Link。

```bat
stm32\build.bat                :: 默认：子集字模 + 华文琥珀   (~40KB/64KB)
stm32\build.bat full           :: 完整消息字模（手机中文全显示）
stm32\build.bat nosong         :: 丢华文琥珀，回退小字模      (~29KB/64KB)
```

烧录（**先备份、绝不改读保护/选项字节**）：

```bat
tools\openocd\...\bin\openocd.exe -s ...\scripts ^
  -f interface/stlink.cfg -f target/stm32f1x.cfg -c "transport select swd" ^
  -c "program stm32/obj/watch.elf verify reset exit"
```

产物：`stm32/obj/watch.elf/.bin` 与可复用归档 `stm32/obj/libui.a`。

## 移植到其它板子

1. 照 `ui/hal.h` 写一份端口（tick / idle / clock / link / key / display flush+busy / meter / pace）。
2. 链接 `libui.a`（或 `ui/src/*.c`），`ui_init(); for(;;) ui_frame();`。
3. 自定义页不用 demo：`ui_core_init();` 循环里 `ui_draw_begin(); … ui_draw_flush();`。
   示例见 `ui/ui.h` 顶部注释。屏幕尺寸用 `HAL_LCD_W/H`，布局用千分比 `UI_MX/UI_MY`。

约束：C99、无 malloc、纯整数、`ui/` 不 include 芯片头；SVG/像素缓冲须静态存活到帧刷完。

## 图标 / 动画协作流程

1. 用 [`ui/ICON_SVG_PROMPT.md`](ui/ICON_SVG_PROMPT.md) 的规格喂给任意强 AI 产出 C 数据。
2. 放进 `host/gemini_icons.h`，跑 `host/icon_preview.c` 用**真实渲染器**出预览图验收。
3. 只集成被认可的部分到 `ui/src/ui_app.c` / `ui_icons.c`，跑回归后烧录肉眼确认。

## 测试

```bat
host\watch_ui.exe --check & host\watch_ui_240.exe --check
host\build.bat --odd & host\obj\watch_ui_odd.exe --check   :: 128x163 奇数高
cd host\obj\device-baseline && python test_port.py         :: 端口寄存器 mock
cd host && python benchmark_device.py --port COM5          :: 实机性能/截屏
```

## 协作 / 两条路子

本仓库主线 = 可移植核心 `ui/` + STM32 参考端口。核心只认 `ui/hal.h`，可以各自 fork /
另开一路（例如好友的 ESP32），**不强制并入主线**；想共用界面/动画/图标时直接复用 `ui/`。

最小约定（保证可移植）：`ui/` 不 include 芯片头；SVG/像素缓冲静态存活到帧 flush；
布局用千分比 `UI_MX/UI_MY`。其余随意。预算：16KB 可移植 RAM 留 4KB、64KB Flash（链接脚本有 assert）。

## 许可与字体

- **代码：MIT**，见 [LICENSE](LICENSE)。`ui_ease_in_out` 为 LVGL（MIT）缓动的整数移植，已注明出处。
- **字模表不入库**：`ui/src/font_data.c` / `font_data_full.c` / `song_data.c` 已 gitignore。
  历史表光栅自商业字体（华文琥珀 STHupo、SimSun/Consolas），不可公开传播；克隆后按上面
  「首次克隆」自举生成。
- **推荐开源字体（自举/替换用，均允许光栅再分发）**：
  - 标题/时钟显示字：**ZCOOL QingKe HuangYou（站酷庆科黄油体，OFL）**——粗圆显示体，接近原艺术字观感。
  - 小字中文：**Noto Sans SC / Source Han Sans（OFL）**。
  - 小字 ASCII：**Inter（OFL）**，仓库已带 `host/fonts/Inter-Medium.ttf`。
  用这些字体重新光栅后，字模表即可安全入库（届时可取消 gitignore）。工具：
  `host/gen_font.c`（点阵）、`host/gen_clock.c`（显示字）、`host/subset_font.py`（子集）。

## 相关文档

- [动画交接.md](动画交接.md) · [本轮动画优化测试报告](本轮动画优化测试报告-2026-11-11.md)
- [ui/ICON_SVG_PROMPT.md](ui/ICON_SVG_PROMPT.md)