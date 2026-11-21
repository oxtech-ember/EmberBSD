/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 EmberBSD contributors.
 * Origin: EmberBSD contributors (AI-assisted).
 */

#ifndef __lint__
#error "This header models x86 intrinsic declarations for lint only"
#endif

#ifndef OPENSSL_LINT_X86_INTRINSICS_H
#define OPENSSL_LINT_X86_INTRINSICS_H

/* Distinguish integer vector widths in ISO C. Clang checks vector operations,
 * alignment, target features, and immediate operands with its actual header. */
typedef struct lint_m128i { unsigned char bytes[16]; } __m128i;
typedef struct lint_m256i { unsigned char bytes[32]; } __m256i;
typedef struct lint_m512i { unsigned char bytes[64]; } __m512i;
typedef unsigned char __mmask8;

#define _MM_HINT_T0 3

__m128i _mm_loadu_si128(const __m128i *);
void _mm_prefetch(const void *, int);
__m128i _mm_setr_epi8(char, char, char, char, char, char, char, char,
    char, char, char, char, char, char, char, char);
__m128i _mm_setzero_si128(void);
void _mm_storeu_si128(__m128i *, __m128i);

__m256i _mm256_add_epi8(__m256i, __m256i);
__m256i _mm256_alignr_epi8(__m256i, __m256i, int);
__m256i _mm256_and_si256(__m256i, __m256i);
__m256i _mm256_andnot_si256(__m256i, __m256i);
__m256i _mm256_blend_epi32(__m256i, __m256i, int);
__m256i _mm256_blendv_epi8(__m256i, __m256i, __m256i);
__m256i _mm256_cmpeq_epi8(__m256i, __m256i);
__m256i _mm256_cmpgt_epi8(__m256i, __m256i);
int _mm256_extract_epi16(__m256i, int);
int _mm256_extract_epi32(__m256i, int);
long long _mm256_extract_epi64(__m256i, int);
int _mm256_extract_epi8(__m256i, int);
__m256i _mm256_mulhi_epu16(__m256i, __m256i);
__m256i _mm256_mullo_epi16(__m256i, __m256i);
__m256i _mm256_or_si256(__m256i, __m256i);
__m256i _mm256_permute2x128_si256(__m256i, __m256i, int);
__m256i _mm256_set_epi8(char, char, char, char, char, char, char, char,
    char, char, char, char, char, char, char, char,
    char, char, char, char, char, char, char, char,
    char, char, char, char, char, char, char, char);
__m256i _mm256_set_m128i(__m128i, __m128i);
__m256i _mm256_set1_epi32(int);
__m256i _mm256_set1_epi8(char);
__m256i _mm256_setr_epi8(char, char, char, char, char, char, char, char,
    char, char, char, char, char, char, char, char,
    char, char, char, char, char, char, char, char,
    char, char, char, char, char, char, char, char);
__m256i _mm256_setzero_si256(void);
__m256i _mm256_shuffle_epi8(__m256i, __m256i);
__m256i _mm256_slli_si256(__m256i, int);
__m256i _mm256_srli_si256(__m256i, int);
void _mm256_storeu_si256(__m256i *, __m256i);
__m256i _mm256_sub_epi8(__m256i, __m256i);
__m256i _mm256_subs_epu8(__m256i, __m256i);

__m512i _mm512_aesdec_epi128(__m512i, __m512i);
__m512i _mm512_aesdeclast_epi128(__m512i, __m512i);
__m512i _mm512_broadcast_i32x4(__m128i);
__m512i _mm512_loadu_si512(const void *);
__m512i _mm512_mask_blend_epi64(__mmask8, __m512i, __m512i);
void _mm512_mask_storeu_epi64(void *, __mmask8, __m512i);
__m512i _mm512_maskz_loadu_epi64(__mmask8, const void *);
__m512i _mm512_permutexvar_epi64(__m512i, __m512i);
__m512i _mm512_set_epi64(long long, long long, long long, long long,
    long long, long long, long long, long long);
void _mm512_storeu_si512(void *, __m512i);
__m512i _mm512_xor_si512(__m512i, __m512i);

#endif
