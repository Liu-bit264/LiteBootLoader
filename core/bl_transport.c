#include "bl_transport.h"
#include "bl_port.h"
#include "board_config.h"

/* 通道注册表（architecture.md §6.1）：USART1 有线 / UART2 蓝牙 / WIFI 占位。
 * WIFI stub 不提供统计符号（wifi_stub.c），注册表以统计函数指针缺席判定
 * "未实现"，init 时跳过——预留真能编译、真能被选路识别（规划书目标 2）。 */
typedef struct {
    const bl_uart_ops *ops;
    uint32_t (*rx_total)(void);
    uint32_t (*rx_pending)(void);
    bool     present;
    uint32_t last_byte_ms;   /* 活动通道最后收到字节的时刻 */
} chan_t;

static chan_t s_chans[] = {
    /* [0] WIRED */ { &bl_uart,    bl_uart_port_rx_total,    bl_uart_port_rx_pending,    false, 0u },
    /* [1] BT    */ { &bl_uart_bt, bl_uart_bt_port_rx_total, bl_uart_bt_port_rx_pending, false, 0u },
    /* [2] WIFI  */ { &bl_wifi,    0,                        0,                          false, 0u },
};
#define CHAN_COUNT (sizeof(s_chans) / sizeof(s_chans[0]))

static int32_t s_active = -1;   /* 活动通道（正在收帧）；-1 = 无 */

/* 活动通道静默释放：与协议解析器帧内字节超时同一窗口——半帧仍可能在途时不
   切换通道，避免两路字节交错进同一全局解析器（protocol 层无感切换的仲裁基础） */
static int32_t active_or_expire(void)
{
    if (s_active >= 0) {
        uint32_t now = bl_clock.tick_ms();
        if ((uint32_t)(now - s_chans[s_active].last_byte_ms) >= BL_FRAME_BYTE_TIMEOUT_MS) {
            s_active = -1;
        }
    }
    return s_active;
}

void bl_transport_init(void)
{
    s_active = -1;
    for (uint32_t i = 0; i < CHAN_COUNT; i++) {
        if (s_chans[i].rx_total == 0) {
            continue;   /* 占位通道（WIFI stub）：无统计实现 = 未实现 */
        }
        s_chans[i].ops->init();
        s_chans[i].present = true;
    }
}

bool bl_transport_send(const uint8_t *buf, uint32_t len)
{
    int32_t act = active_or_expire();
    uint32_t ch = (act >= 0) ? (uint32_t)act : (uint32_t)BL_TRANSPORT_CH_WIRED;
    return s_chans[ch].ops->write(buf, len);
}

uint32_t bl_transport_recv(uint8_t *buf, uint32_t max)
{
    int32_t act = active_or_expire();
    if (act >= 0) {
        uint32_t n = 0;
        if (s_chans[act].ops->read(buf, max, &n) && n != 0u) {
            s_chans[act].last_byte_ms = bl_clock.tick_ms();
            return n;
        }
        return 0u;   /* 锁定期间不采样其他通道（双主机互斥，architecture.md §6.1） */
    }
    for (uint32_t i = 0; i < CHAN_COUNT; i++) {
        if (!s_chans[i].present) {
            continue;
        }
        uint32_t n = 0;
        if (s_chans[i].ops->read(buf, max, &n) && n != 0u) {
            s_active = (int32_t)i;
            s_chans[i].last_byte_ms = bl_clock.tick_ms();
            return n;
        }
    }
    return 0u;
}

uint8_t bl_transport_active_channel(void)
{
    return (active_or_expire() >= 0) ? (uint8_t)s_active : (uint8_t)BL_TRANSPORT_CH_NONE;
}

uint32_t bl_transport_rx_total(void)
{
    uint32_t sum = 0u;
    for (uint32_t i = 0; i < CHAN_COUNT; i++) {
        if (s_chans[i].rx_total != 0) {
            sum += s_chans[i].rx_total();
        }
    }
    return sum;
}

uint32_t bl_transport_rx_pending(void)
{
    uint32_t sum = 0u;
    for (uint32_t i = 0; i < CHAN_COUNT; i++) {
        if (s_chans[i].rx_pending != 0) {
            sum += s_chans[i].rx_pending();
        }
    }
    return sum;
}
