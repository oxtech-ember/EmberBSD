/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Run the real acpi_gtdt_walk with byte-built GTDT revisions 2 and 3.
 * The revision 3 table repeats the CIX P1 Orange Pi 6 Plus layout:
 * an EL2 virtual timer, then one non-secure watchdog at offset 104.
 * AI-assisted.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ACPI_APPLICATION
#include <acpi.h>

#define CHECK(c) do { \
	checks++; \
	if (!(c)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

static unsigned int checks;
static ACPI_TABLE_HEADER *gtdt_header;

#include "gtdt-walk.h"

static uint64_t storage[64];
static unsigned char *const table = (unsigned char *)storage;

struct seen {
	unsigned int count, stop_after;
	uint8_t type[8];
	uint16_t length[8];
	uint64_t refresh, control;
	uint32_t gsiv, flags;
};

static void
put(size_t off, uint64_t v, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		table[off + i] = (unsigned char)(v >> (8 * i));
}

static uint64_t
get(const unsigned char *p, size_t n)
{
	uint64_t v = 0;
	size_t i;

	for (i = 0; i < n; i++)
		v |= (uint64_t)p[i] << (8 * i);
	return v;
}

static ACPI_STATUS
record(ACPI_GTDT_HEADER *hdrp, void *aux)
{
	struct seen *s = aux;
	const unsigned char *p = (const unsigned char *)hdrp;

	CHECK(s->count < 8);
	s->type[s->count] = hdrp->Type;
	s->length[s->count] = hdrp->Length;
	if (hdrp->Type == ACPI_GTDT_TYPE_WATCHDOG && hdrp->Length >= 28) {
		s->refresh = get(p + 4, 8);
		s->control = get(p + 12, 8);
		s->gsiv = (uint32_t)get(p + 20, 4);
		s->flags = (uint32_t)get(p + 24, 4);
	}
	s->count++;
	return s->stop_after != 0 && s->count >= s->stop_after ? AE_ERROR : AE_OK;
}

/* Fixed fields: 36-byte header, then the timer fields up to offset 96. */
static void
gtdt(uint32_t length, uint8_t revision, uint32_t count, uint32_t offset)
{
	memset(storage, 0, sizeof(storage));
	memcpy(table, ACPI_SIG_GTDT, 4);
	put(4, length, 4);
	put(8, revision, 1);
	put(36, UINT64_MAX, 8);		/* CounterBlockAddresss */
	put(48, 29, 4);			/* SecureEl1Interrupt */
	put(56, 30, 4);			/* NonSecureEl1Interrupt */
	put(64, 27, 4);			/* VirtualTimerInterrupt */
	put(72, 26, 4);			/* NonSecureEl2Interrupt */
	put(80, UINT64_MAX, 8);		/* CounterReadBlockAddress */
	put(88, count, 4);
	put(92, offset, 4);
	gtdt_header = (ACPI_TABLE_HEADER *)table;
}

static void
watchdog(size_t off, uint64_t refresh, uint64_t control, uint32_t gsiv,
    uint32_t flags)
{
	put(off, ACPI_GTDT_TYPE_WATCHDOG, 1);
	put(off + 1, 28, 2);
	put(off + 4, refresh, 8);
	put(off + 12, control, 8);
	put(off + 20, gsiv, 4);
	put(off + 24, flags, 4);
}

static void
timer_block(size_t off)
{
	put(off, ACPI_GTDT_TYPE_TIMER_BLOCK, 1);
	put(off + 1, 20, 2);
	put(off + 4, 0x2a810000, 8);
}

static struct seen
walk(void)
{
	struct seen s;

	memset(&s, 0, sizeof(s));
	acpi_gtdt_walk(record, &s);
	return s;
}

int
main(void)
{
	struct seen s;

	CHECK(sizeof(ACPI_TABLE_GTDT) == 96 && sizeof(ACPI_GTDT_HEADER) == 3);

	/* CIX P1: revision 3, EL2 virtual timer at 96, watchdog at 104. */
	gtdt(132, 3, 1, 104);
	put(96, 28, 4);			/* VirtualEL2TimerGsiv */
	put(100, 0, 4);
	watchdog(104, 0x16008000, 0x16003000, 408, 0);
	s = walk();
	CHECK(s.count == 1 && s.type[0] == ACPI_GTDT_TYPE_WATCHDOG);
	CHECK(s.length[0] == 28 && s.refresh == 0x16008000);
	CHECK(s.control == 0x16003000 && s.gsiv == 408);
	CHECK((s.flags & ACPI_GTDT_WATCHDOG_SECURE) == 0);

	/* Revision 2: platform timers follow the fixed fields. */
	gtdt(144, 2, 2, 96);
	timer_block(96);
	watchdog(116, 0x1000, 0x2000, 60, ACPI_GTDT_WATCHDOG_SECURE);
	s = walk();
	CHECK(s.count == 2 && s.type[0] == ACPI_GTDT_TYPE_TIMER_BLOCK);
	CHECK(s.type[1] == ACPI_GTDT_TYPE_WATCHDOG && s.control == 0x2000);

	/* PlatformTimerCount bounds the walk; a failure stops it. */
	put(88, 1, 4);
	CHECK(walk().count == 1);
	put(88, 2, 4);
	memset(&s, 0, sizeof(s));
	s.stop_after = 1;
	acpi_gtdt_walk(record, &s);
	CHECK(s.count == 1);
	put(88, 0, 4);
	CHECK(walk().count == 0);

	/* Offsets outside the table, or before the platform timers, yield none. */
	gtdt(132, 3, 1, 0);
	watchdog(104, 1, 2, 3, 0);
	CHECK(walk().count == 0);
	put(92, 95, 4);
	CHECK(walk().count == 0);
	put(92, 132, 4);
	CHECK(walk().count == 0);
	gtdt(80, 1, 1, 96);
	CHECK(walk().count == 0);

	/* Malformed entries stop the walk without reading past the table. */
	gtdt(132, 3, 2, 104);
	watchdog(104, 1, 2, 3, 0);
	put(105, 0, 2);
	CHECK(walk().count == 0);
	put(105, 29, 2);
	CHECK(walk().count == 0);
	put(105, 2, 2);
	CHECK(walk().count == 0);
	gtdt(133, 3, 2, 104);
	watchdog(104, 1, 2, 3, 0);
	CHECK(walk().count == 1);
	gtdt(135, 3, 2, 104);
	watchdog(104, 1, 2, 3, 0);
	put(132, ACPI_GTDT_TYPE_WATCHDOG, 1);
	put(133, 28, 2);
	CHECK(walk().count == 1);

	printf("acpi gtdt walk: %u checks\n", checks);
	return 0;
}
