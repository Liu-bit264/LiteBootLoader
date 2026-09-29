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
#if BL_SIGN_EN
    /* ADR-020：启用验签的支持包要求 auth 标志（启动期零密码运算；
       legacy VERIFY 持久化的镜像 auth=0，不构成有效可跳转 APP） */
    if (meta.app_auth != 1u) {
        return false;
    }
#endif
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
    /* 第 8+9 步：读 APP 向量表首项与 Reset Handler，切 MSP 并立即跳转。
       必须单块原子完成——阶段 2 实测教训：先经 C 函数 bl_port_set_msp 切 MSP，
       该函数自身的 POP {r4,pc} 会从"新栈"弹出未初始化垃圾作为 PC，随机跳址
       落入 HardFault（栈帧 PC=0 / LR=set_msp 的 POP 指令，fault 现场实锤）。 */
    uint32_t msp = bl_port_read_word(BL_APP_BASE);
    uint32_t reset = bl_port_read_word(BL_APP_BASE + 4u);
    bl_port_switch_msp_and_jump(msp, reset);   /* 真汇编实现（bl_jump.s），不返回 */
    for (;;) {
    } /* 不可达 */
}
