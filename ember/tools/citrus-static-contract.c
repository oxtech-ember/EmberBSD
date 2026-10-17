/* SPDX-License-Identifier: BSD-2-Clause */
/* Execute native Citrus code. Only host ABI, paths and allocation are modeled. */
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <iconv.h>
#include <sys/queue.h>
#include "citrus_namespace.h"
#include "citrus_types.h"
#include "citrus_module.h"
#include "citrus_stdenc.h"
#include "citrus_hash.h"
#include "citrus_iconv.h"
#include "citrus_mapper.h"
#include "citrus_csmapper.h"
#include "citrus_iconv_std_local.h"
#include "citrus_ctype.h"
#include "citrus_region.h"
#include "citrus_lookup.h"
#include "citrus-static-contract-data.h"

_CITRUS_CTYPE_GETOPS_FUNC(UTF8);

int
citrus_test_issetugid(void)
{

	/* The host fixture models an ordinary, unprivileged process. */
	return 0;
}

static unsigned int checks, calls, fail_at, live, failed;
/* Encoded records do not keep an otherwise leaked allocation reachable. */
static uintptr_t allocations[4096];

#define CHECK(x) do { \
	checks++; \
	if (!(x)) { \
		fprintf(stderr, "%s:%d: %s (errno=%d)\n", \
		    __FILE__, __LINE__, #x, errno); \
		exit(1); \
	} \
} while (0)

static int
allocation_fails(void)
{

	calls++;
	if (fail_at != 0 && calls == fail_at) {
		errno = ENOMEM;
		failed++;
		return 1;
	}
	return 0;
}

static void *
record(void *p)
{
	size_t i;

	if (p != NULL) {
		for (i = 0; i < __arraycount(allocations); i++) {
			if (allocations[i] == 0) {
				allocations[i] = ~(uintptr_t)p;
				live++;
				return p;
			}
		}
		abort();
	}
	return p;
}

void *
citrus_test_malloc(size_t n)
{

	return allocation_fails() ? NULL : record(malloc(n));
}

void *
citrus_test_calloc(size_t n, size_t size)
{

	return allocation_fails() ? NULL : record(calloc(n, size));
}

void
citrus_test_free(void *p)
{
	size_t i;

	if (p == NULL)
		return;
	for (i = 0; i < __arraycount(allocations); i++) {
		if (allocations[i] == ~(uintptr_t)p) {
			allocations[i] = 0;
			live--;
			free(p);
			return;
		}
	}
	abort();
}

char *
citrus_test_strdup(const char *s)
{

	return allocation_fails() ? NULL : record(strdup(s));
}

void *
citrus_test_realloc
(void *p, size_t size)
{
	void *q;
	size_t i;

	if (allocation_fails())
		return NULL;
	if (p == NULL)
		return record(realloc(p, size));
	for (i = 0; i < __arraycount(allocations); i++) {
		if (allocations[i] == ~(uintptr_t)p) {
			q = realloc(p, size);
			if (q != NULL)
				allocations[i] = ~(uintptr_t)q;
			return q;
		}
	}
	abort();
}

static void
write_data(const char *name, const void *data, size_t size)
{
	FILE *fp;

	fp = fopen(name, "wb");
	CHECK(fp != NULL);
	CHECK(fwrite(data, 1, size, fp) == size);
	CHECK(fclose(fp) == 0);
}

static void
init_data(void)
{

	write_data("data/lookup/db.db", lookup_db, sizeof(lookup_db));
	write_data("data/esdb/UTF/UTF-8.esdb", utf8_esdb, sizeof(utf8_esdb));
	write_data("data/esdb/ISO646/ISO646-US.esdb", ascii_esdb,
	    sizeof(ascii_esdb));
}

static void
registry(void)
{
	static const char *names[] = {
		"UTF8", "iconv_std", "mapper_646", "mapper_none"
	};
	static const char *interfaces[] = {
		"stdenc", "iconv", "mapper", "mapper"
	};
	_citrus_module_t handle, again;
	size_t i;

	for (i = 0; i < __arraycount(names); i++) {
		handle = NULL;
		CHECK(_citrus_load_module(&handle, names[i]) == 0);
		CHECK(handle != NULL);
		CHECK(_citrus_find_getops(handle, names[i], interfaces[i]) != NULL);
		CHECK(_citrus_find_getops(handle, names[i], "wrong") == NULL);
		CHECK(_citrus_find_getops(handle, "wrong", interfaces[i]) == NULL);
		CHECK(_citrus_find_getops(handle, NULL, interfaces[i]) == NULL);
		CHECK(_citrus_find_getops(handle, names[i], NULL) == NULL);
		_citrus_unload_module(handle);
		CHECK(_citrus_load_module(&again, names[i]) == 0);
		CHECK(handle == again);
	}
	handle = again;
	CHECK(_citrus_load_module(&handle, "UTF1632") == EINVAL);
	CHECK(handle == again);
	CHECK(_citrus_load_module(&handle, "utf8") == EINVAL);
	CHECK(handle == again);
	CHECK(_citrus_load_module(&handle, NULL) == EINVAL);
	CHECK(handle == again);
	CHECK(_citrus_load_module(NULL, "UTF8") == EINVAL);
	CHECK(_citrus_find_getops(NULL, "UTF8", "stdenc") == NULL);
	CHECK(_citrus_find_getops((_citrus_module_t)(uintptr_t)1,
	    "UTF8", "stdenc") == NULL);
	_citrus_unload_module(NULL);
	_citrus_unload_module((_citrus_module_t)(uintptr_t)1);
	CHECK(live == 0);
}

static void
lookup_oom(const char *path)
{
	struct _citrus_lookup *cl;
	struct _citrus_region data;
	int error;

	CHECK(_citrus_lookup_seq_open(&cl, path, 1) == 0);
	calls = 0;
	fail_at = 1;
	error = _citrus_lookup_seq_lookup(cl, "ASCII", &data);
	fail_at = 0;
	CHECK(error == ENOMEM);
	CHECK(_citrus_lookup_seq_lookup(cl, "ASCII", &data) == 0);
	_citrus_lookup_seq_close(cl);
	CHECK(live == 0);
}

static void
stdenc_oom(void)
{
	struct _citrus_stdenc *ce;
	unsigned int n, total;

	calls = 0;
	CHECK(_citrus_stdenc_open(&ce, "UTF8", NULL, 0) == 0);
	total = calls;
	CHECK(total >= 3);
	_citrus_stdenc_close(ce);
	CHECK(live == 0);
	for (n = 1; n <= total; n++) {
		ce = NULL;
		calls = 0;
		fail_at = n;
		CHECK(_citrus_stdenc_open(&ce, "UTF8", NULL, 0) == ENOMEM);
		CHECK(ce == NULL);
		CHECK(live == 0);
		fail_at = 0;
		CHECK(_citrus_stdenc_open(&ce, "UTF8", NULL, 0) == 0);
		_citrus_stdenc_close(ce);
		CHECK(live == 0);
	}
	ce = NULL;
	CHECK(_citrus_stdenc_open(&ce, "UTF1632", NULL, 0) == EINVAL);
	CHECK(ce == NULL);
	CHECK(live == 0);
	CHECK(_citrus_stdenc_open(&ce, "NONE", NULL, 0) == 0);
	_citrus_stdenc_close(ce);
	CHECK(live == 0);
}

static void
convert(const char *to, const char *from, const char *input, size_t length,
    const char *expected, size_t output_length, size_t nonidentical)
{
	char buffer[128], *in, *out;
	size_t left, available;
	iconv_t cd;

	cd = iconv_open(to, from);
	CHECK(cd != (iconv_t)-1);
	in = __UNCONST(input);
	left = length;
	out = buffer;
	available = sizeof(buffer);
	CHECK(iconv(cd, &in, &left, &out, &available) == nonidentical);
	CHECK(left == 0 && in == input + length);
	CHECK((size_t)(out - buffer) == output_length);
	CHECK(memcmp(buffer, expected, output_length) == 0);
	CHECK(iconv(cd, NULL, NULL, &out, &available) == 0);
	CHECK(iconv_close(cd) == 0);
}

static void
multi_entry_cleanup(void)
{
	struct _citrus_iconv_ops ops;
	struct _citrus_iconv_shared shared = { 0 };
	struct _citrus_iconv_std_shared *is;
	struct _citrus_iconv_std_src *src;
	struct _citrus_iconv_std_dst *dst;
	_citrus_module_t handle;
	_citrus_iconv_getops_t getops;
	unsigned int before, i, j;

	/* Warm only the process-lifetime mapper area before accounting. */
	convert("UTF8", "ASCII", "USB", 3, "USB", 3, 0);
	before = live;
	CHECK(_citrus_load_module(&handle, "iconv_std") == 0);
	getops = (_citrus_iconv_getops_t)
	    _citrus_find_getops(handle, "iconv_std", "iconv");
	CHECK(getops(&ops, sizeof(ops), _CITRUS_ICONV_ABI_VERSION) == 0);
	is = citrus_test_calloc(1, sizeof(*is));
	CHECK(is != NULL);
	CHECK(_citrus_stdenc_open(&is->is_src_encoding, "NONE", NULL, 0) == 0);
	CHECK(_citrus_stdenc_open(&is->is_dst_encoding, "NONE", NULL, 0) == 0);
	TAILQ_INIT(&is->is_srcs);
	for (i = 0; i < 2; i++) {
		src = citrus_test_calloc(1, sizeof(*src));
		CHECK(src != NULL);
		TAILQ_INIT(&src->ss_dsts);
		TAILQ_INSERT_TAIL(&is->is_srcs, src, ss_entry);
		for (j = 0; j < 2; j++) {
			dst = citrus_test_calloc(1, sizeof(*dst));
			CHECK(dst != NULL);
			CHECK(_citrus_csmapper_open(&dst->sd_mapper, "UCS",
			    "ISO646-US", 0, NULL) == 0);
			TAILQ_INSERT_TAIL(&src->ss_dsts, dst, sd_entry);
		}
	}
	shared.ci_closure = is;
	ops.io_uninit_shared(&shared);
	CHECK(live == before);
	_citrus_unload_module(handle);
}

static int
state_query_failure(struct _citrus_stdenc *ce, void *state, int id,
    struct _citrus_stdenc_state_desc *desc)
{

	(void)ce;
	(void)state;
	(void)id;
	(void)desc;
	return EIO;
}

static void
state_error(void)
{
	struct _citrus_iconv *cv;
	struct _citrus_iconv_std_shared *shared;
	_citrus_stdenc_get_state_desc_t saved;
	char buffer[8], *in, *out;
	size_t left, available, invalids;
	const char *source;
	iconv_t cd;

	cd = iconv_open("UTF8", "UTF8");
	CHECK(cd != (iconv_t)-1);
	cv = cd;
	shared = cv->cv_shared->ci_closure;
	saved = shared->is_src_encoding->ce_ops->eo_get_state_desc;
	shared->is_src_encoding->ce_ops->eo_get_state_desc = state_query_failure;
	in = __UNCONST("");
	left = 0;
	out = buffer;
	available = sizeof(buffer);
	source = in;
	invalids = 0xdeadbeef;
	CHECK(_citrus_iconv_convert(cv, &source, &left, &out, &available,
	    0, &invalids) == EIO);
	CHECK(invalids == 0);
	invalids = 0xdeadbeef;
	CHECK(__iconv(cd, &in, &left, &out, &available, 0, &invalids) == (size_t)-1);
	CHECK(invalids == 0);
	CHECK(errno == EIO);
	CHECK(left == 0 && out == buffer && available == sizeof(buffer));
	shared->is_src_encoding->ce_ops->eo_get_state_desc = saved;
	CHECK(iconv_close(cd) == 0);
}

static void
ctype_error(void)
{
	_citrus_ctype_ops_rec_t ops;
	void *closure;
	char buffer[16];
	int result;

	CHECK(_citrus_UTF8_ctype_getops(&ops, sizeof(ops),
	    _CITRUS_CTYPE_ABI_VERSION) == 0);
	CHECK(ops.co_init(&closure, NULL, 0, 128) == 0);
	CHECK(ops.co_wctomb(closure, buffer, L'A', &result) == 0);
	CHECK(result == 1 && buffer[0] == 'A');
	CHECK(ops.co_wctomb(closure, buffer, (wchar_t)0xd800, &result) == EILSEQ);
	CHECK(result == -1);
	CHECK(ops.co_wctomb(closure, NULL, L'A', &result) == 0);
	CHECK(result == 0);
	ops.co_uninit(closure);
	CHECK(live == 0);
}

static void
stdenc_output(void)
{
	struct _citrus_stdenc *ce;
	void *state;
	const char *in;
	_citrus_wc_t wc;
	size_t result;

	CHECK(_citrus_stdenc_open(&ce, "UTF8", NULL, 0) == 0);
	state = calloc(1, _citrus_stdenc_get_state_size(ce));
	CHECK(state != NULL);
	CHECK(_citrus_stdenc_init_state(ce, state) == 0);
	in = "\xf4\x8f\xbf\xbf";
	wc = 0xdeadbeef;
	CHECK(_citrus_stdenc_mbtowc(ce, &wc, &in, 4, state, &result) == 0);
	CHECK(wc == 0x10ffff && result == 4);
	CHECK(_citrus_stdenc_init_state(ce, state) == 0);
	in = "\xc3";
	wc = 0xdeadbeef;
	CHECK(_citrus_stdenc_mbtowc(ce, &wc, &in, 1, state, &result) == 0);
	CHECK(result == (size_t)-2 && wc == 0xdeadbeef);
	CHECK(_citrus_stdenc_init_state(ce, state) == 0);
	in = "\xff";
	CHECK(_citrus_stdenc_mbtowc(ce, &wc, &in, 1, state, &result) == EILSEQ);
	CHECK(result == (size_t)-1 && wc == 0xdeadbeef);
	CHECK(_citrus_stdenc_init_state(ce, state) == 0);
	in = "A";
	CHECK(_citrus_stdenc_mbtowc(ce, NULL, &in, 1, state, &result) == 0);
	CHECK(result == 1 && *in == '\0');
	CHECK(_citrus_stdenc_init_state(ce, state) == 0);
	in = NULL;
	wc = 0xdeadbeef;
	CHECK(_citrus_stdenc_mbtowc(ce, &wc, &in, 0, state, &result) == 0);
	CHECK(result == 0 && wc == 0xdeadbeef);
	in = "";
	CHECK(_citrus_stdenc_mbtowc(ce, &wc, &in, 1, state, &result) == 0);
	CHECK(result == 0 && wc == 0);
	free(state);
	_citrus_stdenc_close(ce);
	CHECK(live == 0);
}

static void
conversion_errors(void)
{
	static const char *bad[] = { "\xff", "\xc0\xaf", "\xed\xa0\x80" };
	char buffer[16], *in, *out;
	size_t i, left, available;
	iconv_t cd;

	cd = iconv_open("UTF-8", "UTF-8");
	CHECK(cd != (iconv_t)-1);
	for (i = 0; i < __arraycount(bad); i++) {
		in = __UNCONST(bad[i]);
		left = strlen(in);
		out = buffer;
		available = sizeof(buffer);
		errno = 0;
		CHECK(iconv(cd, &in, &left, &out, &available) == (size_t)-1);
		CHECK(errno == EILSEQ);
		CHECK(in == bad[i] && left == strlen(bad[i]) && out == buffer);
		CHECK(iconv(cd, NULL, NULL, NULL, NULL) == 0);
	}
	in = __UNCONST("\xe2\x82");
	left = 2;
	out = buffer;
	available = sizeof(buffer);
	CHECK(iconv(cd, &in, &left, &out, &available) == (size_t)-1);
	CHECK(errno == EINVAL && left == 2 && out == buffer);
	CHECK(iconv(cd, NULL, NULL, NULL, NULL) == 0);
	in = __UNCONST("\xc3\xa9");
	left = 2;
	available = 1;
	CHECK(iconv(cd, &in, &left, &out, &available) == (size_t)-1);
	CHECK(errno == E2BIG && left == 2 && out == buffer && available == 1);
	available = sizeof(buffer);
	CHECK(iconv(cd, &in, &left, &out, &available) == 0);
	CHECK(left == 0 && out == buffer + 2);
	CHECK(iconv_close(cd) == 0);
	errno = 0;
	CHECK(iconv_close((iconv_t)-1) == -1 && errno == EBADF);
	CHECK(iconv((iconv_t)-1, NULL, NULL, NULL, NULL) == (size_t)-1);
	CHECK(errno == EBADF);
	CHECK(iconv_open("UTF-32", "ASCII") == (iconv_t)-1);
	CHECK(errno == EINVAL);
}

int
main(int argc, char **argv)
{
	static const char ascii[] = "NetBSD USB #$@[\\]^`{|}~\0";
	static const char utf8[] = "A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80";
	iconv_t cd;

	CHECK(argc >= 2);
	CHECK(setenv("ICONV_MAX_REUSE", "0", 1) == 0);
	if (strcmp(argv[1], "init") == 0) {
		init_data();
	} else if (strcmp(argv[1], "lookup") == 0) {
		registry();
		stdenc_oom();
	} else if (strcmp(argv[1], "cleanup") == 0) {
		multi_entry_cleanup();
	} else if (strcmp(argv[1], "state-error") == 0) {
		state_error();
	} else if (strcmp(argv[1], "ctype-error") == 0) {
		ctype_error();
	} else if (strcmp(argv[1], "stdenc-output") == 0) {
		stdenc_output();
	} else if (strcmp(argv[1], "lookup-db") == 0) {
		lookup_oom("data/lookup/db");
	} else if (strcmp(argv[1], "lookup-plain") == 0) {
		lookup_oom("data/lookup/plain");
	} else if (strcmp(argv[1], "oom-open") == 0) {
		CHECK(argc == 3);
		fail_at = (unsigned int)strtoul(argv[2], NULL, 10);
		cd = iconv_open("UTF-8", "ASCII");
		fail_at = 0;
		if (cd != (iconv_t)-1) {
			CHECK(iconv_close(cd) == 0);
		} else {
			CHECK(failed == 1);
			/* Native optional alias lookup falls back on allocation error. */
			CHECK(errno == ENOMEM || errno == EINVAL);
		}
		/* A failed open must not leave a broken cached converter. */
		convert("UTF-8", "ASCII", "USB", 3, "USB", 3, 0);
	} else if (strcmp(argv[1], "missing") == 0) {
		CHECK(iconv_open("UTF-8", "ASCII") == (iconv_t)-1);
		CHECK(errno != 0);
	} else {
		CHECK(strcmp(argv[1], "convert") == 0);
		convert("ASCII", "ASCII", ascii, sizeof(ascii), ascii, sizeof(ascii), 0);
		convert("646", "UTF-8", ascii, sizeof(ascii), ascii, sizeof(ascii), 0);
		convert("UTF8", "us-ascii", ascii, sizeof(ascii), ascii, sizeof(ascii), 0);
		convert("UTF-8", "UTF-8", utf8, sizeof(utf8), utf8, sizeof(utf8), 0);
		convert("ASCII", "UTF8", "\xc3\xa9", 2, "?", 1, 1);
		conversion_errors();
	}
	printf("Citrus actual-source %s: %u checks, 0 failures\n", argv[1], checks);
	return 0;
}
