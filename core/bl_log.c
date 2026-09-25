#include "bl_log.h"
#include "bl_port.h"
#include "bl_protocol.h"
#include <stdarg.h>

#if !BL_LOG_DISABLE

static bl_log_level_t s_level = BL_LOG_LEVEL_DEFAULT;

static const char k_tag[4][3] = { "E:", "W:", "I:", "D:" };

void bl_log_init(void) { s_level = BL_LOG_LEVEL_DEFAULT; }

void bl_log_set_level(bl_log_level_t level) { s_level = level; }

bl_log_level_t bl_log_get_level(void) { return s_level; }

static void emit_char(char c)
{
    (void)bl_uart.write((const uint8_t *)&c, 1);
}

static void emit_str(const char *s)
{
    while (*s) {
        emit_char(*s++);
    }
}

static void emit_hex(uint32_t v)
{
    char buf[8];
    int i = 0;
    if (v == 0) {
        emit_char('0');
        return;
    }
    while (v) {
        uint32_t d = v & 0xFu;
        buf[i++] = (char)((d < 10u) ? ('0' + d) : ('A' + d - 10u));
        v >>= 4;
    }
    while (i) {
        emit_char(buf[--i]);
    }
}

static void emit_dec(uint32_t v)
{
    char buf[11];
    int i = 0;
    if (v == 0) {
        buf[i++] = '0';
    }
    while (v) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i) {
        emit_char(buf[--i]);
    }
}

static void emit_int(int32_t v)
{
    if (v < 0) {
        emit_char('-');
        emit_dec((uint32_t)(-v));
    } else {
        emit_dec((uint32_t)v);
    }
}

void bl_log_printf(bl_log_level_t level, const char *fmt, ...)
{
    if (BL_LOG_DISABLE) {
        return;
    }
    if ((uint32_t)level > (uint32_t)s_level) {
        return;
    }
    if (bl_protocol_is_active(bl_clock.tick_ms())) {
        return; /* 协议活跃期静默（ADR-009） */
    }

    emit_str(k_tag[(uint32_t)level & 3u]);

    va_list ap;
    va_start(ap, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            emit_char(*p);
            continue;
        }
        p++;
        switch (*p) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            emit_str(s ? s : "(null)");
            break;
        }
        case 'd': {
            emit_int(va_arg(ap, int32_t));
            break;
        }
        case 'u': {
            emit_dec(va_arg(ap, uint32_t));
            break;
        }
        case 'x': {
            emit_hex(va_arg(ap, uint32_t));
            break;
        }
        case 'c': {
            emit_char((char)va_arg(ap, int));
            break;
        }
        case '%':
            emit_char('%');
            break;
        default:
            emit_char('%');
            emit_char(*p);
            break;
        }
        if (*p == '\0') {
            break;
        }
    }
    va_end(ap);
    emit_char('\r');
    emit_char('\n');
}

#else /* BL_LOG_DISABLE */

void bl_log_init(void) {}
void bl_log_set_level(bl_log_level_t level) { (void)level; }
bl_log_level_t bl_log_get_level(void) { return BL_LOG_LEVEL_DEFAULT; }
void bl_log_printf(bl_log_level_t level, const char *fmt, ...)
{
    (void)level;
    (void)fmt;
}

#endif /* BL_LOG_DISABLE */
