#include "bl_boot.h"
#include "bl_metadata.h"
#include "bl_crc.h"
#include "bl_port.h"
#include "board_config.h"

/* §5.1 第 1/2 步：MSP 在 SRAM 范围、Reset Handler 在 APP 区且 Thumb 位有效 */
static bool vectors_ok(uint32_t base)
{
    uint32_t msp = bl_port_read_word(base);
    uint32_t reset = bl_port_read_word(base + 4u);

    if (msp < (BL_SRAM_BASE + 64u) || msp > (BL_SRAM_BASE + BL_SRAM_SIZE)) {
        return false;
    }
    if (reset < BL_APP_BASE || reset >= (BL_APP_BASE + BL_APP_SIZE)) {
        return false;
    }
    if ((reset & 1u) == 0u) {
        return false;   /* Thumb 位 */
    }
    return true;
}

bool bl_boot_app_valid(void)
{
    bl_meta_t meta;
    bl_meta_load(&meta);
    if (meta.app_size == 0u || meta.app_size > BL_APP_SIZE) {
        return false;
    }
    if (!vectors_ok(BL_APP_BASE)) {
        return false;
    }
    /* 实时 CRC 重算（失效安全：元数据可信但镜像必须重验证，partition.md §6.3） */
    uint32_t crc = BL_CRC32_INIT;
    uint8_t chunk[256];
    for (uint32_t done = 0; done < meta.app_size; done += sizeof(chunk)) {
        uint32_t n = meta.app_size - done;
        if (n > sizeof(chunk)) {
            n = sizeof(chunk);
        }
        if (!bl_flash.read(BL_APP_BASE + done, chunk, n)) {
            return false;
        }
        crc = bl_crc32_update(crc, chunk, n);
        bl_wdg.refresh();
    }
    crc ^= 0xFFFFFFFFu;
    return crc == meta.app_crc32;
}

void bl_boot_jump(void)
{
    /* 第 4 步：关全局中断 */
    bl_port_disable_irq();
    /* 第 5 步：停 SysTick 并清除状态 */
    bl_port_stop_systick();
    /* 第 6 步：反初始化外设 + 清 pending */
    bl_port_uart_deinit();
    bl_port_i2c_release();
    bl_port_clear_pending_irqs();
    /* 第 7 步：VTOR = APP 基址 */
    bl_port_set_vtor(BL_APP_BASE);
    /* 第 8 步：MSP = APP 向量表首项 */
    uint32_t msp = bl_port_read_word(BL_APP_BASE);
    bl_port_set_msp(msp);
    /* 第 9 步：跳转 Reset Handler（IWDG 保持运行，跳转前已喂狗） */
    uint32_t reset = bl_port_read_word(BL_APP_BASE + 4u);
    bl_port_jump(reset);
    for (;;) {
    } /* 不可达 */
}
