#include "bl_protocol.h"
#include "bl_crc.h"
#include "bl_transport.h"
#include "bl_core.h"
#include "bl_port.h"
#include "board_config.h"

/* 解析状态（protocol.md §4.2） */
typedef enum {
    PS_SOF1, PS_SOF2, PS_VER, PS_CMD, PS_SEQ, PS_LEN_LO, PS_LEN_HI,
    PS_DATA, PS_CRC_LO, PS_CRC_HI, PS_EOF1, PS_EOF2,
} ps_state_t;

static ps_state_t  s_state;
static uint8_t     s_ver, s_cmd, s_seq;
static uint16_t    s_len;
static uint16_t    s_got;
static uint8_t     s_data[BL_FRAME_DATA_MAX];
static uint16_t    s_crc_rx;
static uint32_t    s_last_valid_ms;
static uint32_t    s_last_byte_ms;
static bool        s_has_valid_frame;
static uint32_t    s_stat_crc_ok;
static uint32_t    s_stat_delivered;
static uint32_t    s_stat_crc_fail;
static uint32_t    s_stat_byte_timeout;
static uint32_t    s_stat_timeout_pending;   /* 超时触发时环形缓冲仍有字节（主循环被阻塞的信号） */
/* 最近一次帧内超时的现场快照（GET_INFO 遥测）：饥饿时长/缓冲待取/解析状态/已收字节 */
static uint32_t    s_dbg_gap_ms, s_dbg_pending, s_dbg_state, s_dbg_got;

void bl_protocol_init(void)
{
    s_state = PS_SOF1;
    s_has_valid_frame = false;
    s_last_valid_ms = 0u;
    s_got = 0u;
    s_len = 0u;
}

bool bl_protocol_is_active(uint32_t now_ms)
{
    if (!s_has_valid_frame) {
        return false;
    }
    return (now_ms - s_last_valid_ms) < BL_PROTOCOL_ACTIVE_MS;
}

uint32_t bl_protocol_stat_crc_ok(void) { return s_stat_crc_ok; }

uint32_t bl_protocol_stat_delivered(void) { return s_stat_delivered; }

uint32_t bl_protocol_stat_crc_fail(void) { return s_stat_crc_fail; }

uint32_t bl_protocol_stat_byte_timeout(void) { return s_stat_byte_timeout; }

uint32_t bl_protocol_stat_timeout_pending(void) { return s_stat_timeout_pending; }

void bl_protocol_stat_timeout_detail(uint32_t out[4])
{
    /* 最近一次帧内超时现场：[饥饿时长ms, 缓冲待取, 解析状态, 已收字节数] */
    out[0] = s_dbg_gap_ms;
    out[1] = s_dbg_pending;
    out[2] = s_dbg_state;
    out[3] = s_dbg_got;
}

static void frame_complete(void)
{
    s_state = PS_SOF1;
    /* CRC 覆盖 VER..DATA 末尾：VER,CMD,SEQ,LEN(2B) 共 5 字节 + DATA */
    uint8_t hdr[5];
    hdr[0] = s_ver;
    hdr[1] = s_cmd;
    hdr[2] = s_seq;
    hdr[3] = (uint8_t)(s_len & 0xFFu);
    hdr[4] = (uint8_t)(s_len >> 8);
    uint16_t expect = bl_crc16_update(BL_CRC16_INIT, hdr, sizeof(hdr));
    expect = bl_crc16_update(expect, s_data, s_len);
    if (expect != s_crc_rx) {
        s_stat_crc_fail++;
        return; /* 静默丢弃（protocol.md §4.2：CMD 不可信，无法回帧） */
    }
    s_stat_crc_ok++;
    s_last_valid_ms = bl_clock.tick_ms();
    s_has_valid_frame = true;
    if (s_ver != BL_PROTOCOL_VER) {
        return; /* 版本不符丢弃 */
    }
    if (s_cmd & 0x80u) {
        return; /* 响应方向帧不来自主机，丢弃 */
    }
    s_stat_delivered++;
    bl_core_frame_received(s_cmd, s_seq, s_data, s_len);
}

void bl_protocol_feed(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        s_last_byte_ms = bl_clock.tick_ms();
        switch (s_state) {
        case PS_SOF1:
            if (b == 0xAAu) { s_state = PS_SOF2; }
            break;
        case PS_SOF2:
            if (b == 0x55u) {
                s_state = PS_VER;
                s_got = 0u;
            } else {
                s_state = (b == 0xAAu) ? PS_SOF2 : PS_SOF1;
            }
            break;
        case PS_VER:
            s_ver = b; s_state = PS_CMD; break;
        case PS_CMD:
            s_cmd = b; s_state = PS_SEQ; break;
        case PS_SEQ:
            s_seq = b; s_state = PS_LEN_LO; break;
        case PS_LEN_LO:
            s_len = b; s_state = PS_LEN_HI; break;
        case PS_LEN_HI:
            s_len |= (uint16_t)((uint16_t)b << 8);
            if (s_len > BL_FRAME_DATA_MAX) {
                s_state = PS_SOF1;   /* 非法长度拒绝 */
            } else {
                s_state = (s_len == 0u) ? PS_CRC_LO : PS_DATA;
            }
            break;
        case PS_DATA:
            s_data[s_got++] = b;
            if (s_got >= s_len) { s_state = PS_CRC_LO; }
            break;
        case PS_CRC_LO:
            s_crc_rx = b; s_state = PS_CRC_HI; break;
        case PS_CRC_HI:
            s_crc_rx |= (uint16_t)((uint16_t)b << 8);
            s_state = PS_EOF1;
            break;
        case PS_EOF1:
            if (b != 0x55u) { s_state = PS_SOF1; }
            else { s_state = PS_EOF2; }
            break;
        case PS_EOF2:
            if (b == 0xAAu) { frame_complete(); }
            else { s_state = PS_SOF1; }
            break;
        default:
            s_state = PS_SOF1;
            break;
        }
        /* 帧内超时由 bl_protocol_poll 复位（BL_FRAME_BYTE_TIMEOUT_MS） */
    }
}

void bl_protocol_poll(uint32_t now_ms)
{
    (void)now_ms;
    uint32_t now = bl_clock.tick_ms();   /* 现场重读，避免循环顶部旧值参与比较 */
    if (s_state != PS_SOF1 &&
        (now - s_last_byte_ms) >= BL_FRAME_BYTE_TIMEOUT_MS) {
        s_dbg_gap_ms = now - s_last_byte_ms;
        s_dbg_pending = bl_transport_rx_pending();
        s_dbg_state = (uint32_t)s_state;
        s_dbg_got = s_got;
        if (s_dbg_pending != 0u) {
            s_stat_timeout_pending++;
        }
        s_stat_byte_timeout++;
        s_state = PS_SOF1;   /* 半帧丢弃（protocol.md §4.2 帧内超时） */
    }
}

bool bl_protocol_send(uint8_t resp_cmd, uint8_t seq, const uint8_t *data, uint32_t len)
{
    if (len > BL_FRAME_DATA_MAX) {
        return false;
    }
    uint8_t frame[16u + BL_FRAME_DATA_MAX];
    uint32_t k = 0;
    frame[k++] = 0xAAu; frame[k++] = 0x55u;
    frame[k++] = BL_PROTOCOL_VER;
    frame[k++] = resp_cmd;
    frame[k++] = seq;
    frame[k++] = (uint8_t)(len & 0xFFu);
    frame[k++] = (uint8_t)(len >> 8);
    for (uint32_t i = 0; i < len; i++) {
        frame[k++] = data[i];
    }
    /* 覆盖 VER..DATA：frame[2] 起 5 字节头 + DATA */
    uint16_t crc = bl_crc16_update(BL_CRC16_INIT, &frame[2], 5u);
    crc = bl_crc16_update(crc, data, len);
    frame[k++] = (uint8_t)(crc & 0xFFu);
    frame[k++] = (uint8_t)(crc >> 8);
    frame[k++] = 0x55u; frame[k++] = 0xAAu;
    return bl_transport_send(frame, k);
}
