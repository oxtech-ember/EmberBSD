/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD vendor recovery MCU7502 fixed actions. */
/* MCU_ACTION: 0 = arm60, 1 = feed60, 2 = disable. */
#ifndef MCU_ACTION
#error MCU_ACTION must be selected explicitly
#endif
#if MCU_ACTION < 0 || MCU_ACTION > 2
#error invalid MCU_ACTION
#endif
.global _start
.section .text
_start:
    mov x0, #-100
    adr x1, device_path
    mov x2, #2
    mov x3, #0
    mov x8, #56
    svc #0
    tbnz x0, #63, open_failed
    mov x19, x0
    adr x20, results
    mov x21, #4
    mov w1, #0x4d0b
    movk w1, #0x8002, lsl #16
    mov x2, #0
    bl call_ioctl
    str x0, [x20]
    mov w3, #0x0307
    movk w3, #0x17, lsl #16
    cmp x0, x3
    b.ne finish
    mov w1, #0x4d0c
    movk w1, #0x8002, lsl #16
    mov x2, #0
    bl call_ioctl
    str x0, [x20, #8]
    cmp x0, #255
    b.hi finish
#if MCU_ACTION == 2
    mov w1, #0x4d02
    movk w1, #0x4002, lsl #16
    mov x2, #0
#else
    mov w1, #0x4d03
    movk w1, #0x4002, lsl #16
    mov x2, #60
#endif
    bl call_ioctl
    str x0, [x20, #16]
    cmp x0, #7
    b.ne finish
    bl pause_100ms
    cbnz x0, finish
#if MCU_ACTION == 0
    mov w1, #0x4d01
    movk w1, #0x4002, lsl #16
    mov x2, #0
    bl call_ioctl
    str x0, [x20, #24]
    cmp x0, #7
    b.ne finish
    bl pause_100ms
    cbnz x0, finish
    /* Set the 60-second period again after enable; no failed call is retried. */
    mov w1, #0x4d03
    movk w1, #0x4002, lsl #16
    mov x2, #60
    bl call_ioctl
    str x0, [x20, #32]
    cmp x0, #7
    b.ne finish
    bl pause_100ms
    cbnz x0, finish
#endif
    mov w1, #0x4d0c
    movk w1, #0x8002, lsl #16
    mov x2, #0
    bl call_ioctl
    str x0, [x20, #40]
    cmp x0, #255
    b.hi finish
    mov x3, #0xc360
    str x3, [x20, #48]
    mov x21, #0
finish:
    mov x0, x19
    mov x8, #57
    svc #0
    mov x0, #1
    mov x1, x20
    mov x2, #56
    mov x8, #64
    svc #0
    cmp x0, #56
    b.ne write_failed
    mov x0, x21
    b exit
call_ioctl:
    mov x0, x19
    mov x8, #29
    svc #0
    ret
pause_100ms:
    adr x0, delay
    mov x1, #0
    mov x8, #101
    svc #0
    ret
open_failed:
    mov x0, #2
    b exit
write_failed:
    mov x0, #3
exit:
    mov x8, #93
    svc #0
.section .rodata
.balign 8
delay:
    .quad 0, 100000000
device_path:
    .asciz "/dev/mcu_7502"
.section .bss
.balign 8
results:
    .skip 56
.section .note.GNU-stack,"",%progbits
