/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted), reject indexed operands crossing their CU. */
/* FORM: GNU str/address or DWARF5 strx/addrx; KIND: missing/truncated/overflow/max. */
.section .debug_abbrev,"",%progbits
.uleb128 1
.uleb128 0x11
.byte 0
.uleb128 0x03
.uleb128 FORM
.byte 0,0
.uleb128 2
.uleb128 0x11
.byte 0
.byte 0,0
.byte 0
.section .debug_info,"",%progbits
.long first_end-first_body
first_body:
#if FORM < 0x100
.short 5
.byte 1,8
.long 0
#else
.short 4
.long 0
.byte 8
#endif
.uleb128 1
#if KIND == 1
.byte 0x80
#elif KIND == 2
.rept 9
.byte 0x80
.endr
.byte 2
#elif KIND == 3
.rept 9
.byte 0xff
.endr
.byte 1
#endif
first_end:
/* A zero low byte would terminate the previous CU's malformed index. */
.long 0x100
second_body:
.short 4
.long 0
.byte 8
.uleb128 2
.space 0x100-(.-second_body),0
.section .debug_str_offsets,"",%progbits
.long 0
.section .debug_str,"MS",%progbits,1
.asciz "cross-cu.c"
