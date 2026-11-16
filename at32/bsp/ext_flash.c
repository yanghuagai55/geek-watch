#include "ext_flash.h"
#include "board_config.h"
#include "i2c_bus.h"

#include <string.h>

static uint8_t flash_addr;

/* 等待 EEPROM 内部页编程完成（FRAM 首次即 ACK；EEPROM 典型 2..5ms）。 */
static int wait_ready(void) {
    int tries;
    if (!flash_addr) {
        return 0;
    }
    for (tries = 0; tries < 450; tries++) {
        if (i2c_probe(flash_addr)) {
            return 1;
        }
    }
    return 0;
}

int ext_flash_init(void) {
    uint8_t a;
    flash_addr = 0;
    if (i2c_probe(EXT_FLASH_ADDR)) {
        flash_addr = EXT_FLASH_ADDR;
        return 1;
    }
    for (a = 0x51u; a <= 0x57u; a++) {
        if (i2c_probe(a)) {
            flash_addr = a;
            return 1;
        }
    }
    return 0;
}

int ext_flash_present(void) {
    if (flash_addr && i2c_probe(flash_addr)) {
        return 1;
    }
    return ext_flash_init();
}

uint8_t ext_flash_addr(void) {
    return flash_addr;
}

int ext_flash_read(uint32_t offset, uint8_t *dst, uint32_t len) {
    if (!dst || offset > EXT_FLASH_SIZE || len > EXT_FLASH_SIZE - offset) {
        return 0;
    }
    if (len == 0) {
        return 1;
    }
    if (!flash_addr && !ext_flash_init()) {
        return 0;
    }
    while (len > 0) {
        uint32_t chunk = len > 128u ? 128u : len;
        if (!wait_ready()) {
            return 0;
        }
        if (!i2c_read16(flash_addr, (uint16_t)offset, dst, (int)chunk)) {
            return 0;
        }
        offset += chunk;
        dst += chunk;
        len -= chunk;
    }
    return 1;
}

int ext_flash_write(uint32_t offset, const uint8_t *src, uint32_t len) {
    if (!src || offset > EXT_FLASH_SIZE || len > EXT_FLASH_SIZE - offset) {
        return 0;
    }
    if (len == 0) {
        return 1;
    }
    if (!flash_addr && !ext_flash_init()) {
        return 0;
    }
    while (len > 0) {
        uint32_t page_rem = EXT_FLASH_PAGE_SIZE - (offset & (EXT_FLASH_PAGE_SIZE - 1u));
        uint32_t chunk = len < page_rem ? len : page_rem;
        if (!wait_ready()) {
            return 0;
        }
        if (!i2c_write16(flash_addr, (uint16_t)offset, src, (int)chunk)) {
            return 0;
        }
        offset += chunk;
        src += chunk;
        len -= chunk;
    }
    (void)wait_ready();
    return 1;
}

int ext_flash_erase(uint32_t offset, uint32_t len) {
    uint8_t zero[32];
    if (offset > EXT_FLASH_SIZE || len > EXT_FLASH_SIZE - offset) {
        return 0;
    }
    if (len == 0) {
        return 1;
    }
    memset(zero, 0, sizeof zero);
    while (len > 0) {
        uint32_t page_rem = EXT_FLASH_PAGE_SIZE - (offset & (EXT_FLASH_PAGE_SIZE - 1u));
        uint32_t chunk = len < page_rem ? len : page_rem;
        if (chunk > sizeof zero) {
            chunk = sizeof zero;
        }
        if (!ext_flash_write(offset, zero, chunk)) {
            return 0;
        }
        offset += chunk;
        len -= chunk;
    }
    return 1;
}
