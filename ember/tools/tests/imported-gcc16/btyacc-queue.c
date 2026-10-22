/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 EmberBSD contributors.
 * AI-assisted host fixture. Included growth blocks retain the byacc license.
 */
#include <assert.h>
#ifdef NDEBUG
#error This fixture requires assertions
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE 4

typedef int YYINT;
typedef int YYSTYPE;
typedef struct { int first_line; } YYLTYPE;

static YYINT *yylexemes, *yylexp;
static YYSTYPE *yylvals, *yylvp, *yylve, *yylvlim;
#ifdef YYLTYPE_IS_DECLARED
static YYLTYPE *yylpsns, *yylpp, *yylpe, *yylplim;
#endif

struct slot {
	void *p;
	size_t n;
	int live;
};

static struct slot slots[8];
static unsigned calls, fail_at;

static void
remember(void *p, size_t n)
{
	unsigned i;

	assert(p != NULL);
	for (i = 0; i < 8; i++) {
		if (!slots[i].live) {
			slots[i].p = p;
			slots[i].n = n;
			slots[i].live = 1;
			return;
		}
	}
	abort();
}

static size_t
forget(void *p)
{
	unsigned i;

	for (i = 0; i < 8; i++) {
		if (slots[i].live && slots[i].p == p) {
			size_t n = slots[i].n;
			slots[i].live = 0;
			return n;
		}
	}
	fprintf(stderr, "lost block %p\n", p);
	abort();
}

static void *
hook_realloc(void *p, size_t n)
{
	void *q;
	size_t oldn = 0;

	/* Growth always starts from an existing queue block. */
	assert(p != NULL);
	if (fail_at != 0 && ++calls == fail_at)
		return NULL;
	q = malloc(n);
	assert(q != NULL);
	memset(q, 0, n);
	oldn = forget(p);
	memcpy(q, p, oldn < n ? oldn : n);
	free(p);
	remember(q, n);
	return q;
}

#define realloc hook_realloc
#include "queue-sites.inc"
#undef realloc

static unsigned
live_count(void)
{
	unsigned i, n = 0;

	for (i = 0; i < 8; i++)
		n += slots[i].live != 0;
	return n;
}

static void
release_all(void)
{
	unsigned i;

	for (i = 0; i < 8; i++) {
		if (!slots[i].live)
			continue;
		free(slots[i].p);
		slots[i].live = 0;
	}
	yylexemes = yylexp = NULL;
	yylvals = yylvp = yylve = yylvlim = NULL;
#ifdef YYLTYPE_IS_DECLARED
	yylpsns = yylpp = yylpe = yylplim = NULL;
#endif
}

static void *
fill(size_t size, int value)
{
	unsigned char *p = malloc(size);
	assert(p != NULL);
	memset(p, value, size);
	remember(p, size);
	return p;
}

static void
setup(void)
{
	unsigned i;

	memset(slots, 0, sizeof(slots));
	yylexemes = fill(BASE * sizeof(*yylexemes), 0);
	yylvals = fill(BASE * sizeof(*yylvals), 0);
	for (i = 0; i < BASE; i++) {
		yylexemes[i] = 42;
		yylvals[i] = 43;
	}
	yylexp = yylexemes + BASE;
	yylvp = yylve = yylvlim = yylvals + BASE;
#ifdef YYLTYPE_IS_DECLARED
	yylpsns = fill(BASE * sizeof(*yylpsns), 0);
	for (i = 0; i < BASE; i++)
		yylpsns[i].first_line = 44;
	yylpp = yylpe = yylplim = yylpsns + BASE;
#endif
}

static void
check_site(const char *name, int (*grow)(void), unsigned fail)
{
	void *lex0, *val0;
#ifdef YYLTYPE_IS_DECLARED
	void *ps0;
#endif
	int rc;
	unsigned live0;

	setup();
	lex0 = yylexemes;
	val0 = yylvals;
#ifdef YYLTYPE_IS_DECLARED
	ps0 = yylpsns;
#endif
	live0 = live_count();
	fail_at = fail;
	calls = 0;
	fprintf(stderr, "%s fail %u\n", name, fail);
	rc = grow();
	if (fail == 0) {
		assert(rc == 0);
		assert(yylexemes != lex0 && yylexemes[0] == 42);
		assert(yylexp == yylexemes + BASE);
		assert(yylvals != val0 && yylvals[0] == 43);
		assert(yylvp == yylvals + BASE && yylve == yylvp);
		assert(yylvlim == yylvals + (BASE + YYLVQUEUEGROWTH));
#ifdef YYLTYPE_IS_DECLARED
		assert(yylpsns != ps0 && yylpsns[0].first_line == 44);
		assert(yylpp == yylpsns + BASE);
		assert(yylplim == yylpsns + (BASE + YYLVQUEUEGROWTH));
#endif
	} else if (fail == 1) {
		assert(rc == 1);
		assert(yylexemes == lex0 && yylvals == val0);
		assert(yylexemes[0] == 42 && yylvals[0] == 43);
		assert(yylexp == (YYINT *)lex0 + BASE);
		assert(yylvp == (YYSTYPE *)val0 + BASE);
#ifdef YYLTYPE_IS_DECLARED
		assert(yylpsns == ps0 && yylpsns[0].first_line == 44);
#endif
		assert(live_count() == live0);
	} else if (fail == 2) {
		assert(rc == 1);
		assert(yylexemes != lex0 && yylexemes[0] == 42);
		assert(yylexp == yylexemes + BASE);
		assert(yylvals == val0 && yylvals[0] == 43);
		assert(yylvp == (YYSTYPE *)val0 + BASE);
#ifdef YYLTYPE_IS_DECLARED
		assert(yylpsns == ps0 && yylpsns[0].first_line == 44);
#endif
	} else {
		assert(fail == 3 && rc == 1);
#ifdef YYLTYPE_IS_DECLARED
		assert(yylexemes != lex0 && yylexemes[0] == 42);
		assert(yylexp == yylexemes + BASE);
		assert(yylvals != val0 && yylvals[0] == 43);
		assert(yylvp == yylvals + BASE);
		assert(yylvlim == yylvals + (BASE + YYLVQUEUEGROWTH));
		assert(yylpsns == ps0 && yylpsns[0].first_line == 44);
		assert(yylpp == (YYLTYPE *)ps0 + BASE);
#else
		assert(0);
#endif
	}
	release_all();
	assert(live_count() == 0);
}

int
main(void)
{
	static const struct {
		const char *name;
		int (*fn)(void);
	} sites[] = {
		{ "skel-0", grow_skel_0 },
		{ "skel-1", grow_skel_1 },
		{ "embed-0", grow_embed_0 },
		{ "embed-1", grow_embed_1 },
	};
	unsigned limit = 2;
	unsigned s, fail;

#ifdef YYLTYPE_IS_DECLARED
	limit = 3;
#endif
	for (s = 0; s < sizeof(sites) / sizeof(sites[0]); s++) {
		for (fail = 0; fail <= limit; fail++)
			check_site(sites[s].name, sites[s].fn, fail);
	}
	puts("btyacc-queue: lexical growth keeps the previous blocks");
	return 0;
}
