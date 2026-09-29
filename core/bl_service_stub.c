/* 可选服务的弱默认实现（实验：服务非强制挂载）。
 * core 对显示/调试只依赖接口（bl_display.h/bl_debug.h）；本文件提供 __weak
 * 空实现——工程不链任何显示/调试服务时由它兜底（日志静默、状态指示无），
 * 链接了真实服务（services/display_led|display_oled、services/debug_uart）
 * 时强符号覆盖，弱段被链接器未用段裁剪，零开销。
 * bl_port_i2c_release 同理：无 I2C 的端口不必再提供空定义。 */
#include "bl_display.h"
#include "bl_debug.h"
#include "bl_port.h"

static void stub_display_init(void) {}
static void stub_display_state(bl_display_state_t state) { (void)state; }
static void stub_display_progress(uint8_t percent) { (void)percent; }
static void stub_display_crc(bool ok) { (void)ok; }
static void stub_display_tick(uint32_t now_ms) { (void)now_ms; }

__weak const bl_display_ops bl_display = {
    .init = stub_display_init,
    .set_state = stub_display_state,
    .set_progress = stub_display_progress,
    .set_crc_ok = stub_display_crc,
    .tick = stub_display_tick,
};

static void stub_debug_init(void) {}
static void stub_debug_set_level(bl_debug_level_t level) { (void)level; }
static bl_debug_level_t stub_debug_get_level(void) { return BL_LOG_LEVEL_DEFAULT; }
static void stub_debug_log(bl_debug_level_t level, const char *fmt, ...)
{
    (void)level;
    (void)fmt;
}

__weak const bl_debug_ops bl_debug = {
    .init = stub_debug_init,
    .set_level = stub_debug_set_level,
    .get_level = stub_debug_get_level,
    .log = stub_debug_log,
};

__weak void bl_port_i2c_release(void)
{
}
