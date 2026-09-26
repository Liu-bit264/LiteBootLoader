#include "bl_storage.h"
#include "bl_metadata.h"
#include "bl_crc.h"
#include "bl_port.h"
#include "board_config.h"
#include <string.h>

static uint8_t s_chunk[BL_VERIFY_CHUNK];   /* 1 KiB：VERIFY 分块缓冲，静态分配 */

/* APP 相对擦除单元位图（ADR-015：页→擦除单元；下标为 APP 内第 r 个覆盖单元）。
   ERASE_APP 逐单元置位；写路径只对未标记单元做"扫描+按需擦除"。
   背景（升级流程 selftest 实测教训）：旧实现每次写前无条件扫描，
   同一页的第二次写入会触发整页擦除，把先前分块全部抹掉（两轮 verify
   回读 CRC 同为 0xc71b794b = FF*504+最后 8B，实锤）。
   复位后单元状态未知，init 清零，由扫描兜底。 */
static uint8_t s_erased_map[(BL_APP_UNITS_MAX + 7u) / 8u];

static uint16_t s_app_first_unit;   /* 第一个覆盖 APP 区的全局单元号 */
static uint16_t s_app_unit_total;   /* 覆盖 APP 区的单元数（现实分区下全局序号连续） */

static void erased_map_set(uint32_t rel_unit)
{
    s_erased_map[rel_unit >> 3] |= (uint8_t)(1u << (rel_unit & 7u));
}

static bool erased_map_test(uint32_t rel_unit)
{
    return (s_erased_map[rel_unit >> 3] & (uint8_t)(1u << (rel_unit & 7u))) != 0u;
}

bool bl_storage_init(void)
{
    bl_flash.init();
    memset(s_erased_map, 0, sizeof(s_erased_map));

    /* 校验单元表几何完整（各单元等大且拼满 Flash 由端口层保证的正确性入口），
       并定位覆盖 APP 区的单元区间（F1 均匀页退化为 16..61 连续区间） */
    uint32_t count = bl_flash.unit_count();
    uint32_t covered = 0u;
    uint32_t end_addr = BL_APP_BASE + BL_APP_SIZE;
    s_app_first_unit = 0u;
    s_app_unit_total = 0u;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t ua = bl_flash.unit_addr(i);
        uint32_t us = bl_flash.unit_size(i);
        if (us == 0u) {
            return false;   /* 单元表残缺：端口几何声明与实现不一致 */
        }
        covered += us;
        if (ua + us > BL_APP_BASE && ua < end_addr) {
            if (s_app_unit_total == 0u) {
                s_app_first_unit = (uint16_t)i;
            }
            s_app_unit_total++;
        }
    }
    return covered == BL_FLASH_SIZE && s_app_unit_total > 0u &&
           s_app_unit_total <= BL_APP_UNITS_MAX;
}

/* APP 内第 rel 个覆盖单元的几何（依赖覆盖单元全局序号连续，现实分区恒成立） */
static bool app_unit_geom(uint32_t rel, uint32_t *addr, uint32_t *size)
{
    if (rel >= s_app_unit_total) {
        return false;
    }
    uint32_t gi = s_app_first_unit + rel;
    uint32_t ua = bl_flash.unit_addr(gi);
    uint32_t us = bl_flash.unit_size(gi);
    if (us == 0u || ua < BL_APP_BASE) {
        return false;
    }
    *addr = ua;
    *size = us;
    return true;
}

/* 单元内容是否非擦除态；读失败按无需擦处理（F1 读仅范围校验可失败，
   合法单元内不可达，与既有页扫描行为一致） */
static bool unit_needs_erase(uint32_t addr, uint32_t size)
{
    uint8_t tmp[64];
    for (uint32_t off = 0; off < size; off += sizeof(tmp)) {
        uint32_t n = size - off;
        if (n > sizeof(tmp)) {
            n = sizeof(tmp);
        }
        if (!bl_flash.read(addr + off, tmp, n)) {
            return false;
        }
        for (uint32_t i = 0; i < n; i++) {
            if (tmp[i] != 0xFFu) {
                return true;
            }
        }
    }
    return false;
}

bl_status_t bl_storage_erase_app(void)
{
    for (uint32_t r = 0; r < s_app_unit_total; r++) {
        bl_wdg.refresh();   /* 喂狗点：每擦除单元之间（architecture.md §7） */
        if (!bl_flash.erase_unit(s_app_first_unit + r)) {
            return BL_STATUS_FLASH_ERROR;
        }
        erased_map_set(r);
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

    /* 写前确保目标单元为擦除态（ADR-007 修订）：位图已标记的直接跳过，
       未标记单元扫描后按需擦除——同一单元的后续写入不再整单元擦除。
       F4 语义：块落到大扇区中段时擦的是整个扇区，位图保证该扇区只擦一次 */
    uint32_t end = offset + len;
    for (uint32_t rel = 0; rel < s_app_unit_total; rel++) {
        uint32_t ua, us;
        if (!app_unit_geom(rel, &ua, &us)) {
            return BL_STATUS_FLASH_ERROR;
        }
        uint32_t rel_off = ua - BL_APP_BASE;    /* 单元在 APP 内的起始偏移 */
        if (rel_off >= end) {
            break;                              /* 后续单元更靠后 */
        }
        if (rel_off + us <= offset) {
            continue;                           /* 单元整体在本块之前 */
        }
        if (!erased_map_test(rel)) {
            bl_wdg.refresh();
            if (unit_needs_erase(ua, us)) {
                bl_wdg.refresh();
                if (!bl_flash.erase_unit(s_app_first_unit + rel)) {
                    return BL_STATUS_FLASH_ERROR;
                }
            }
            erased_map_set(rel);
        }
    }

    bl_wdg.refresh();
    if (!bl_flash.write(BL_APP_BASE + offset, data, len)) {
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
