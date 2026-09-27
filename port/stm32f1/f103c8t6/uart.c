#include "uart.h"
#include "uart2.h"
#include "board_config.h"
#include "bl_port.h"
#include "clock.h"
#include "stm32f10x.h"
static volatile uint8_t  s_ring[BL_RX_RING_SIZE];
static volatile uint32_t s_head;   /* 写指针（IRQ） */
static volatile uint32_t s_tail;   /* 读指针（主循环） */
static volatile uint32_t s_rx_total;   /* 累计接收字节（含丢弃，接线诊断） */

void bl_uart_port_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;

    /* PA9 = USART1_TX 复用推挽 50MHz，PA10 = RX 浮空输入 */
    GPIOA->CRH &= ~(GPIO_CRH_MODE9 | GPIO_CRH_CNF9 | GPIO_CRH_MODE10 | GPIO_CRH_CNF10);
    GPIOA->CRH |= GPIO_CRH_MODE9_0 | GPIO_CRH_MODE9_1 | GPIO_CRH_CNF9_1;
    GPIOA->CRH |= GPIO_CRH_CNF10_0;

    uint32_t pclk2 = bl_clock_get_hz();
    USART1->BRR = (pclk2 + BL_UART_BAUD / 2u) / BL_UART_BAUD;
    USART1->CR2 = 0u;
    USART1->CR3 = 0u;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

    NVIC_SetPriority(USART1_IRQn, 1u);
    NVIC_ClearPendingIRQ(USART1_IRQn);
    NVIC_EnableIRQ(USART1_IRQn);

    s_head = s_tail = 0u;
}

void bl_uart_port_deinit(void)
{
    USART1->CR1 = 0u;                        /* UE/TE/RE/RXNEIE 全关 */
    NVIC_DisableIRQ(USART1_IRQn);
    s_head = s_tail = 0u;
    /* 引脚恢复浮空模拟态，交 APP 配置 */
    GPIOA->CRH &= ~(GPIO_CRH_MODE9 | GPIO_CRH_CNF9 | GPIO_CRH_MODE10 | GPIO_CRH_CNF10);
    RCC->APB2ENR &= ~RCC_APB2ENR_USART1EN;
}

void bl_port_uart_deinit(void)
{
    bl_uart_port_deinit();
    bl_uart_bt_port_deinit();   /* 蓝牙通道随九步跳转第 6 步一并反初始化 */
}

/* 蓝牙通道反初始化弱默认：不含 uart2.c 的目标（APP 示例无蓝牙）链接空实现，
   含 uart2.c 的工程由强符号覆盖（弱符号模式同 bl_display_user_page） */
__weak void bl_uart_bt_port_deinit(void)
{
}

uint32_t bl_uart_port_rx_pop(uint8_t *buf, uint32_t max)
{
    uint32_t n = 0u;
    while (n < max && s_tail != s_head) {
        buf[n++] = s_ring[s_tail];
        s_tail = (s_tail + 1u) % BL_RX_RING_SIZE;
    }
    return n;
}

uint32_t bl_uart_port_rx_total(void) { return s_rx_total; }

uint32_t bl_uart_port_rx_pending(void)
{
    return (s_head + BL_RX_RING_SIZE - s_tail) % BL_RX_RING_SIZE;
}

static void ring_push(uint8_t b)
{
    uint32_t next = (s_head + 1u) % BL_RX_RING_SIZE;
    s_rx_total++;
    if (next == s_tail) {
        return; /* 溢出丢字节：帧同步恢复兜底（protocol.md §4.2） */
    }
    s_ring[s_head] = b;
    s_head = next;
}

void USART1_IRQHandler(void)
{
    if (USART1->SR & USART_SR_RXNE) {
        ring_push((uint8_t)(USART1->DR & 0xFFu));
    }
    /* ORE/NE/FE 清除：读 SR 后读 DR 即清（SR 非错误中断源时无需处理） */
    if (USART1->SR & (USART_SR_ORE | USART_SR_NE | USART_SR_FE)) {
        (void)USART1->DR;
    }
}

/* ---- bl_uart_ops ---- */
static void ops_init(void)
{
    bl_uart_port_init();
}

static bool ops_read(uint8_t *buf, uint32_t len, uint32_t *out_len)
{
    *out_len = bl_uart_port_rx_pop(buf, len);
    return true;
}

static bool ops_write(const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        while (!(USART1->SR & USART_SR_TXE)) {
        }
        USART1->DR = buf[i];
    }
    while (!(USART1->SR & USART_SR_TC)) {
    }
    return true;
}

const bl_uart_ops bl_uart = {
    .init = ops_init,
    .read = ops_read,
    .write = ops_write,
};
