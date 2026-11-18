#include "ext_flash.h"
#include "board_config.h"
#include "i2c_bus.h"

#include <string.h>

static uint8_t flash_addr;
static uint8_t flash_probed;

/* 等待 EEPROM 内部页编程完成（FRAM 首次即 ACK；EEPROM 典型 2..5ms）。 */
static int wait_ready(void) {
    int tries;
    if (!flash_addr) {
        return 0;
    }
    for (tries = 0; tries < 500; tries++) {
        if (i2c_probe(flash_addr)) {
            return 1;
        }
    }
    return 0;
}

int ext_flash_init(void) {
    uint8_t a;
    flash_probed = 1u;
    flash_addr = 0u;
    for (a = EXT_FLASH_ADDR; a <= 0x57u; a++) {
        if (i2c_probe(a)) {
            flash_addr = a;
            return 1;
        }
    }
    return 0;
}

int ext_flash_present(void) {
    if (!flash_probed) {
        return ext_flash_init();
    }
    return flash_addr != 0u;
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
    if (!ext_flash_present()) {
        return 0;
    }
    if (i2c_read16(flash_addr, (uint16_t)offset, dst, (int)len)) {
        return 1;
    }
    return wait_ready() && i2c_read16(flash_addr, (uint16_t)offset, dst, (int)len);
}

int ext_flash_write(uint32_t offset, const uint8_t *src, uint32_t len) {
    if (!src || offset > EXT_FLASH_SIZE || len > EXT_FLASH_SIZE - offset) {
        return 0;
    }
    if (len == 0) {
        return 1;
    }
    if (!ext_flash_present()) {
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
    return wait_ready();
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
