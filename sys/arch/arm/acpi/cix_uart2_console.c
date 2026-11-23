/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original CIX UART2 console applicability gate, AI-assisted. */
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/sha2.h>
#include <sys/systm.h>
#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <machine/acpi_machdep.h>
#include <arm/acpi/cix_uart2_console.h>

__CTASSERT(offsetof(ACPI_TABLE_FADT, Dsdt) == 40);
__CTASSERT(offsetof(ACPI_TABLE_FADT, XDsdt) == 140);

#define CIX_UART2_HEADER 36
#define CIX_UART2_ENTRIES 256

/* Both firmware tables describe COM2 with the same PL011 resources. */
static const struct {
	uint32_t length;
	uint8_t digest[SHA256_DIGEST_LENGTH];
} cix_uart2_dsdt_profiles[] = {
	{ 81063, { /* Orange Pi 6 Plus UEFI V1.5 */
	    0x9e, 0x7b, 0xf3, 0x0c, 0xb2, 0x94, 0x84, 0x91,
	    0x0c, 0xce, 0xec, 0x20, 0x8a, 0xdf, 0x6f, 0xa9,
	    0xf1, 0xfa, 0xb4, 0xed, 0x54, 0xc9, 0x32, 0x36,
	    0xd5, 0x3b, 0xb4, 0x11, 0x9b, 0x04, 0x26, 0x0a
	} },
	{ 77645, { /* Orange Pi 6 Plus UEFI V1.4, firmware DSDT */
	    0xf2, 0xf6, 0xdd, 0x34, 0xf1, 0x6e, 0x3b, 0x5f,
	    0x04, 0xdd, 0xbf, 0xee, 0xa1, 0x9b, 0x45, 0x66,
	    0x05, 0x84, 0x0b, 0xbf, 0x2e, 0xf9, 0x9f, 0x8c,
	    0x95, 0x59, 0x3b, 0x38, 0x87, 0x7e, 0x42, 0x39
	} },
};

/* Mapping is a trusted native service, not hostile-firmware containment. */
static int
cix_uart2_map(uint64_t pa, uint32_t size, const uint8_t **out)
{

	if (pa == 0 || size == 0 || size > 131072 ||
	    pa > UINT64_MAX - size ||
	    pa + size > UINT64_MAX - (PAGE_SIZE - 1))
		return EINVAL;
	void *mapped;
	if (ACPI_FAILURE(acpi_md_OsMapMemory(pa, size, &mapped)))
		return EIO;
	*out = mapped;
	return 0;
}

static uint64_t
cix_uart2_le(const uint8_t *p, unsigned int n)
{
	uint64_t value = 0;
	for (unsigned int i = 0; i < n; i++)
		value |= (uint64_t)p[i] << (8 * i);
	return value;
}

static bool
cix_uart2_checksum(const uint8_t *p, uint32_t size)
{
	uint8_t value = 0;
	for (uint32_t i = 0; i < size; i++)
		value += p[i];
	return value == 0;
}

static int
cix_uart2_header(uint64_t pa, uint8_t header[CIX_UART2_HEADER])
{
	const uint8_t *p;
	int error = cix_uart2_map(pa, CIX_UART2_HEADER, &p);
	if (error != 0)
		return error;
	memcpy(header, p, CIX_UART2_HEADER);
	acpi_md_OsUnmapMemory(__UNCONST(p), CIX_UART2_HEADER);
	return 0;
}

static int
cix_uart2_map_table(uint64_t pa, const uint8_t header[CIX_UART2_HEADER],
    uint32_t length, const uint8_t **out)
{
	const uint8_t *p;
	int error;

	error = cix_uart2_map(pa, length, &p);
	if (error != 0)
		return error;
	if (memcmp(p, header, CIX_UART2_HEADER) != 0 ||
	    !cix_uart2_checksum(p, length)) {
		acpi_md_OsUnmapMemory(__UNCONST(p), length);
		return EPROTO;
	}
	*out = p;
	return 0;
}

static int
cix_uart2_table(uint64_t pa, const char *signature, uint32_t min,
    uint32_t max, const uint8_t **out, uint32_t *size)
{
	uint8_t header[CIX_UART2_HEADER];
	uint32_t length;
	int error;

	error = cix_uart2_header(pa, header);
	if (error != 0)
		return error;
	length = (uint32_t)cix_uart2_le(header + 4, 4);
	if (memcmp(header, signature, 4) != 0 || length < min || length > max)
		return EPROTO;
	error = cix_uart2_map_table(pa, header, length, out);
	if (error == 0)
		*size = length;
	return error;
}

static int
cix_uart2_check_dsdt(uint64_t pa)
{
	uint8_t header[CIX_UART2_HEADER], digest[SHA256_DIGEST_LENGTH];
	const uint8_t *table;
	uint32_t length;
	SHA256_CTX hash;
	int error;

	error = cix_uart2_header(pa, header);
	if (error != 0)
		return error;
	if (memcmp(header, "DSDT", 4) != 0)
		return EPROTO;
	length = (uint32_t)cix_uart2_le(header + 4, 4);
	/* Only map a full DSDT after matching its exact recorded size. */
	for (size_t i = 0; i < __arraycount(cix_uart2_dsdt_profiles); i++) {
		if (length != cix_uart2_dsdt_profiles[i].length)
			continue;
		error = cix_uart2_map_table(pa, header, length, &table);
		if (error != 0)
			return error;
		SHA256_Init(&hash);
		SHA256_Update(&hash, table, length);
		SHA256_Final(digest, &hash);
		acpi_md_OsUnmapMemory(__UNCONST(table), length);
		if (memcmp(digest, cix_uart2_dsdt_profiles[i].digest,
		    sizeof(digest)) == 0)
			return 0;
		return ENOTSUP;
	}
	return EPROTO;
}

int
cix_uart2_console_probe(void)
{
	uint8_t prefix[20], header[CIX_UART2_HEADER];
	const uint8_t *table;
	uint64_t address, fadt = 0, dsdt, legacy;
	uint32_t length, entries;
	int error;

	address = acpi_md_OsGetRootPointer();
	error = cix_uart2_map(address, sizeof(prefix), &table);
	if (error != 0)
		return error;
	memcpy(prefix, table, sizeof(prefix));
	acpi_md_OsUnmapMemory(__UNCONST(table), sizeof(prefix));
	if (memcmp(prefix, "RSD PTR ", 8) != 0 || prefix[15] != 2 ||
	    !cix_uart2_checksum(prefix, sizeof(prefix)))
		return EPROTO;
	error = cix_uart2_header(address, header);
	if (error != 0)
		return error;
	if (memcmp(header, prefix, sizeof(prefix)) != 0 ||
	    cix_uart2_le(header + 20, 4) != CIX_UART2_HEADER ||
	    !cix_uart2_checksum(header, CIX_UART2_HEADER))
		return EPROTO;
	address = cix_uart2_le(header + 24, 8);
	error = cix_uart2_table(address, "XSDT", CIX_UART2_HEADER + 8,
	    CIX_UART2_HEADER + CIX_UART2_ENTRIES * 8, &table, &length);
	if (error != 0)
		return error;
	if ((length - CIX_UART2_HEADER) % 8 != 0) {
		error = EPROTO;
		goto root_done;
	}
	entries = (length - CIX_UART2_HEADER) / 8;
	for (uint32_t i = 0; i < entries; i++) {
		address = cix_uart2_le(table + CIX_UART2_HEADER + i * 8, 8);
		error = cix_uart2_header(address, header);
		if (error != 0)
			goto root_done;
		uint64_t extent = cix_uart2_le(header + 4, 4);
		if (extent < CIX_UART2_HEADER || extent > 65536 ||
		    address > UINT64_MAX - extent ||
		    address + extent > UINT64_MAX - (PAGE_SIZE - 1)) {
			error = EPROTO;
			goto root_done;
		}
		if (memcmp(header, "FACP", 4) == 0) {
			if (fadt != 0) {
				error = EPROTO;
				goto root_done;
			}
			fadt = address;
		}
	}
	if (fadt == 0)
		error = ENOENT;
root_done:
	acpi_md_OsUnmapMemory(__UNCONST(table), length);
	if (error != 0)
		return error;
	/*
	 * Only Dsdt[40:44] and XDsdt[140:148] are used. Like native ACPICA,
	 * trust the complete field extent, not the unreliable revision byte.
	 */
	error = cix_uart2_table(fadt, "FACP", 148, 4096, &table, &length);
	if (error != 0)
		return error;
	legacy = cix_uart2_le(table + 40, 4);
	dsdt = cix_uart2_le(table + 140, 8);
	acpi_md_OsUnmapMemory(__UNCONST(table), length);
	if (legacy != 0 && dsdt != 0 && legacy != dsdt)
		return EPROTO;
	if (dsdt == 0)
		dsdt = legacy;
	return cix_uart2_check_dsdt(dsdt);
}
