

#define BIT_SET 0x4
#define BIT_CLR 0x8
#define BIT_TOG 0xc

        .text
        .global timer_fiq_handler, timer_fiq_handler_end

timer_fiq_handler:
    push {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,lr}
    bl c_irq_handler
    pop  {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,lr}
    subs pc, lr, #4
timer_fiq_handler_end:
