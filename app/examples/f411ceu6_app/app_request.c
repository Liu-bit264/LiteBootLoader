/* app_request.c —— 升级口最小响应器（external_interface.md §5）
 * 只实现两命令：PING(0x01) 回状态+协议版本；SET_META(0x06) 仅认 field 0x01
 * bl_request。收到置位请求：参数区掉电安全写（复用 bl_metadata 双副本）→
 * 回 OK → 提示 → 100ms 后复位，BL 上电消费请求进升级模式。
 * 帧格式与 BL 一致（protocol.md §4）；其余命令回 RANGE_ERROR。
 * 与 f103c8t6_app/app_request.c 逐行同源（芯片无关，仅目录分置）。 */
#include <string.h>
#include "app_request.h"
#include "bl_port.h"
#include "bl_metadata.h"
#include "bl_crc.h"
#include "board_config.h"

#define APP_PROTO_VER        0x01u
#define APP_CMD_PING         0x01u
#define APP_CMD_SET_META     0x06u
#define APP_META_BL_REQUEST  0x01u
#define APP_FRAME_MAX        (8u + BL_FRAME_DATA_MAX)   /* SOF2+头5+DATA256+CRC2+EOF2 = 267 */

static uint8_t  s_buf[APP_FRAME_MAX];
static uint32_t s_len;

static void respond(uint8_t cmd, uint8_t seq, const uint8_t *data, uint32_t len)
{
    uint8_t f[16u + BL_FRAME_DATA_MAX];
    uint32_t k = 0;
    f[k++] = 0xAAu; f[k++] = 0x55u;
    f[k++] = APP_PROTO_VER;
    f[k++] = (uint8_t)(cmd | 0x80u);
    f[k++] = seq;
    f[k++] = (uint8_t)len; f[k++] = (uint8_t)(len >> 8);
    for (uint32_t i = 0; i < len; i++) {
        f[k++] = data[i];
    }
    uint16_t crc = bl_crc16_update(BL_CRC16_INIT, &f[2], 5u);
    crc = bl_crc16_update(crc, data, len);
    f[k++] = (uint8_t)crc; f[k++] = (uint8_t)(crc >> 8);
    f[k++] = 0x55u; f[k++] = 0xAAu;
    bl_uart.write(f, k);
}

static void on_frame(uint8_t cmd, uint8_t seq, const uint8_t *data, uint32_t len)
{
    uint8_t st;
    if (cmd == APP_CMD_PING && len == 0u) {
        uint8_t d[2] = { 0x00u, APP_PROTO_VER };
        respond(APP_CMD_PING, seq, d, 2u);
        return;
    }
    if (cmd == APP_CMD_SET_META && len == 2u && data[0] == APP_META_BL_REQUEST) {
        st = bl_meta_set_bl_request(data[1] != 0u) ? 0x00u : 0x02u;
        respond(APP_CMD_SET_META, seq, &st, 1u);
        if (st == 0x00u && data[1] != 0u) {
            static const char msg[] = "\r\nA:request bl, rebooting\r\n";
            bl_uart.write((const uint8_t *)msg, (uint32_t)(sizeof(msg) - 1u));
            bl_clock.delay_ms(50u);   /* 确保响应与提示完整发出 */
            bl_port_system_reset();   /* BL 上电消费 bl_request → 升级模式 */
        }
        return;
    }
    st = 0x03u;   /* RANGE_ERROR：示例响应器只实现以上命令 */
    respond(cmd, seq, &st, 1u);
}

/* 整帧尝试解析：要求 SOF/长度/CRC 全部一致；成功返回 true 并分发。
 * 注意（review 2026-09-27 P2 加固）：这是「最小示例」重组器——按 EOF 定界累积，
 * DATA/CRC 内出现 0x55 0xAA 会提前定界（伪 EOF）。伪 EOF 或校验失败一律丢弃
 * 已积累内容、等主机重发，不再靠 267B 超长复位兜底（避免畸形输入拖延正常帧）。
 * 严谨性仍弱于 core/bl_protocol.c 的流式状态机，勿复用到生产路径。 */
static bool try_parse(void)
{
    if (s_len < 11u) {
        return false;
    }
    uint32_t ln = (uint32_t)s_buf[5] | ((uint32_t)s_buf[6] << 8);
    if (ln > BL_FRAME_DATA_MAX || s_len != (11u + ln)) {
        return false;
    }
    uint16_t want = (uint16_t)s_buf[7 + ln] | ((uint16_t)s_buf[8 + ln] << 8);
    if (s_buf[0] != 0xAAu || s_buf[1] != 0x55u ||
        bl_crc16_update(BL_CRC16_INIT, &s_buf[2], (uint32_t)(5 + ln)) != want) {
        return false;   /* CRC/边界不合法：丢弃，等待主机重发 */
    }
    on_frame(s_buf[3], s_buf[4], &s_buf[7], ln);
    return true;
}

void app_request_init(void)
{
    s_len = 0u;
}

void app_request_poll(void)
{
    uint8_t b[32];
    uint32_t n = 0u;
    if (!bl_uart.read(b, sizeof(b), &n) || n == 0u) {
        return;
    }
    for (uint32_t i = 0; i < n; i++) {
        if (s_len >= APP_FRAME_MAX) {
            s_len = 0u;   /* 超长防护 */
        }
        s_buf[s_len++] = b[i];
        if (s_len >= 2u && s_buf[s_len - 2u] == 0x55u && s_buf[s_len - 1u] == 0xAAu) {
            /* EOF 定界点：成帧则分发，伪 EOF（DATA/CRC 内 55 AA）或坏帧丢弃；
               两种情况都清空积累区——伪帧不再等 267B 兜底（review 2026-09-27 P2） */
            try_parse();
            s_len = 0u;
        }
    }
}
