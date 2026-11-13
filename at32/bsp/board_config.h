#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* AT32 极客手表引脚映射。
   依据匹配 PCB 的网表 AT32_Smartwatch_NET.net（2026-11-07，与 PCB BOM 同版）。
   所有底层驱动只从这里取引脚，改板只改这一处。
   注意：MPU_INT/BLE_STATE 在 11-12 原理图导出里标号位置有出入，
   但功能上 INT 可轮询替代、BLE_STATE 仅作状态输入，不阻塞核心功能。 */

#include "at32f421.h"

/* ---- I2C 传感器总线（软件 I2C，PA9=SCL / PA10=SDA，外部 10k 上拉）----
   挂 MPU6050(0x68) / QMC5883L(0x0D) / BMP580(0x46)。 */
#define I2C_PORT        GPIOA
#define I2C_SCL_PIN     GPIO_PINS_9
#define I2C_SDA_PIN     GPIO_PINS_10
#define I2C_CRM_CLK     CRM_GPIOA_PERIPH_CLOCK

/* ---- 显示屏 SPI1（已在 port.c 实现，这里仅登记）----
   SCK PA5 / MOSI PA7 / DC PA6 / RES PA4 / BLK PB0。 */
#define LCD_BLK_PORT    GPIOB
#define LCD_BLK_PIN     GPIO_PINS_0

/* ---- 蓝牙透传 USART2（已在 port.c 实现）TX PA2 / RX PA3 ----
   实测这块 HC-04BLE 在 9600 下也不回包；工程按原 115200 走，
   蓝牙问题见 README「已知问题」：先 unpair，或改用 CH340 有线。 */
#ifndef BLE_UART_BAUD
#define BLE_UART_BAUD   115200
#endif
#define BLE_STATE_PORT  GPIOB
#define BLE_STATE_PIN   GPIO_PINS_1   /* HC-08 连接状态输入 */

/* ---- 传感器中断/就绪 ----
   2026-11-12 原理图：MPU_INT 从 PA1 移到 PB6，PA1 让给电池检测。 */
#define MPU_INT_PORT    GPIOB
#define MPU_INT_PIN     GPIO_PINS_6   /* MPU6050 INT */
#define MAG_DRDY_PORT   GPIOB
#define MAG_DRDY_PIN    GPIO_PINS_5   /* QMC5883L DRDY */
#define BMP_INT_PORT    GPIOB
#define BMP_INT_PIN     GPIO_PINS_4   /* BMP580 INT */

/* ---- 模拟 ---- */
#define MIC_ADC_PORT    GPIOA
#define MIC_ADC_PIN     GPIO_PINS_0   /* MAX9814 输出，ADC1 通道 0 */
#define MIC_ADC_CHANNEL ADC_CHANNEL_0
/* 电池分压 BAT_ADC：PA1 = ADC1_IN1（11-12 原理图 P14 电池检测页）。
   分压比 R1=R2=100k -> 实际电压 = 测得 * 2，C4 100nF 滤波。 */
#ifndef WATCH_BAT_ADC_ENABLE
#define WATCH_BAT_ADC_ENABLE 1
#endif
#define BAT_ADC_PORT    GPIOA
#define BAT_ADC_PIN     GPIO_PINS_1
#define BAT_ADC_CHANNEL ADC_CHANNEL_1
#define BAT_DIV_NUM     2
#define BAT_DIV_DEN     1

/* ---- 蜂鸣器（PA8 = TMR1_CH1，MOS 低边驱动，有源蜂鸣器）---- */
#define BEEP_PORT       GPIOA
#define BEEP_PIN        GPIO_PINS_8
#define BEEP_CRM_CLK    CRM_GPIOA_PERIPH_CLOCK

/* ---- 状态灯（PB3，绿）---- */
#define LED_PORT        GPIOB
#define LED_PIN         GPIO_PINS_3
#define LED_CRM_CLK     CRM_GPIOB_PERIPH_CLOCK

/* ---- 按键（已在 port.c 实现）PA11/PA12/PA15，按下为低 ---- */

#endif