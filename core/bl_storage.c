#include "bl_storage.h"
#include "bl_metadata.h"
#include "bl_crc.h"
#include "bl_port.h"
#include "board_config.h"
#include <string.h>

static uint8_t s_chunk[BL_VERIFY_CHUNK];   /* 1 KiB：VERIFY/擦除探测复用，静态分配 */

bool bl_storage_init(void)
{
    bl_flash.init();
    return bl_flash.page_size() == BL_PAGE_SIZE;
}

static bool page_needs_erase(uint32_t addr)
{
    uint8_t tmp[64];
    for (uint32_t off = 0; off < BL_PAGE_SIZE; off += sizeof(tmp)) {
        if (!bl_flash.read(addr + off, tmp, sizeof(tmp))) {
            return false; /* 读失败按需要擦处理（保守） */
        }
        for (uint32_t i = 0; i < sizeof(tmp); i++) {
            if (tmp[i] != 0xFFu) {
                return true;
            }
        }
    }
    return false;
}

bl_status_t bl_storage_erase_app(void)
{
    uint32_t pages = BL_APP_SIZE / BL_PAGE_SIZE;
    uint32_t first = (BL_APP_BASE - BL_FLASH_BASE) / BL_PAGE_SIZE;
    for (uint32_t i = 0; i < pages; i++) {
        bl_wdg.refresh();   /* 喂狗点：每页之间（architecture.md §7） */
        if (!bl_flash.erase_page(first + i)) {
            return BL_STATUS_FLASH_ERROR;
        }
    }
    return BL_STATUS_OK;
}

bl_status_t bl_storage_write_chunk(uint32_t offset, const uint8_t *data, uint32_t len)
{
    if (len == 0u || len > (BL_FRAME_DATA_MAX - 4u)) {
        return BL_STATUS_RANGE_ERROR;
    }
    if ((offset % 4u) != 0u || offset > BL_APP_SIZE ||
        (BL_APP_SIZE - offset) < len) {
        return BL_STATUS_RANGE_ERROR;
    }

    /* 写前自动擦页（ADR-007）：受影响页有非 0xFF 字节则先擦，页间喂狗 */
    uint32_t addr = BL_APP_BASE + offset;
    uint32_t page_span = ((offset % BL_PAGE_SIZE) + len + BL_PAGE_SIZE - 1u) / BL_PAGE_SIZE;
    uint32_t page_idx = (addr - BL_FLASH_BASE) / BL_PAGE_SIZE;
    for (uint32_t p = 0; p < page_span; p++) {
        uint32_t pa = BL_FLASH_BASE + (page_idx + p) * BL_PAGE_SIZE;
        if (page_needs_erase(pa)) {
            bl_wdg.refresh();
            if (!bl_flash.erase_page(page_idx + p)) {
                return BL_STATUS_FLASH_ERROR;
            }
        }
    }

    bl_wdg.refresh();
    if (!bl_flash.write(addr, data, len)) {
        return BL_STATUS_FLASH_ERROR;
    }
    return BL_STATUS_OK;
}

bl_status_t bl_storage_verify_app(uint32_t size, uint32_t expect_crc,
                                  uint32_t *calc_crc, uint32_t *calc_size)
{
    if (size == 0u || size > BL_APP_SIZE || (size % 4u) != 0u) {
        return BL_STATUS_RANGE_ERROR;
    }
    uint32_t crc = BL_CRC32_INIT;
    for (uint32_t done = 0; done < size; done += BL_VERIFY_CHUNK) {
        uint32_t n = size - done;
        if (n > BL_VERIFY_CHUNK) {
            n = BL_VERIFY_CHUNK;
        }
        if (!bl_flash.read(BL_APP_BASE + done, s_chunk, n)) {
            return BL_STATUS_FLASH_ERROR;
        }
        crc = bl_crc32_update(crc, s_chunk, n);
        bl_wdg.refresh();   /* 喂狗点：每 1 KiB 块之间 */
        *calc_size = done + n;
    }
    crc ^= 0xFFFFFFFFu;
    *calc_crc = crc;
    *calc_size = size;
    if (crc != expect_crc) {
        return BL_STATUS_CRC_ERROR;
    }
    if (!bl_meta_commit_app(size, crc)) {
        return BL_STATUS_FLASH_ERROR;
    }
    return BL_STATUS_OK;
}
