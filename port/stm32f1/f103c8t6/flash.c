#include "flash.h"
#include "board_config.h"
#include "bl_port.h"
#include "stm32f10x.h"

uint32_t bl_flash_page_count(void) { return BL_FLASH_SIZE / BL_PAGE_SIZE; }

void bl_flash_lock(void)
{
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR |= FLASH_CR_LOCK;
}

static bool unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = 0x45670123u;
        FLASH->KEYR = 0xCDEF89ABu;
        if (FLASH->CR & FLASH_CR_LOCK) {
            return false;
        }
    }
    return true;
}

static bool wait_done(void)
{
    uint32_t guard = 4000000u;
    while ((FLASH->SR & FLASH_SR_BSY) && --guard) {
    }
    if (FLASH->SR & FLASH_SR_BSY) {
        return false;
    }
    if (FLASH->SR & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR)) {
        FLASH->SR = FLASH_SR_PGERR | FLASH_SR_WRPRTERR | FLASH_SR_EOP;
        return false;
    }
    FLASH->SR = FLASH_SR_EOP;
    return true;
}

/* ---- bl_flash_ops ---- */
static void ops_init(void)
{
    /* 等待周期由 SystemInit 设置（72MHz -> 2WS）；此处清历史错误标志 */
    FLASH->SR = FLASH_SR_PGERR | FLASH_SR_WRPRTERR | FLASH_SR_EOP;
}

static bool ops_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (!bl_flash.is_range_valid(addr, len)) {
        return false;
    }
    const uint8_t *src = (const uint8_t *)addr;
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = src[i];
    }
    return true;
}

static bool ops_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    /* 半字编程；尾部非对齐以 0xFF 补齐（镜像本身按 4 字节对齐，此为防御） */
    if ((addr & 1u) != 0u || !bl_flash.is_range_valid(addr, len)) {
        return false;
    }
    if (!unlock()) {
        return false;
    }
    bool ok = true;
    for (uint32_t i = 0; i < len && ok; i += 2u) {
        uint16_t hw = (uint16_t)data[i];
        if ((i + 1u) < len) {
            hw |= (uint16_t)(data[i + 1u] << 8);
        } else {
            hw |= 0xFF00u;
        }
        if (!wait_done()) {
            ok = false;
            break;
        }
        FLASH->CR |= FLASH_CR_PG;
        *(volatile uint16_t *)addr = hw;
        if (!wait_done()) {
            ok = false;
        }
        addr += 2u;
    }
    bl_flash_lock();
    return ok;
}

static bool ops_erase_page(uint32_t page_index)
{
    if (page_index >= bl_flash_page_count()) {
        return false;
    }
    if (!unlock()) {
        return false;
    }
    bool ok = false;
    if (wait_done()) {
        FLASH->CR |= FLASH_CR_PER;
        FLASH->AR = BL_FLASH_BASE + page_index * BL_PAGE_SIZE;
        FLASH->CR |= FLASH_CR_STRT;
        ok = wait_done();
        FLASH->CR &= ~FLASH_CR_PER;
    }
    bl_flash_lock();
    return ok;
}

static bool ops_erase_range(uint32_t addr, uint32_t len)
{
    if ((addr % BL_PAGE_SIZE) != 0u || (len % BL_PAGE_SIZE) != 0u ||
        !bl_flash.is_range_valid(addr, len)) {
        return false;
    }
    uint32_t first = (addr - BL_FLASH_BASE) / BL_PAGE_SIZE;
    uint32_t count = len / BL_PAGE_SIZE;
    for (uint32_t i = 0; i < count; i++) {
        if (!bl_flash.erase_page(first + i)) {
            return false;
        }
    }
    return true;
}

static uint32_t ops_page_size(void) { return BL_PAGE_SIZE; }

static bool ops_range_valid(uint32_t addr, uint32_t len)
{
    return len > 0u && addr >= BL_FLASH_BASE &&
           (addr - BL_FLASH_BASE) <= (BL_FLASH_SIZE - len);
}

const bl_flash_ops bl_flash = {
    .init = ops_init,
    .read = ops_read,
    .write = ops_write,
    .erase_page = ops_erase_page,
    .erase_range = ops_erase_range,
    .page_size = ops_page_size,
    .is_range_valid = ops_range_valid,
};
