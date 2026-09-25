#ifndef BL_LOG_H
#define BL_LOG_H
/* 日志（design.md ADR-009）：与协议共用 USART1，协议活跃期静默；编译期可整体关闭 */
#include <stdint.h>

typedef enum {
    BL_LOG_ERROR = 0,
    BL_LOG_WARN  = 1,
    BL_LOG_INFO  = 2,
    BL_LOG_DEBUG = 3,
} bl_log_level_t;

#ifndef BL_LOG_DISABLE
#define BL_LOG_DISABLE 0
#endif
#ifndef BL_LOG_LEVEL_DEFAULT
#define BL_LOG_LEVEL_DEFAULT BL_LOG_INFO
#endif

void bl_log_init(void);
void bl_log_set_level(bl_log_level_t level);
bl_log_level_t bl_log_get_level(void);
/* 受 BL_LOG_DISABLE 与协议活跃状态门控；fmt 支持最小子集 %s %d %u %x %c %% */
void bl_log_printf(bl_log_level_t level, const char *fmt, ...);

#define BL_LOGE(...) bl_log_printf(BL_LOG_ERROR, __VA_ARGS__)
#define BL_LOGW(...) bl_log_printf(BL_LOG_WARN, __VA_ARGS__)
#define BL_LOGI(...) bl_log_printf(BL_LOG_INFO, __VA_ARGS__)
#define BL_LOGD(...) bl_log_printf(BL_LOG_DEBUG, __VA_ARGS__)

#endif /* BL_LOG_H */
