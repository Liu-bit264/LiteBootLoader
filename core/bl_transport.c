#include "bl_transport.h"
#include "bl_port.h"
#include "uart.h"

void bl_transport_init(void) { bl_uart.init(); }

bool bl_transport_send(const uint8_t *buf, uint32_t len)
{
    return bl_uart.write(buf, len);
}

uint32_t bl_transport_recv(uint8_t *buf, uint32_t max)
{
    uint32_t n = 0;
    bl_uart.read(buf, max, &n);
    return n;
}

uint32_t bl_transport_rx_total(void) { return bl_uart_port_rx_total(); }

uint32_t bl_transport_rx_pending(void) { return bl_uart_port_rx_pending(); }
