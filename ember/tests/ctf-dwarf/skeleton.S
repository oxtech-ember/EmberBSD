/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted), a split CU tied to supplementary types. */
#if DWARF64
#define OFFSET .quad
#define LENGTH(end, body) .long 0xffffffff; .quad end-body
#else
#define OFFSET .long
#define LENGTH(end, body) .long end-body
#endif
	.file "sup-fixture.c"
	.section .debug_abbrev,"",%progbits
	.byte 1,0x4a,0 /* skeleton_unit */
	.byte 0x76,8 /* dwo_name/string */
	.byte 0,0,0
	.section .debug_info,"",%progbits
	LENGTH(.Lend,.Lbody)
.Lbody:
	.short 5
	.byte 4,8 /* skeleton, address size */
	OFFSET 0
	.quad 0x454d42455244574f
	.byte 1
	.asciz "input.dwo"
.Lend:
	.data
	.globl ember_sup_global
	.type ember_sup_global,%object
ember_sup_global:
	.zero 16
	.size ember_sup_global,16
	.globl ember_sup_inherited
	.type ember_sup_inherited,%object
ember_sup_inherited:
	.zero 4
	.size ember_sup_inherited,4
