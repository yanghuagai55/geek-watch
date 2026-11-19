# AT32 手表 UI

![target](https://img.shields.io/badge/target-AT32F421K8T7-blue) ![ram](https://img.shields.io/badge/RAM-14.3%2F16KB-green) ![flash](https://img.shields.io/badge/flash-55.0%2F64KB-orange) ![fps](https://img.shields.io/badge/FPS-partial--refresh-brightgreen)

## 没有操作系统。存储一共 96 KB，比很多代码单文件都小。

能存东西的地方是片内 Flash 64 KB，加上外置 I2C 存储 32 KB。运行时内存再另算，SRAM 只有 16 KB。

在这么小的地方上跑一套纯 C99 的界面和动画，参考端口是 **AT32F421K8T7**。屏是 1.8 寸 ST7735S，128×160，RGB565。左右翻页，点进去放大，再点一层从右边盖上来。核心只依赖 `ui/hal.h`，换板子时重写这一层。

下面先看界面怎么动，再看这块表在 16 KB 内存里怎么把画面送出去，最后是构建、烧录和调试。画面都在 `media/`。可以交互着看的解说页是 [底层架构.html](底层架构.html)。

## 界面

五页围成一圈：表盘、运动、消息、设置、音乐。表盘长按可以换数字、指针、九轴和相册四种。另外四页各是一枚会动的圆角图标。确定键按下之后图标放大成整页，再进一层时新页面从右边盖上来，背后的列表缩小并压暗。右上角一直有电量。颜色是暖白、薄荷和琥珀，底是黑的。

表盘上只有冒号和秒针在动，其余行不送。

<div align="center">
<table>
<tr>
<td align="center"><img src="media/冒号呼吸.gif" width="200" alt="冒号"><br><b>冒号</b></td>
<td align="center"><img src="media/秒针.gif" width="200" alt="秒针"><br><b>秒针</b></td>
</tr>
</table>
</div>

四个主页图标各自循环。点进去就是整页。

<div align="center">
<table>
<tr>
<td align="center"><img src="media/跑步图标.gif" width="168" alt="跑步"><br><b>跑步</b></td>
<td align="center"><img src="media/信封开合.gif" width="168" alt="信封"><br><b>信封</b></td>
<td align="center"><img src="media/齿轮旋转.gif" width="168" alt="齿轮"><br><b>齿轮</b></td>
<td align="center"><img src="media/音符摆动.gif" width="168" alt="音符"><br><b>音符</b></td>
</tr>
<tr>
<td align="center"><img src="media/打开运动.gif" width="168" alt="进入运动"><br>进入运动</td>
<td align="center"><img src="media/打开消息.gif" width="168" alt="进入消息"><br>进入消息</td>
<td align="center"><img src="media/打开设置.gif" width="168" alt="进入设置"><br>进入设置</td>
<td align="center"><img src="media/打开音乐.gif" width="168" alt="进入音乐"><br>进入音乐</td>
</tr>
</table>
</div>

运动里有今日步数和两小时一根的柱子，滑过去是三维指南针，再进去是传感器。指南针的刻度和指针跟着九轴姿态转。消息正文是手机发来的点阵。设置里可以改抬腕、震动、亮度、熄屏，调试打开后底部多一行内存、CPU 和帧率。音乐页的柱子不分析音频，只在播放时起伏，用来添一点气氛。表盘上短按确定也可以播放或暂停。

<div align="center">
<table>
<tr>
<td align="center"><img src="media/3D指南针.gif" width="200" alt="3D指南针"><br><b>3D 指南针</b><br>航向、俯仰和横滚随手腕变化</td>
<td align="center"><img src="media/均衡器.gif" width="200" alt="音乐页柱状图"><br><b>音乐柱状图</b><br>播放时上下起伏</td>
</tr>
</table>
</div>

<div align="center">
<table>
<tr>
<td align="center"><img src="media/堆叠进入运动菜单.gif" width="168" alt="运动子页"><br>运动子页</td>
<td align="center"><img src="media/堆叠进入消息详情.gif" width="168" alt="消息详情"><br>消息详情</td>
<td align="center"><img src="media/堆叠进入亮度.gif" width="168" alt="亮度"><br>亮度</td>
<td align="center"><img src="media/堆叠进入关于.gif" width="168" alt="关于"><br>关于</td>
</tr>
</table>
</div>

## 特性

- **局部刷新**：屏幕切成 40 条、每条 4 行。变过的行整宽送出，没变的行不画也不传。静止后再补一次全屏，盖掉面板上偶发的写错。
- **异步 SPI DMA**：SPI1，15 或 30 MHz，DMA1 通道 3。完成要同时看到传输完成、发送空、总线不忙。空闲进 `WFI`。
- **可移植核心**：`ui/` 没有芯片头文件，没有 `malloc`，没有浮点。编译成 `libui.a` 就能换板。
- **动画**：滑动、淡入淡出、缩放、列表堆叠。同时最多 4 个属性在动。缓动是整数三次曲线。
- **图标**：矢量线段和圆盘，可以带颜色下标。跑步、信封、齿轮、音符有自己的循环。
- **字**：设置常用字留在片内。其余中文在外置字库里按需取。标题和表盘大数字用华文琥珀。消息正文由手机做成点阵再发下来。
- **调试角标**：一行盖在底部，显示整片 SRAM、CPU 和帧率，不把页面往上顶。
- **蓝牙帧**：`A5 命令 长度 数据 异或`。校时、消息、歌词、按键、亮度、截屏、性能、传感器都走这一条串口。
- **运动**：计步、抬腕、12 根柱子。三维指南针跟着九轴姿态转。传感器页显示加速度、陀螺、磁场和融合后的航向。
- **电源**：PA1 电池分压，右上角电量。背光 20 kHz PWM。熄屏 30 秒 / 1 分 / 2 分 / 5 分，按键或抬腕把背光拉回来。

## 硬件

| 项 | 说明 |
| --- | --- |
| MCU | AT32F421K8T7，LQFP-32，Cortex-M4，无硬件浮点，最高 120 MHz |
| 存储 | 片内 64 KB Flash、16 KB SRAM。代码区 55 KB，末尾 9 KB 是自定义表盘热槽 |
| 时钟 | 外部 8 MHz × 15 = 120 MHz。晶振失败则内部 HICK 48/2 × 5，仍是 120 MHz |
| 屏幕 | 1.8 寸 ST7735S，128×160。SCK PA5 / MOSI PA7 / RES PA4 / DC PA6 / 背光 PB0。模块 CS 内部接地 |
| 串口 | USART2，TX PA2 / RX PA3，115200 8N1，接蓝牙透传 |
| 按键 | PA11 上一项 / PA12 确定 / PA15 下一项。上拉，按下为低 |
| 电池 | PA1，ADC1_IN1。100 kΩ + 100 kΩ，读数 ×2 |
| I2C | PA9 SCL / PA10 SDA，I2C1 主机 400 kHz。MPU6050 `0x68`、QMC5883P `0x2C`、外置存储 `0x50`。这条总线和屏幕抢同一个 DMA 通道，所以 I2C 不用 DMA |
| 其他 | PB3 状态灯。PB1 蓝牙连接状态。PA8 驱动 1207 震动马达。PA0 麦克风座已接成模拟输入，当前固件不采集 |

最近一次实机构建（`ab8f254`）：代码 **56316 / 56320** 字节，55 KB 代码区只剩 4 字节。静态 RAM 仍按上一份链接地图计 **14656 / 16384** 字节，栈大约还剩 1.7 KB。调试角标里的 `R` 是整片 SRAM 加上当前栈。`at32/obj/` 里的 `watch.bin` 不进仓库，烧录文件单独上传。

窗口地址不要改成对称偏移。这块面板的起点不加偏移，列终点 +2，行终点 +1。窄列窗口会画成梳齿，所以脏区再小也按整行送。

## 底层

没有操作系统。`main` 里初始化板子和界面，然后一直调用 `ui_frame()`。一帧的预算是 16 ms。

一帧里的顺序是固定的：解析已经收到的串口字节，看按键边沿，处理熄屏和抬腕，推进动画，决定这一页可以占用哪一块 SRAM，把绘图命令录进一张最多 80 条的表，再只光栅化变过的横条。DMA 送出一条的时候，CPU 可以画下一条。应答 `ok` / `bad` 等画面送完才发出去，避免插进截屏字节。帧与帧之间如果串口是空的、屏也没在传，CPU 执行 `WFI`。这段空闲每次最多从外置存储读 64 字节，用来把当前页的绘制程序装进 SRAM。

全屏 RGB565 是 40960 字节，16 KB 内存放不下一帧缓冲。内存里只有两块 1024 字节的条带（128 × 4 × 2）。每一条绘图命令都有一个哈希和包围盒。和上一帧相比没变的命令不产生脏区。变了的话，旧位置和新位置都要重画，这样字挪走之后旧的那一块会被擦掉。

SRAM 里的动态池是 12608 字节，其中双缓冲占 2048 字节。主菜单图标、指针表盘、运动页、音乐页、三维指南针是放在外置存储里的 Thumb 小程序，走进哪一页才装进同一块连续内存。放大动画还没过 70/255 时，主页图标仍在画，这块内存先留给主菜单模块。关于页的彩图如果一次装不下，就按 4 行从外置存储流式贴。

外置 32 KB 的现况：

| 起始 | 长度 | 内容 |
| --- | --- | --- |
| `0x0000` | 256 B | 设置双槽 |
| `0x0100` | 512 B | 目录双槽，带 CRC |
| `0x0300` | 32000 B | 动态池。表盘和程序从低地址往上，消息和媒体从高地址往下 |

片内 `0x0800DC00` 起的 9 KB 是表盘热槽。自定义图放得下就写进这里，之后 CPU 直接读，不再每次走 I2C。

传感器在屏 DMA 空闲时采样，大约 50 Hz。计步和抬腕、九轴融合都是整数。融合用 Q24 四元数，没有软浮点。姿态轴在进界面之前已经扳成手表坐标：屏幕右是 +X，屏幕上是 +Y，屏幕外是 +Z。

## 目录

```
ui/                 可移植核心。hal.h 是唯一硬件边界，ui.h 是公共 API
at32/               AT32F421 端口：port.c、build.bat、AT32F421x8_FLASH.ld
  bsp/              I2C、MPU6050、QMC5883、外置存储、电池、灯、马达
  packs/            烧录用的 ArteryTek.AT32F421_DFP.2.2.0.pack
host/               PC 模拟和工具：build.bat、ble_tool.py、benchmark_device.py
                    gen_font.c、gen_clock.c、subset_font.py、pack_pic_mods.py、upload_asset.py
app/                Flutter 伴侣（Windows / Android）。GPL-3.0-or-later，不适用根目录 MIT
media/              实机截图、动图、PCB 渲染，README 和解说页共用
底层架构.html        可交互的底层解说页
```

## 固件构建

需要 `arm-none-eabi-gcc`。默认路径 `D:\dev\arm-none-eabi-gcc\bin`，可以用环境变量 `ARM_GCC_BIN` 改。

```bat
at32\build.bat
```

`at32\build.bat` 只有这一种固件：带 `AT32_SLIM_ROM`，设置常用字留在片内，运动、表盘、音乐、指南针的绘制放到外置模块。产物在 `at32/obj/`：`watch.elf`、`watch.bin`、`libui.a`。这个目录被忽略，不进仓库。

## 烧录

pyOCD + CMSIS-DAP（PWLINK2 / DAPLink 都可以）：

```bash
pyocd flash --pack at32/packs/ArteryTek.AT32F421_DFP.2.2.0.pack \
    -t _at32f421k8t7 -u <探针UID> at32/obj/watch.bin
```

官方设备包里的目标名带前导 `-`，pyOCD 会解析成 `_at32f421k8t7`。用这个带下划线的名字即可。

SWD 只接 PA13（SWDIO）、PA14（SWCLK）、GND、3V3。探针可以同时给板子供电。

## 调试工具

仓库里没有 WinUI 工程。电脑侧调试用 `host/ble_tool.py`：默认经典 SPP，地址 `04:26:01:08:02:0C`，通道 1。`--ble` 走 BLE（默认 `C4:26:01:08:02:0C`），`--port COM7` 走串口。`--mon-port` 可以另开一个口看模块的 UART。依赖是 `pyserial`、`bleak`、`winrt-Windows.Devices.Bluetooth`。

```bash
python host/ble_tool.py ping
python host/ble_tool.py time
python host/ble_tool.py msg 0 "HELLO WATCH"
python host/ble_tool.py music "DEMO LYRIC" 1
python host/ble_tool.py snap host/obj/shot.bmp
python host/ble_tool.py sensor

python host/ble_tool.py --port COM7 ping
python host/ble_tool.py --ble ping
```

抓屏一帧 40964 字节。经典 SPP 大约 4 秒，HC-04 的 BLE 大约 3.7 秒，CH340 有线大约 1 到 2 秒。HC-08 只有大约 151 字节/秒，不适合传画面。

`key 1|2|4` 会自动按下再松开。`longkey 2` 长按确定键约 600 ms，用来从子页返回。`unpair` 只对 BLE 有效。

### 协议

```
帧 = A5  命令  长度  数据...  异或
异或从 A5 算到数据末尾
合法帧回 ok，校验错回 bad。ok 在这一帧画面送完之后才发出
```

常用命令：

| 命令 | 方向 | 作用 |
| --- | --- | --- |
| `01` | 手机 → 表 | 校时。年小端、月、日、时、分、秒 |
| `03` `04` | 手机 → 表 | 歌词；播放态 0 暂停 / 1 播放 |
| `05` `08` | 手机 → 表 | 亮度 0/1/2；调试角标开关 |
| `06` | 手机 → 表 | 下一帧把整屏 RGB565 从串口送回 |
| `07` | 手机 → 表 | 按键电平。必须先按下再发 0 松开，才会形成边沿 |
| `09` `12` | 表 → 手机 | 要性能 / 性能包。含 SRAM 用量、CPU×10、帧率 |
| `0A` | 手机 → 表 | 关掉子页，回到表盘 |
| `0B` | 手机 → 表 | 曲名点阵，4 块 × 128 字节，128×32 |
| `10` | 表 → 手机 | 播放、暂停、下一首、上一首 |
| `11` | 表 → 手机 | 截屏像素 |
| `13` | 手机 → 表 | 传感器快照。回一行 `DBG ...`，不回 `ok` |
| `22`–`25` | 手机 → 表 | 消息列表点阵、详情点阵、清空、条数 |
| `28` `29` | 手机 → 表 | 外置图片的宽高格式，然后是分块数据 |
| `2F` | 手机 → 表 | 写入可执行模块或片外字库 |

`07` 的位是 1 上一项、2 确定、4 下一项。自己写脚本时不要只发按下：

```
A5 07 01 04 a7
A5 07 01 00 a3
```

## 在电脑上跑

需要 MSVC 的 `cl`，以及 Pillow（用来拼预览图）。字模表不入库，第一次要先生成：

```bat
host\build.bat regen
python host\subset_font.py
host\build.bat
host\watch_ui.exe
host\watch_ui.exe --check
```

窗口里左右翻页，回车进入，长按返回。`--check` 含脏区和全屏是否等价。

## 移植

1. 按 `ui/hal.h` 实现 tick、idle、时钟、串口、按键、`hal_display_flush` / `busy`、帧节拍。
2. 链接 `libui.a`，然后 `ui_init(); for (;;) ui_frame();`。
3. 不用自带的五页时，改为 `ui_core_init()`，循环里 `ui_draw_begin()`、绘制、`ui_draw_flush()`。

约束：C99，无 malloc，纯整数。`ui/` 不包含芯片头。图片和矢量数据要活到这一帧刷完。

## 测试

```bat
host\watch_ui.exe --check & host\watch_ui_240.exe --check
host\build.bat --odd & host\obj\watch_ui_odd.exe --check
cd host && python benchmark_device.py --port COM5
```

第三行是实机。`--odd` 用 128×163，用来确认条带高不能整除屏幕高度时仍然对齐。

## 伴侣应用

`app/` 编出来的 Windows 程序是 `app/build/windows/x64/runner/Release/watch_companion.exe`，窗口标题是「AT32 手表助手」。窗口够宽时用左侧导航，窄到竖屏比例时改成底部五个入口。下面两张是本机 Release 的界面，当时没有连上手表，所以遥测是演示数据。

<div align="center">
<table>
<tr>
<td align="center"><img src="media/伴侣_横屏.png" width="560" alt="伴侣应用宽窗口"><br><b>宽窗口</b><br>左侧导航，表盘和 3D 姿态并排</td>
<td align="center"><img src="media/伴侣_竖屏.png" width="220" alt="伴侣应用窄窗口"><br><b>窄窗口</b><br>底部导航，内容和手机竖屏同一套</td>
</tr>
</table>
</div>

消息这边已经修好，不再算作遗留：清空之后滞留的分块写不回旧通知，动态池空间不够时先丢掉可重拉的详情缓存，不再把消息行挤没；安卓通知监听被系统强停后会自己重绑。

## 已知问题

### 经典蓝牙不要走系统分配的 COM 口

Windows 给 SPP 分配的虚拟串口会超时或卡死。驱动和配对是好的，裸 RFCOMM 套接字连通道 1 可以通，ping 大约 81 ms。`host/ble_tool.py` 走的就是这条，不要自己去开系统分配的 COM 口。

HC-04 同一时刻只服务一个无线对端。手机连着的时候电脑连不上。先在系统蓝牙里断开手机。

推荐顺序：经典 SPP 套接字，然后 BLE，然后 CH340 有线（CH340 TX → PA3，RX → PA2，共地，115200）。

### BLE 能连上但通知起不来

Windows 缓存了旧的 GATT 句柄。断电和重启电脑蓝牙都没用。解除配对即可，不需要管理员：

```bash
python host/ble_tool.py --addr C4:26:01:08:02:0C unpair
```

### 按键点一下没反应

协议是边沿。只发按下不会再触发一次，必须再发一帧全 0 的松开。`ble_tool.py key 4` 已经包含按下和 90 ms 后的松开。

### 调试角标挡住页面点

角标是盖上去的一行，不参加排版。打开之后主页不会上移，底部的点会被挡住。`R` 是整片 SRAM，含当前栈。

## 许可与字体

两套许可证，互不覆盖。

| 目录 | 许可证 | 文件 |
| --- | --- | --- |
| `ui/`、`at32/`、`host/` | MIT | 仓库根目录 [LICENSE](LICENSE) |
| `app/` | **GPL-3.0-or-later，不是 MIT** | [app/LICENSE](app/LICENSE) |

根目录的 MIT 文本只适用于固件和 PC 工具。`app/` 使用了 FlClash 的 `prism_ui`，整份伴侣应用按 GPL-3.0-or-later 发布。分发 `app/` 的二进制时要同时提供这份目录的完整源码。说明见 [app/THIRD_PARTY_NOTICES.md](app/THIRD_PARTY_NOTICES.md)。

`ui_ease_in_out` 来自 LVGL 缓动的整数移植，LVGL 本身是 MIT。

字模表不入库。历史表从商业字体光栅而来，不能公开传。克隆之后用 `host/gen_font.c`、`host/gen_clock.c`、`host/subset_font.py` 在本地生成。ASCII 小字可以用仓库里的 `host/fonts/Inter-Medium.ttf`。若要入库，换站酷庆科黄油体、思源黑体或 Inter 重新光栅。
