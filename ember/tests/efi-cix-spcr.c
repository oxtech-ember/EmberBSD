/* Origin: EmberBSD CIX SPCR production command contracts, 2026-10-15. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <efi.h>
#include "efi-cix-spcr-fixtures.h"

#define ARENA_SIZE (6 * 4096)
static uint8_t *arena;
static uint8_t *shadow;
#ifdef CIX_LEGACY_TEST
static uint8_t *top;
#endif
static uint8_t saved[ARENA_SIZE];
static EFI_MEMORY_DESCRIPTOR descriptors[6];
static unsigned maps, helps, checks;
static unsigned fail_map, protect_map, fail_alloc, fail_pages;
static unsigned allocations, releases, pool_allocs, pool_frees;
static uint64_t shadow_address, shadow_flags;
static unsigned shadow_type;
static UINTN map_size, map_stride;
static UINT32 map_version;
static EFI_ALLOCATE_TYPE allocation_type;
static char output[256];
static void *root;

static int
capture(const char *fmt, ...)
{
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = vsnprintf(output, sizeof(output), fmt, ap);
	va_end(ap);
	return n;
}
#define printf capture
#undef uefi_call_wrapper
#define uefi_call_wrapper(fn, n, ...) fn(__VA_ARGS__)
static EFI_STATUS
getmap(UINTN *size, EFI_MEMORY_DESCRIPTOR *map, UINTN *key,
    UINTN *stride, UINT32 *version)
{
	maps++;
	if (maps == fail_map)
		return EFI_ABORTED;
	if (maps == protect_map)
		descriptors[2].Attribute = EFI_MEMORY_RP;
	assert(*size >= sizeof(descriptors));
	memset(map, 0, *size);
	if (map_stride >= sizeof(descriptors[0]) && map_stride <= 256) {
		for (unsigned i = 0; i < 6; i++)
			memcpy((uint8_t *)map + i * map_stride, &descriptors[i],
			    sizeof(descriptors[i]));
	} else {
		memcpy(map, descriptors, sizeof(descriptors));
	}
	*size = map_size;
	*key = 1;
	*stride = map_stride;
	*version = map_version;
	return EFI_SUCCESS;
}
static EFI_STATUS
pages(EFI_ALLOCATE_TYPE type, EFI_MEMORY_TYPE memory, UINTN n,
    EFI_PHYSICAL_ADDRESS *address)
{
	assert(memory == EfiACPIReclaimMemory && n == 1);
	allocation_type = type;
	if (type == AllocateMaxAddress)
		assert(*address == UINT32_MAX);
	allocations++;
	if (fail_pages)
		return EFI_OUT_OF_RESOURCES;
	*address = shadow_address;
	descriptors[5].Type = shadow_type;
	descriptors[5].PhysicalStart = shadow_address;
	descriptors[5].Attribute = shadow_flags;
	return EFI_SUCCESS;
}
static EFI_STATUS
freepages(EFI_PHYSICAL_ADDRESS address, UINTN n)
{
	assert(address == shadow_address && n == 1);
	releases++;
	descriptors[5].Type = EfiConventionalMemory;
	return EFI_SUCCESS;
}
static EFI_BOOT_SERVICES services = {
	.GetMemoryMap = getmap, .AllocatePages = pages, .FreePages = freepages
};

static EFI_BOOT_SERVICES *BS = &services;
static void *
AllocatePool(UINTN n)
{
	pool_allocs++;
	return fail_alloc ? NULL : malloc(n);
}
static void FreePool(void *p) { pool_frees++; free(p); }
static EFI_STATUS
LibGetSystemConfigurationTable(EFI_GUID *guid, void **p)
{
	static const EFI_GUID acpi = ACPI_20_TABLE_GUID;
	if (memcmp(guid, &acpi, sizeof(acpi)) != 0)
		return EFI_NOT_FOUND;
	*p = root;
	return EFI_SUCCESS;
}
static void command_help(char *s) { (void)s; helps++; }
static void command_printtab(const char *k, const char *f, ...)
{ (void)k; (void)f; }
/* Old model/status helpers link, but the correction must not call SMBIOS. */
struct smbtable { unsigned cookie; void *tblhdr; };
struct smbios_sys { uint8_t vendor, product; };
#define SMBIOS_TYPE_SYSTEM 1
static void smbios_init(void *p) { (void)p; abort(); }
static int smbios_find_table(int n, struct smbtable *p)
{ (void)n; (void)p; abort(); }
static const char *smbios_get_string(struct smbtable *p, int n, char *s, int z)
{ (void)p; (void)n; (void)s; (void)z; abort(); }
#include "efiacpi-body.c"
#include "command-body.c"
#undef printf

static void
put(uint8_t *p, uint64_t value, unsigned n)
{
	for (unsigned i = 0; i < n; i++)
		p[i] = (uint8_t)(value >> (8 * i));
}
static uint8_t
sum(const uint8_t *p, size_t n)
{
	uint8_t s = 0;
	for (size_t i = 0; i < n; i++) s += p[i];
	return s;
}
static void
checksum(uint8_t *p, size_t n, size_t off)
{
	p[off] = 0;
	p[off] = (uint8_t)-sum(p, n);
}
static void
setup(void)
{
	memset(arena, 0, ARENA_SIZE);
	memset(descriptors, 0, sizeof(descriptors));
	for (unsigned i = 0; i < 6; i++) {
		descriptors[i].Type = EfiACPIReclaimMemory;
		descriptors[i].PhysicalStart = (uintptr_t)&arena[i * 4096];
		descriptors[i].NumberOfPages = 1;
	}
	memcpy(arena, "RSD PTR ", 8);
	arena[15] = 2;
	put(arena + 20, 36, 4);
	put(arena + 24, (uintptr_t)(arena + 4096), 8);
	checksum(arena, 20, 8);
	checksum(arena, 36, 32);
	memcpy(arena + 4096, "XSDT", 4);
	put(arena + 4100, 52, 4);
	arena[4104] = 1;
	put(arena + 4132, (uintptr_t)(arena + 8192), 8);
	put(arena + 4140, (uintptr_t)(arena + 12288), 8);
	checksum(arena + 4096, 52, 9);
	memcpy(arena + 8192, fixture_before, 80);
	memcpy(arena + 12288, fixture_dbg2, 92);
	maps = helps = fail_map = protect_map = fail_alloc = fail_pages = 0;
	allocations = releases = pool_allocs = pool_frees = 0;
	map_size = sizeof(descriptors);
	map_stride = sizeof(descriptors[0]);
	map_version = EFI_MEMORY_DESCRIPTOR_VERSION;
	shadow = arena + 5 * 4096;
	shadow_address = (uintptr_t)shadow;
	shadow_type = EfiACPIReclaimMemory;
	shadow_flags = 0;
	descriptors[5].Type = EfiConventionalMemory;
	root = arena;
	output[0] = 0;
	assert(efi_acpi_probe() == 0);
	efi_acpi_enable(1);
}

static uint64_t
get(const uint8_t *p, unsigned n)
{
	uint64_t value = 0;
	for (unsigned i = 0; i < n; i++)
		value |= (uint64_t)p[i] << (8 * i);
	return value;
}

static void
legacy(void)
{
	uint8_t *rsdt = arena + 4 * 4096;
	assert((uintptr_t)arena + ARENA_SIZE < UINT32_MAX);
	memcpy(rsdt, "RSDT", 4);
	put(rsdt + 4, 44, 4);
	rsdt[8] = 1;
	/* Deliberately reverse XSDT order. */
	put(rsdt + 36, (uintptr_t)(arena + 12288), 4);
	put(rsdt + 40, (uintptr_t)(arena + 8192), 4);
	checksum(rsdt, 44, 9);
	put(arena + 16, (uintptr_t)rsdt, 4);
	checksum(arena, 20, 8);
	checksum(arena, 36, 32);
}

static void
failure(const char *why)
{
	memcpy(saved, arena, ARENA_SIZE);
	const void *original = efi_acpi_root();
	command_acpi("cix-spcr");
	if (efi_acpi_root() != original || memcmp(saved, arena, ARENA_SIZE) != 0 ||
	    strstr(output, why) == NULL) {
		fprintf(stderr, "FAIL: expected unchanged '%s', received '%s'\n",
		    why, output);
		abort();
	}
	assert(helps == 0);
	assert(allocations == releases + fail_pages);
	assert(pool_allocs == pool_frees + fail_alloc);
	checks++;
}

static void
success(int has_legacy)
{
	uint8_t *newroot, *xsdt, *spcr;
	unsigned changed = 0;
	memcpy(saved, arena, ARENA_SIZE);
	command_acpi("cix-spcr");
	newroot = efi_acpi_root();
	assert(newroot == shadow && memcmp(saved, arena, 5 * 4096) == 0);
	assert(maps == 2 && allocations == 1 && releases == 0);
	assert(pool_allocs == pool_frees && helps == 0);
	assert(strstr(output, "shadow installed") != NULL);
	assert(allocation_type ==
	    (has_legacy ? AllocateMaxAddress : AllocateAnyPages));
	assert(sum(newroot, 20) == 0 && sum(newroot, 36) == 0);
	xsdt = (uint8_t *)(uintptr_t)get(newroot + 24, 8);
	assert(xsdt >= shadow && xsdt + 52 <= shadow + 4096);
	assert(sum(xsdt, 52) == 0);
	spcr = (uint8_t *)(uintptr_t)get(xsdt + 36, 8);
	assert(spcr >= shadow && spcr + 80 <= shadow + 4096);
	assert(memcmp(spcr, fixture_after, 80) == 0 && sum(spcr, 80) == 0);
	assert(get(xsdt + 44, 8) == (uintptr_t)(arena + 12288));
	for (unsigned i = 0; i < 80; i++) {
		if (spcr[i] == fixture_before[i]) continue;
		assert(i == 9 || i == 46 || i == 54);
		changed++;
	}
	assert(changed == 3);
	if (has_legacy) {
		uint8_t *rsdt = (uint8_t *)(uintptr_t)get(newroot + 16, 4);
		assert(rsdt >= shadow && rsdt + 44 <= shadow + 4096);
		assert(sum(rsdt, 44) == 0);
		assert(get(rsdt + 36, 4) == (uintptr_t)(arena + 12288));
		assert(get(rsdt + 40, 4) == (uintptr_t)spcr);
	} else {
		assert(get(newroot + 16, 4) == 0);
	}
	memcpy(saved, arena, ARENA_SIZE);
	command_acpi("cix-spcr");
	assert(strstr(output, "already correct") != NULL);
	assert(allocations == 1 && releases == 0 && maps == 3);
	assert(memcmp(saved, arena, ARENA_SIZE) == 0 && efi_acpi_root() == newroot);
	checks += 2;
}

int
main(void)
{
#ifdef CIX_LEGACY_TEST
	/* Low virtual addresses allow real 32-bit RSDT pointers. */
	arena = mmap((void *)(uintptr_t)0x40000000, ARENA_SIZE,
	    PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
	assert(arena != MAP_FAILED && (uintptr_t)arena + ARENA_SIZE < UINT32_MAX);
	top = mmap((void *)(uintptr_t)(UINT64_C(0x100000000) - 4096), 4096,
	    PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
	assert(top == (uint8_t *)(uintptr_t)(UINT64_C(0x100000000) - 4096));
#else
	arena = mmap(NULL, ARENA_SIZE, PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_PRIVATE, -1, 0);
	assert(arena != MAP_FAILED);
	(void)legacy;
#endif
	setup(); success(0);
	setup(); map_stride = 48; map_size = 6 * 48; success(0);
	setup(); descriptors[2].Type = EfiACPIMemoryNVS; success(0);

#ifdef CIX_LEGACY_TEST
	setup(); legacy(); success(1);
#endif
	setup(); descriptors[2].Attribute = EFI_MEMORY_WP | UINT64_C(0x20000);
	success(0);
	setup(); memcpy(arena + 8192, fixture_after, 80); failure("already correct");
	setup(); command_acpi("off"); assert(!efi_acpi_enabled());
	failure("disabled"); assert(maps == 0);
	command_acpi(""); assert(strstr(output, "disabled")); checks++;
	command_acpi("on"); assert(efi_acpi_enabled());
	command_acpi(NULL); assert(strstr(output, "enabled")); checks++;
	command_acpi("other"); assert(helps == 1); checks++;
	setup(); root = NULL; assert(efi_acpi_probe() == 0); failure("unavailable");
	setup(); fail_alloc = 1; failure("cannot allocate memory map");
	setup(); fail_pages = 1; failure("cannot allocate ACPI shadow page");
	setup(); fail_map = 1; failure("cannot read bounded");
	setup(); fail_map = 2; failure("cannot read bounded");
	setup(); protect_map = 2; failure("invalid");

	setup(); shadow_type = EfiLoaderData; failure("invalid shadow allocation");
	setup(); shadow_flags = EFI_MEMORY_WP; failure("invalid shadow allocation");
	setup(); shadow_flags = UINT64_C(0x20000);
	failure("invalid shadow allocation");
	setup(); shadow_flags = EFI_MEMORY_RP; failure("invalid shadow allocation");
	setup(); shadow_address++; failure("memory map");
	setup(); shadow_address = (uintptr_t)arena; failure("memory map");
	setup(); shadow_address = 0; failure("invalid shadow allocation");

	/* Root checksum, signature, length and revision gates. */
	for (unsigned i = 0; i < 36; i++) {
		setup(); arena[i] ^= 1; failure("invalid");
	}
	setup(); arena[15] = 3; checksum(arena, 20, 8); checksum(arena, 36, 32);
	failure("RSDP");
	for (unsigned n = 0; n < 4; n++) {
		const uint64_t address[] = {0, UINT64_MAX, UINT64_MAX - 15,
		    (uintptr_t)arena + ARENA_SIZE};
		setup(); root = (void *)(uintptr_t)address[n]; assert(efi_acpi_probe() == 0);
		failure(n == 0 ? "unavailable" : "RSDP");
		setup(); put(arena + 24, address[n], 8);
		checksum(arena, 36, 32); failure("XSDT");
		setup(); put(arena + 4132, address[n], 8);
		checksum(arena + 4096, 52, 9); failure("ACPI table");
	}
	for (unsigned off = 0; off < 52; off++) {
		setup(); arena[4096 + off] ^= 1; failure("XSDT");
	}
	for (unsigned n = 0; n < 5; n++) {
		const unsigned lengths[] = {0, 35, 53, 65537, UINT32_MAX};
		setup(); put(arena + 4100, lengths[n], 4); failure("XSDT");
	}
	setup(); put(arena + 4100, 36, 4); checksum(arena + 4096, 36, 9);
	failure("entry count");
	setup(); put(arena + 4100, 36 + 257 * 8, 4);
	checksum(arena + 4096, 36 + 257 * 8, 9); failure("entry count");

	/* Every non-checksum SPCR/DBG2 byte must match, even with a valid checksum. */
	for (unsigned table = 2; table < 4; table++) {
		unsigned length = table == 2 ? 80 : 92;
		for (unsigned off = 0; off < length; off++) {
			setup(); arena[table * 4096 + off] ^= 1;
			failure("ACPI table");
			if (off == 9 || (off >= 4 && off < 8)) continue;
			setup(); arena[table * 4096 + off] ^= 1;
			checksum(arena + table * 4096, length, 9);
			failure(off < 4 ? "missing" : "profile");
		}
	}
	setup(); put(arena + 4140, (uintptr_t)(arena + 8192), 8);
	checksum(arena + 4096, 52, 9); failure("duplicate");
	setup(); memcpy(arena + 12288, fixture_before, 80); failure("duplicate");
	setup(); memcpy(arena + 8192, fixture_dbg2, 92); failure("duplicate");
	setup(); put(arena + 4100, 44, 4); checksum(arena + 4096, 44, 9);
	failure("missing");

	/* Memory descriptor shape, permissions and range arithmetic. */
	setup(); map_stride = 0; failure("memory map");
	setup(); map_stride = 39; failure("memory map");
	setup(); map_stride = 257; failure("memory map");
	setup(); map_size--; failure("memory map");
	setup(); map_size = 0; failure("memory map");
	setup(); map_size = CIX_SPCR_MAP_MAX + 1; failure("memory map");
	setup(); map_size = sizeof(descriptors[0]) * 1025; failure("memory map");
	setup(); map_version = 2; failure("memory map");
	setup(); descriptors[1].NumberOfPages = 0; failure("memory map");
	setup(); descriptors[1].NumberOfPages = UINT64_MAX; failure("memory map");
	setup(); descriptors[1].PhysicalStart++; failure("memory map");
	setup(); descriptors[1].PhysicalStart = UINT64_MAX - 4095;
	failure("memory map");
	setup(); descriptors[1].PhysicalStart = descriptors[0].PhysicalStart;
	failure("memory map");
	for (unsigned i = 0; i < 4; i++) {
		setup(); descriptors[i].Attribute = EFI_MEMORY_RP; failure("invalid");
		setup(); descriptors[i].Type = EfiConventionalMemory; failure("invalid");
	}
	/* A full table ending exactly at a page boundary is valid. Crossing is not. */
	setup(); memcpy(arena + 12288 - 80, fixture_before, 80);
	put(arena + 4132, (uintptr_t)(arena + 12288 - 80), 8);
	checksum(arena + 4096, 52, 9);
	command_acpi("cix-spcr"); assert(efi_acpi_root() == shadow); checks++;
	setup(); memcpy(arena + 12288 - 79, fixture_before, 80);
	put(arena + 4132, (uintptr_t)(arena + 12288 - 79), 8);
	checksum(arena + 4096, 52, 9); failure("ACPI table");

#ifdef CIX_LEGACY_TEST
	/* Synthetic legacy roots: consistency and checksums, not hardware evidence. */
	setup(); legacy(); shadow_address = UINT64_C(0x100000000);
	failure("invalid shadow allocation");
	/* The last page below 4 GiB keeps every 32-bit RSDT reference valid. */
	setup(); legacy(); shadow = top; shadow_address = (uintptr_t)top;
	success(1);
	setup(); legacy(); arena[16384 + 9] ^= 1; failure("RSDT");
	setup(); legacy(); put(arena + 16384 + 40, (uintptr_t)(arena + 12288), 4);
	checksum(arena + 16384, 44, 9); failure("duplicate RSDT");
	setup(); legacy(); put(arena + 16384 + 40, 0, 4);
	checksum(arena + 16384, 44, 9); failure("disagree");
	setup(); legacy(); put(arena + 16384 + 4, 40, 4);
	checksum(arena + 16384, 40, 9); failure("RSDT");
	setup(); legacy(); descriptors[4].Attribute = EFI_MEMORY_RP; failure("RSDT");
#endif
	assert(munmap(arena, ARENA_SIZE) == 0);
#ifdef CIX_LEGACY_TEST
	assert(munmap(top, 4096) == 0);
#endif
	printf("PASS: %u CIX SPCR production contracts\n", checks);
	return 0;
}
