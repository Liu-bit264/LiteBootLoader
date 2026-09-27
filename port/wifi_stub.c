#include "bl_port.h"

/* WIFI 通道占位（规划书目标 2：只预留实现层与 API，不做具体实现）。
 * 与 bl_uart_ops 同签名；不提供接收统计——transport 通道注册表以
 * 统计函数指针缺席判定"未实现"，选路逻辑枚举时自动跳过：
 * 真能编译、真能被识别，而非空注释（architecture.md §6.1）。
 * 实接入时：实现三 ops + 两个统计函数，并保持登记在 core/bl_transport.c。 */
static void wifi_init(void)
{
}

static bool wifi_read(uint8_t *buf, uint32_t len, uint32_t *out_len)
{
    (void)buf;
    (void)len;
    *out_len = 0u;
    return false;
}

static bool wifi_write(const uint8_t *buf, uint32_t len)
{
    (void)buf;
    (void)len;
    return false;
}

const bl_uart_ops bl_wifi = {
    .init = wifi_init,
    .read = wifi_read,
    .write = wifi_write,
};
