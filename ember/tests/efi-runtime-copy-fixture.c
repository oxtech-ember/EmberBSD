/* SPDX-License-Identifier: BSD-2-Clause */
/* AI-assisted EmberBSD execution model. It does not map firmware memory. */
@SOURCE_COMMENTS@
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define _KERNEL 1
#ifndef __GNUC_PREREQ__
#define __GNUC_PREREQ__(major, minor) \
	(__GNUC__ > (major) || (__GNUC__ == (major) && __GNUC_MINOR__ >= (minor)))
#endif
@MEMCPY_DECLARATIONS@

/* The opaque table checks copy size and contents, not the firmware ABI. */
struct fixture_rt {
	unsigned char bytes[112];
};
struct fixture_systbl {
	struct fixture_rt *st_rt;
};
static struct fixture_rt firmware, efi_rt;
static unsigned wrapper_calls, phase;
static int runtime_map;
static void *efi_runtime_pmap = &runtime_map;

static void
fail(const char *message)
{

	fprintf(stderr, "FAIL: %s\n", message);
	exit(1);
}

void *
kasan_memcpy(void *dst, const void *src, size_t len)
{

	wrapper_calls++;
	if (src == &firmware)
		fail("foreign EFI read reached the KASAN wrapper");
	return (memcpy)(dst, src, len);
}

static void
pmap_update(const void *map)
{

	if (map != efi_runtime_pmap || phase != 0)
		fail("runtime pmap update order");
	phase = 1;
}

static void
pmap_deactivate_sync(const void *map, const void *cookie)
{

	if (map != efi_runtime_pmap || cookie != &phase || phase != 1)
		fail("runtime pmap deactivate order");
	for (size_t i = 0; i < sizeof(efi_rt.bytes); i++)
		if (efi_rt.bytes[i] != firmware.bytes[i])
			fail("runtime table copy contents or size");
	phase = 2;
}

static void
copy_runtime(void)
{
	const struct fixture_systbl table = { &firmware };
	const struct fixture_systbl *systbl = &table;
	const void *cookie = &phase;

	@RUNTIME_COPY@
}

int
main(void)
{
	unsigned char source[19], destination[19] = { 0 };

	for (size_t i = 0; i < sizeof(firmware.bytes); i++)
		firmware.bytes[i] = (unsigned char)(i * 7 + 3);
	copy_runtime();
	if (phase != 2 || wrapper_calls != 0)
		fail("foreign EFI read must use only the raw copy");
	for (size_t i = 0; i < sizeof(source); i++)
		source[i] = (unsigned char)(i + 1);
	memcpy(destination, source, sizeof(source));
	for (size_t i = 0; i < sizeof(source); i++)
		if (destination[i] != source[i])
			fail("ordinary copy contents");
#ifdef KASAN
	if (wrapper_calls != 1)
		fail("ordinary copy lost the KASAN wrapper");
#else
	if (wrapper_calls != 0)
		fail("ordinary build invoked a KASAN wrapper");
#endif
	puts("EFI runtime copy execution contract: PASS");
	return 0;
}
