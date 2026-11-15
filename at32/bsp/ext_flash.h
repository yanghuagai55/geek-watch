#ifndef EXT_FLASH_H
#define EXT_FLASH_H

#include <stdint.h>

/* 外部 32KB I2C Flash / EEPROM / FRAM 驱动（24C256 / FM24C256，0x50..0x57，16 位寻址）。
   支持 64 字节页边界自动切分与有界 ACK 轮询；未焊接芯片时自动降级返回 0，绝不卡死主循环。 */
int ext_flash_init(void);
int ext_flash_present(void);
uint8_t ext_flash_addr(void);
int ext_flash_read(uint32_t offset, uint8_t *dst, uint32_t len);
int ext_flash_write(uint32_t offset, const uint8_t *src, uint32_t len);
int ext_flash_erase(uint32_t offset, uint32_t len);

#endif
