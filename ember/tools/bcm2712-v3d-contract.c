/* Origin: EmberBSD BCM2712 V3D actual-source failure contract, 2026-10-09. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef __BIT
#define __BIT(n) (UINT32_C(1) << (n))
#endif
#ifndef __aligned
#define __aligned(n) __attribute__((aligned(n)))
#endif
#ifndef htole32
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define htole32(v) ((uint32_t)(v))
#define le32toh(v) ((uint32_t)(v))
#else
#define htole32(v) __builtin_bswap32((uint32_t)(v))
#define le32toh(v) __builtin_bswap32((uint32_t)(v))
#endif
#endif
#define ACPI_TYPE_DEVICE 6
#define ACPI_VALID_HID 1
#define ACPI_FAILURE(s) ((s) != 0)
#define BCMMBOX_CHANARM2VC 8
#define CFATTACH_DECL_NEW(n, s, m, a, d, r)
#define aprint_naive(...) ((void)0)
#define aprint_normal(...) ((void)0)
#define aprint_normal_dev(d, ...) contract_print(__VA_ARGS__)
#define aprint_error_dev(...) ((void)0)

typedef uint64_t bus_addr_t;
typedef size_t bus_size_t;
typedef unsigned int bus_space_tag_t;
typedef void *ACPI_HANDLE;
typedef unsigned int bus_dma_tag_t;
typedef unsigned int bus_space_handle_t;
typedef void *cfdata_t;
typedef uint64_t ACPI_INTEGER;
typedef int ACPI_STATUS;
typedef struct {
	unsigned int Valid;
	struct { char *String; } HardwareId;
	const char *cid;
} ACPI_DEVICE_INFO;
struct acpi_node {
	int ad_type;
	void *ad_handle;
	ACPI_DEVICE_INFO *ad_devinfo;
};
struct acpi_attach_args {
	struct acpi_node *aa_node;
	bus_space_tag_t aa_memt;
	bus_dma_tag_t aa_dmat;
};
struct acpi_mem { bus_addr_t ar_base; bus_size_t ar_length; };
struct acpi_resources { int parsed; };
static int acpi_resource_parse_ops_default;
typedef struct device { void *private; } *device_t;
#define device_private(d) ((d)->private)
struct bcm2835pmwdog_softc {
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
};
static struct bcm2835pmwdog_softc *bcmpmwdog_v3d_sc;

#include "properties.h"

static unsigned int cases, requests, maps, unmaps, reads, gpu_reads;
static unsigned int fail_map, fail_read, malformed_word, malformed_call;
static unsigned int resource_count, cleanups, registration_error;
static unsigned int fail_eval, fail_parse, pm_fault;
static uint32_t malformed_value, clock_state, rate, pm_value;
static uint32_t sms_words[4], ident1;
static int mailbox_error;
static unsigned int mailbox_error_call;
static char output[4096];
static size_t output_used;
static bool active[3];
static ACPI_INTEGER sta, cca;
static struct acpi_mem resources[3];
static int (*finalizer)(device_t);
enum probe_stage {
	PROBE_TAKEOVER, PROBE_DMA, PROBE_QUEUE, PROBE_IRQ, PROBE_FAULT,
	PROBE_COUNT
};
static unsigned int probe_calls[PROBE_COUNT], probe_order[PROBE_COUNT];
static unsigned int probe_count;
static int probe_errors[PROBE_COUNT];

static int bcmmbox_request(uint8_t, void *, size_t, uint32_t *);
static int bus_space_map(bus_space_tag_t, bus_addr_t, bus_size_t, int,
    bus_space_handle_t *);
static int bus_space_peek_4(bus_space_tag_t, bus_space_handle_t,
    bus_size_t, uint32_t *);
static void bus_space_unmap(bus_space_tag_t, bus_space_handle_t, bus_size_t);
static int acpi_eval_integer(void *, const char *, ACPI_INTEGER *);
static int acpi_resource_parse(device_t, void *, const char *,
    struct acpi_resources *, void *);
static struct acpi_mem *acpi_res_mem(struct acpi_resources *, unsigned int);
static void acpi_resource_cleanup(struct acpi_resources *);
static int acpi_match_hid(ACPI_DEVICE_INFO *, const char * const *);
static int config_finalize_register(device_t, int (*)(device_t));
static void contract_print(const char *, ...);

#include "pm-accessor.h"
#include "driver.h"

static struct bcmv3d_softc sc;
static struct device dev = { &sc };
static struct bcm2835pmwdog_softc pm = { 1, 100 };
static ACPI_DEVICE_INFO info = { ACPI_VALID_HID, { "BCM2712" }, "BCM2850" };
static struct acpi_node node = { ACPI_TYPE_DEVICE, &node, &info };
static struct acpi_attach_args aa = { &node, 1, 2 };

#ifdef BCM2712_V3D_TAKEOVER
/* Keep the production finalizer; substitute only its hardware experiments. */
static int
probe_result(device_t device, enum probe_stage stage)
{
	assert(device == &dev && sc.sc_observed);
	assert(requests == 2 && maps == 3 && unmaps == 3);
	assert(probe_count < PROBE_COUNT);
	probe_order[probe_count++] = stage;
	probe_calls[stage]++;
	return probe_errors[stage];
}

int
bcmv3d_takeover_probe(device_t device, bus_space_tag_t bst)
{
	assert(bst == aa.aa_memt);
	return probe_result(device, PROBE_TAKEOVER);
}

#ifdef BCM2712_V3D_DMA_PROBE
int
bcmv3d_dma_probe(device_t device, bus_dma_tag_t dmat)
{
	assert(dmat == aa.aa_dmat);
	return probe_result(device, PROBE_DMA);
}
#endif
#ifdef BCM2712_V3D_QUEUE_PROBE
int
bcmv3d_queue_probe(device_t device, bus_dma_tag_t dmat)
{
	assert(dmat == aa.aa_dmat);
	return probe_result(device, PROBE_QUEUE);
}
#endif
#ifdef BCM2712_V3D_IRQ_PROBE
int
bcmv3d_irq_probe(device_t device, bus_dma_tag_t dmat, ACPI_HANDLE handle)
{
	assert(dmat == aa.aa_dmat && handle == node.ad_handle);
	return probe_result(device, PROBE_IRQ);
}
#endif
#ifdef BCM2712_V3D_FAULT_PROBE
int
bcmv3d_fault_probe(device_t device, bus_dma_tag_t dmat, ACPI_HANDLE handle)
{
	assert(dmat == aa.aa_dmat && handle == node.ad_handle);
	return probe_result(device, PROBE_FAULT);
}
#endif
#endif

static void
contract_print(const char *format, ...)
{
	va_list ap;
	int len;

	va_start(ap, format);
	len = vsnprintf(output + output_used, sizeof(output) - output_used,
	    format, ap);
	va_end(ap);
	assert(len >= 0 && (size_t)len < sizeof(output) - output_used);
	output_used += (size_t)len;
}

static int
bcmmbox_request(uint8_t chan, void *buf, size_t len, uint32_t *response)
{
	uint32_t *words = buf;
	uint32_t tag = le32toh(words[2]), value;

	requests++;
	assert(chan == 8 && len == 32 && ((uintptr_t)buf & 15) == 0);
	assert(le32toh(words[0]) == 32 && words[1] == 0);
	assert(le32toh(words[3]) == 8 && words[4] == 0);
	assert(le32toh(words[5]) == 5 && words[6] == 0 && words[7] == 0);
	assert(tag == VCPROPTAG_GET_CLOCKSTATE || tag == VCPROPTAG_GET_CLOCKRATE);
	assert(requests <= 2);
	assert(tag == (requests == 1 ? VCPROPTAG_GET_CLOCKSTATE :
	    VCPROPTAG_GET_CLOCKRATE));
	/* A successful result must be reported before the next request. */
	if (requests > 1)
		assert(strstr(output, "clock5 state=") != NULL);
	if (mailbox_error != 0 && requests == mailbox_error_call)
		return mailbox_error;
	value = tag == VCPROPTAG_GET_CLOCKSTATE ? clock_state : rate;
	words[1] = htole32(VCPROP_REQ_SUCCESS);
	words[4] = htole32(VCPROPTAG_RESPONSE | 8);
	words[6] = htole32(value);
	if (malformed_call == requests)
		words[malformed_word] = htole32(malformed_value);
	*response = 0x10000;
	return 0;
}

static int
bus_space_map(bus_space_tag_t tag, bus_addr_t addr, bus_size_t len,
    int flags, bus_space_handle_t *handle)
{
	unsigned int i;

	assert(requests == 2 && clock_state == 1 && rate != 0);
	assert((pm_value & BCMV3D_RESET_N) != 0);
	maps++;
	if (maps == fail_map)
		return ENOMEM;
	for (i = 0; i < 3; i++)
		if (bcmv3d_addresses[i] == addr)
			break;
	assert(i < 3 && bcmv3d_lengths[i] == len && flags == 0);
	if (i != BCMV3D_SMS)
		assert(gpu_reads == 4);
	assert(!active[i]);
	active[i] = true;
	*handle = i;
	return 0;
}

static int
bus_space_peek_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset, uint32_t *value)
{
	reads++;
	if (handle == 100) {
		assert(offset == 0x304);
		if (pm_fault)
			return EFAULT;
		*value = pm_value;
		return 0;
	}
	assert(handle < 3 && active[handle]);
	gpu_reads++;
	if (gpu_reads == fail_read)
		return EFAULT;
	if (handle == BCMV3D_SMS) {
		assert(gpu_reads <= 4 && offset == ((gpu_reads - 1) % 2) * 0x400);
		*value = sms_words[gpu_reads - 1];
	} else if (handle == BCMV3D_CORE) {
		assert(offset == 0);
		*value = 0x07003356;
	} else {
		assert(offset == BCMV3D_HUB_IDENT1 || offset == BCMV3D_HUB_IDENT3 ||
		    offset == BCMV3D_MMU_DEBUG);
		*value = offset == BCMV3D_HUB_IDENT1 ? ident1 :
		    offset == BCMV3D_HUB_IDENT3 ? 0x600 : 0x261;
	}
	return 0;
}

static void
bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t handle, bus_size_t len)
{
	assert(handle < 3 && active[handle] && len == bcmv3d_lengths[handle]);
	active[handle] = false;
	unmaps++;
}

static int
acpi_eval_integer(void *handle, const char *name, ACPI_INTEGER *value)
{
	if ((fail_eval == 1 && strcmp(name, "_STA") == 0) ||
	    (fail_eval == 2 && strcmp(name, "_CCA") == 0))
		return EIO;
	assert(strcmp(name, "_STA") == 0 || strcmp(name, "_CCA") == 0);
	*value = strcmp(name, "_STA") == 0 ? sta : cca;
	return 0;
}

static int
acpi_resource_parse(device_t device, void *handle, const char *name,
    struct acpi_resources *res, void *ops)
{
	assert(strcmp(name, "_CRS") == 0);
	res->parsed = !fail_parse;
	return fail_parse ? EIO : 0;
}

static struct acpi_mem *
acpi_res_mem(struct acpi_resources *res, unsigned int index)
{
	assert(res->parsed);
	return index < resource_count ? &resources[index] : NULL;
}

static void
acpi_resource_cleanup(struct acpi_resources *res)
{
	assert(res->parsed);
	res->parsed = 0;
	cleanups++;
}

static int
acpi_match_hid(ACPI_DEVICE_INFO *device_info, const char * const *ids)
{
	return strcmp(device_info->cid, ids[0]) == 0;
}

static int
config_finalize_register(device_t device, int (*fn)(device_t))
{
	assert(requests == 0 && maps == 0 && reads == 0 && finalizer == NULL);
	if (registration_error)
		return ENOMEM;
	finalizer = fn;
	return 0;
}

static void
reset(void)
{
	assert(!active[0] && !active[1] && !active[2]);
	memset(&sc, 0, sizeof(sc));
	sc.sc_dev = &dev;
	sc.sc_bst = 1;
	sc.sc_dmat = aa.aa_dmat;
	sc.sc_handle = node.ad_handle;
	memset(probe_calls, 0, sizeof(probe_calls));
	memset(probe_order, 0, sizeof(probe_order));
	memset(probe_errors, 0, sizeof(probe_errors));
	probe_count = 0;
	requests = maps = unmaps = reads = gpu_reads = 0;
	fail_map = fail_read = malformed_word = malformed_call = 0;
	malformed_value = 0;
	cleanups = registration_error = fail_eval = fail_parse = pm_fault = 0;
	mailbox_error = 0;
	mailbox_error_call = 1;
	output_used = 0;
	output[0] = '\0';
	clock_state = 1;
	rate = 800000000;
	pm_value = BCMV3D_RESET_N;
	memset(sms_words, 0, sizeof(sms_words));
	ident1 = 0x117;
	sta = 0x0f;
	cca = 0;
	resource_count = 3;
	for (unsigned int i = 0; i < 3; i++) {
		resources[i].ar_base = bcmv3d_addresses[i];
		resources[i].ar_length = bcmv3d_lengths[i];
	}
	bcmpmwdog_v3d_sc = &pm;
	finalizer = NULL;
	cases++;
}

static int
denied(void)
{
	int error = bcmv3d_observe(&sc);

	assert(error != 0);
	assert(maps == 0 && gpu_reads == 0 && unmaps == 0);
	return error;
}

#ifdef BCM2712_V3D_TAKEOVER
static void
finalize_contract(void)
{
	static const enum probe_stage sequence[] = {
		PROBE_TAKEOVER,
#ifdef BCM2712_V3D_DMA_PROBE
		PROBE_DMA,
#endif
#ifdef BCM2712_V3D_QUEUE_PROBE
		PROBE_QUEUE,
#endif
#ifdef BCM2712_V3D_IRQ_PROBE
		PROBE_IRQ,
#endif
#ifdef BCM2712_V3D_FAULT_PROBE
		PROBE_FAULT,
#endif
	};
	static const int failures[] = { EIO, ETIMEDOUT, EFAULT };
	const unsigned int count = sizeof(sequence) / sizeof(sequence[0]);
	unsigned int i, j, f;

	/* Observation failures must never reach even the takeover. */
	for (i = 0; i < 3; i++) {
		reset();
		if (i == 0)
			mailbox_error = ETIMEDOUT;
		else if (i == 1)
			fail_map = 1;
		else
			ident1 = 0xffffffff;
		assert(bcmv3d_finalize(&dev) == 0 && probe_count == 0);
		assert(bcmv3d_finalize(&dev) == 0 && probe_count == 0);
	}
	reset();
	assert(bcmv3d_finalize(&dev) == 0 && probe_count == count);
	for (i = 0; i < count; i++) {
		assert(probe_order[i] == (unsigned int)sequence[i]);
		assert(probe_calls[sequence[i]] == 1);
	}
	assert(bcmv3d_finalize(&dev) == 0 && probe_count == count);

	/* An uncertain or failed stage must prevent all subsequent work. */
	for (i = 0; i < count; i++)
		for (f = 0; f < sizeof(failures) / sizeof(failures[0]); f++) {
			reset();
			probe_errors[sequence[i]] = failures[f];
			assert(bcmv3d_finalize(&dev) == 0);
			assert(probe_count == i + 1);
			for (j = 0; j < count; j++) {
				assert(probe_calls[sequence[j]] == (j <= i));
				if (j <= i)
					assert(probe_order[j] ==
					    (unsigned int)sequence[j]);
			}
			assert(bcmv3d_finalize(&dev) == 0 && probe_count == i + 1);
		}
}
#endif

int
main(void)
{
	struct bcmv3d_clock_result value;
	struct bcmv3d_clock_result *clocks[] = { &sc.sc_clock_state,
	    &sc.sc_clock_rate };
	static const uint32_t tags[] = { VCPROPTAG_GET_CLOCKSTATE,
	    VCPROPTAG_GET_CLOCKRATE };
	static const char * const stages[] = { "firmware V3D clock state",
	    "firmware V3D clock rate" };
	static const char * const names[] = { "state", "rate" };
	static const int transport_errors[] = { ETIMEDOUT, EIO, ENXIO };
	static const unsigned int words[] = { 0, 1, 2, 3, 4, 5, 7 };
	static const uint32_t bad[] = { 31, 0x80000001, 0x12345, 12,
	    0x80000004, 4, 1 };
	uint32_t reply[8];
	char expected[160];

	reset();
	assert(bcmv3d_addresses[0] == UINT64_C(0x1002000000));
	assert(bcmv3d_addresses[1] == UINT64_C(0x1002008000));
	assert(bcmv3d_addresses[2] == UINT64_C(0x1002030800));
	assert(bcmv3d_lengths[0] == 0x4000 && bcmv3d_lengths[1] == 0x6000 &&
	    bcmv3d_lengths[2] == 0x700);
	assert(bcmv3d_match(NULL, NULL, &aa));
	info.HardwareId.String = "BCM2711";
	assert(!bcmv3d_match(NULL, NULL, &aa));
	info.HardwareId.String = "BCM2712";
	info.cid = "other";
	assert(!bcmv3d_match(NULL, NULL, &aa));
	info.cid = "BCM2850";
	info.Valid = 0;
	assert(!bcmv3d_match(NULL, NULL, &aa));
	info.Valid = ACPI_VALID_HID;

	for (unsigned int call = 1; call <= 2; call++)
		for (unsigned int i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
			reset();
			malformed_call = call;
			malformed_word = words[i];
			malformed_value = bad[i];
			assert(denied() == EIO);
			assert(requests == call && reads == 1);
			assert(sc.sc_pm_valid && sc.sc_pm_error == 0);
			assert(strcmp(sc.sc_stage, stages[call - 1]) == 0);
			for (unsigned int j = 0; j < 2; j++) {
				assert(clocks[j]->status == (j < call - 1 ?
				    BCMV3D_CLOCK_VALID : j == call - 1 ?
				    BCMV3D_CLOCK_RESPONSE_ERROR :
				    BCMV3D_CLOCK_NOT_QUERIED));
				assert(clocks[j]->error == (j == call - 1 ? EIO : 0));
			}
			reply[0] = 32;
			reply[1] = VCPROP_REQ_SUCCESS;
			reply[2] = tags[call - 1];
			reply[3] = 8;
			reply[4] = VCPROPTAG_RESPONSE | 8;
			reply[5] = 5;
			reply[6] = call == 1 ? clock_state : rate;
			reply[7] = 0;
			reply[words[i]] = bad[i];
			snprintf(expected, sizeof(expected),
			    "clock5 %s: rejected reply %08x %08x %08x %08x "
			    "%08x %08x %08x %08x\n", names[call - 1],
			    reply[0], reply[1], reply[2], reply[3],
			    reply[4], reply[5], reply[6], reply[7]);
			assert(strstr(output, expected) != NULL);
			assert(strstr(output, "PM_GRAFX2712=0x40\n") != NULL);
			assert(strstr(output, "transport error") == NULL);
		}
	reset();
	assert(bcmv3d_clock(&sc, 0x38002, &value) == EINVAL && requests == 0);
	assert(value.status == BCMV3D_CLOCK_INVALID_TAG && value.error == EINVAL);
	reset();
	assert(bcmv3d_clock(&sc, VCPROPTAG_GET_CLOCK_MEASURED, &value) == EINVAL);
	assert(value.status == BCMV3D_CLOCK_INVALID_TAG && requests == 0);
	/* Clock ID zero is not accepted for either required property. */
	for (unsigned int call = 1; call <= 2; call++) {
		reset(); malformed_call = call; malformed_word = 5;
		malformed_value = 0;
		assert(denied() == EIO && requests == call && reads == 1);
		assert(clocks[call - 1]->status == BCMV3D_CLOCK_RESPONSE_ERROR);
	}
	for (unsigned int call = 1; call <= 2; call++)
		for (unsigned int i = 0; i < 3; i++) {
			reset();
			mailbox_error = transport_errors[i];
			mailbox_error_call = call;
			assert(denied() == mailbox_error);
			assert(requests == call && reads == 1 && sc.sc_pm_valid);
			assert(strcmp(sc.sc_stage, stages[call - 1]) == 0);
			for (unsigned int j = 0; j < 2; j++) {
				assert(clocks[j]->status == (j < call - 1 ?
				    BCMV3D_CLOCK_VALID : j == call - 1 ?
				    BCMV3D_CLOCK_TRANSPORT_ERROR :
				    BCMV3D_CLOCK_NOT_QUERIED));
				assert(clocks[j]->error ==
				    (j == call - 1 ? mailbox_error : 0));
			}
			snprintf(expected, sizeof(expected),
			    "clock5 %s: transport error %d\n",
			    names[call - 1], mailbox_error);
			assert(strstr(output, expected) != NULL);
			assert(strstr(output, "rejected reply") == NULL);
		}
	/* An ignored tag and an extended reply both leave the GPU gate shut. */
	for (unsigned int i = 0; i < 2; i++) {
		reset(); malformed_call = 2; malformed_word = 4;
		malformed_value = i == 0 ? 0 : VCPROPTAG_RESPONSE | 12;
		assert(denied() == EIO && requests == 2 && reads == 1);
		assert(sc.sc_clock_state.value == 1);
		assert(sc.sc_clock_rate.status == BCMV3D_CLOCK_RESPONSE_ERROR);
		assert(strstr(output, "clock5 rate=") == NULL);
	}
	/* Independent PM failure must not hide the earlier firmware error. */
	reset(); malformed_call = 2; malformed_word = 4; pm_fault = 1;
	assert(denied() == EIO && strcmp(sc.sc_stage, stages[1]) == 0);
	assert(!sc.sc_pm_valid && sc.sc_pm_error == EFAULT && reads == 1);
	assert(strstr(output, "PM_GRAFX2712: error") != NULL);
	assert(strstr(output, "PM_GRAFX2712=") == NULL);
	reset(); mailbox_error = ETIMEDOUT; bcmpmwdog_v3d_sc = NULL;
	assert(denied() == ETIMEDOUT && strcmp(sc.sc_stage, stages[0]) == 0);
	assert(!sc.sc_pm_valid && sc.sc_pm_error == ENXIO && requests == 1);
	reset(); bcmpmwdog_v3d_sc = NULL; denied(); assert(reads == 0);
	assert(!sc.sc_pm_valid && sc.sc_pm_error == ENXIO);
	assert(strcmp(sc.sc_stage, "BCM2712 PM owner") == 0);
	reset(); pm_fault = 1; assert(denied() == EFAULT);
	assert(!sc.sc_pm_valid && sc.sc_pm_error == EFAULT);
	reset(); clock_state = 0; denied();
	reset(); clock_state = 3; denied();
	reset(); clock_state = 0x101; denied();
	reset(); rate = 0; denied();
	assert(sc.sc_clock_rate.status == BCMV3D_CLOCK_VALID);
	assert(strstr(output, "clock5 rate=0 Hz\n") != NULL);
	reset(); pm_value = 0; denied();
	assert(sc.sc_pm_valid && strstr(output, "PM_GRAFX2712=0\n") != NULL);
	/* All sixteen old/new mode pairs, independently in both SMS banks. */
	for (unsigned int bank = 0; bank < 2; bank++)
		for (unsigned int old = 0; old < 4; old++)
			for (unsigned int new = 0; new < 4; new++) {
				reset();
				sms_words[bank] = sms_words[bank + 2] =
				    (old << 4) | (new << 6);
				if (old == new) {
					assert(bcmv3d_observe(&sc) == 0);
					assert(maps == 3 && unmaps == 3 && gpu_reads == 8);
				} else {
					assert(bcmv3d_observe(&sc) == EBUSY);
					assert(maps == 1 && unmaps == 1 && gpu_reads == 2);
				}
			}
	/* Every bit outside the mode fields independently denies HUB access. */
	for (unsigned int bank = 0; bank < 2; bank++)
		for (unsigned int bit = 0; bit < 32; bit++) {
			if (bit >= 4 && bit <= 7)
				continue;
			reset();
			sms_words[bank] = sms_words[bank + 2] = __BIT(bit);
			assert(bcmv3d_observe(&sc) == EBUSY);
			assert(maps == 1 && unmaps == 1 && gpu_reads == 2);
		}
	/* Even individually idle mode values must not change between reads. */
	for (unsigned int bank = 0; bank < 2; bank++)
		for (unsigned int before = 0; before < 4; before++)
			for (unsigned int after = 0; after < 4; after++) {
				if (before == after)
					continue;
				reset();
				sms_words[bank] = (before << 4) | (before << 6);
				sms_words[bank + 2] = (after << 4) | (after << 6);
				assert(bcmv3d_observe(&sc) == EBUSY);
				assert(maps == 1 && unmaps == 1);
				assert(gpu_reads == 3 + bank);
			}
	for (unsigned int i = 0; i < 4; i++) {
		reset(); sms_words[i] = 0x0d;
		assert(bcmv3d_observe(&sc) == EBUSY);
		assert(maps == 1 && unmaps == 1 && gpu_reads <= 4);
	}
	reset(); sms_words[0] = __BIT(28);
	assert(bcmv3d_observe(&sc) == EBUSY && maps == 1 && unmaps == 1);
	reset();
	for (unsigned int i = 0; i < 4; i++)
		sms_words[i] = 0x0d;
	assert(bcmv3d_observe(&sc) == EBUSY && maps == 1 && unmaps == 1);
	for (unsigned int i = 1; i <= 3; i++) {
		reset(); fail_map = i;
		assert(bcmv3d_observe(&sc) == ENOMEM);
		assert(maps == i && unmaps == i - 1);
	}
	for (unsigned int i = 1; i <= 8; i++) {
		reset(); fail_read = i;
		assert(bcmv3d_observe(&sc) == EFAULT);
		assert(maps == unmaps && gpu_reads == i);
	}
	reset(); ident1 = 0xffffffff;
	assert(bcmv3d_observe(&sc) == ENODEV && maps == unmaps);
	reset(); ident1 = 0x127;
	assert(bcmv3d_observe(&sc) == ENODEV && maps == unmaps);
	reset(); ident1 = 0x217;
	assert(bcmv3d_observe(&sc) == ENODEV && maps == unmaps);
	for (unsigned int i = 0; i < 3; i++) {
		reset(); resources[i].ar_base++;
		bcmv3d_attach(NULL, &dev, &aa);
		assert(finalizer == NULL && cleanups == 1 && maps == 0);
		reset(); resources[i].ar_length--;
		bcmv3d_attach(NULL, &dev, &aa);
		assert(finalizer == NULL && cleanups == 1 && maps == 0);
		reset(); resource_count = i;
		bcmv3d_attach(NULL, &dev, &aa);
		assert(finalizer == NULL && cleanups == 1 && maps == 0);
	}
	for (unsigned int i = 0; i < 6; i++) {
		reset();
		switch (i) {
		case 0: sta = 0; break;
		case 1: cca = 1; break;
		case 2: fail_eval = 1; break;
		case 3: fail_eval = 2; break;
		case 4: fail_parse = 1; break;
		case 5: registration_error = 1; break;
		}
		bcmv3d_attach(NULL, &dev, &aa);
		assert(finalizer == NULL && requests == 0 && maps == 0);
	}
	reset();
	bcmv3d_attach(NULL, &dev, &aa);
	assert(finalizer != NULL && cleanups == 1);
	assert(requests == 0 && reads == 0 && maps == 0);
	assert(finalizer(&dev) == 0);
	assert(sc.sc_observed && requests == 2 && maps == 3 && unmaps == 3);
	assert(gpu_reads == 8 && sc.sc_ident1 == 0x117);
	assert(strstr(output, "measured") == NULL);
	assert(finalizer(&dev) == 0 && requests == 2 && maps == 3);
	reset(); pm_value = 0x1040; rate = 500000000;
	sms_words[1] = sms_words[3] = 0x50;
	assert(bcmv3d_observe(&sc) == 0 && requests == 2);
	assert(maps == 3 && unmaps == 3 && gpu_reads == 8);
	assert(sc.sc_sms[0] == 0 && sc.sc_sms[1] == 0x50);
	reset(); mailbox_error = ETIMEDOUT;
	assert(bcmv3d_finalize(&dev) == 0);
	assert(bcmv3d_finalize(&dev) == 0 && requests == 1);
#ifdef BCM2712_V3D_TAKEOVER
	finalize_contract();
#endif
	printf("PASS: %u BCM2712 V3D observation failure/cleanup cases\n", cases);
	return 0;
}
