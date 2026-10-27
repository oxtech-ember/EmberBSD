/* SPDX-License-Identifier: BSD-2-Clause */
/* Stable records exercise the word-copy and byte-copy paths at every offset. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct context {
	unsigned int calls;
	int direction;
};

static int
compare(const void *a, const void *b)
{
	const unsigned char *aa = a, *bb = b;

	return (int)aa[0] - (int)bb[0];
}

static int
compare_r(const void *a, const void *b, void *cookie)
{
	struct context *ctx = cookie;

	ctx->calls++;
	return ctx->direction * compare(a, b);
}

static int
check(size_t n, size_t width, size_t offset, unsigned int pattern, int reentrant)
{
	const size_t guard = 16;
	size_t bytes = n * width, length = 2 * guard + offset + bytes;
	unsigned char *allocation = malloc(length), *expected = malloc(bytes + 1);
	unsigned char *base, tmp[16];
	struct context ctx = { 0, -1 };
	int direction = reentrant ? ctx.direction : 1;
	int status, failure;
	size_t i, j, k;

	if (allocation == NULL || expected == NULL) {
		free(allocation);
		free(expected);
		return (1);
	}
	memset(allocation, 0xa5, length);
	base = allocation + guard + offset;
	for (i = 0; i < n; i++) {
		unsigned char *record = base + i * width;
		unsigned int key = (unsigned int)(i * 31 / n);

		if (pattern == 1)
			key = 31 - key;
		else if (pattern == 2)
			key = 7;
		else if (pattern == 3)
			key = (unsigned int)(((i * 73 + 19) ^ (i >> 2)) % 31);
		record[0] = (unsigned char)key;
		record[1] = (unsigned char)i;
		record[2] = (unsigned char)(i >> 8);
		for (k = 3; k < width; k++)
			record[k] = (unsigned char)(i * 17 + k * 3);
	}
	memcpy(expected, base, bytes);
	/* Independent stable insertion oracle, comparing only the first byte. */
	for (i = 1; i < n; i++) {
		memcpy(tmp, expected + i * width, width);
		for (j = i; j > 0 && direction * compare(expected +
		    (j - 1) * width, tmp) > 0; j--)
			memmove(expected + j * width, expected + (j - 1) * width, width);
		memcpy(expected + j * width, tmp, width);
	}
	status = reentrant ? mergesort_r(base, n, width, compare_r, &ctx) :
	    mergesort(base, n, width, compare);
	failure = status != 0 || memcmp(base, expected, bytes) != 0 ||
	    (reentrant && n > 1 && ctx.calls == 0);
	for (i = 0; i < guard + offset; i++)
		failure |= allocation[i] != 0xa5;
	for (i = guard + offset + bytes; i < length; i++)
		failure |= allocation[i] != 0xa5;
	free(expected);
	free(allocation);
	if (failure)
		fprintf(stderr, "n=%zu width=%zu offset=%zu pattern=%u reentrant=%d\n",
		    n, width, offset, pattern, reentrant);
	return (failure);
}

int
main(void)
{
	const size_t widths[] = { 4, 5, 7, 8, 12, 16 };
	const size_t counts[] = { 0, 1, 2, 5, 6, 15, 16, 17, 33, 128, 257 };
	size_t i, j, offset;
	unsigned int pattern;
	int reentrant;
	unsigned char invalid[4] = { 3, 2, 1, 0 };

	for (i = 0; i < sizeof(widths) / sizeof(widths[0]); i++)
		for (j = 0; j < sizeof(counts) / sizeof(counts[0]); j++)
			for (offset = 0; offset < sizeof(void *); offset++)
				for (pattern = 0; pattern < 4; pattern++)
					for (reentrant = 0; reentrant < 2; reentrant++)
						if (check(counts[j], widths[i], offset,
						    pattern, reentrant))
							return (1);
	errno = 0;
	if (mergesort(invalid, 2, sizeof(void *) / 2 - 1, compare) != -1 ||
	    errno != EINVAL || memcmp(invalid, "\3\2\1\0", 4) != 0)
		return (2);
	return (0);
}
