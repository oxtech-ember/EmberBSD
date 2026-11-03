/* Portable target-format floating point for lint. */
#ifndef LINT_FP_H
#define LINT_FP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint64_t hi, lo;
} lint_fp_t;
enum lint_fp_format { LINT_FP32, LINT_FP64, LINT_FP80, LINT_FP128 };
enum lint_fp_status {
	LINT_FP_INVALID = 1,
	LINT_FP_DIVZERO = 2,
	LINT_FP_OVERFLOW = 4,
	LINT_FP_UNDERFLOW = 8,
	LINT_FP_INEXACT = 16
};

bool lint_fp_parse(const char *, size_t, enum lint_fp_format, lint_fp_t *,
		   unsigned *);
bool lint_fp_format(lint_fp_t, unsigned, char *, size_t);
lint_fp_t lint_fp_from_i64(int64_t, enum lint_fp_format, unsigned *);
lint_fp_t lint_fp_from_u64(uint64_t, enum lint_fp_format, unsigned *);
bool lint_fp_to_i64(lint_fp_t, int64_t *);
bool lint_fp_to_u64(lint_fp_t, uint64_t *);
lint_fp_t lint_fp_round(lint_fp_t, enum lint_fp_format, unsigned *);
lint_fp_t lint_fp_binary(char, lint_fp_t, lint_fp_t, enum lint_fp_format,
			 unsigned *);
lint_fp_t lint_fp_negate(lint_fp_t);
int lint_fp_compare(lint_fp_t, lint_fp_t); /* -1, 0, 1; 2 means unordered */
bool lint_fp_is_zero(lint_fp_t);
bool lint_fp_is_finite(lint_fp_t);
bool lint_fp_is_nan(lint_fp_t);
bool lint_fp_signbit(lint_fp_t);
lint_fp_t lint_fp_max_finite(enum lint_fp_format);
#endif
