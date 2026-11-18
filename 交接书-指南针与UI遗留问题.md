# 交接书：指南针 9 轴异常 / 加载占位 / 关于页文字

> 写给接手的大模型或工程师。先读第 1、2、3 节，再看第 4 节未解问题。
> 本文只描述现状和证据，不替你做决定。

---

## 1. 项目与环境

- 仓库：`d:\dev\class`
- 硬件：AT32F421K8T7 + 1.8" 128×160 SPI 屏 + GY-521(MPU6050, 0x68) + GY-271 模块（丝印 HMC5883L，实际识别为 QMC5883P @0x2C）+ AT24C256。
- 固件：`at32/`，`AT32_SLIM_ROM` 构建，55K 代码区正好用满。
- 编译：`& D:\dev\class\at32\build.bat`
- 烧录：
  `python -m pyocd flash --pack at32/packs/ArteryTek.AT32F421_DFP.2.2.0.pack -t _at32f421k8t7 -u 3F9BA70EFF927EB3658E87C67010294B at32/obj/watch.bin`
- BLE 调试：`python host/ble_tool.py --ble sensor` / `snap` / `key 4` / `debug 1`
- SWD 读 RAM：`python -m pyocd cmd -t _at32f421k8t7 --pack at32/packs/ArteryTek.AT32F421_DFP.2.2.0.pack -u 3F9BA70EFF927EB3658E87C67010294B -c "read32 0x20000260 16"`

**空间状态：`text 56252 + data 16 = 56268 / 56320 B`，只剩 52 B。**
任何新增功能都要先腾空间。上一轮为腾空间删掉了传感器页每 3 秒一次的 I2C 扫描（地址改看 BLE `sensor` 的 `M/Q` 字段）。

**重要：`app/` 是另一个 agent（grok）正在改的 Flutter 工程，工作区里有大量未提交改动。不要动 `app/`，不要把 `app/` 加进提交。**
`ui/clock_font.h` 只有换行符差异，也不要提交。

当前提交：
- `51fbf48` fix(at32,ui): 修复 MPU/I2C 配置写丢与指南针不动；消息/音乐/关于页修正
- `d681e26` feat(at32): 0x13 传感器快照输出完整九轴数值

---

## 2. 用户这次的原话与诉求

1. 3D 指南针失效、不动、很多次不准。
2. 蓝牙连上后关于页“图片正常加载，但文字不加载”。
3. 消息列表 `>` 指引箭头没对齐。
4. 音乐断开后显示“无连接”，但上方旧歌曲标题没清空。
5. 要 ASCII `LOADING`。
6. **“三个点搞 loading 那些你也都没修”** —— 指模块未就绪时仍显示 `...`（见 §4.2）。
7. **“怎么可能，之前都还好着呢”** —— 不认可“MPU 硬件坏了”的结论。

第 7 条很关键：**我之前下“硬件故障”结论太早**。更可能是我用 SWD 调试时写入 `SIGNAL_PATH_RESET(0x68)=0x07` 把加速计/陀螺的信号路锁住了。详见 §4.1 的时间线。

---

## 3. 已经做完并验证的部分

### 3.1 I2C 驱动（`at32/bsp/i2c_bus.c`）
- **`dt_bit.dt = x` 改成整寄存器直写 `I2C_HW->dt = x`**。
  位域赋值会被编译成“读-改-写”，发送缓冲为空时读 DT 会触发 underflow，写入值被硬件丢掉。这是“MPU 配置寄存器写不进去”的直接原因之一。
- 新增 `bus_clear()`：PA9/PA10 切 GPIO 开漏，打 9 个 SCL 时钟 + STOP，再切回 I2C。`i2c_bus_init()` 和 `i2c_recover()` 都会调用。
  实测：调试时 MPU 把 SDA 拉死，`I2C0 M0 Q0`，只有这个函数能救回来。

### 3.2 MPU6050（`at32/bsp/mpu6050.c`）
- **不再做 `DEVICE_RESET`**。这颗片复位后会拒绝后续配置写（只有 PWR1 生效、A/G 恒 0）。现在直接覆盖 SMPLRT/CONFIG/GYRO/ACCEL/PWR1，并回读 `CONFIG==0x03`、`PWR1` 的 SLEEP 位校验。
- **14 字节 burst 读改成逐寄存器读**。这颗片 `0x3B` 起连续读会返回 0/超时；逐字节读才稳定（14 次单字节，约 2ms@400kHz，50Hz 下约 11% 总线占用）。
- 配置：SMPLRT=0x07(125Hz)、CONFIG=0x03、GYRO=0x18(±2000dps)、ACCEL=0x18(±16g)、PWR1=0x00(内部 8MHz)。

### 3.3 UI（`ui/src/ui_app.c`）
- 消息列表 `>`：原来按 8px 居中，实际字模 8×16，改成 `ui_text_height(">",1)` 居中。
- 消息列表加载中、音乐 MUS1 未就绪：显示 `LOADING`（`draw_loading()`，noinline，固定屏幕中心）。
- 音乐断连：`mode bit0` 只在 `connected && mask_ready` 时置位，断连不再贴旧歌曲点阵，只显示“无连接”。
- 关于页：先分配 1.3KB 文字、再分配 2.8KB 彩图；预热从 `on_confirm` 挪到 `msg_link_sync()` 中 SYS1/音乐/消息缓冲释放之后；`about_loaded` 只在文字缓存拿到时置 1，没拿到下一帧重试。
- `0x13` 快照改为完整九轴：
  `M%d Q%d AX%u A%d,%d,%d G%d,%d,%d M%d,%d,%d R%d P%d H%u`
  （`at32/port.c`，已提交 `d681e26`）

### 3.4 已验证
- 寄存器侧：`CONFIG=03 GYRO=18 SMPLRT=07 PWR1=00 PWR2=00 SIGPATH=00` 能读回（有一次 `ACCEL=FF`，之后再写 `0x18` 读回 `0x18`，存疑）。
- BLE 侧：`DBG M1 Q2 AX9 A0,0,0 G0,0,0 M-361,711,-579 R0 P0 H27` —— 磁力计、温度、九轴字段都能正常从蓝牙传出；A/G 仍是 0（见 §4.1）。
- 轴对齐（按 `IMG_20261115_133942.jpg`）：
  - GY-521 排针丝印 `INT AD0 XCL XDA SDA SCL GND VCC`，Pin1/VCC 在右上，两块模块排针都朝手表 12 点。
  - GY-521 丝印 X 朝下、Y 朝右 → 代码 `ax=+y_mpu, ay=-x_mpu, az=+z_mpu` 正确。
  - 磁力计 X 朝上；右手系下 Y 必须朝左 → `mx=-y_qmc, my=+x_qmc, mz=+z_qmc` 正确。
  - 实读 `M-361,711,-579`：Y 分量最大（≈北）、Z 为负（北半球磁倾角向下），物理上自洽，说明表平放、12 点大致朝北偏左约 27°。

---

## 4. 未解问题（重点）

### 4.1 MPU6050 加速度/陀螺恒 0（最高优先级）

**现象**
- BLE：`A0,0,0 G0,0,0`，但温度正常（约 35.8℃）、磁力计正常、`AX9` 仍显示 9。
- 直接读 0x3B..0x48：`00 00 FF FF FF FF FF 00 00 00 00 00 00 00`
  - ax=0x0000, ay=0xFFFF(-1), az=0xFFFF(-1), temp=0xFF00(约35.8℃), 陀螺全 0。
- 寄存器 dump 多次为：`PWR1=00 PWR2=00 SIGPATH=00 CONFIG=82/03 GYRO=84/18 ACCEL=80/FF SMPLRT=63/07`。
  即：配置有时能读回我们的值，但数据仍恒 0。

**时间线（重要，别重复踩坑）**
1. 本轮开始前（commit `0fc06a6`）：14 字节 burst 读，用户报“3D 指南针不动、不准”，BLE `9AX=3`。此时是**读法问题**。
2. 改逐寄存器读 + 修 DT 写之后：一度 `9AX=9`、`A0,0,2206`（约 1.08g）、`H31`，数据真实。
3. 我用 SWD 手动写了一次 **`SIGNAL_PATH_RESET(0x68)=0x07`**（想救零数据），之后 A/G 变成 0；再写 `0x68=0x00`、`PWR_MGMT_1=0x80`、断电复位都没恢复。
4. 中间有一次手动完整 reset+config 后又出现过真实数据（`A0,0,2206`），随后又归零 —— 间歇性。

**判断**
- 这**不像芯片彻底坏了**（WHO=0x68、温度在变、配置寄存器能写能读）。更像 `SIGNAL_PATH_RESET` 把加速计/陀螺模拟前端锁在复位态，或这颗克隆片需要一个**真正的断电**（拔 USB + 电池）才能清。
- 我把它归为“硬件故障”是过早结论，向用户道歉。先做**冷启动**，不要先换模块。

**建议的排查顺序**
1. **彻底断电 10 秒再上电**（不是按 RST，是拔电源），再 `sensor`。
2. 若仍为 0：
   - 读 `0x3A INT_STATUS`，看 data-ready 是否在翻；若 DRDY=1 但数据为 0 → 模拟前端没采样。
   - 写 `PWR_MGMT_2(0x6C)=0x00`；写 `SIGNAL_PATH_RESET(0x68)=0x07`，等 50ms，再写 `0x00`；写 `PWR_MGMT_1=0x00`。
   - 试 `PWR_MGMT_1=0x01`（CLKSEL=1，PLL），对比。
   - 用万用表量 GY-521 的 VCC/GND（3.3V）和 SDA/SCL 静态电平。
3. 若冷启动后恢复：**不要再写 0x68=0x07**。用三种姿态核对轴：
   - 平放屏朝上：`A≈0,0,1000`
   - 12 点朝下：`A≈0,-1000,0`
   - 3 点朝下：`A≈-1000,0,0`
   再转四个朝向核对 `H≈0/90/180/270`、`M` 的 Y 分量 `+B/0/-B/0`。
4. 若冷启动后仍为 0 且寄存器都正常：才考虑换 GY-521 模块或飞线。

**代码层建议（修完数据后做）**
- `mpu6050_read()` 现在的有效性判据是 `(ax | az | tp) == 0`；温度不为 0 时，即使加速度全 0 也会通过，导致 `imu_ok=1`、`fused_axes=9`，但姿态永远不 snap。
- 建议加：`|ax|+|ay|+|az|` 落在约 300..12000（0.3g..6g）之外时计一次坏样本，连续 N 次（例如 100 次 = 2s）置 `ok=0`，让 `port_service` 走 1s 重试；这样总线/芯片异常能自愈。
- 代价约 40B Flash，需要先腾空间（见 §5）。

### 4.2 “三个点 ...” 还在（用户明确点名）

`ui_app.c` 里 `pic_mod_exec_std()`（约 956-969 行）在片外模块没就绪时画 `"..."`：

```c
static int pic_mod_exec_std(...) {
    int ok = pic_mod_exec(...);
    if (!ok) {
        ...
        ui_text_draw_center(..., "...", col_dim, a, 1);
    }
    return ok;
}
```

调用点（都会出现三个点）：
- `ui_app.c:1592` 运动计步页（SPR1 mode 0）
- `ui_app.c:1744` 指南针 C3D1 未就绪时的 SPR1 兜底（mode 2）
- `ui_app.c:2015` 传感器数值页（SPR1 mode 1）

另外 `ui_app.c:1173-1174` 是相册表盘无图时的 `"..."`（不是加载态，是否改由产品决定）。

**最小改法**：把 `draw_loading()` 定义挪到 `pic_mod_exec_std()` 之前，或直接在这里画 `"LOADING"`。尺寸几乎不变（一次文字绘制）。这是这次没修完的地方。

### 4.3 视觉效果尚未实机确认

因为手机 App 占着 BLE，我无法 `snap`。需要断开手机后逐项确认：
- 消息列表 `>` 是否与行内容垂直居中；
- 消息列表/音乐页 `LOADING`；
- 关于页（连蓝牙）图片 + 三行文字是否都在；
- 音乐断连后上方点阵是否消失。

`snap` 用法：`python host/ble_tool.py --ble key 4` 导航，`python host/ble_tool.py --ble snap host/obj/x.bmp`，再用 PIL 转 PNG 看。

### 4.4 空间

当前 52B。若要加 §4.1 的坏样本判据，需要再腾 ~40B。可选：
- 继续缩短 `0x13` 快照格式（去掉 `M%d Q%d` 或 `R%d P%d`）；
- `draw_loading` 的两个调用点二选一；
- 删掉 `ui_app.c` 里 `ha_i2c_scan` 相关残留（`st->i2c_scan_ms/i2c_scan_str` 已不用）；
- 谨慎：不要再动 `font_data.c`（会牵动片外字库 id=9 的 App 资源）。

---

## 5. 当前代码改动位置速查

| 文件 | 改动 |
|---|---|
| `at32/bsp/i2c_bus.c` | DT 直写；`bus_delay/bus_clear`；`i2c_bus_init/i2c_recover` 先解锁总线 |
| `at32/bsp/mpu6050.c` | 去掉 DEVICE_RESET；配置数组最后写 PWR1；回读校验；逐寄存器读 14 字节 |
| `ui/src/ui_app.c` | `draw_loading()`；消息 `>` 居中；列表/音乐 LOADING；音乐断连不贴点阵；关于页先文字后彩图 + 预热挪到 `msg_link_sync`；传感器页去掉 I2C 重扫 |
| `at32/port.c` | `0x13` 快照输出完整九轴 |

**不要动**：
- `app/`（grok 的 Flutter 改动，未提交）
- `ui/clock_font.h`（仅换行符）
- `蓝牙连接参数与接口.md` 的 §5.7/§5.8（上一轮已按协议更新过）

---

## 6. 给接手者的建议决策

1. **先冷启动手表**，确认 MPU 是否恢复；不要先改代码、不要先换硬件。
2. 恢复后先做 §4.1 的三种姿态 + 四个朝向核对，确认 `board_align_imu/mag` 没问题（数学本身已核对：TRIAD/Mahony/ENU 航向都是对的）。
3. 把 `pic_mod_exec_std` 的 `"..."` 改成 `LOADING`（§4.2），这是用户明确点名的遗留。
4. 加加速计坏样本判据，避免“温度正常但 A=0”被误判为 9 轴正常。
5. 断开手机后用 `snap` 做 §4.3 的视觉验收。
6. 若要继续压空间，优先删调试显示，不要动字库和协议字段。

---

## 7. 附：一次典型现场数据

```
# BLE 0x13
DBG M1 Q2 AX9 A0,0,0 G0,0,0 M-361,711,-579 R0 P0 H27

# MPU 寄存器（手动逐字节读）
PWR1=00 PWR2=00 SIGPATH=00 CONFIG=03 GYRO=18 ACCEL=FF SMPLRT=07
ACCEL[3B..40] = 00 00 FF FF FF FF FF
TEMP [41..42] = 00
GYRO [43..48] = 00 00 00 00 00 00
```

`H27` 是磁力计单独算出来的 2D 航向（atan2(-mx,my)），不是 9 轴结果；因为加速度为 0，TRIAD 不 snap，四元数一直是单位阵，所以 3D 表盘/磁针不会动。
