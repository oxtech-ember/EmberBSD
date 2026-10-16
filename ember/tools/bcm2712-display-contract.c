/* SPDX-License-Identifier: BSD-2-Clause */
/* Compile the production decoder and ACPI observer with fake services. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define htole32 OSSwapHostToLittleInt32
#define le32toh OSSwapLittleToHostInt32
#else
#include <sys/endian.h>
#endif
#ifndef __aligned
#define __aligned(n) __attribute__((__aligned__(n)))
#endif
#include "display.h"
#include "properties.h"
#include "core.h"

/* ACPICA service ABI. Target compilation also uses the real ACPICA headers. */
typedef void *device_t;
typedef void *ACPI_HANDLE;
typedef uint64_t ACPI_INTEGER;
typedef unsigned int ACPI_STATUS;
#define AE_OK 0
#define AE_NOT_FOUND 1
#define AE_BAD_DATA 2
#define AE_TYPE 3
#define AE_ERROR 4
#define ACPI_FAILURE(s) ((s) != AE_OK)
#define ACPI_RESOURCE_TYPE_ADDRESS64 1
#define ACPI_RESOURCE_TYPE_EXTENDED_IRQ 2
#define ACPI_RESOURCE_TYPE_END_TAG 3
#define ACPI_MEMORY_RANGE 0
#define ACPI_CONSUMER 1
#define ACPI_POS_DECODE 0
#define ACPI_ADDRESS_FIXED 1
#define ACPI_READ_WRITE_MEMORY 1
#define ACPI_NON_CACHEABLE_MEMORY 0
#define ACPI_LEVEL_SENSITIVE 0
#define ACPI_ACTIVE_HIGH 0
#define ACPI_EXCLUSIVE 0
#define BCMMBOX_CHANARM2VC 8

typedef struct {
	unsigned int ResourceType, ProducerConsumer, Decode;
	unsigned int MinAddressFixed, MaxAddressFixed;
	struct { unsigned int StringLength; } ResourceSource;
	struct { struct { unsigned int WriteProtect, Caching, RangeType, Translation; } Mem; } Info;
	struct { uint64_t Granularity, Minimum, Maximum;
		uint64_t TranslationOffset, AddressLength; } Address;
} ACPI_RESOURCE_ADDRESS64;
typedef struct {
	unsigned int ProducerConsumer, Triggering, Polarity, Shareable;
	unsigned int InterruptCount, WakeCapable;
	struct { unsigned int StringLength; } ResourceSource;
	uint32_t Interrupts[8];
} ACPI_RESOURCE_EXTENDED_IRQ;
typedef struct {
	unsigned int Type;
	union { ACPI_RESOURCE_ADDRESS64 Address64;
		ACPI_RESOURCE_EXTENDED_IRQ ExtendedIrq; } Data;
} ACPI_RESOURCE;

static ACPI_STATUS acpi_dsd_integer(ACPI_HANDLE, const char *, ACPI_INTEGER *);
static ACPI_STATUS acpi_eval_integer(ACPI_HANDLE, const char *, ACPI_INTEGER *);
static ACPI_STATUS AcpiWalkResources(ACPI_HANDLE, const char *,
    ACPI_STATUS (*)(ACPI_RESOURCE *, void *), void *);
static int bcmmbox_request(unsigned int, void *, size_t, uint32_t *);
static void aprint_normal_dev(device_t, const char *, ...);
#include "adapter.h"

/* Golden vectors are transcribed from the pinned firmware and device tree. */
static const struct bcm2712_display_memory golden_memory[] = {
	{ 0x1002000000ULL, 0x4000 }, { 0x1002008000ULL, 0x6000 },
	{ 0x1002030800ULL, 0x700 }, { 0x107c580000ULL, 0x1a000 },
	{ 0x1000005200ULL, 0x80 }, { 0x107c410000ULL, 0x100 },
	{ 0x107c411000ULL, 0x100 }, { 0x107c500000ULL, 0x28 },
	{ 0x107c501000ULL, 0x20 }, { 0x107c502000ULL, 0x30 },
	{ 0x107c700000ULL, 0x10 }, { 0x107c720000ULL, 0x100 },
	{ 0x107d510600ULL, 0x30 }, { 0x107d508380ULL, 0x10 },
	{ 0x107d508200ULL, 0x58 }, { 0x107d508280ULL, 0x58 },
	{ 0x107c701400ULL, 0x300 }, { 0x107c701000ULL, 0x200 },
	{ 0x107c701d00ULL, 0x300 }, { 0x107c702000ULL, 0x80 },
	{ 0x107c703800ULL, 0x200 }, { 0x107c704000ULL, 0x800 },
	{ 0x107c700100ULL, 0x80 }, { 0x107d510800ULL, 0x100 },
	{ 0x107c706400ULL, 0x300 }, { 0x107c706000ULL, 0x200 },
	{ 0x107c706d00ULL, 0x300 }, { 0x107c707000ULL, 0x80 },
	{ 0x107c708800ULL, 0x200 }, { 0x107c709000ULL, 0x800 },
	{ 0x107c700180ULL, 0x80 }, { 0x107d511000ULL, 0x100 }
};
static const uint32_t golden_irq[] = { 282, 281, 133, 142, 129, 271, 274 };
static const uint32_t golden_clock[] = { 13, 14, 4, 16 };
static const unsigned int golden_leaf_domain[] = { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2 };
static const unsigned int golden_leaf_bit[] = { 0, 1, 2, 9, 16, 1, 2, 3, 7, 8, 11, 12, 13, 14, 15, 1, 2 };
static ACPI_RESOURCE resources[41];
static size_t nresources;
static ACPI_INTEGER version, sta, cca;
static ACPI_STATUS dsd_status, eval_status, walk_status;
static unsigned int exchanges, walks;
static int bad_word, fail_at, fail_errno;
static uint32_t clock_state, clock_rate;
static int bad_clock;

static void
reset(void)
{
	size_t i;
	memset(resources, 0, sizeof(resources));
	for (i = 0; i < 32; i++) {
		ACPI_RESOURCE_ADDRESS64 *a = &resources[i].Data.Address64;
		resources[i].Type = ACPI_RESOURCE_TYPE_ADDRESS64;
		a->ResourceType = ACPI_MEMORY_RANGE;
		a->ProducerConsumer = ACPI_CONSUMER;
		a->MinAddressFixed = a->MaxAddressFixed = ACPI_ADDRESS_FIXED;
		a->Info.Mem.WriteProtect = ACPI_READ_WRITE_MEMORY;
		a->Address.Minimum = golden_memory[i].address;
		a->Address.AddressLength = golden_memory[i].length;
		a->Address.Maximum = a->Address.Minimum + a->Address.AddressLength - 1;
	}
	resources[32].Type = ACPI_RESOURCE_TYPE_EXTENDED_IRQ;
	resources[32].Data.ExtendedIrq.ProducerConsumer = ACPI_CONSUMER;
	resources[32].Data.ExtendedIrq.InterruptCount = 7;
	memcpy(resources[32].Data.ExtendedIrq.Interrupts, golden_irq, sizeof(golden_irq));
	resources[33].Type = ACPI_RESOURCE_TYPE_END_TAG;
	nresources = 34;
	version = 1; sta = 15; cca = 0;
	dsd_status = eval_status = walk_status = AE_OK;
	exchanges = walks = 0;
	bad_word = fail_at = bad_clock = -1;
	fail_errno = ETIMEDOUT; clock_state = 1; clock_rate = 120000000;
}
static ACPI_STATUS
acpi_dsd_integer(ACPI_HANDLE h, const char *property, ACPI_INTEGER *out)
{
	assert(h != NULL);
	assert(strcmp(property, "emberbsd,display-contract-version") == 0);
	if (dsd_status == AE_OK) *out = version;
	return dsd_status;
}
static ACPI_STATUS
acpi_eval_integer(ACPI_HANDLE h, const char *property, ACPI_INTEGER *out)
{
	assert(h != NULL);
	assert(strcmp(property, "_STA") == 0 || strcmp(property, "_CCA") == 0);
	if (eval_status == AE_OK) *out = strcmp(property, "_STA") == 0 ? sta : cca;
	return eval_status;
}
static ACPI_STATUS
AcpiWalkResources(ACPI_HANDLE h, const char *method,
    ACPI_STATUS (*callback)(ACPI_RESOURCE *, void *), void *arg)
{
	size_t i;
	ACPI_STATUS status;
	assert(h != NULL && strcmp(method, "_CRS") == 0);
	walks++;
	if (walk_status != AE_OK) return walk_status;
	for (i = 0; i < nresources; i++) {
		status = callback(&resources[i], arg);
		if (status != AE_OK) return status;
	}
	return AE_OK;
}
static int
bcmmbox_request(unsigned int channel, void *buffer, size_t length, uint32_t *reply)
{
	uint32_t *p = buffer, id, tag;
	unsigned int index = exchanges++;
	assert(channel == 8 && length == 32 && ((uintptr_t)p & 15) == 0);
	assert(index < 8);
	id = golden_clock[index / 2];
	tag = index % 2 ? VCPROPTAG_GET_CLOCKRATE : VCPROPTAG_GET_CLOCKSTATE;
	assert(le32toh(p[0]) == 32 && p[1] == 0 && le32toh(p[2]) == tag);
	assert(le32toh(p[3]) == 8 && p[4] == 0 && le32toh(p[5]) == id);
	assert(p[6] == 0 && p[7] == 0);
	if ((int)index == fail_at) return fail_errno;
	p[1] = htole32(VCPROP_REQ_SUCCESS);
	p[4] = htole32(VCPROPTAG_RESPONSE | 8);
	p[6] = htole32(index % 2 ? 120000000 : 1);
	if ((int)(index / 2) == bad_clock)
		p[6] = htole32(index % 2 ? clock_rate : clock_state);
	if (bad_word >= 0) p[bad_word] ^= htole32(1);
	*reply = 0;
	return 0;
}
static void
aprint_normal_dev(device_t dev, const char *format, ...)
{
	(void)dev; (void)format;
}
static void
expect(int error, unsigned int requests)
{
	struct bcm2712_display_contract contract, before;
	struct bcm2712_display_clocks clocks, before_clocks;
	memset(&contract, 0xa5, sizeof(contract)); before = contract;
	memset(&clocks, 0x5a, sizeof(clocks)); before_clocks = clocks;
	assert(bcm2712_display_acpi_query(NULL, &resources, &contract, &clocks) == error);
	assert(exchanges == requests);
	if (error != 0) {
		assert(memcmp(&contract, &before, sizeof(contract)) == 0);
		assert(memcmp(&clocks, &before_clocks, sizeof(clocks)) == 0);
	} else {
		assert(contract.version == 1);
		assert(memcmp(contract.memory, golden_memory, sizeof(golden_memory)) == 0);
		assert(memcmp(contract.irq, golden_irq, sizeof(golden_irq)) == 0);
		for (size_t i = 0; i < 4; i++) {
			assert(clocks.clock[i].id == golden_clock[i]);
			assert(clocks.clock[i].state == 1 && clocks.clock[i].rate == 120000000);
		}
	}
}
int
main(void)
{
	unsigned int i;
	struct bcm2712_display_contract output, before_output;
	struct bcm2712_display_memory changed[32];
	uint32_t irq[7];
	memset(&output, 0xa5, sizeof(output));
	before_output = output;
	reset(); expect(0, 8);
	assert(bcm2712_display_decode(2, golden_memory, 32, golden_irq, 7, &output) == EOPNOTSUPP);
	reset(); dsd_status = AE_NOT_FOUND; expect(EOPNOTSUPP, 0); assert(walks == 0);
	reset(); dsd_status = AE_TYPE; expect(EIO, 0);
	reset(); version = 0; expect(EOPNOTSUPP, 0);
	reset(); version = UINT64_C(0x100000001); expect(EOPNOTSUPP, 0); assert(walks == 0);
	reset(); eval_status = AE_ERROR; expect(EIO, 0);
	reset(); sta = 0; expect(EINVAL, 0);
	reset(); cca = 1; expect(EINVAL, 0);
	reset(); walk_status = AE_ERROR; expect(EIO, 0);
	reset(); resources[0].Type = 99; expect(EINVAL, 0);
	reset(); nresources = 31; expect(EINVAL, 0);
	reset(); resources[33] = resources[0]; expect(EINVAL, 0);
	reset(); resources[32].Data.ExtendedIrq.InterruptCount = 8; expect(EINVAL, 0);
	reset(); resources[32].Data.ExtendedIrq.InterruptCount = 0; expect(EINVAL, 0);
	reset(); resources[32].Data.ExtendedIrq.WakeCapable = 1; expect(EINVAL, 0);
	reset(); resources[32].Data.ExtendedIrq.ResourceSource.StringLength = 1; expect(EINVAL, 0);
	reset(); resources[0].Data.Address64.ResourceSource.StringLength = 1; expect(EINVAL, 0);
	reset(); resources[32].Data.ExtendedIrq.Triggering = 1; expect(EINVAL, 0);
	reset(); resources[32].Data.ExtendedIrq.Polarity = 1; expect(EINVAL, 0);
	reset(); resources[32].Data.ExtendedIrq.Shareable = 1; expect(EINVAL, 0);
	reset(); resources[32].Data.ExtendedIrq.ProducerConsumer = 0; expect(EINVAL, 0);
	reset(); resources[0].Data.Address64.Address.TranslationOffset = 1; expect(EINVAL, 0);
	reset(); resources[0].Data.Address64.Address.Maximum++; expect(EINVAL, 0);
	reset(); resources[0].Data.Address64.Info.Mem.RangeType = 1; expect(EINVAL, 0);
	reset(); resources[0].Data.Address64.Info.Mem.Translation = 1; expect(EINVAL, 0);
	reset(); resources[0].Data.Address64.Info.Mem.Caching = 1; expect(EINVAL, 0);
	reset(); resources[0].Data.Address64.Info.Mem.WriteProtect = 0; expect(EINVAL, 0);
	reset(); resources[0].Data.Address64.MinAddressFixed = 0; expect(EINVAL, 0);
	reset(); resources[0].Data.Address64.MaxAddressFixed = 0; expect(EINVAL, 0);
	reset(); resources[0].Data.Address64.Decode = 1; expect(EINVAL, 0);
	reset(); resources[0].Data.Address64.ProducerConsumer = 0; expect(EINVAL, 0);
	reset(); resources[0].Data.Address64.ResourceType = 1; expect(EINVAL, 0);
	reset(); resources[0].Data.Address64.Address.Granularity = 1; expect(EINVAL, 0);
	for (i = 0; i < 32; i++) {
		reset(); resources[i].Data.Address64.Address.AddressLength = 0; expect(EINVAL, 0);
		memcpy(changed, golden_memory, sizeof(changed));
		changed[i].length = 0;
		assert(bcm2712_display_decode(1, changed, 32, golden_irq, 7, &output) == EINVAL);
		changed[i].length = golden_memory[i].length + 1;
		assert(bcm2712_display_decode(1, changed, 32, golden_irq, 7, &output) == EINVAL);
	}
	memcpy(changed, golden_memory, sizeof(changed)); changed[31] = changed[0];
	assert(bcm2712_display_decode(1, changed, 32, golden_irq, 7, &output) == EINVAL);
	changed[31].address = UINT64_MAX; changed[31].length = 2;
	assert(bcm2712_display_decode(1, changed, 32, golden_irq, 7, &output) == EINVAL);
	for (i = 0; i < 7; i++) {
		memcpy(irq, golden_irq, sizeof(irq)); irq[i]++;
		assert(bcm2712_display_decode(1, golden_memory, 32, irq, 7, &output) == EINVAL);
	}
	assert(bcm2712_display_decode(1, golden_memory, 32, golden_irq, 6, &output) == EINVAL);
	assert(bcm2712_display_decode(1, NULL, 32, golden_irq, 7, &output) == EINVAL);
	assert(bcm2712_display_decode(1, golden_memory, 32, NULL, 7, &output) == EINVAL);
	assert(bcm2712_display_decode(1, golden_memory, 32, golden_irq, 7, NULL) == EINVAL);
	for (i = 0; i < 8; i++) {
		reset(); fail_at = (int)i; expect(ETIMEDOUT, i + 1);
		if (i == 6) continue;
		reset(); bad_word = (int)i; expect(EIO, 1);
	}
	for (i = 0; i < 4; i++) {
		reset(); bad_clock = (int)i; clock_state = 0; expect(EBUSY, 2 * (i + 1));
		reset(); bad_clock = (int)i; clock_state = 3; expect(EBUSY, 2 * (i + 1));
		reset(); bad_clock = (int)i; clock_rate = 0; expect(EBUSY, 2 * (i + 1));
	}
	reset(); bad_clock = 0; clock_rate = 119999999; expect(EBUSY, 2);
	assert(strcmp(bcm2712_display_irqs[0].name, "v3d-hub") == 0);
	assert(strcmp(bcm2712_display_irqs[1].name, "v3d-core") == 0);
	assert(bcm2712_display_resources[10].owner == BCM2712_OWNER_DVP_CLOCK);
	assert(bcm2712_display_resources[11].owner == BCM2712_OWNER_HD_SHARED);
	assert(bcm2712_display_domains[0].memory_index == 9 && bcm2712_display_domains[0].irq_index == 4);
	assert(bcm2712_display_domains[1].memory_index == 12 && bcm2712_display_domains[1].irq_index == 5);
	assert(bcm2712_display_domains[2].memory_index == 13 && bcm2712_display_domains[2].irq_index == 6);
	for (i = 0; i < 17; i++) {
		assert(bcm2712_display_leaves[i].domain == golden_leaf_domain[i]);
		assert(bcm2712_display_leaves[i].bit == golden_leaf_bit[i]);
		assert(bcm2712_display_leaves[i].bit < 32);
		for (unsigned int j = 0; j < i; j++)
			assert(bcm2712_display_leaves[i].domain != bcm2712_display_leaves[j].domain ||
			    bcm2712_display_leaves[i].bit != bcm2712_display_leaves[j].bit);
	}
	assert(memcmp(&output, &before_output, sizeof(output)) == 0);
	/* Cold resources remain available without running any clock transaction. */
	{
		struct bcm2712_display_contract cold, saved;
		reset(); bad_clock = 0; clock_rate = 0;
		expect(EBUSY, 2);
		exchanges = 0;
		memset(&cold, 0xa5, sizeof(cold));
		assert(bcm2712_display_acpi_resources(NULL, &resources, &cold) == 0);
		assert(exchanges == 0 && cold.version == 1);
		assert(memcmp(cold.memory, golden_memory, sizeof(golden_memory)) == 0);
		saved = cold;
		version = 0;
		assert(bcm2712_display_acpi_resources(NULL, &resources, &cold) == EOPNOTSUPP);
		assert(memcmp(&cold, &saved, sizeof(cold)) == 0 && exchanges == 0);
	}
	puts("BCM2712 display contract: resources, flags, versions and bounded clock failures PASS");
	return 0;
}
