/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 EmberBSD contributors.
 * AI-assisted host fixture. The included macros retain their nvi license.
 */
#include <assert.h>
#ifdef NDEBUG
#error This fixture requires assertions
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct win {
	char *tmp_bp;
	size_t tmp_blen;
	unsigned flags;
} WIN;

typedef struct scr {
	WIN *wp;
} SCR;

#define W_TMP_INUSE 0x0001
#define F_SET(p, f) ((p)->flags |= (f))
#define F_CLR(p, f) ((p)->flags &= ~(unsigned)(f))
#define F_ISSET(p, f) ((p)->flags & (f))

static int binc_fail;

static void *
binc(SCR *sp, void *bp, size_t *bsizep, size_t min)
{
	size_t csize;
	void *next;

	(void)sp;
	if (min && *bsizep >= min)
		return bp;
	if (binc_fail)
		return NULL;
	csize = *bsizep + (min > 256 ? min : 256);
	next = realloc(bp, csize ? csize : 1);
	assert(next != NULL);
	memset((char *)next + *bsizep, 0, csize - *bsizep);
	*bsizep = csize;
	return next;
}

#include "nvi-space.inc"

static int
grow_ret(SCR *sp, char *bp, size_t blen, size_t nlen, char **out, size_t *outlen)
{
	ADD_SPACE_RET(sp, char, bp, blen, nlen);
	*out = bp;
	*outlen = blen;
	return 0;
}

static int
grow_goto(SCR *sp, char *bp, size_t blen, size_t nlen, char **out, size_t *outlen)
{
	ADD_SPACE_GOTO(sp, char, bp, blen, nlen);
	*out = bp;
	*outlen = blen;
	return 0;
alloc_err:
	return 1;
}

static char *
sample(const char *text, size_t *len)
{
	char *bp;

	*len = strlen(text);
	bp = malloc(*len);
	assert(bp != NULL);
	memcpy(bp, text, *len);
	return bp;
}

static void
check_window(int (*grow)(SCR *, char *, size_t, size_t, char **, size_t *))
{
	WIN win;
	SCR scr;
	char *out;
	size_t blen, olen;
	char *private_bp;

	memset(&win, 0, sizeof(win));
	win.tmp_bp = sample("wxyz", &win.tmp_blen);
	win.flags = W_TMP_INUSE;
	scr.wp = &win;
	out = NULL;
	olen = 0;
	assert(grow(&scr, win.tmp_bp, win.tmp_blen, 80, &out, &olen) == 0);
	assert(out == win.tmp_bp && olen == win.tmp_blen && olen >= 80);
	assert(memcmp(win.tmp_bp, "wxyz", 4) == 0);
	assert((win.flags & W_TMP_INUSE) != 0);

	private_bp = sample("abcd", &blen);
	out = NULL;
	assert(grow(&scr, private_bp, blen, 80, &out, &olen) == 0);
	if (out == NULL)
		abort();
	assert(out != win.tmp_bp && olen >= 80);
	assert(memcmp(out, "abcd", 4) == 0);
	assert(memcmp(win.tmp_bp, "wxyz", 4) == 0);
	free(out);
	out = NULL;

	binc_fail = 1;
	assert(grow(&scr, win.tmp_bp, win.tmp_blen, win.tmp_blen + 80, &out, &olen) == 1);
	assert(memcmp(win.tmp_bp, "wxyz", 4) == 0);
	assert((win.flags & W_TMP_INUSE) == 0);
	binc_fail = 0;
	free(win.tmp_bp);
}

static void
check_private(SCR *sp, int (*grow)(SCR *, char *, size_t, size_t, char **, size_t *))
{
	char *bp, *out;
	size_t blen, olen;

	bp = sample("abcd", &blen);
	out = NULL;
	olen = 0;
	assert(grow(sp, bp, blen, blen, &out, &olen) == 0);
	if (out == NULL)
		abort();
	assert(out == bp && olen == blen && memcmp(out, "abcd", 4) == 0);
	free(out);
	out = NULL;

	bp = sample("abcd", &blen);
	assert(grow(sp, bp, blen, 64, &out, &olen) == 0);
	if (out == NULL)
		abort();
	assert(olen >= 64 && memcmp(out, "abcd", 4) == 0);
	free(out);
	out = NULL;

	bp = sample("abcd", &blen);
	binc_fail = 1;
	assert(grow(sp, bp, blen, 64, &out, &olen) == 1);
	assert(memcmp(bp, "abcd", 4) == 0);
	binc_fail = 0;
	free(bp);
}

int
main(void)
{
	SCR hidden;

	check_window(grow_ret);
	check_window(grow_goto);
	puts("nvi-space: window buffer and private buffer pass");
	hidden.wp = NULL;
	check_private(&hidden, grow_ret);
	check_private(&hidden, grow_goto);
	check_private(NULL, grow_ret);
	check_private(NULL, grow_goto);
	puts("nvi-space: null window keeps the caller buffer");
	return 0;
}
