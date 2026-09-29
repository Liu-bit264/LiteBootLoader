#ifndef BL_DISPLAY_H
#define BL_DISPLAY_H
/* 显示服务统一 API（architecture.md §8；ADR-014）：
 * core 只依赖本接口，具体实现由 services/display_oled（OLED+LED 状态指示）等
 * 独立模块链接期提供，绑定风格与 port/bl_port.h 的 ops 一致。
 * 约定：所有周期调用必须非阻塞（LED 状态机每次调用更新；屏幕限频分片刷新）。 */
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BL_DISPLAY_WAITING = 0,     /* 等待升级：LED 1Hz 慢闪 */
    BL_DISPLAY_UPGRADING,       /* 正在升级：LED 5Hz 快闪 + OLED 进度 */
    BL_DISPLAY_APP_INVALID,     /* APP 无效/校验失败：LED 双闪 */
    BL_DISPLAY_JUMPING,         /* 即将跳转：LED 常亮 */
    BL_DISPLAY_FAULT,           /* 致命错误：LED 三短闪 */
} bl_display_state_t;

typedef struct {
    void (*init)(void);
    void (*set_state)(bl_display_state_t state);
    void (*set_progress)(uint8_t percent);   /* 0-100 */
    void (*set_crc_ok)(bool ok);             /* VERIFY 结果展示 */
    void (*tick)(uint32_t now_ms);           /* LED 模式更新 + OLED 限频分片刷 */
} bl_display_ops;

extern const bl_display_ops bl_display;      /* 实现：services/display_oled */

/* 用户自检页面扩展点（AGENTS §7.1）：弱符号，默认空。
 * BL_DISPLAY_USER_PAGE=1 时，显示服务在等待/升级模式下以本钩子替代标准页，
 * 用户覆盖后经 bsp ssd1306 自行绘制与限频。 */
void bl_display_user_page(void);

#endif /* BL_DISPLAY_H */
