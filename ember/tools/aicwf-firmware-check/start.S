/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; own Cortex-M4 vectors and ABI observation trampoline. */
.syntax unified
.thumb
.section .vectors,"a",%progbits
.global vectors
vectors:
.word 0x203ff000
.word reset
.rept 14
.word fault
.endr
.rept 31
.word fault
.endr
.word irq31

.section .text.start,"ax",%progbits
.global reset
.thumb_func
reset:
 cpsid i
 ldr r0, =__bss_start
 ldr r1, =__bss_end
 movs r2, #0
1: cmp r0, r1
 bhs 2f
 str r2, [r0], #4
 b 1b
2: bl harness_main
 b fault
.thumb_func
fault:
 bl harness_assert
 b fault
.thumb_func
irq31:
.global harness_irq_entry
harness_irq_entry:
 push {r4, lr}
 bl harness_irq
 pop {r4, pc}

.global abi_call
.thumb_func
abi_call:
 push {r4-r11, lr}
 sub sp, sp, #20
 str r1, [sp, #4]
 ldr r2, [r0, #20]
 str r2, [sp]
 ldr r12, [r0]
 mov r3, r0
 ldr r0, [r3, #4]
 ldr r1, [r3, #8]
 ldr r2, [r3, #12]
 ldr r3, [r3, #16]
 movw r4, #0x4444
 movw r5, #0x5555
 movw r6, #0x6666
 movw r7, #0x7777
 movw r8, #0x8888
 movw r9, #0x9999
 movw r10, #0xaaaa
 movw r11, #0xbbbb
 .global harness_before_call
harness_before_call:
 blx r12
 ldr r12, [sp, #4]
 str r0, [r12]
 str r1, [r12, #4]
 mrs r2, primask
 str r2, [r12, #8]
 movs r2, #0
.macro check reg, value
 movw r0, #\value
 eor r3, \reg, r0
 orrs r2, r3
.endm
 check r4, 0x4444
 check r5, 0x5555
 check r6, 0x6666
 check r7, 0x7777
 check r8, 0x8888
 check r9, 0x9999
 check r10, 0xaaaa
 check r11, 0xbbbb
 str r2, [r12, #12]
 .global harness_call_returned
harness_call_returned:
 .rept 8
 nop
 .endr
 .global harness_before_release
harness_before_release:
 add sp, sp, #20
 pop {r4-r11, pc}

.section .stub.rng,"ax",%progbits
.thumb_func
rng_stub:
 b.w harness_rng
.section .stub.assert,"ax",%progbits
.thumb_func
assert_stub:
 b.w harness_assert
