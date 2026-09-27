#include "uart2.h"
#include "board_config.h"
#include "bl_port.h"
#include "clock.h"
#include "stm32f10x.h"

/* USART2 PA2/PA3（architecture.md §6.1 通道 1）：HC-05 蓝牙字节流。
 * 与 uart.c 同构：RX 中断 + 环形缓冲，TX 阻塞。
 * 时钟：USART2 挂 APB1，PCLK1 = SYSCLK/2（clock.c 两种时钟路径均固定
 * PPRE1_DIV2，RM0008 §7.2）；HSE 回退 HSI 时为 4MHz，115200 误差 -0.8% 可用。 */
static volatile uint8_t  s_ring[BL_RX_RING_SIZE];
static volatile uint32_t s_head;       /* 写指针（IRQ） */
static volatile uint32_t s_tail;       /* 读指针（主循环） */
static volatile uint32_t s_rx_total;   /* 累计接收字节（含丢弃，诊断） */

void bl_uart_bt_port_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;   /* PA2/PA3 为缺省复用映射，无需 AFIO */
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    /* PA2 = USART2_TX 复用推挽 50MHz，PA3 = RX 浮空输入（CRL：pin0-7） */
    GPIOA->CRL &= ~(GPIO_CRL_MODE2 | GPIO_CRL_CNF2 | GPIO_CRL_MODE3 | GPIO_CRL_CNF3);
    GPIOA->CRL |= GPIO_CRL_MODE2_0 | GPIO_CRL_MODE2_1 | GPIO_CRL_CNF2_1;
    GPIOA->CRL |= GPIO_CRL_CNF3_0;

    uint32_t pclk1 = bl_clock_get_hz() / 2u;
    USART2->BRR = (pclk1 + BL_BT_UART_BAUD / 2u) / BL_BT_UART_BAUD;
    USART2->CR2 = 0u;
    USART2->CR3 = 0u;
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

    NVIC_SetPriority(USART2_IRQn, 1u);
    NVIC_ClearPendingIRQ(USART2_IRQn);
    NVIC_EnableIRQ(USART2_IRQn);

    s_head = s_tail = 0u;
}

void bl_uart_bt_port_deinit(void)
{
    USART2->CR1 = 0u;                     /* UE/TE/RE/RXNEIE 全关 */
    NVIC_DisableIRQ(USART2_IRQn);
    s_head = s_tail = 0u;
    /* 引脚恢复浮空，交 APP 配置 */
    GPIOA->CRL &= ~(GPIO_CRL_MODE2 | GPIO_CRL_CNF2 | GPIO_CRL_MODE3 | GPIO_CRL_CNF3);
    RCC->APB1ENR &= ~RCC_APB1ENR_USART2EN;
}

uint32_t bl_uart_bt_port_rx_pop(uint8_t *buf, uint32_t max)
{
    uint32_t n = 0u;
    while (n < max && s_tail != s_head) {
        buf[n++] = s_ring[s_tail];
        s_tail = (s_tail + 1u) % BL_RX_RING_SIZE;
    }
    return n;
}

uint32_t bl_uart_bt_port_rx_total(void) { return s_rx_total; }

uint32_t bl_uart_bt_port_rx_pending(void)
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

void USART2_IRQHandler(void)
{
    if (USART2->SR & USART_SR_RXNE) {
        ring_push((uint8_t)(USART2->DR & 0xFFu));
    }
    /* ORE/NE/FE 清除：读 SR 后读 DR 即清 */
    if (USART2->SR & (USART_SR_ORE | USART_SR_NE | USART_SR_FE)) {
        (void)USART2->DR;
    }
}

/* ---- bl_uart_ops（通道 1：蓝牙，bl_port.h 声明） ---- */
static void ops_init(void)
{
    bl_uart_bt_port_init();
}

static bool ops_read(uint8_t *buf, uint32_t len, uint32_t *out_len)
{
    *out_len = bl_uart_bt_port_rx_pop(buf, len);
    return true;
}

static bool ops_write(const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        while (!(USART2->SR & USART_SR_TXE)) {
        }
        USART2->DR = buf[i];
    }
    while (!(USART2->SR & USART_SR_TC)) {
    }
    return true;
}

const bl_uart_ops bl_uart_bt = {
    .init = ops_init,
    .read = ops_read,
    .write = ops_write,
};
