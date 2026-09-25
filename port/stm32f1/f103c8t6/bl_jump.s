; bl_jump.s —— 九步跳转第 8+9 步的原子序列
; r0 = APP 初始 MSP（向量表首项），r1 = APP Reset Handler（Thumb 地址）
; 切 MSP 后立即 BX，不存在任何经由新栈的返回路径。
; 背景（阶段 2 实测教训）：在 C 函数里调 __set_MSP 后，该函数自身的
; POP {r4,pc} 尾声会从"新栈"弹出未初始化垃圾作为 PC（fault 现场实锤：
; 栈帧 PC=0 / LR=set_msp 的 POP 指令），故此序列必须用真汇编实现。
        THUMB
        REQUIRE8
        PRESERVE8
        AREA    |.text|, CODE, READONLY
        EXPORT  bl_port_switch_msp_and_jump
bl_port_switch_msp_and_jump
        MSR     MSP, r0
        BX      r1
        END
