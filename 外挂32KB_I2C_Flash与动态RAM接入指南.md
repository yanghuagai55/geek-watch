# 外挂 32 KB I2C Flash 与动态 RAM 接入执行指南

本文档记录了手表底层**动态 RAM 块分配器（`ui_mem`）**与**外挂 32 KB I2C Flash / EEPROM / FRAM（`ext_flash`）**的硬件接线、内存分区、蓝牙通信协议及二次开发接口。后续焊接或接入 32 KB I2C 存储芯片后，无需修改底层驱动即可即插即用。

---

## 一、硬件接入与兼容芯片

### 1. 推荐兼容芯片（256 Kbit = 32 KB，16 位寻址）
| 型号 | 类型 | 页大小 | 写入等待时间 | 备注 |
| :--- | :--- | :--- | :--- | :--- |
| **AT24C256 / BL24C256 / CAT24C256** | I2C EEPROM | 64 B | 约 2~5 ms（驱动已内置有界 ACK 轮询） | 最常见、成本极低 |
| **FM24C256 /FM24CL256 / MB85RC256V** | I2C FRAM（铁电） | 无页限制（兼容 64 B 切分） | 0 ms（瞬时 ACK） | 擦写寿命极高、无写入延迟，高速传图首选 |

### 2. 引脚连接（挂载于现有软件 I2C 总线）
芯片直接并联在主控现有的传感器 I2C 总线上（定义见 `at32/bsp/board_config.h`）：
- **VCC**：`3.3V`
- **GND**：`GND`
- **SCL**：接 `PA9`（与 MPU6050 / QMC5883L / BMP580 共用，已有外部上拉）
- **SDA**：接 `PA10`（同上）
- **WP（写保护）**：**务必接 `GND`**（若悬空或接高电平会导致无法写入）
- **A0 / A1 / A2（器件地址脚）**：建议全部接 `GND`（对应默认 7 位地址 `0x50`）。驱动 `ext_flash_init()` 会优先探测 `0x50`，若未命中会自动扫描 `0x51 .. 0x57`，因此任意地址跳线均可自动识别。

### 3. 上电与热插拔自检验证
1. **无需重编固件**：底层驱动已编译进固件，未接芯片时自动降级为纯动态 RAM 模式；接上芯片后自动启用 32 KB 持久化缓存与流式读取。
2. **手表端直看**：进入 **运动 -> 传感器数值** 页面，底部 `I2C` 扫描行每隔 1 秒刷新一次，接好芯片后会直接显示出 **`50`**（如 `I2C 0D 50 68`）。
3. **串口开机日志**：USART2（`115200`）开机自检会打印：
   - 检测到芯片：`i2c: ... 50 ...` 及 `ext OK`
   - 未接芯片：`ext --`

---

## 二、动态 RAM 机制（`ui_mem`）说明

### 1. 分配器原理（`ui/src/ui_mem.c`）
- 采用**零带内头部开销（Zero In-Band Overhead）**的外置有序区间表 `blks[32]` 管理静态池 `UI_RAM_BUDGET`（`128x160` 下为 `9,472 B`）。
- 移除了 `ui/src/link.c` 中原本常驻占用 `.bss` 的 `2,560 B` 静态位图缓冲 `mask_buf`，将其并入 `UI_RAM_BUDGET` 动态池：
  - **表盘 / 运动 / 设置页**：位图缓冲占用为 **`0 B`**，空余约 **`2,744 B`** 连续动态内存供页面或新功能自由调配。
  - **音乐页打开时**：按需动态申请 **`2,048 B`**（`DYN_MEDIA`，128×32 4bpp 曲名点阵），离开音乐页或切换纯文本歌词时立即调用 `ui_mem_free()` 归还。
  - **消息列表页打开时**：按需动态申请 **`1,792 B`**（`DYN_MSG_LIST`，128×112 1bpp 列表点阵）。
  - **消息详情页打开时**：自动释放列表缓冲并申请 **`2,560 B`**（`DYN_MSG_DETAIL`，128×160 1bpp 详情点阵），退回表盘时彻底释放。

### 2. 动态内存 API（`ui/ui.h`）
```c
/* 申请 size 字节并按 align（2 的幂，如 1/2/4/8）对齐，返回前自动清零；空间不足返回 NULL */
void *ui_mem_alloc(uint32_t size, uint32_t align);

/* 释放由 ui_mem_alloc 分配的内存块，相邻空闲块自动合并（传入 NULL 安全无操作） */
void ui_mem_free(void *ptr);

/* 查询当前动态池中最大连续可分配的空闲字节数 */
uint32_t ui_mem_avail(void);

/* 查询当前已分配字节数与总预算字节数 */
void ui_mem_get(uint32_t *used_bytes, uint32_t *total_bytes);
```

---

## 三、32 KB 外置 I2C Flash 分区表（`0x0000 .. 0x7FFF`）

外置存储总容量 `32,768 B`（`HAL_EXT_FLASH_SIZE`），在 `ui/ui.h` 与 `ui/src/link.c` 中规划如下：

| 起始偏移 | 结束偏移 | 容量 | 宏定义 | 用途说明 |
| :--- | :--- | :--- | :--- | :--- |
| `0x0000` | `0x00FF` | `256 B` | 保留元信息区 | 预留给后续自定义配置、表盘索引或目录头 |
| `0x0100` | `0x02FF` | `512 B` | `LINK_EXT_OFF_MEDIA` | **媒体曲名点阵缓存**（128×32 1bpp 原始点阵，重进音乐页秒恢复） |
| `0x0300` | `0x09FF` | `1,792 B` | `LINK_EXT_OFF_MSG_LIST` | **消息列表点阵缓存**（128×112 1bpp，从详情退回列表时瞬时恢复，不闪白屏） |
| `0x0A00` | `0x3BFF` | `12,800 B` | `LINK_EXT_OFF_MSG_DETAIL` | **5 条消息详情独立槽位缓存**（Slot `0..4`，每槽 `2,560 B` = 128×160 1bpp） |
| `0x3C00` | `0x7FFF` | `17,408 B` | `LINK_EXT_OFF_IMG` | **图片预览与扩展资源区**（支持 1bpp / 4bpp 灰度 / RGB565 彩图流式渲染） |

### 多消息详情槽位地址计算
每条消息详情占 `2,560 B`（`0x0A00` 字节）：
- **消息 0**：`0x0A00 .. 0x13FF`
- **消息 1**：`0x1400 .. 0x1DFF`
- **消息 2**：`0x1E00 .. 0x27FF`
- **消息 3**：`0x2800 .. 0x31FF`
- **消息 4**：`0x3200 .. 0x3BFF`

---

## 四、蓝牙/串口协议新增与扩展指令

所有帧格式保持不变：`A5 [CMD] [LEN] [PAYLOAD...] [XOR]`（`XOR` 为从 `A5` 到 `PAYLOAD` 末尾的异或校验和）。

### 1. 指定槽位的消息详情下发（`0x23` 扩展模式）
除原有的 `[chunk(0..19), 128B_data]`（共 129 字节负载，写入当前打开的槽位）外，`0x23` 新增支持**带槽位号后台预存模式**（共 130 字节负载）：
- **CMD**：`0x23`（`LINK_MSG_DETAIL_BMP`）
- **LEN**：`130`（`0x82`）
- **PAYLOAD**：`[slot(0..4), chunk(0..19), 128B_data]`
- **效果**：手机 App 可在后台一次性把 5 条消息的正文详情全部推进外置 32 KB Flash 的 `0..4` 槽位中；用户在手表上点开任意一条消息时直接从外置 Flash 秒开，即使断开蓝牙也能完整翻阅 5 条消息详情。

### 2. 图片预览下发协议（`0x28` 元信息 + `0x29` 分块数据 / `0x26` RLE）

#### (1) 步骤一：发送图片元信息（`0x28` `LINK_EXT_IMG_META`）
- **CMD**：`0x28`
- **LEN**：`5`
- **PAYLOAD**：`[w_lo, w_hi, h_lo, h_hi, fmt]`
  - `w`：图片宽度（小端 `uint16`，如 `128` -> `80 00`）
  - `h`：图片高度（小端 `uint16`，如 `160` -> `A0 00`）
  - `fmt`：像素格式
    - `1`（`UI_EXT_FMT_1BPP`）：1bpp 单色点阵，满屏 `128×160` 仅需 `2,560 B`
    - `2`（`UI_EXT_FMT_4BPP`）：4bpp 16 级灰度/透明度位图，满屏 `128×160` 仅需 `10,240 B`
    - `3`（`UI_EXT_FMT_RGB565`）：16bpp RGB565 彩图（小端字节序），`17,408 B` 分区最高可存 `128×68`（`17,408 B`）或 `92×92`（`16,928 B`）真彩图

#### (2) 步骤二：分块发送图片数据（`0x29` `LINK_EXT_IMG_DATA`）
- **CMD**：`0x29`
- **LEN**：`1 + N`（每块 `N <= 128` 字节，除最后一块外固定 `N = 128`）
- **PAYLOAD**：`[chunk_index(0..135), data_bytes...]`
- **也支持 RLE 压缩发送（`0x26` `LINK_MASK_RLE`）**：
  - `PAYLOAD`：`[0x04(type=图片), chunk_index, (val, count)...]`，解压满 128 字节后自动写入对应图片块。
- **自动显示**：当最后一块写入完成时，`link_img_preview_ready()` 自动置 `1` 并触发画面刷新；在消息详情页中会自动居中渲染该预览图（发送 `0x24` 清空或调用 `link_img_preview_clear()` 可退出图片预览）。

---

## 五、底层 HAL 与流式渲染 API 速查

### 1. 外置 32 KB Flash HAL 接口（`ui/hal.h`）
```c
#define HAL_EXT_FLASH_SIZE (32u * 1024u)

int hal_ext_flash_present(void);
int hal_ext_flash_read(uint32_t offset, uint8_t *dst, uint32_t len);
int hal_ext_flash_write(uint32_t offset, const uint8_t *src, uint32_t len);
int hal_ext_flash_erase(uint32_t offset, uint32_t len);
```
> **注意**：`ext_flash_write()` 内部已自动处理 64 字节页边界切分（跨页写入不会发生页内回卷覆盖），并内置有界 ACK 轮询。

### 2. 免全屏 RAM 的按带流式贴图接口（`ui/ui.h`）
```c
/* 直接从外置 32KB Flash 的 ext_offset 处按 4 行分带流式读取并渲染到屏幕 (x, y) */
void ui_ext_image_draw(int16_t x, int16_t y, int16_t w, int16_t h,
                       uint32_t ext_offset, uint8_t fmt,
                       uint16_t color, uint8_t opacity);
```
- **核心优势**：渲染器每刷一个 4 行分带（`UI_BAND_H = 4`），仅在栈上使用 `256 B` 临时行缓冲从外置 Flash 读取当前可见的源图像行，**0 字节动态 RAM 占用**，并完整支持页面缩放动画（`ui_draw_xform`）、透明度混合与脏区裁剪。

---

## 六、回归测试与编译烧录指令

1. **PC 端全分辨率自测（含动态内存乱序释放合并 + 32 KB Flash 流式贴图验证）**：
   ```bash
   cmd.exe //c "D:\\dev\\class\\host\\build.bat --odd"
   ./host/watch_ui.exe --render-check && ./host/watch_ui.exe --check
   ./host/watch_ui_240.exe --render-check && ./host/watch_ui_240.exe --check
   ./host/obj/watch_ui_odd.exe --render-check && ./host/obj/watch_ui_odd.exe --check
   ```
2. **AT32F421 固件编译与 PowerWriter 烧录**：
   ```bash
   cmd.exe //c "D:\\dev\\class\\at32\\build.bat"
   /d/dev/miniconda3/Scripts/pyocd load --pack /d/dev/class/at32/packs/ArteryTek.AT32F421_DFP.2.2.0.pack --target=-at32f421k8t7 /d/dev/class/at32/obj/watch.elf
   ```
