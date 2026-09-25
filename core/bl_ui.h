#ifndef BL_UI_H
#define BL_UI_H
/* UI（architecture.md §8）：LED 模式 + OLED 限频分片刷新，全部非阻塞 */
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BL_UI_WAITING = 0,     /* 等待升级：1Hz 慢闪 */
    BL_UI_UPGRADING,       /* 正在升级：5Hz 快闪 + 进度 */
    BL_UI_APP_INVALID,     /* APP 无效/校验失败：双闪 */
    BL_UI_JUMPING,         /* 即将跳转：常亮 */
    BL_UI_FAULT,           /* 致命错误：三短闪 */
} bl_ui_state_t;

void bl_ui_init(void);
void bl_ui_set_state(bl_ui_state_t state);
void bl_ui_set_progress(uint8_t percent);     /* 0-100 */
void bl_ui_set_crc_ok(bool ok);               /* VERIFY 结果展示 */
void bl_ui_tick(uint32_t now_ms);

#endif /* BL_UI_H */
