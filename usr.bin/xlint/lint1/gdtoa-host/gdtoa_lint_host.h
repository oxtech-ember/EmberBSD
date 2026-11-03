/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted). Private host interface for lint's gdtoa. */
#ifndef GDTOA_LINT_HOST_H
#define GDTOA_LINT_HOST_H
#include <assert.h>
#include <stdint.h>
#define GDTOA_LOCALE_TYPE void *
#ifndef __UNCONST
#define __UNCONST(a) ((void *)(uintptr_t)(const void *)(a))
#endif
#ifndef _DIAGASSERT
#define _DIAGASSERT(a) assert(a)
#endif
#ifndef __type_fit
#define __type_fit(t, v) ((t)(v) == (v))
#endif

/* Avoid collisions with libc gdtoa when lint1 is linked statically. */
#define __Balloc_D2A lint_gdtoa_Balloc_D2A
#define __Bfree_D2A lint_gdtoa_Bfree_D2A
#define __any_on_D2A lint_gdtoa_any_on_D2A
#define __b2d_D2A lint_gdtoa_b2d_D2A
#define __bigtens_D2A lint_gdtoa_bigtens_D2A
#define __cmp_D2A lint_gdtoa_cmp_D2A
#define __copybits_D2A lint_gdtoa_copybits_D2A
#define __d2b_D2A lint_gdtoa_d2b_D2A
#define __decrement_D2A lint_gdtoa_decrement_D2A
#define __diff_D2A lint_gdtoa_diff_D2A
#define __dtoa_result_D2A lint_gdtoa_dtoa_result_D2A
#define __freedtoa lint_gdtoa_freedtoa
#define __gdtoa lint_gdtoa_gdtoa
#define __gethex_D2A lint_gdtoa_gethex_D2A
#define __hexdig_D2A lint_gdtoa_hexdig_D2A
#define __hexdig_init_D2A lint_gdtoa_hexdig_init_D2A
#define __hexnan_D2A lint_gdtoa_hexnan_D2A
#define __hi0bits_D2A lint_gdtoa_hi0bits_D2A
#define __i2b_D2A lint_gdtoa_i2b_D2A
#define __increment_D2A lint_gdtoa_increment_D2A
#define __lo0bits_D2A lint_gdtoa_lo0bits_D2A
#define __lshift_D2A lint_gdtoa_lshift_D2A
#define __match_D2A lint_gdtoa_match_D2A
#define __mult_D2A lint_gdtoa_mult_D2A
#define __multadd_D2A lint_gdtoa_multadd_D2A
#define __nrv_alloc_D2A lint_gdtoa_nrv_alloc_D2A
#define __pow5mult_D2A lint_gdtoa_pow5mult_D2A
#define __quorem_D2A lint_gdtoa_quorem_D2A
#define __ratio_D2A lint_gdtoa_ratio_D2A
#define __rshift_D2A lint_gdtoa_rshift_D2A
#define __rv_alloc_D2A lint_gdtoa_rv_alloc_D2A
#define __s2b_D2A lint_gdtoa_s2b_D2A
#define __set_ones_D2A lint_gdtoa_set_ones_D2A
#define __strcp_D2A lint_gdtoa_strcp_D2A
#define __strtodg_D2A lint_gdtoa_strtodg_D2A
#define __tens_D2A lint_gdtoa_tens_D2A
#define __tinytens_D2A lint_gdtoa_tinytens_D2A
#define __trailz_D2A lint_gdtoa_trailz_D2A
#endif
