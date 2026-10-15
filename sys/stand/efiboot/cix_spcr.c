/* Origin: EmberBSD opt-in CIX SPCR table correction, 2026-10-15. */
/* SPDX-License-Identifier: BSD-2-Clause */

#include "cix_spcr.h"
#ifdef _STANDALONE
#include <lib/libkern/libkern.h>
#else
#include <string.h>
#endif

/* UEFI 2.10: the bundled GNU-EFI headers predate EFI_MEMORY_RO. */
#define CIX_MEMORY_RO UINT64_C(0x20000)
#define CIX_MAX_DESCRIPTORS 1024
#define CIX_MAX_TABLES 256
#define CIX_MAX_TABLE_SIZE 65536
#define ACPI_HEADER_SIZE 36

struct cix_range {
	uint64_t address;
	uint64_t length;
};

/* Read unaligned little-endian firmware fields without typed dereferences. */
static uint64_t
getle(const uint8_t *p, unsigned n)
{
	uint64_t v = 0;
	unsigned i;

	for (i = 0; i < n; i++)
		v |= (uint64_t)p[i] << (8 * i);
	return v;
}

static uint8_t
sum(const uint8_t *p, unsigned n)
{
	uint8_t s = 0;
	unsigned i;

	for (i = 0; i < n; i++)
		s += p[i];
	return s;
}

static void
get_descriptor(const struct cix_spcr_map *map, UINTN i,
    EFI_MEMORY_DESCRIPTOR *md)
{

	memcpy(md, (const uint8_t *)map->data + i * map->stride, sizeof(*md));
}

/* Reject invalid or overlapping descriptors, including non-ACPI entries. */
static int
map_valid(const struct cix_spcr_map *map)
{
	EFI_MEMORY_DESCRIPTOR a, b;
	uint64_t end;
	UINTN i, j, count;

	if (map == NULL || map->data == NULL || map->size == 0 ||
	    map->size > CIX_SPCR_MAP_MAX ||
	    map->stride < sizeof(a) || map->stride > 256 ||
	    map->size % map->stride != 0 ||
	    map->version != EFI_MEMORY_DESCRIPTOR_VERSION)
		return 0;
	count = map->size / map->stride;
	if (count > CIX_MAX_DESCRIPTORS)
		return 0;
	for (i = 0; i < count; i++) {
		get_descriptor(map, i, &a);
		if (a.NumberOfPages == 0 ||
		    a.NumberOfPages > UINT64_MAX / EFI_PAGE_SIZE ||
		    a.PhysicalStart % EFI_PAGE_SIZE != 0 ||
		    a.PhysicalStart > UINT64_MAX - a.NumberOfPages * EFI_PAGE_SIZE)
			return 0;
		end = a.PhysicalStart + a.NumberOfPages * EFI_PAGE_SIZE;
		for (j = 0; j < i; j++) {
			get_descriptor(map, j, &b);
			if (a.PhysicalStart < b.PhysicalStart +
			    b.NumberOfPages * EFI_PAGE_SIZE && b.PhysicalStart < end)
				return 0;
		}
	}
	return 1;
}

/* Every read or write must fit one complete, unambiguous ACPI descriptor. */
static uint8_t *
acpi_range(const struct cix_spcr_map *map, uint64_t address, uint64_t length,
    int write)
{
	EFI_MEMORY_DESCRIPTOR md;
	UINTN i;

	if (address == 0 || length == 0 || address > UINTPTR_MAX ||
	    length > UINTPTR_MAX - address)
		return NULL;
	for (i = 0; i < map->size / map->stride; i++) {
		get_descriptor(map, i, &md);
		if (address < md.PhysicalStart ||
		    address + length > md.PhysicalStart + md.NumberOfPages * EFI_PAGE_SIZE)
			continue;
		if ((md.Type != EfiACPIReclaimMemory && md.Type != EfiACPIMemoryNVS) ||
		    (md.Attribute & EFI_MEMORY_RP) != 0 ||
		    (write && (md.Attribute & (EFI_MEMORY_WP | CIX_MEMORY_RO)) != 0))
			return NULL;
		return (uint8_t *)(uintptr_t)address;
	}
	return NULL;
}

static const uint8_t *
acpi_table(const struct cix_spcr_map *map, uint64_t address, uint32_t *length)
{
	const uint8_t *p;

	p = acpi_range(map, address, ACPI_HEADER_SIZE, 0);
	if (p == NULL)
		return NULL;
	*length = getle(p + 4, 4);
	if (*length < ACPI_HEADER_SIZE || *length > CIX_MAX_TABLE_SIZE)
		return NULL;
	p = acpi_range(map, address, *length, 0);
	if (p == NULL || sum(p, *length) != 0)
		return NULL;
	return p;
}

static int
add_range(struct cix_range *ranges, unsigned count, uint64_t address,
    uint64_t length)
{
	unsigned i;

	for (i = 0; i < count; i++) {
		if (address < ranges[i].address + ranges[i].length &&
		    ranges[i].address < address + length)
			return 0;
	}
	ranges[count].address = address;
	ranges[count].length = length;
	return 1;
}

/*
 * Policy: exact observed CIXTEK/SKY1EDK2 table pair, OEM revision 0x1000101,
 * creator "CIX " revision 1. This is a table profile, not board authentication.
 * SPCR revision 2 describes PL011, MMIO 0x040e0000 and GSI 331.
 * DBG2 revision 0 has one PL011 at 0x040d0000, size 0x1000, \\_SB.COM2.
 * Exact bytes constrain every field, offset, count, reserved byte and length.
 * DBG2 contains no interrupt. GSI 330 is part of this observed policy only.
 * Captures and the separately verified RAM correction date from 2026-10-15.
 */
static const uint8_t cix_before[80] = {
	0x53, 0x50, 0x43, 0x52, 0x50, 0x00, 0x00, 0x00,
	0x02, 0xe6, 0x43, 0x49, 0x58, 0x54, 0x45, 0x4b,
	0x53, 0x4b, 0x59, 0x31, 0x45, 0x44, 0x4b, 0x32,
	0x01, 0x01, 0x00, 0x01, 0x43, 0x49, 0x58, 0x20,
	0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
	0x00, 0x20, 0x00, 0x03, 0x00, 0x00, 0x0e, 0x04,
	0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x4b, 0x01,
	0x00, 0x00, 0x07, 0x00, 0x01, 0x00, 0x02, 0x00,
	0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t cix_dbg2[92] = {
	0x44, 0x42, 0x47, 0x32, 0x5c, 0x00, 0x00, 0x00,
	0x00, 0x8b, 0x43, 0x49, 0x58, 0x54, 0x45, 0x4b,
	0x53, 0x4b, 0x59, 0x31, 0x45, 0x44, 0x4b, 0x32,
	0x01, 0x01, 0x00, 0x01, 0x43, 0x49, 0x58, 0x20,
	0x01, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x01,
	0x0a, 0x00, 0x26, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x80, 0x03, 0x00, 0x00, 0x00, 0x16, 0x00,
	0x22, 0x00, 0x00, 0x20, 0x00, 0x03, 0x00, 0x00,
	0x0d, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
	0x00, 0x00, 0x5c, 0x5f, 0x53, 0x42, 0x2e, 0x43,
	0x4f, 0x4d, 0x32, 0x00,
};

const char *
cix_spcr_prepare(uint64_t root, const struct cix_spcr_map *map,
    struct cix_spcr_plan *plan)
{
	struct cix_range ranges[CIX_MAX_TABLES + 3];
	const uint8_t *rsdp, *xsdt, *rsdt = NULL, *table, *spcr = NULL;
	const uint8_t *dbg2 = NULL;
	uint8_t corrected[sizeof(cix_before)];
	uint64_t address;
	uint32_t length, xlength, rlength = 0;
	unsigned i, j, count, nranges = 0;

	if (!map_valid(map))
		return "invalid EFI memory map";
	rsdp = acpi_range(map, root, 36, 0);
	if (rsdp == NULL || memcmp(rsdp, "RSD PTR ", 8) != 0 ||
	    rsdp[15] != 2 || getle(rsdp + 20, 4) != 36 ||
	    sum(rsdp, 20) != 0 || sum(rsdp, 36) != 0)
		return "invalid or unsupported RSDP";
	add_range(ranges, nranges++, root, 36);
	address = getle(rsdp + 24, 8);
	xsdt = acpi_table(map, address, &xlength);
	if (xsdt == NULL || memcmp(xsdt, "XSDT", 4) != 0 || xsdt[8] != 1 ||
	    (xlength - ACPI_HEADER_SIZE) % 8 != 0 ||
	    !add_range(ranges, nranges++, address, xlength))
		return "invalid or unsupported XSDT";
	count = (xlength - ACPI_HEADER_SIZE) / 8;
	if (count == 0 || count > CIX_MAX_TABLES)
		return "unsupported XSDT entry count";
	for (i = 0; i < count; i++) {
		address = getle(xsdt + ACPI_HEADER_SIZE + i * 8, 8);
		table = acpi_table(map, address, &length);
		if (table == NULL || !add_range(ranges, nranges++, address, length))
			return "invalid, duplicate or overlapping ACPI table";
		if (memcmp(table, "SPCR", 4) == 0) {
			if (spcr != NULL || length != sizeof(cix_before))
				return "duplicate or unsupported SPCR";
			spcr = table;
		} else if (memcmp(table, "DBG2", 4) == 0) {
			if (dbg2 != NULL || length != sizeof(cix_dbg2))
				return "duplicate or unsupported DBG2";
			dbg2 = table;
		}
	}
	address = getle(rsdp + 16, 4);
	if (address != 0) {
		rsdt = acpi_table(map, address, &rlength);
		if (rsdt == NULL || memcmp(rsdt, "RSDT", 4) != 0 ||
		    rsdt[8] != 1 || rlength != ACPI_HEADER_SIZE + count * 4 ||
		    !add_range(ranges, nranges, address, rlength))
			return "invalid or unsupported RSDT";
		/* Require the same unique child set, independent of entry order. */
		for (i = 0; i < count; i++) {
			address = getle(rsdt + ACPI_HEADER_SIZE + i * 4, 4);
			for (j = 0; j < i; j++) {
				if (address == getle(rsdt + ACPI_HEADER_SIZE + j * 4, 4))
					return "duplicate RSDT entry";
			}
			for (j = 0; j < count; j++) {
				if (address == getle(xsdt + ACPI_HEADER_SIZE + j * 8, 8))
					break;
			}
			if (j == count)
				return "RSDT and XSDT disagree";
		}
	}
	if (spcr == NULL || dbg2 == NULL)

		return "SPCR or DBG2 is missing";
	if (memcmp(dbg2, cix_dbg2, sizeof(cix_dbg2)) != 0)
		return "DBG2 does not match the CIX profile";
	memcpy(corrected, cix_before, sizeof(corrected));
	corrected[46] = 0x0d;
	corrected[54] = 0x4a;
	corrected[9] = 0;
	corrected[9] = (uint8_t)-sum(corrected, sizeof(corrected));
	if (memcmp(spcr, corrected, sizeof(corrected)) == 0)
		return "already correct";
	if (memcmp(spcr, cix_before, sizeof(cix_before)) != 0)
		return "SPCR does not match the CIX profile";
	plan->rsdp = rsdp;
	plan->xsdt = xsdt;
	plan->rsdt = rsdt;
	plan->spcr = spcr;
	plan->xlength = xlength;
	plan->rlength = rlength;
	return NULL;
}

static void
putle(uint8_t *p, uint64_t value, unsigned n)
{
	unsigned i;

	for (i = 0; i < n; i++)
		p[i] = (uint8_t)(value >> (8 * i));
}

static void
checksum(uint8_t *p, unsigned length, unsigned offset)
{

	p[offset] = 0;
	p[offset] = (uint8_t)-sum(p, length);
}

const char *
cix_spcr_build(uint64_t root, const struct cix_spcr_map *map, uint64_t dest)
{
	struct cix_spcr_plan plan;
	const char *error;
	uint8_t *page, *xsdt, *rsdt, *spcr;
	uint64_t address;
	unsigned i, roff, soff;

	error = cix_spcr_prepare(root, map, &plan);
	if (error != NULL)
		return error;
	page = acpi_range(map, dest, EFI_PAGE_SIZE, 1);
	if (page == NULL || dest % EFI_PAGE_SIZE != 0 ||
	    (plan.rsdt != NULL && dest > (uint64_t)UINT32_MAX + 1 - EFI_PAGE_SIZE))
		return "invalid shadow allocation";
	/* AllocatePages owns this page. Also reject aliases of any input table. */
	if ((root < dest + EFI_PAGE_SIZE && dest < root + 36) ||
	    ((uintptr_t)plan.xsdt < dest + EFI_PAGE_SIZE &&
	    dest < (uintptr_t)plan.xsdt + plan.xlength) ||
	    (plan.rsdt != NULL && (uintptr_t)plan.rsdt < dest + EFI_PAGE_SIZE &&
	    dest < (uintptr_t)plan.rsdt + plan.rlength))
		return "shadow allocation overlaps a root table";
	for (i = ACPI_HEADER_SIZE; i < plan.xlength; i += 8) {
		address = getle(plan.xsdt + i, 8);
		/* Headers were validated; include ranges that start before dest. */
		if (address < dest + EFI_PAGE_SIZE &&
		    dest < address + getle((const uint8_t *)(uintptr_t)address + 4, 4))
			return "shadow allocation overlaps an ACPI table";
	}
	roff = (40 + plan.xlength + 7) & ~7U;
	soff = (roff + plan.rlength + 7) & ~7U;
	if (soff + sizeof(cix_before) > EFI_PAGE_SIZE)
		return "shadow tables exceed one page";
	/* No fallible work remains. Only loader-owned pages are written. */
	memset(page, 0, EFI_PAGE_SIZE);
	memcpy(page, plan.rsdp, 36);
	xsdt = page + 40;
	memcpy(xsdt, plan.xsdt, plan.xlength);
	rsdt = page + roff;
	if (plan.rsdt != NULL)
		memcpy(rsdt, plan.rsdt, plan.rlength);
	spcr = page + soff;
	memcpy(spcr, plan.spcr, sizeof(cix_before));
	spcr[46] = 0x0d;
	spcr[54] = 0x4a;
	checksum(spcr, sizeof(cix_before), 9);
	for (i = ACPI_HEADER_SIZE; i < plan.xlength; i += 8) {
		if (getle(xsdt + i, 8) == (uintptr_t)plan.spcr)
			putle(xsdt + i, dest + soff, 8);
	}
	checksum(xsdt, plan.xlength, 9);
	if (plan.rsdt != NULL) {
		for (i = ACPI_HEADER_SIZE; i < plan.rlength; i += 4) {
			if (getle(rsdt + i, 4) == (uintptr_t)plan.spcr)
				putle(rsdt + i, dest + soff, 4);
		}
		checksum(rsdt, plan.rlength, 9);
		putle(page + 16, dest + roff, 4);
	}
	putle(page + 24, dest + 40, 8);
	checksum(page, 20, 8);
	checksum(page, 36, 32);
	return NULL;
}
