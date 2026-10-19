/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Actual query sources and ACPICA types; controlled namespace services only. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <platform/acenv.h>
#include <actypes.h>
#include <acrestyp.h>
#include <acexcep.h>

#include "display.h"
#include "iommuc.h"

#ifndef __aligned
#define __aligned(n) __attribute__((__aligned__(n)))
#endif
#define ASSERT_SLEEPABLE() ((void)0)
#ifndef htole32
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define htole32(v) ((uint32_t)(v))
#else
#define htole32(v) __builtin_bswap32((uint32_t)(v))
#endif
#endif
#ifndef le32toh
#define le32toh(v) htole32(v)
#endif
#define BCMMBOX_CHANARM2VC 8
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
typedef void *device_t;

static unsigned int checks, calls, fail_at, allocations, frees, mailbox_calls;
static bool hard_irq, soft_irq;
static int nodes[40];
#define CACHE ((ACPI_HANDLE)&nodes[0])
#define GPU ((ACPI_HANDLE)&nodes[1])
#define OTHER ((ACPI_HANDLE)&nodes[2])
#define ALIAS ((ACPI_HANDLE)&nodes[39])
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, \
    "ICAC contract failed at line %d: %s\n", __LINE__, #c); abort(); } } while (0)

static struct {
	ACPI_RESOURCE resource[4];
	unsigned int nresource, dep_count;
	ACPI_OBJECT references[40];
	ACPI_OBJECT_TYPE dep_type, cache_type, gpu_type, actual_type;
	char hid[32], compatible[64];
	ACPI_INTEGER version, uid, sta, gpu_version, gpu_sta, gpu_cca;
	bool missing, gpu_missing, alias_gpu, hid_valid, hid_null, null_package, null_elements;
} input;

static int cpu_intr_p(void) { return hard_irq; }
static int cpu_softintr_p(void) { return soft_irq; }
static bool failing(void) { return ++calls == fail_at; }
static void *allocate(size_t n)
{
	void *p = calloc(1, n);
	CHECK(p != NULL);
	allocations++;
	return p;
}
static void fixture_free(void *p)
{
	if (p != NULL) {
		frees++;
		free(p);
	}
}
#undef ACPI_FREE
#define ACPI_FREE(p) fixture_free(p)
#define kmem_strfree(p) fixture_free(p)

static ACPI_STATUS AcpiGetHandle(ACPI_HANDLE, const char *, ACPI_HANDLE *);
static ACPI_STATUS AcpiGetType(ACPI_HANDLE, ACPI_OBJECT_TYPE *);
static ACPI_STATUS AcpiGetObjectInfo(ACPI_HANDLE, ACPI_DEVICE_INFO **);
static ACPI_STATUS AcpiEvaluateObjectTyped(ACPI_HANDLE, const char *, void *,
    ACPI_BUFFER *, ACPI_OBJECT_TYPE);
static ACPI_STATUS acpi_dsd_integer(ACPI_HANDLE, const char *, ACPI_INTEGER *);
static ACPI_STATUS acpi_dsd_string(ACPI_HANDLE, const char *, char **);
static ACPI_STATUS acpi_eval_integer(ACPI_HANDLE, const char *, ACPI_INTEGER *);
static ACPI_STATUS AcpiWalkResources(ACPI_HANDLE, const char *,
    ACPI_STATUS (*)(ACPI_RESOURCE *, void *), void *);
static int bcmmbox_request(int, uint32_t *, size_t, uint32_t *);
static void aprint_normal_dev(device_t, const char *, ...);
static ACPI_STATUS acpi_eval_reference_handle(ACPI_OBJECT *, ACPI_HANDLE *);

#include "properties.h"
#include "display-core.h"
#include "display-adapter.h"
#include "reference-helper.h"
#include "iommuc-adapter.h"

static ACPI_STATUS
AcpiGetHandle(ACPI_HANDLE parent, const char *path, ACPI_HANDLE *out)
{
	CHECK(parent == NULL);
	if (failing())
		return AE_ERROR;
	if (strcmp(path, "\\_SB.GPU0") == 0) {
		if (input.gpu_missing)
			return AE_NOT_FOUND;
		*out = GPU;
		return AE_OK;
	}
	if (strcmp(path, "\\_SB.ICAC") != 0 || input.missing)
		return AE_NOT_FOUND;
	*out = CACHE;
	return AE_OK;
}

static ACPI_STATUS
AcpiGetType(ACPI_HANDLE handle, ACPI_OBJECT_TYPE *out)
{
	unsigned int i;

	if (failing())
		return AE_ERROR;
	for (i = 0; i < __arraycount(nodes); i++) {
		if (handle == &nodes[i]) {
			*out = handle == CACHE ? input.cache_type : handle == GPU ? input.gpu_type : ACPI_TYPE_DEVICE;
			return AE_OK;
		}
	}
	return AE_NOT_FOUND;
}

static ACPI_STATUS
AcpiGetObjectInfo(ACPI_HANDLE handle, ACPI_DEVICE_INFO **out)
{
	ACPI_DEVICE_INFO *info;

	CHECK(handle == CACHE || handle == ALIAS);
	if (failing())
		return AE_ERROR;
	info = allocate(sizeof(*info));
	info->Type = input.actual_type;
	info->Valid = input.hid_valid ? ACPI_VALID_HID : 0;
	info->HardwareId.String = input.hid_null ? NULL : input.hid;
	*out = info;
	return AE_OK;
}

static ACPI_STATUS
acpi_dsd_string(ACPI_HANDLE handle, const char *name, char **out)
{
	CHECK(handle == CACHE || handle == ALIAS);
	CHECK(strcmp(name, "compatible") == 0);
	if (failing())
		return AE_ERROR;
	*out = allocate(strlen(input.compatible) + 1);
	strcpy(*out, input.compatible);
	return AE_OK;
}

static ACPI_STATUS
acpi_dsd_integer(ACPI_HANDLE handle, const char *name, ACPI_INTEGER *out)
{
	if (failing())
		return AE_ERROR;
	if (handle == GPU || (handle == ALIAS && input.alias_gpu)) {
		CHECK(strcmp(name, BCM2712_DISPLAY_PROPERTY) == 0);
		*out = input.gpu_version;
	} else {
		CHECK(handle == CACHE || handle == ALIAS);
		CHECK(strcmp(name, BCM2712_IOMMUC_PROPERTY) == 0);
		*out = input.version;
	}
	return AE_OK;
}

static ACPI_STATUS
acpi_eval_integer(ACPI_HANDLE handle, const char *name, ACPI_INTEGER *out)
{
	if (failing())
		return AE_ERROR;
	if (handle == GPU || (handle == ALIAS && input.alias_gpu)) {
		CHECK(strcmp(name, "_STA") == 0 || strcmp(name, "_CCA") == 0);
		*out = strcmp(name, "_STA") == 0 ? input.gpu_sta : input.gpu_cca;
	} else {
		CHECK(handle == CACHE || handle == ALIAS);
		CHECK(strcmp(name, "_UID") == 0 || strcmp(name, "_STA") == 0);
		*out = strcmp(name, "_UID") == 0 ? input.uid : input.sta;
	}
	return AE_OK;
}

static ACPI_STATUS
AcpiEvaluateObjectTyped(ACPI_HANDLE handle, const char *name, void *args,
    ACPI_BUFFER *buffer, ACPI_OBJECT_TYPE type)
{
	ACPI_OBJECT *package;

	CHECK((handle == GPU || (handle == ALIAS && input.alias_gpu)) && strcmp(name, "_DEP") == 0 && args == NULL);
	CHECK(type == ACPI_TYPE_PACKAGE && buffer->Length == ACPI_ALLOCATE_BUFFER);
	if (failing())
		return AE_ERROR;
	if (input.null_package) {
		buffer->Pointer = NULL;
		return AE_OK;
	}
	package = allocate(sizeof(*package));
	package->Type = input.dep_type;
	package->Package.Count = input.dep_count;
	package->Package.Elements = input.null_elements ? NULL : input.references;
	buffer->Pointer = package;
	return AE_OK;
}

static ACPI_RESOURCE
memory(uint64_t address, uint64_t length)
{
	ACPI_RESOURCE r = { 0 };
	ACPI_RESOURCE_ADDRESS64 *a = &r.Data.Address64;

	r.Type = ACPI_RESOURCE_TYPE_ADDRESS64;
	a->ResourceType = ACPI_MEMORY_RANGE;
	a->ProducerConsumer = ACPI_CONSUMER;
	a->Decode = ACPI_POS_DECODE;
	a->MinAddressFixed = a->MaxAddressFixed = ACPI_ADDRESS_FIXED;
	a->Info.Mem.WriteProtect = ACPI_READ_WRITE_MEMORY;
	a->Info.Mem.Caching = ACPI_NON_CACHEABLE_MEMORY;
	a->Address.Minimum = address;
	a->Address.Maximum = address + length - 1;
	a->Address.AddressLength = length;
	return r;
}

static ACPI_STATUS
AcpiWalkResources(ACPI_HANDLE handle, const char *name,
    ACPI_STATUS (*callback)(ACPI_RESOURCE *, void *), void *arg)
{
	ACPI_RESOURCE r;
	ACPI_STATUS status;
	unsigned int i;

	CHECK(strcmp(name, "_CRS") == 0);
	if (failing())
		return AE_ERROR;
	if (handle == GPU || (handle == ALIAS && input.alias_gpu)) {
		for (i = 0; i < BCM2712_DISPLAY_NMEM; i++) {
			r = memory(bcm2712_display_resources[i].memory.address,
			    bcm2712_display_resources[i].memory.length);
			if (ACPI_FAILURE(status = callback(&r, arg)))
				return status;
		}
		for (i = 0; i < BCM2712_DISPLAY_NIRQ; i++) {
			memset(&r, 0, sizeof(r));
			r.Type = ACPI_RESOURCE_TYPE_EXTENDED_IRQ;
			r.Data.ExtendedIrq.ProducerConsumer = ACPI_CONSUMER;
			r.Data.ExtendedIrq.Triggering = ACPI_LEVEL_SENSITIVE;
			r.Data.ExtendedIrq.Polarity = ACPI_ACTIVE_HIGH;
			r.Data.ExtendedIrq.Shareable = ACPI_EXCLUSIVE;
			r.Data.ExtendedIrq.InterruptCount = 1;
			r.Data.ExtendedIrq.Interrupts[0] = bcm2712_display_irqs[i].gsi;
			if (ACPI_FAILURE(status = callback(&r, arg)))
				return status;
		}
		memset(&r, 0, sizeof(r));
		r.Type = ACPI_RESOURCE_TYPE_END_TAG;
		return callback(&r, arg);
	}
	CHECK(handle == CACHE || handle == ALIAS);
	for (i = 0; i < input.nresource; i++) {
		if (ACPI_FAILURE(status = callback(&input.resource[i], arg)))
			return status;
	}
	return AE_OK;
}

static int
bcmmbox_request(int channel, uint32_t *request, size_t length, uint32_t *response)
{
	(void)channel;
	(void)request;
	(void)length;
	(void)response;
	mailbox_calls++;
	abort();
}

static void
aprint_normal_dev(device_t dev, const char *format, ...)
{
	(void)dev;
	(void)format;
	abort();
}

static void
setup(void)
{
	CHECK(allocations == frees && mailbox_calls == 0);
	memset(&input, 0, sizeof(input));
	calls = fail_at = 0;
	hard_irq = soft_irq = false;
	input.resource[0] = memory(UINT64_C(0x1000005b00), UINT64_C(0x80));
	input.resource[1].Type = ACPI_RESOURCE_TYPE_END_TAG;
	input.nresource = 2;
	input.dep_type = ACPI_TYPE_PACKAGE;
	input.cache_type = input.gpu_type = input.actual_type = ACPI_TYPE_DEVICE;
	input.hid_valid = true;
	strcpy(input.hid, "PRP0001");
	strcpy(input.compatible, "brcm,bcm2712-iommuc");
	input.version = input.gpu_version = 1;
	input.sta = input.gpu_sta = 15;
	input.dep_count = 1;
	input.references[0].Type = ACPI_TYPE_LOCAL_REFERENCE;
	input.references[0].Reference.ActualType = ACPI_TYPE_DEVICE;
	input.references[0].Reference.Handle = CACHE;
}

static int
query(bool dependency, bool accepted)
{
	struct { uint64_t before; struct bcm2712_iommuc_contract data;
	    uint64_t after; } out, saved;
	struct bcm2712_iommuc_contract expected;
	ACPI_HANDLE cache = ALIAS;
	int error;

	memset(&out, 0xa5, sizeof(out));
	saved = out;
	error = dependency ? bcm2712_iommuc_acpi_dependency(GPU, &cache, &out.data) :
	    bcm2712_iommuc_acpi_resources(CACHE, &out.data);
	if (accepted) {
		CHECK(error == 0);
		memset(&expected, 0, sizeof(expected));
		expected.version = 1;
		expected.address = UINT64_C(0x1000005b00);
		expected.length = UINT64_C(0x80);
		CHECK(memcmp(&out.data, &expected, sizeof(expected)) == 0);
		CHECK(!dependency || cache == CACHE);
	} else {
		CHECK(error != 0);
		CHECK(memcmp(&out, &saved, sizeof(out)) == 0);
		CHECK(cache == ALIAS);
	}
	CHECK(out.before == saved.before && out.after == saved.after);
	CHECK(allocations == frees && mailbox_calls == 0);
	return error;
}

int
main(void)
{
	unsigned int i, count, mode;
	ACPI_RESOURCE_ADDRESS64 *a;
	struct bcm2712_iommuc_contract out;
	ACPI_HANDLE handle;

	for (mode = 0; mode < 2; mode++) {
		setup();
		CHECK(query(mode != 0, true) == 0);
		count = calls;
		for (i = 1; i <= count; i++) {
			setup(); fail_at = i; query(mode != 0, false);
		}
		for (i = 0; i < 32; i++) {
			setup(); a = &input.resource[0].Data.Address64;
			switch (i) {
			case 0: input.missing = true; break;
			case 1: input.cache_type = ACPI_TYPE_INTEGER; break;
			case 2: input.actual_type = ACPI_TYPE_INTEGER; break;
			case 3: input.hid_valid = false; break;
			case 4: input.hid_null = true; break;
			case 5: strcpy(input.hid, "BCM2712"); break;
			case 6: strcpy(input.compatible, "brcm,bcm2712-iommu"); break;
			case 7: input.version = 2; break;
			case 8: input.uid = 1; break;
			case 9: input.sta = 14; break;
			case 10: a->ResourceType = ACPI_IO_RANGE; break;
			case 11: a->ProducerConsumer = ACPI_PRODUCER; break;
			case 12: a->Decode = ACPI_SUB_DECODE; break;
			case 13: a->MinAddressFixed = 0; break;
			case 14: a->MaxAddressFixed = 0; break;
			case 15: a->Info.Mem.WriteProtect = ACPI_READ_ONLY_MEMORY; break;
			case 16: a->Info.Mem.Caching = ACPI_CACHABLE_MEMORY; break;
			case 17: a->Info.Mem.RangeType = 1; break;
			case 18: a->Info.Mem.Translation = 1; break;
			case 19: a->ResourceSource.StringLength = 1; break;
			case 20: a->Address.Granularity = 1; break;
			case 21: a->Address.TranslationOffset = 1; break;
			case 22: a->Address.Minimum++; break;
			case 23: a->Address.Maximum--; break;
			case 24: a->Address.AddressLength--; break;
			case 25: input.nresource = 1; break;
			case 26: input.nresource = 3;
				input.resource[2].Type = ACPI_RESOURCE_TYPE_END_TAG; break;
			case 27: input.resource[1] = input.resource[0]; break;
			case 28: input.resource[0].Type = ACPI_RESOURCE_TYPE_FIXED_MEMORY32; break;
			case 29: input.resource[0].Type = ACPI_RESOURCE_TYPE_EXTENDED_IRQ; break;
			case 30: hard_irq = true; break;
			case 31: soft_irq = true; break;
			}
			query(mode != 0, false);
			if (i >= 30)
				CHECK(calls == 0);
		}
	}
	for (i = 0; i < 32; i++) {
		if (i == 15)
			continue;
		setup(); input.sta = i; query(false, false);
		setup(); input.gpu_sta = i; query(true, false);
	}
	setup();
	CHECK(bcm2712_iommuc_acpi_resources(NULL, &out) == EINVAL && calls == 0);
	CHECK(bcm2712_iommuc_acpi_resources(CACHE, NULL) == EINVAL && calls == 0);
	CHECK(bcm2712_iommuc_acpi_resources(ALIAS, &out) == EINVAL);
	setup();
	CHECK(bcm2712_iommuc_acpi_dependency(NULL, &handle, &out) == EINVAL);
	CHECK(bcm2712_iommuc_acpi_dependency(GPU, NULL, &out) == EINVAL);
	CHECK(bcm2712_iommuc_acpi_dependency(GPU, &handle, NULL) == EINVAL);
	CHECK(calls == 0);
	for (i = 0; i < 17; i++) {
		setup();
		switch (i) {
		case 0: input.gpu_version = 2; break;
		case 1: input.gpu_sta = 14; break;
		case 2: input.gpu_cca = 1; break;
		case 3: input.dep_type = ACPI_TYPE_INTEGER; break;
		case 4: input.dep_count = 0; break;
		case 5: input.dep_count = 33; break;
		case 6: input.null_package = true; break;
		case 7: input.null_elements = true; break;
		case 8: input.references[0].Type = ACPI_TYPE_ANY; break;
		case 9: input.references[0].Type = ACPI_TYPE_STRING;
			input.references[0].String.Length = ACPI_TYPE_DEVICE;
			input.references[0].String.Pointer = "\\_SB.ICAC"; break;
		case 10: input.references[0].Type = ACPI_TYPE_INTEGER; break;
		case 11: input.references[0].Reference.ActualType = ACPI_TYPE_ANY; break;
		case 12: input.references[0].Reference.Handle = NULL; break;
		case 13: input.references[0].Reference.Handle = ALIAS; break;
		case 15: input.gpu_missing = true; break;
		case 16: input.gpu_type = ACPI_TYPE_INTEGER; break;
		case 14: input.dep_count = 2;
			input.references[1] = input.references[0]; break;
		}
		query(true, false);
	}
	setup();
	for (i = 0; i < 32; i++) {
		input.references[i].Type = ACPI_TYPE_LOCAL_REFERENCE;
		input.references[i].Reference.ActualType = ACPI_TYPE_DEVICE;
		input.references[i].Reference.Handle = &nodes[i + 2];
	}
	input.dep_count = 32;
	for (i = 0; i < 32; i++) {
		handle = input.references[i].Reference.Handle;
		input.references[i].Reference.Handle = CACHE;
		CHECK(query(true, true) == 0);
		input.references[i].Reference.Handle = handle;
	}
	setup(); input.alias_gpu = true;
	memset(&out, 0xa5, sizeof(out)); handle = ALIAS;
	CHECK(bcm2712_iommuc_acpi_dependency(ALIAS, &handle, &out) == EINVAL);
	CHECK(calls == 1 && handle == ALIAS);
	for (i = 0; i < sizeof(out); i++)
		CHECK(((uint8_t *)&out)[i] == 0xa5);
	setup(); input.dep_count = 3;
	input.references[1] = input.references[2] = input.references[0];
	input.references[1].Reference.Handle = input.references[2].Reference.Handle = OTHER;
	query(true, false);
	setup(); input.references[0].Reference.Handle = (void *)&checks;
	query(true, false);
	printf("BCM2712 ICAC descriptor: %u checks PASS; actual ACPI types/query/helper, host only\n", checks);
	return 0;
}
