/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 EmberBSD contributors.
 * AI-assisted host fixture; the included del function retains its nvi license.
 */
#include <assert.h>
#ifdef NDEBUG
#error This fixture requires assertions
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef CHAR_T
#define CHAR_T uint32_t
#endif
typedef unsigned long db_recno_t;
typedef struct { unsigned rptlines[1]; } SCR;
typedef struct { db_recno_t lno; size_t cno; } MARK;
#define L_DELETED 0
#define DBG_FATAL 0
#define M_ERR 0
#define ENTIRE_LINE SIZE_MAX
#define INTERRUPT_CHECK 100
#define INTERRUPTED(sp) 0
static CHAR_T source[4] = { 'a', 'b', 'c', 'd' };
static size_t first_length, allocated_chars;
static int messages, allocations, get_failed, large_probe;
static int
db_get(SCR *sp, db_recno_t n, int flags, CHAR_T **p, size_t *len)
{
	(void)sp; (void)flags;
	if (get_failed)
		return 1;
	*p = source;
	if (len != NULL)
		*len = n == 1 ? first_length : 0;
	return 0;
}
static int db_last(SCR *sp, db_recno_t *n) { (void)sp; *n = 3; return 0; }
static int db_delete(SCR *sp, db_recno_t n) { (void)sp; (void)n; return 0; }
static int
db_set(SCR *sp, db_recno_t n, const CHAR_T *p, size_t len)
{
	(void)sp; (void)n;
	assert(len <= 4);
	if (len)
		assert(memcmp(p, source, len * sizeof(*p)) == 0);
	return 0;
}
static void msgq(SCR *sp, int type, const char *text)
{ (void)sp; (void)type; (void)text; messages++; }
/* Observe the real function's allocation request without allocating SIZE_MAX. */
#define GET_SPACE_RETW(sp,bp,blen,nlen) do { \
	allocated_chars = (nlen); allocations++; \
	if (large_probe) return 1; \
	assert(allocated_chars <= 260); \
	blen = allocated_chars * sizeof(CHAR_T); \
	bp = malloc(blen ? blen : 1); assert(bp != NULL); \
} while (0)
#define ADD_SPACE_RETW(sp,bp,blen,nlen) GET_SPACE_RETW(sp,bp,blen,nlen)
#define FREE_SPACEW(sp,bp,blen) do { (void)(blen); free(bp); } while (0)
#define MEMCPYW(a,b,n) memcpy(a,b,(n)*sizeof(CHAR_T))
#include "nvi-function.inc"

int
main(void)
{
	SCR scr = { { 0 } };
	MARK from = { 1, 2 }, to = { 2, 0 };
	first_length = 4;
	assert(del(&scr, &from, &to, 0) == 0);
	assert(allocations == 1 && allocated_chars == 258 && messages == 0);
	from.cno = 100;
	allocations = 0;
	assert(del(&scr, &from, &to, 0) == 0);
	assert(allocations == 1 && allocated_chars == 260);
	get_failed = 1;
	allocations = 0;
	assert(del(&scr, &from, &to, 0) == 1 && allocations == 0);
	get_failed = 0;
	large_probe = 1;
	first_length = from.cno = SIZE_MAX / sizeof(CHAR_T) - 256;
	assert(del(&scr, &from, &to, 0) == 1);
	assert(allocations == 1 && allocated_chars == first_length + 256);
	first_length = ++from.cno;
	allocations = 0;
	assert(del(&scr, &from, &to, 0) == 1);
	assert(allocations == 0 && messages == 1);
	first_length = from.cno = SIZE_MAX;
	assert(del(&scr, &from, &to, 0) == 1);
	assert(allocations == 0 && messages == 2);
	puts("nvi: normal, clamped, read failure and allocation boundaries pass");
	return 0;
}
