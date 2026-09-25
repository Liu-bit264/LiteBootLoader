#include "bl_core.h"
#include "bl_protocol.h"
#include "bl_transport.h"
#include "bl_storage.h"
#include "bl_metadata.h"
#include "bl_boot.h"
#include "bl_ui.h"
#include "bl_log.h"
#include "bl_version.h"
#include "bl_port.h"
#include "board_config.h"

typedef enum { BL_STATE_WAIT_HOST, BL_STATE_UPGRADE_WAIT, BL_STATE_FAULT } core_state_t;

static core_state_t s_state;
static uint32_t     s_wait_start;
static uint32_t     s_last_hb;
static bl_meta_t    s_meta;

/* ---- 工具 ---- */
static void resp_status(uint8_t cmd, uint8_t seq, bl_status_t st)
{
    uint8_t d = (uint8_t)st;
    bl_protocol_send((uint8_t)(cmd | 0x80u), seq, &d, 1u);
}

static void set_upgrade_ui(void)
{
    bl_ui_set_state(BL_UI_UPGRADING);
}

/* ---- 命令处理（protocol.md §5） ---- */
static void handle_ping(uint8_t seq, const uint8_t *data, uint32_t len)
{
    (void)data;
    if (len != 0u) {
        resp_status(BL_CMD_PING, seq, BL_STATUS_RANGE_ERROR);
        return;
    }
    uint8_t d[2] = { (uint8_t)BL_STATUS_OK, BL_PROTOCOL_VER };
    bl_protocol_send((uint8_t)(BL_CMD_PING | 0x80u), seq, d, 2u);
}

static void handle_get_info(uint8_t seq, const uint8_t *data, uint32_t len)
{
    (void)data;
    if (len != 0u) {
        resp_status(BL_CMD_GET_INFO, seq, BL_STATUS_RANGE_ERROR);
        return;
    }
    uint8_t d[31];
    uint32_t k = 0;
    bl_meta_t m;
    bl_meta_load(&m);   /* 实时读取：VERIFY 持久化后 core 缓存副本已陈旧（Review F1） */
    d[k++] = (uint8_t)BL_STATUS_OK;
    d[k++] = BL_VERSION_MAJOR;
    d[k++] = BL_VERSION_MINOR;
    d[k++] = BL_VERSION_PATCH;
    for (uint32_t w = 0; w < 3u; w++) {   /* 96 位 UID，小端字序 */
        uint32_t v = bl_port_read_word(BL_UID_ADDR + w * 4u);
        d[k++] = (uint8_t)v; d[k++] = (uint8_t)(v >> 8);
        d[k++] = (uint8_t)(v >> 16); d[k++] = (uint8_t)(v >> 24);
    }
    uint32_t fk = bl_port_read_word(BL_FLSIZE_ADDR) & 0xFFFFu;
    d[k++] = (uint8_t)fk; d[k++] = (uint8_t)(fk >> 8);
    d[k++] = bl_boot_app_valid() ? 0x01u : 0x00u;
    d[k++] = (uint8_t)m.app_size;
    d[k++] = (uint8_t)(m.app_size >> 8);
    d[k++] = (uint8_t)(m.app_size >> 16);
    d[k++] = (uint8_t)(m.app_size >> 24);
    d[k++] = (uint8_t)m.app_crc32;
    d[k++] = (uint8_t)(m.app_crc32 >> 8);
    d[k++] = (uint8_t)(m.app_crc32 >> 16);
    d[k++] = (uint8_t)(m.app_crc32 >> 24);
    d[k++] = (uint8_t)m.seq;
    d[k++] = (uint8_t)(m.seq >> 8);
    d[k++] = (uint8_t)(m.seq >> 16);
    d[k++] = (uint8_t)(m.seq >> 24);
    bl_protocol_send((uint8_t)(BL_CMD_GET_INFO | 0x80u), seq, d, k);
}

static void handle_erase(uint8_t seq, const uint8_t *data, uint32_t len)
{
    (void)data;
    if (len != 0u) {
        resp_status(BL_CMD_ERASE_APP, seq, BL_STATUS_RANGE_ERROR);
        return;
    }
    set_upgrade_ui();
    bl_ui_set_progress(0u);
    resp_status(BL_CMD_ERASE_APP, seq, bl_storage_erase_app());
}

static void handle_write(uint8_t seq, const uint8_t *data, uint32_t len)
{
    if (len < 4u) {
        resp_status(BL_CMD_WRITE_CHUNK, seq, BL_STATUS_RANGE_ERROR);
        return;
    }
    uint32_t offset = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                      ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    set_upgrade_ui();
    bl_ui_set_progress((uint8_t)((offset * 100u) / BL_APP_SIZE));
    resp_status(BL_CMD_WRITE_CHUNK, seq,
                bl_storage_write_chunk(offset, data + 4u, len - 4u));
}

static void handle_verify(uint8_t seq, const uint8_t *data, uint32_t len)
{
    if (len != 8u) {
        resp_status(BL_CMD_VERIFY_APP, seq, BL_STATUS_RANGE_ERROR);
        return;
    }
    uint32_t size = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                    ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    uint32_t crc = (uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                   ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
    set_upgrade_ui();
    uint32_t calc_crc = 0u, calc_size = 0u;
    bl_status_t st = bl_storage_verify_app(size, crc, &calc_crc, &calc_size);
    bl_ui_set_crc_ok(st == BL_STATUS_OK);
    uint8_t d[9];
    d[0] = (uint8_t)st;
    d[1] = (uint8_t)calc_crc; d[2] = (uint8_t)(calc_crc >> 8);
    d[3] = (uint8_t)(calc_crc >> 16); d[4] = (uint8_t)(calc_crc >> 24);
    d[5] = (uint8_t)calc_size; d[6] = (uint8_t)(calc_size >> 8);
    d[7] = (uint8_t)(calc_size >> 16); d[8] = (uint8_t)(calc_size >> 24);
    bl_protocol_send((uint8_t)(BL_CMD_VERIFY_APP | 0x80u), seq, d, sizeof(d));
}

static void handle_set_meta(uint8_t seq, const uint8_t *data, uint32_t len)
{
    if (len < 2u) {
        resp_status(BL_CMD_SET_META, seq, BL_STATUS_RANGE_ERROR);
        return;
    }
    bl_status_t st = BL_STATUS_OK;
    switch (data[0]) {
    case 0x01u:   /* bl_request */
        if (len != 2u) { st = BL_STATUS_RANGE_ERROR; break; }
        if (!bl_meta_set_bl_request(data[1] != 0u)) { st = BL_STATUS_FLASH_ERROR; }
        break;
    case 0x02u:   /* app_version */
        if (len != 4u) { st = BL_STATUS_RANGE_ERROR; break; }
        if (!bl_meta_set_app_version(data[1], data[2], data[3])) {
            st = BL_STATUS_FLASH_ERROR;
        }
        break;
    default:
        st = BL_STATUS_RANGE_ERROR;
        break;
    }
    resp_status(BL_CMD_SET_META, seq, st);
}

static void handle_get_meta(uint8_t seq, const uint8_t *data, uint32_t len)
{
    (void)data;
    if (len != 0u) {
        resp_status(BL_CMD_GET_META, seq, BL_STATUS_RANGE_ERROR);
        return;
    }
    bl_meta_t m;
    bl_meta_load(&m);
    uint8_t d[21];
    uint32_t k = 0;
    d[k++] = (uint8_t)BL_STATUS_OK;
    d[k++] = (uint8_t)m.seq; d[k++] = (uint8_t)(m.seq >> 8);
    d[k++] = (uint8_t)(m.seq >> 16); d[k++] = (uint8_t)(m.seq >> 24);
    d[k++] = (uint8_t)m.flags; d[k++] = (uint8_t)(m.flags >> 8);
    d[k++] = (uint8_t)(m.flags >> 16); d[k++] = (uint8_t)(m.flags >> 24);
    d[k++] = (uint8_t)m.app_ver_major;
    d[k++] = (uint8_t)m.app_ver_minor;
    d[k++] = (uint8_t)m.app_ver_patch;
    d[k++] = (uint8_t)m.app_size; d[k++] = (uint8_t)(m.app_size >> 8);
    d[k++] = (uint8_t)(m.app_size >> 16); d[k++] = (uint8_t)(m.app_size >> 24);
    d[k++] = (uint8_t)m.app_crc32; d[k++] = (uint8_t)(m.app_crc32 >> 8);
    d[k++] = (uint8_t)(m.app_crc32 >> 16); d[k++] = (uint8_t)(m.app_crc32 >> 24);
    d[k++] = m.active_copy;
    bl_protocol_send((uint8_t)(BL_CMD_GET_META | 0x80u), seq, d, k);
}

static void handle_jump(uint8_t seq, const uint8_t *data, uint32_t len)
{
    (void)data;
    if (len != 0u) {
        resp_status(BL_CMD_JUMP_APP, seq, BL_STATUS_RANGE_ERROR);
        return;
    }
    if (!bl_boot_app_valid()) {
        resp_status(BL_CMD_JUMP_APP, seq, BL_STATUS_STATE_ERROR);
        return;
    }
    resp_status(BL_CMD_JUMP_APP, seq, BL_STATUS_OK);
    bl_clock.delay_ms(50u);   /* 保证主机收到响应（ADR-007） */
    bl_wdg.refresh();          /* 喂狗点：跳转前（architecture.md §7） */
    bl_ui_set_state(BL_UI_JUMPING);
    bl_boot_jump();            /* 不返回 */
}

static void handle_reset(uint8_t seq, const uint8_t *data, uint32_t len)
{
    (void)data;
    if (len != 0u) {
        resp_status(BL_CMD_RESET, seq, BL_STATUS_RANGE_ERROR);
        return;
    }
    resp_status(BL_CMD_RESET, seq, BL_STATUS_OK);
    bl_clock.delay_ms(100u);
    bl_port_system_reset();
    for (;;) {
    }
}

void bl_core_frame_received(uint8_t cmd, uint8_t seq, const uint8_t *data, uint32_t len)
{
    /* 任意有效帧使 WAIT_HOST 转入升级模式（ADR-004） */
    if (s_state == BL_STATE_WAIT_HOST) {
        s_state = BL_STATE_UPGRADE_WAIT;
    }
    switch (cmd) {
    case BL_CMD_PING:        handle_ping(seq, data, len); break;
    case BL_CMD_GET_INFO:    handle_get_info(seq, data, len); break;
    case BL_CMD_ERASE_APP:   handle_erase(seq, data, len); break;
    case BL_CMD_WRITE_CHUNK: handle_write(seq, data, len); break;
    case BL_CMD_VERIFY_APP:  handle_verify(seq, data, len); break;
    case BL_CMD_SET_META:    handle_set_meta(seq, data, len); break;
    case BL_CMD_GET_META:    handle_get_meta(seq, data, len); break;
    case BL_CMD_JUMP_APP:    handle_jump(seq, data, len); break;
    case BL_CMD_RESET:       handle_reset(seq, data, len); break;
    default:                 resp_status(cmd, seq, BL_STATUS_STATE_ERROR); break;
    }
}

void bl_core_init(void)
{
    bl_transport_init();
    bl_storage_init();
    bl_log_init();
    bl_protocol_init();
    bl_ui_init();

    bl_meta_load(&s_meta);

    if (s_meta.flags & BL_META_FLAG_BL_REQUEST) {
        /* 消费并清除 bl_request（partition.md §8）：清除写本身掉电安全 */
        (void)bl_meta_set_bl_request(false);
        s_meta.flags &= ~BL_META_FLAG_BL_REQUEST;
        BL_LOGI("BL request -> upgrade mode");
        s_state = BL_STATE_UPGRADE_WAIT;
        bl_ui_set_state(BL_UI_WAITING);
        return;
    }

    if (bl_boot_app_valid()) {
        BL_LOGI("APP valid, wait %ums", (uint32_t)BL_BOOT_WAIT_MS);
        s_state = BL_STATE_WAIT_HOST;
        s_wait_start = bl_clock.tick_ms();
        bl_ui_set_state(BL_UI_WAITING);
    } else {
        BL_LOGI("APP invalid -> upgrade mode");
        s_state = BL_STATE_UPGRADE_WAIT;
        bl_ui_set_state(BL_UI_APP_INVALID);
    }
}

void bl_core_run(void)
{
    uint8_t rx[64];
    for (;;) {
        bl_wdg.refresh();   /* 喂狗点 1：主循环顶 */

        uint32_t now = bl_clock.tick_ms();
        uint32_t n = bl_transport_recv(rx, sizeof(rx));
        if (n) {
            bl_protocol_feed(rx, n);
        }
        bl_protocol_poll(now);   /* 帧内 50ms 超时复位半帧 */
        bl_ui_tick(now);

#if !BL_LOG_DISABLE
        /* 空闲心跳（design.md ADR-009）：串口链路自检——协议活跃期自动静默 */
        if (!bl_protocol_is_active(now) &&
            (now - s_last_hb) >= BL_LOG_HEARTBEAT_MS) {
            s_last_hb = now;
            BL_LOGI("hb %u", now);
        }
#endif

        switch (s_state) {
        case BL_STATE_WAIT_HOST:
            if ((now - s_wait_start) >= BL_BOOT_WAIT_MS) {
                /* 窗口结束：校验在 init 已通过，此处直接九步跳转 */
                bl_wdg.refresh();
                bl_ui_set_state(BL_UI_JUMPING);
                bl_boot_jump();   /* 不返回 */
            }
            break;
        case BL_STATE_UPGRADE_WAIT:
            /* LED：协议活跃=快闪，否则按 APP 有效性慢闪/双闪（ADR-008） */
            bl_ui_set_state(bl_protocol_is_active(now) ? BL_UI_UPGRADING
                            : (bl_boot_app_valid() ? BL_UI_WAITING : BL_UI_APP_INVALID));
            break;
        case BL_STATE_FAULT:
        default:
            bl_ui_set_state(BL_UI_FAULT);
            break;
        }
    }
}
