/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted file/allocation models, real source consumers. */
#include <sys/types.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dev/firmload.h>

static unsigned int checks;
#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

struct firmware_handle { void *fh_vp; off_t size; };
static struct firmware_handle image;
static const unsigned char payload[] = { 0x12, 0x34, 0x56, 0x78 };
static void *allocation;
static size_t allocation_size;
static int opens, closes, reads, allocations, names_live;
static int read_error;
static bool fail_alloc, model_missing, all_missing, opened;

static void
reset(off_t size)
{
	CHECK(allocation == NULL && !opened && names_live == 0);
	image.size = size;
	image.fh_vp = &image;
	opens = closes = reads = allocations = 0;
	read_error = 0;
	fail_alloc = model_missing = all_missing = false;
}

int
firmware_open(const char *driver, const char *name, firmware_handle_t *fh)
{
	CHECK(!opened);
	CHECK(strcmp(driver, "if_bwfm") == 0 || strcmp(driver, "if_aicwf") == 0);
	opens++;
	if (all_missing || (model_missing && strcmp(name, "radio.board.bin") == 0))
		return ENOENT;
	opened = true;
	*fh = &image;
	return 0;
}

int
firmware_close(firmware_handle_t fh)
{
	CHECK(fh == &image && opened);
	closes++;
	opened = false;
	return 0;
}

off_t
firmware_get_size(firmware_handle_t fh)
{
	CHECK(fh == &image && opened);
	return fh->size;
}

void *
firmware_malloc(size_t size)
{
	CHECK(allocation == NULL);
	/* Kernel kmem requires a positive size. Never allocate malformed sizes. */
	CHECK(size > 0);
	allocations++;
	if (fail_alloc)
		return NULL;
	CHECK(size <= sizeof(payload));
	allocation = malloc(size);
	CHECK(allocation != NULL);
	allocation_size = size;
	return allocation;
}

#define UIO_READ 0
#define UIO_SYSSPACE 1
#define kauth_cred_get() NULL
#define curlwp NULL

static int
vn_rdwr(int op, const void *vp, void *buf, int size, off_t offset, int segment,
    int flags, const void *cred, const void *resid, const void *lwp)
{
	CHECK(op == UIO_READ && vp == &image && opened && offset == 0);
	CHECK(segment == UIO_SYSSPACE && flags == 0);
	CHECK(cred == NULL && resid == NULL && lwp == NULL);
	CHECK(buf != NULL && buf == allocation && size > 0);
	CHECK((size_t)size == allocation_size);
	reads++;
	memcpy(buf, payload, size);
	return read_error;
}

void
firmware_free(void *buf, size_t size)
{
	CHECK(buf != NULL && buf == allocation && size == allocation_size);
	free(buf);
	allocation = NULL;
}

static char *
kmem_asprintf(const char *fmt, ...)
{
	va_list ap;
	char *s = malloc(80);
	CHECK(s != NULL);
	va_start(ap, fmt);
	int n = vsnprintf(s, 80, fmt, ap);
	va_end(ap);
	CHECK(n >= 0 && n < 80);
	names_live++;
	return s;
}

static void
kmem_free(void *p, size_t size)
{
	CHECK(p != NULL && size == strlen(p) + 1 && names_live > 0);
	names_live--;
	free(p);
}

static void
aprint_error_dev(void *dev, const char *fmt, ...)
{
	(void)dev;
	(void)fmt;
}
#define aprint_verbose_dev aprint_error_dev

struct bwfm_softc { void *sc_dev; };
struct aicwf_softc { void *sc_dev; };
struct bwfm_firmware_selector { const char *fwsel_basename; };
struct bwfm_firmware_context {
	const char *ctx_model;
	struct { void *ctx_f_data; size_t ctx_f_size; } ctx_file[1];
};
static const struct { const char *suffix, *description; }
    bwfm_firmware_filetypes[] = { { "bin", "UCODE" } };

#include "firmload-under-test.c"

static void
consumers(void)
{
	struct bwfm_softc bwfm = { 0 };
	struct aicwf_softc aic = { 0 };
	struct bwfm_firmware_selector selector = { "radio" };
	struct bwfm_firmware_context ctx;
	uint8_t *data;
	size_t size;
	int error;

	/* Empty files must fail before the allocator, in both real consumers. */
	reset(0);
	data = NULL; size = 0;
	error = aicwf_fw_read(&aic, "radio.bin", &data, &size);
	CHECK(error == EINVAL && data == NULL && size == 0);
	CHECK(opens == 1 && closes == 1 && allocations == 0);

	for (int which = 0; which < 7; which++) {
		reset(which == 4 ? 0 : sizeof(payload));
		memset(&ctx, 0, sizeof(ctx));
		ctx.ctx_model = which == 6 ? NULL : "board";
		model_missing = which == 1;
		all_missing = which == 2;
		read_error = which == 3 ? EIO : 0;
		fail_alloc = which == 5;
		bwfm_firmware_read_file(&bwfm, &selector, &ctx, 0);
		CHECK(!opened && names_live == 0);
		CHECK(opens == ((which == 1 || which == 2) ? 2 : 1));
		CHECK(closes == (which == 2 ? 0 : 1));
		if (which < 2 || which == 6) {
			CHECK(ctx.ctx_file[0].ctx_f_size == sizeof(payload));
			CHECK(memcmp(ctx.ctx_file[0].ctx_f_data, payload, sizeof(payload)) == 0);
			firmware_free(ctx.ctx_file[0].ctx_f_data, ctx.ctx_file[0].ctx_f_size);
		} else {
			CHECK(ctx.ctx_file[0].ctx_f_data == NULL && allocation == NULL);
		}
	}
	for (int which = 0; which < 4; which++) {
		reset(sizeof(payload));
		read_error = which == 1 ? EIO : 0;
		fail_alloc = which == 2;
		all_missing = which == 3;
		data = NULL; size = 0;
		error = aicwf_fw_read(&aic, "radio.bin", &data, &size);
		CHECK(error == (which == 0 ? 0 : which == 1 ? EIO : which == 2 ? ENOMEM : ENOENT));
		CHECK(!opened && closes == (which == 3 ? 0 : 1));
		if (which == 0) {
			CHECK(size == sizeof(payload) && memcmp(data, payload, size) == 0);
			firmware_free(data, size);
		} else {
			CHECK(data == NULL && size == 0 && allocation == NULL);
		}
	}
}

int
main(void)
{
#ifndef CONSUMERS_ONLY
	static const struct { off_t size; size_t limit; int error; } cases[] = {
		{ -1, SIZE_MAX, EINVAL }, { 0, SIZE_MAX, EINVAL },
		{ 1, 0, EFBIG }, { 4, 3, EFBIG }, { 4, 4, 0 },
		{ (off_t)UINT32_MAX + 1, UINT32_MAX, EFBIG },
		{ (off_t)INT_MAX + 1, SIZE_MAX, EFBIG },
		{ (off_t)UINT32_MAX + 5, SIZE_MAX, EFBIG }
	};
	void *data;
	size_t size;
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		reset(cases[i].size);
		opened = true;
		data = &image; size = 99;
		CHECK(firmware_read_alloc(&image, cases[i].limit, &data, &size) == cases[i].error);
		CHECK(opened && closes == 0);
		if (cases[i].error) {
			CHECK(data == NULL && size == 0 && allocations == 0 && reads == 0);
		} else {
			CHECK(size == sizeof(payload) && memcmp(data, payload, size) == 0);
			firmware_free(data, size);
		}
		firmware_close(&image);
	}
	for (int which = 0; which < 3; which++) {
		reset(which == 2 ? INT_MAX : sizeof(payload));
		opened = true;
		fail_alloc = which != 1;
		read_error = which == 1 ? EIO : 0;
		data = &image; size = 99;
		CHECK(firmware_read_alloc(&image, SIZE_MAX, &data, &size) ==
		    (which == 1 ? EIO : ENOMEM));
		CHECK(data == NULL && size == 0 && allocation == NULL);
		CHECK(opened && closes == 0 && allocations == 1);
		CHECK(reads == (which == 1 ? 1 : 0));
		firmware_close(&image);
	}
	reset(4);
	data = &image; size = 99;
	CHECK(firmware_read_alloc(NULL, 4, &data, &size) == EINVAL);
	CHECK(data == NULL && size == 0);
	CHECK(firmware_read_alloc(&image, 4, NULL, &size) == EINVAL);
	CHECK(firmware_read_alloc(&image, 4, &data, NULL) == EINVAL);
	/* Exercise the real low-level reader before its int-sized service. */
	CHECK(firmware_read(&image, 0, &image, (size_t)INT_MAX + 1) == EFBIG);
	if (SIZE_MAX > UINT32_MAX)
		CHECK(firmware_read(&image, 0, &image,
		    (size_t)UINT32_MAX + 5) == EFBIG);
	CHECK(reads == 0 && allocations == 0);
#endif
	consumers();
	CHECK(allocation == NULL && !opened && names_live == 0);
	printf("firmload: %u checks passed\n", checks);
	return 0;
}
