#ifndef BL_DEBUG_H
#define BL_DEBUG_H
/* 调试服务统一 API（design.md ADR-009/ADR-014）：
 * core 只依赖本接口；实现由 services/debug_uart（USART1，协议活跃期静默）
 * 等独立模块链接期提供。换 RTT/CAN 等调试通道只换实现，core 不动。
 * 编译期开关：BL_LOG_DISABLE=1 整体关闭；BL_LOG_LEVEL_DEFAULT 设缺省级。 */
#include <stdint.h>

typedef enum {
    BL_DEBUG_ERROR = 0,
    BL_DEBUG_WARN  = 1,
    BL_DEBUG_INFO  = 2,
    BL_DEBUG_DEBUG = 3,
} bl_debug_level_t;

#ifndef BL_LOG_DISABLE
#define BL_LOG_DISABLE 0
#endif
#ifndef BL_LOG_LEVEL_DEFAULT
#define BL_LOG_LEVEL_DEFAULT BL_DEBUG_INFO
#endif

typedef struct {
    void (*init)(void);
    void (*set_level)(bl_debug_level_t level);
    bl_debug_level_t (*get_level)(void);
    /* 受编译期开关、级别与协议活跃状态门控；fmt 支持最小子集 %s %d %u %x %c %% */
    void (*log)(bl_debug_level_t level, const char *fmt, ...);
} bl_debug_ops;

extern const bl_debug_ops bl_debug;          /* 实现：services/debug_uart */

/* 兼容宏：既有调用点（BL_LOGx）零改动 */
#define BL_LOGE(...) bl_debug.log(BL_DEBUG_ERROR, __VA_ARGS__)
#define BL_LOGW(...) bl_debug.log(BL_DEBUG_WARN, __VA_ARGS__)
#define BL_LOGI(...) bl_debug.log(BL_DEBUG_INFO, __VA_ARGS__)
#define BL_LOGD(...) bl_debug.log(BL_DEBUG_DEBUG, __VA_ARGS__)

#endif /* BL_DEBUG_H */
