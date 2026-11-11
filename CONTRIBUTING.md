# 贡献指南（好友协作版）

欢迎一起开发。请先读完本页再动手，尤其是**代码硬约束**、**烧录安全**和**字体版权**。

## 开发环境

- Windows + MSVC BuildTools（`cl`，用于 host 模拟/回归）。
- `arm-none-eabi-gcc`（STM32 固件；`ARM_GCC_BIN` 可覆盖路径）。
- OpenOCD + ST-Link（烧录）；Python 3 + Pillow（预览拼图/子集工具）。
- 国产 ST-Link 的 ~0.25V target-voltage 警告可忽略。

## 分支与提交

- 从 `master` 拉功能分支：`feat/xxx` / `fix/xxx`；完成后 PR 回 `master`。
- 提交信息用中文一行主题 + 必要正文，说明"为什么"。
- **验收通过的版本先提交，再开下一轮改动**；不要攒一大包未提交改动。
- 合并前必须跑完「测试」一节全部命令并全绿。

## 代码硬约束（违反会破坏可移植性/内存预算）

- C99；**无 malloc**；**纯整数**（无浮点，M3 无 FPU）。
- `ui/` 内**不得 include 任何芯片头文件**；硬件只走 `ui/hal.h`。
- SVG 命令表 / 像素缓冲必须 `static`（或静态存储），存活到该帧 flush 完。
- 布局用千分比 `UI_MX/UI_MY`，不写死像素；兼容 128×160 / 240×240 / 奇数高。
- 注释从简，只写"为什么"；中文注释可。
- 编译开 `-Wall -Wextra`（host `/W4`），不要留警告。

## 构建与测试

```bat
host\build.bat [--odd] [full] [nosong]
host\watch_ui.exe --check
host\watch_ui_240.exe --check
host\obj\watch_ui_odd.exe --check
cd host\obj\device-baseline && python test_port.py
stm32\build.bat [full|nosong]
cd host && python benchmark_device.py --port COM5   :: 实机
```

改渲染/动画后务必跑 `--check`（含脏条带与全量逐像素等价、settle 零传输）。
改端口后跑 `test_port.py`（寄存器 mock，不碰硬件）。

## 烧录安全（红线）

- **烧录前先备份** Flash（`stm32/backup/` 已有原始备份）。
- **绝不修改读保护 / 选项字节**。
- 只烧当前 STM32 目标；AT32 未到，`at32/` 不要动。
- 需要肉眼确认画面时**停下来叫维护者看**，再继续。

## 内存 / Flash 预算

- 可移植 RAM 目标 16KB，**留 4KB 余量**；栈 2KB；链接脚本 `stm32/stm32.ld` 有 assert。
- Flash 64KB；当前默认 ~40KB。加字模/表前先看 `stm32/obj/watch.size` / `watch.symbols`。
- 字模/点阵表是 Flash 大头（约六成），优先子集/按需，不要整表塞入。

## 字体工作流与版权（重要）

- **字模表不入库**（`.gitignore`）：`font_data.c` / `font_data_full.c` / `song_data.c`。
  历史表光栅自商业字体（华文琥珀/SimSun/Consolas），**禁止推上公开仓库**。
- **首次克隆自举**：`host\build.bat regen` 光栅完整表 → `python host\subset_font.py`
  生成默认子集。regen 需要本地字体；推荐改用下列开源字体（OFL，允许光栅再分发）：
  - 显示字（标题/时钟）：ZCOOL QingKe HuangYou（站酷庆科黄油体）。
  - 小字中文：Noto Sans SC / Source Han Sans。
  - 小字 ASCII：Inter（仓库已带 `host/fonts/Inter-Medium.ttf`）。
  生成器 `host/gen_font.c`（ASCII+CJK 点阵）与 `host/gen_clock.c`（显示字灰度）目前默认
  取系统 Consolas/SimSun/华文琥珀；换开源字体时改其字体名/路径参数即可。
  **用 OFL 字体光栅后的表可以安全入库**（取消 gitignore），方便协作者开箱构建。
- 变体：默认=子集+显示字；`full`=完整消息字模；`nosong`=丢显示字（`song_data_stub.c`
  + `song_draw.c` 自动回退小字模）。
- 不要整表塞入 Flash：字模是 Flash 大头，优先子集/按需；加表前看 `watch.size`。

## 加图标 / 动画

1. 按 `ui/ICON_SVG_PROMPT.md` 规格让外部 AI 产 C 数据（int8 坐标、ci 调色板、≤12 命令/帧）。
2. 放 `host/gemini_icons.h`，编译运行 `host/icon_preview.c` 出真实渲染预览验收。
3. 只集成认可部分；跑回归；烧录肉眼确认。
4. 彩色屏：图标至少两色，禁止全白；信封等要圆角、动画放慢、注意图层顺序。

## 目录约定

- 新硬件端口放 `<chip>/port.c`，实现 `ui/hal.h` 全部函数。
- 新图标/动画优先放 `ui/src/ui_icons.c`，接口进 `ui/ui_icons.h`。
- 预览/生成工具放 `host/`；产物一律进 `obj/`（已 gitignore）。