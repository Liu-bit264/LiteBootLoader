#include "bl_storage.h"
#include "bl_metadata.h"
#include "bl_crc.h"
#include "bl_port.h"
#include "board_config.h"
#include <string.h>

static uint8_t s_chunk[BL_VERIFY_CHUNK];   /* 1 KiB：VERIFY/擦除探测复用，静态分配 */

/* 本会话内已确保处于擦除态的 APP 页位图（APP 相对页号 0..45）。
   ERASE_APP 逐页置位；写路径只对未标记页做"扫描+按需擦除"。
   背景（升级流程 selftest 实测教训）：旧实现每次写前无条件扫描，
   同一页的第二次写入会触发整页擦除，把先前分块全部抹掉（两轮 verify
   回读 CRC 同为 0xc71b794b = FF*504+最后 8B，实锤）。
   复位后页状态未知，init 清零，由扫描兜底。 */
#define BL_APP_PAGES  (BL_APP_SIZE / BL_PAGE_SIZE)
static uint8_t s_erased_map[(BL_APP_PAGES + 7u) / 8u];

static void erased_map_set(uint32_t rel_page)
{
    s_erased_map[rel_page >> 3] |= (uint8_t)(1u << (rel_page & 7u));
}

static bool erased_map_test(uint32_t rel_page)
{
    return (s_erased_map[rel_page >> 3] & (uint8_t)(1u << (rel_page & 7u))) != 0u;
}

bool bl_storage_init(void)
{
    bl_flash.init();
    memset(s_erased_map, 0, sizeof(s_erased_map));
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
        erased_map_set(i);
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

    /* 写前确保目标页为擦除态（ADR-007 修订）：位图已标记的页直接跳过，
       未标记页扫描后按需擦除——同一页的后续写入不再整页擦除 */
    uint32_t addr = BL_APP_BASE + offset;
    uint32_t page_span = ((offset % BL_PAGE_SIZE) + len + BL_PAGE_SIZE - 1u) / BL_PAGE_SIZE;
    uint32_t rel = offset / BL_PAGE_SIZE;
    for (uint32_t p = 0; p < page_span; p++) {
        if (erased_map_test(rel + p)) {
            continue;
        }
        bl_wdg.refresh();
        if (page_needs_erase(BL_APP_BASE + (rel + p) * BL_PAGE_SIZE)) {
            bl_wdg.refresh();
            if (!bl_flash.erase_page((BL_APP_BASE - BL_FLASH_BASE) / BL_PAGE_SIZE +
                                     rel + p)) {
                return BL_STATUS_FLASH_ERROR;
            }
        }
        erased_map_set(rel + p);
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
