#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <stdint.h>

/* 软件 bit-bang I2C（PA9=SCL/PA10=SDA，开漏+外部上拉）。
   返回 1=成功/有ACK，0=失败。不阻塞主循环。 */
void i2c_bus_init(void);
void i2c_recover(void);
int i2c_probe(uint8_t addr7);
int i2c_write(uint8_t addr7, uint8_t reg, const uint8_t *data, int n);
int i2c_read(uint8_t addr7, uint8_t reg, uint8_t *data, int n);
/* 16 位内存地址读写（用于 32KB 24C256 / FM24C256 等 I2C Flash/EEPROM）。 */
int i2c_write16(uint8_t addr7, uint16_t reg16, const uint8_t *data, int n);
int i2c_read16(uint8_t addr7, uint16_t reg16, uint8_t *data, int n);

#endif