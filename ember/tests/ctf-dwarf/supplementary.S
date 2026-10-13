/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted), DWARF5 7.3.6 supplementary-object fixture. */
/* Preprocess with -DSUPPLEMENT=0/1, -DDWARF64=0/1 and -DREF8=0/1. */
#if DWARF64
#define OFFSET .quad
#define LENGTH(end, body) .long 0xffffffff; .quad end-body
#else
#define OFFSET .long
#define LENGTH(end, body) .long end-body
#endif
#if REF8
#define REF .quad
#define REFFORM 0x24 /* DW_FORM_ref_sup8 */
#else
#define REF .long
#define REFFORM 0x1c /* DW_FORM_ref_sup4 */
#endif
	.file "sup-fixture.c"
	.section .debug_sup,"",%progbits
	.short 5
	.byte SUPPLEMENT
#if SUPPLEMENT
	.asciz ""
#else
	.asciz "types.sup"
#endif
	.uleb128 4
	.byte 0x45,0x4d,0x42,0x52

#if SPLIT
	.section .debug_abbrev.dwo,"",%progbits
#else
	.section .debug_abbrev,"",%progbits
#endif
#if SUPPLEMENT
	.uleb128 1; .uleb128 0x3c; .byte 1 /* partial_unit */
	.byte 0,0
	.uleb128 2; .uleb128 0x13; .byte 1 /* structure_type */
	.uleb128 3; .uleb128 8 /* name/string */
	.uleb128 0x0b; .uleb128 0x0b /* byte_size/data1 */
	.byte 0,0
	.uleb128 3; .uleb128 0x0d; .byte 0 /* member */
	.uleb128 3; .uleb128 8
	.uleb128 0x49; .uleb128 0x10 /* type/ref_addr: cross-CU */
#if MEMEXPR
	.uleb128 0x38; .uleb128 0x18 /* data_member_location/exprloc */
#else
	.uleb128 0x38; .uleb128 0x0b /* data_member_location/data1 */
#endif
	.byte 0,0
	.uleb128 4; .uleb128 0x24; .byte 0 /* base_type */
	.uleb128 3; .uleb128 8
	.uleb128 0x0b; .uleb128 0x0b
	.uleb128 0x3e; .uleb128 0x0b /* encoding/data1 */
	.byte 0,0
	.uleb128 5; .uleb128 0x16; .byte 0 /* typedef */
	.uleb128 3; .uleb128 8
	.uleb128 0x49; .uleb128 0x13 /* type/ref4: CU-relative */
	.byte 0,0
	.uleb128 6; .uleb128 0x34; .byte 0 /* variable declaration */
#if BAD_ORIGIN
	.uleb128 0x47; .uleb128 0x13 /* invalid specification/ref4 across CUs */
#else
	.uleb128 0x47; .uleb128 0x10 /* specification/ref_addr: second CU */
#endif
	.uleb128 0x3c; .uleb128 0x19 /* declaration/flag_present */
	.byte 0,0
	.uleb128 7; .uleb128 0x34; .byte 0 /* variable declaration */
	.uleb128 0x49; .uleb128 0x13 /* type/ref4: second CU-relative */
	.uleb128 0x3c; .uleb128 0x19
	.byte 0,0
#else
	.uleb128 1; .uleb128 0x11; .byte 1 /* compile_unit */
	.uleb128 3; .uleb128 0x1d /* name/strp_sup */
	.uleb128 0x13; .uleb128 0x0b /* language/data1 */
	.byte 0,0
	.uleb128 2; .uleb128 0x34; .byte 0 /* variable */
	.uleb128 3; .uleb128 0x1d
#if LOCAL_COLLISION
	.uleb128 0x49; .uleb128 0x13 /* invalid local type reference */
#else
	.uleb128 0x49; .uleb128 REFFORM /* type/ref_sup4 or ref_sup8 */
#endif
	.uleb128 0x3f; .uleb128 0x19 /* external/flag_present */
	.byte 0,0
	.uleb128 3; .uleb128 0x3d; .byte 0 /* imported_unit */
	.uleb128 0x18; .uleb128 REFFORM /* import/ref_sup4 or ref_sup8 */
	.byte 0,0
	.uleb128 4; .uleb128 0x34; .byte 0 /* variable definition */
	.uleb128 3; .uleb128 0x1d /* name/strp_sup */
	.uleb128 0x47; .uleb128 REFFORM /* specification/ref_sup4 or ref_sup8 */
	.uleb128 0x3f; .uleb128 0x19
	.byte 0,0
#endif
	.byte 0

#if SPLIT
	.section .debug_info.dwo,"",%progbits
#else
	.section .debug_info,"",%progbits
#endif
.Linfo:
	LENGTH(.Lend, .Lbody)
.Lbody:
	.short 5
#if SUPPLEMENT
	.byte 3 /* DW_UT_partial */
#elif SPLIT
	.byte 5 /* DW_UT_split_compile */
#else
	.byte 1 /* DW_UT_compile */
#endif
	.byte 8
	OFFSET 0 /* abbrev offset */
#if SPLIT
	.quad 0x454d42455244574f
#endif
.Lroot:
	.uleb128 1
#if SUPPLEMENT
	.uleb128 6; OFFSET .Ldecl2-.Linfo
.Lrecord:
	.uleb128 2; .asciz "ember_sup_record"; .byte 16
	.uleb128 3; .asciz "id"; OFFSET .Llong-.Linfo
#if MEMEXPR
	.uleb128 2; .byte 0x23,0 /* plus_uconst 0 */
#else
	.byte 0
#endif
	.uleb128 3; .asciz "flags"; OFFSET .Lalias-.Linfo
#if MEMEXPR == 2
	.uleb128 4; .byte 0x23,8,0x23,1 /* Reject unsupported multi-op offsets. */
#elif MEMEXPR
	.uleb128 2; .byte 0x23,8 /* plus_uconst 8 */
#else
	.byte 8
#endif
	.byte 0
	.byte 0
.Lend:
/* A separate CU catches conflation of CU-relative and file-relative offsets. */
.Lunit2:
	LENGTH(.Lend2, .Lbody2)
.Lbody2:
	.short 5; .byte 3,8; OFFSET 0
	.uleb128 1
.Llong:
	.uleb128 4; .asciz "long int"; .byte 8,5
.Lunsigned:
	.uleb128 4; .asciz "unsigned int"; .byte 4,7
.Lalias:
	.uleb128 5; .asciz "ember_sup_unsigned"; .long .Lunsigned-.Lunit2
.Ldecl2:
	.uleb128 7; .long .Lunsigned-.Lunit2
	.byte 0
.Lend2:
	.section .debug_str,"",%progbits
	.asciz "sup-fixture.c"
	.asciz "ember_sup_global"
	.asciz "ember_sup_inherited"
#else
	OFFSET 0 /* sup-fixture.c in supplementary .debug_str */
	.byte 0x1d /* C11 */
	.uleb128 2; OFFSET 14
#if LOCAL_COLLISION
	.long .Lend-.Linfo+18 /* main extent plus supplementary struct offset */
#elif DWARF64
	REF 34 /* root plus declaration */
#else
	REF 18 /* root plus declaration */
#endif
	.uleb128 4; OFFSET 31
#if DWARF64
	REF 25 /* declaration preceding the struct */
#else
	REF 13
#endif
	.uleb128 3
#if DWARF64
	REF 24
#else
	REF 12
#endif
	.byte 0
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
#endif
