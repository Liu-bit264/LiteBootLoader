#include "flash.h"
#include "board_config.h"
#include "bl_port.h"
#include "stm32f4xx.h"

/* F411 扇区表（ADR-015 非均匀擦除单元）：board_config BL_ERASE_UNIT_TABLE，
   { 起始地址, 大小 } × BL_ERASE_UNIT_COUNT，与 chips/f411ceu6.json 强制一致 */
typedef struct {
    uint32_t addr;
    uint32_t size;
} erase_unit_t;

static const erase_unit_t s_units[BL_ERASE_UNIT_COUNT] = BL_ERASE_UNIT_TABLE;

void bl_flash_lock(void)
{
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR &= ~FLASH_CR_SER;
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
    uint32_t guard = 8000000u;   /* 128K 扇区擦除典型 ~875ms、最大可翻倍，guard 取宽 */
    while ((FLASH->SR & FLASH_SR_BSY) && --guard) {
    }
    if (FLASH->SR & FLASH_SR_BSY) {
        return false;
    }
    /* F4 错误标志（RM0383 §3.7.5）：WRPERR/PGAERR/PGPERR/PGSERR，写 1 清除 */
    if (FLASH->SR & (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR)) {
        FLASH->SR = FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;
        return false;
    }
    FLASH->SR = FLASH_SR_EOP;
    return true;
}

/* ---- bl_flash_ops ---- */
static void ops_init(void)
{
    /* 等待周期由 SystemInit 设置（100MHz -> 3WS）；此处清历史错误标志 */
    FLASH->SR = FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;
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
    /* PSIZE=x16 半字编程（RM0383 §3.7.2）；尾部非对齐以 0xFF 补齐（防御） */
    if ((addr & 1u) != 0u || !bl_flash.is_range_valid(addr, len)) {
        return false;
    }
    if (!unlock()) {
        return false;
    }
    FLASH->CR = (FLASH->CR & ~FLASH_CR_PSIZE) | FLASH_CR_PSIZE_0;   /* x16 半字 */
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

/* 擦除单元（ADR-015）：F4 非均匀扇区，按单元序号查表 */
static uint32_t ops_unit_count(void) { return BL_ERASE_UNIT_COUNT; }

static uint32_t ops_unit_addr(uint32_t unit_index)
{
    return (unit_index < BL_ERASE_UNIT_COUNT) ? s_units[unit_index].addr : 0u;
}

static uint32_t ops_unit_size(uint32_t unit_index)
{
    return (unit_index < BL_ERASE_UNIT_COUNT) ? s_units[unit_index].size : 0u;
}

static bool ops_erase_unit(uint32_t unit_index)
{
    if (unit_index >= BL_ERASE_UNIT_COUNT) {
        return false;
    }
    if (!unlock()) {
        return false;
    }
    bool ok = false;
    if (wait_done()) {
        uint32_t snb = 0u;
        /* 由单元起始地址反查扇区号（SNB），避免表内重复维护第三列 */
        for (uint32_t i = 0; i < BL_ERASE_UNIT_COUNT; i++) {
            if (s_units[i].addr == s_units[unit_index].addr) {
                snb = i;
                break;
            }
        }
        FLASH->CR = (FLASH->CR & ~(FLASH_CR_PSIZE | FLASH_CR_SNB)) | FLASH_CR_PSIZE_0;
        FLASH->CR |= FLASH_CR_SER | (snb << 3u);
        FLASH->CR |= FLASH_CR_STRT;
        ok = wait_done();
        FLASH->CR &= ~FLASH_CR_SER;
    }
    bl_flash_lock();
    return ok;
}

static bool ops_range_valid(uint32_t addr, uint32_t len)
{
    /* len 上限前置：否则 len > FLASH_SIZE 时右侧无符号回绕使检查失效
       （review 2026-09-27 P3，与 F1 同） */
    return len > 0u && len <= BL_FLASH_SIZE && addr >= BL_FLASH_BASE &&
           (addr - BL_FLASH_BASE) <= (BL_FLASH_SIZE - len);
}

const bl_flash_ops bl_flash = {
    .init = ops_init,
    .read = ops_read,
    .write = ops_write,
    .unit_count = ops_unit_count,
    .unit_addr = ops_unit_addr,
    .unit_size = ops_unit_size,
    .erase_unit = ops_erase_unit,
    .is_range_valid = ops_range_valid,
};
