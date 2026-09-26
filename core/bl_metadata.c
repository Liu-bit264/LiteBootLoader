#include "bl_metadata.h"
#include "bl_crc.h"
#include "bl_port.h"
#include "board_config.h"

/* 页内布局（partition.md §4，多字节小端）：0x00 magic / 0x04 seq / 0x08 app_size /
   0x0C app_crc32 / 0x10 flags / 0x14-0x19 版本 / 0x1A-0x1F 保留0 / 0x20 crc32(覆盖0x00-0x1F) */
#define META_HDR_SIZE  0x20u
#define META_IMG_SIZE  0x24u
static const uint8_t k_magic[4] = { 0x42u, 0x4Cu, 0x50u, 0x31u }; /* "BLP1" */

static bl_meta_t s_meta;
static uint8_t s_active = 0xFFu;   /* 0=A 1=B 0xFF=无 */
static bool s_loaded;

static void le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* 评估单个副本：true = VALID（partition.md §4 判定） */
static bool eval_copy(uint32_t page_addr, bl_meta_t *out)
{
    uint8_t img[META_IMG_SIZE];
    if (!bl_flash.read(page_addr, img, META_IMG_SIZE)) {
        return false;
    }
    for (uint32_t i = 0; i < 4u; i++) {
        if (img[i] != k_magic[i]) {
            return false;
        }
    }
    uint32_t stored = rd32(&img[META_HDR_SIZE]);
    uint32_t calc = bl_crc32_iso_hdlc(img, META_HDR_SIZE);
    if (calc != stored) {
        return false;
    }
    uint32_t flags = rd32(&img[0x10]);
    if (flags & ~BL_META_FLAG_BL_REQUEST) {
        return false; /* 保留位必须 0 */
    }
    out->seq = rd32(&img[0x04]);
    out->app_size = rd32(&img[0x08]);
    out->app_crc32 = rd32(&img[0x0C]);
    out->flags = flags;
    out->app_ver_major = rd16(&img[0x14]);
    out->app_ver_minor = rd16(&img[0x16]);
    out->app_ver_patch = rd16(&img[0x18]);
    return true;
}

/* 在擦除单元表中定位包含 addr 的单元序号（ADR-015）；找不到返回 false */
static bool find_unit_containing(uint32_t addr, uint32_t *unit_index)
{
    uint32_t n = bl_flash.unit_count();
    for (uint32_t i = 0; i < n; i++) {
        uint32_t ua = bl_flash.unit_addr(i);
        uint32_t us = bl_flash.unit_size(i);
        if (us != 0u && ua <= addr && (addr - ua) < us) {
            *unit_index = i;
            return true;
        }
    }
    return false;
}

static bool s_units_checked;

/* 双副本必须落在两个独立擦除单元（partition.md §6 掉电安全前提；F4 两个扇区） */
static bool check_copy_units_independent(void)
{
    uint32_t ia, ib;
    return find_unit_containing(BL_PARAM_BASE, &ia) &&
           find_unit_containing(BL_PARAM_BASE + BL_PARAM_COPY_SIZE, &ib) &&
           ia != ib;
}

bool bl_meta_load(bl_meta_t *out)
{
    if (!s_units_checked) {
        s_units_checked = true;
        if (!check_copy_units_independent()) {
            return false;   /* 副本几何非法：拒绝加载，防止误写 */
        }
    }
    bl_meta_t a, b;
    bool a_ok = eval_copy(BL_PARAM_BASE, &a);                                 /* 副本 A */
    bool b_ok = eval_copy(BL_PARAM_BASE + BL_PARAM_COPY_SIZE, &b);            /* 副本 B */

    if (a_ok && b_ok) {
        s_active = (a.seq >= b.seq) ? 0u : 1u;   /* 双有效取 seq 大（§6.1） */
        s_meta = (s_active == 0u) ? a : b;
    } else if (a_ok) {
        s_active = 0u;
        s_meta = a;
    } else if (b_ok) {
        s_active = 1u;
        s_meta = b;
    } else {
        s_active = 0xFFu;   /* 出厂态 */
        s_meta.seq = 0u;
        s_meta.app_size = 0u;
        s_meta.app_crc32 = 0u;
        s_meta.flags = 0u;
        s_meta.app_ver_major = s_meta.app_ver_minor = s_meta.app_ver_patch = 0u;
    }
    s_meta.active_copy = s_active;
    s_loaded = true;
    *out = s_meta;
    return true;
}

static void build_image(uint8_t *img, uint32_t seq, uint32_t size, uint32_t crc,
                        uint32_t flags, uint16_t ma, uint16_t mi, uint16_t pa)
{
    for (uint32_t i = 0; i < META_IMG_SIZE; i++) {
        img[i] = 0xFFu;
    }
    for (uint32_t i = 0; i < 4u; i++) {
        img[i] = k_magic[i];
    }
    le32(&img[0x04], seq);
    le32(&img[0x08], size);
    le32(&img[0x0C], crc);
    le32(&img[0x10], flags);
    le16(&img[0x14], ma);
    le16(&img[0x16], mi);
    le16(&img[0x18], pa);
    /* 0x1A-0x1F 保留 0 */
    for (uint32_t i = 0x1A; i < META_HDR_SIZE; i++) {
        img[i] = 0x00u;
    }
    le32(&img[META_HDR_SIZE], bl_crc32_iso_hdlc(img, META_HDR_SIZE));
}

/* 掉电安全写（partition.md §6.2）：擦目标副本所在单元 -> 写 0x24B -> 回读校验 */
static bool write_copy(uint8_t copy_idx, uint32_t seq, uint32_t size, uint32_t crc,
                       uint32_t flags, uint16_t ma, uint16_t mi, uint16_t pa)
{
    uint32_t copy_addr = BL_PARAM_BASE + (uint32_t)copy_idx * BL_PARAM_COPY_SIZE;
    uint32_t unit_idx;
    uint8_t img[META_IMG_SIZE];
    uint8_t back[META_IMG_SIZE];

    bl_wdg.refresh();
    if (!find_unit_containing(copy_addr, &unit_idx) ||
        !bl_flash.erase_unit(unit_idx)) {
        return false;
    }
    build_image(img, seq, size, crc, flags, ma, mi, pa);
    bl_wdg.refresh();
    if (!bl_flash.write(copy_addr, img, META_IMG_SIZE)) {
        return false;
    }
    bl_wdg.refresh();
    if (!bl_flash.read(copy_addr, back, META_IMG_SIZE)) {
        return false;
    }
    bl_meta_t chk;
    if (!eval_copy(copy_addr, &chk) || chk.seq != seq) {
        return false;   /* 回读失败：副本保持 INVALID，旧副本语义不变 */
    }
    s_meta = chk;
    s_meta.active_copy = copy_idx;
    s_active = copy_idx;
    return true;
}

static bool commit(uint32_t size, uint32_t crc, uint32_t flags,
                   uint16_t ma, uint16_t mi, uint16_t pa)
{
    uint32_t seq;
    if (s_active == 0xFFu) {
        seq = 1u;                       /* 出厂态：首写页 A */
    } else if (s_meta.seq == 0xFFFFFFFFu) {
        /* 回绕（§6.2）：双副本所在单元重擦，从 1 重新开始 */
        uint32_t unit_a, unit_b;
        bl_wdg.refresh();
        if (!find_unit_containing(BL_PARAM_BASE, &unit_a) ||
            !find_unit_containing(BL_PARAM_BASE + BL_PARAM_COPY_SIZE, &unit_b) ||
            !bl_flash.erase_unit(unit_a) || !bl_flash.erase_unit(unit_b)) {
            return false;
        }
        s_active = 0xFFu;
        seq = 1u;
    } else {
        seq = s_meta.seq + 1u;
    }
    uint8_t target = (s_active == 0u) ? 1u : 0u;   /* 交替写非最新页 */
    return write_copy(target, seq, size, crc, flags, ma, mi, pa);
}

bool bl_meta_commit_app(uint32_t size, uint32_t crc32)
{
    if (!s_loaded) {
        return false;
    }
    return commit(size, crc32, s_meta.flags & ~BL_META_FLAG_BL_REQUEST,
                  s_meta.app_ver_major, s_meta.app_ver_minor, s_meta.app_ver_patch);
}

bool bl_meta_set_bl_request(bool set)
{
    if (!s_loaded) {
        return false;
    }
    uint32_t flags = set ? (s_meta.flags | BL_META_FLAG_BL_REQUEST)
                         : (s_meta.flags & ~BL_META_FLAG_BL_REQUEST);
    return commit(s_meta.app_size, s_meta.app_crc32, flags,
                  s_meta.app_ver_major, s_meta.app_ver_minor, s_meta.app_ver_patch);
}

bool bl_meta_set_app_version(uint16_t ma, uint16_t mi, uint16_t pa)
{
    if (!s_loaded) {
        return false;
    }
    return commit(s_meta.app_size, s_meta.app_crc32, s_meta.flags, ma, mi, pa);
}
