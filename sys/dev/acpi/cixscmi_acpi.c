/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original checked CIX ACPI SCMI boot diagnostic, AI-assisted. */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/intr.h>
#include <sys/kernel.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/timevar.h>

#include <arm/cpufunc.h>
#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/cixscmi_acpi.h>
#include <dev/ic/scmi_smt_diag.h>

/* Fixed boot diagnostic records. No exported service or retry API. */
struct cixscmi_repeat_result {
	int helper_return;
	int decode_error;
	int version_policy_error;
	int ack_error;
	bool query_called;
	bool version_valid;
	uint32_t version;
	uint32_t ack_before;
	uint32_t ack_after;
	bool ack_after_valid;
	struct scmi_smt_diag_trace trace;
};

/* Fixed mixed read-only batch. Values remain distinct from local policy. */
struct cixscmi_batch_result {
	bool query_called;
	bool word_valid;
	bool value_valid;
	bool ack_after_valid;
	bool id67_in_range;
	int helper_return;
	int decode_error;
	int policy_error;
	int ack_error;
	uint32_t raw_low;
	uint32_t raw_high;
	uint32_t num_clocks;
	uint32_t max_async;
	uint32_t ack_before;
	uint32_t ack_after;
	uint64_t rate_hz;
	struct scmi_smt_diag_trace trace;
};

struct cixscmi_acpi_softc;
struct cixscmi_clock67 {
	struct cixscmi_acpi_softc *provider;
};

/* Boot-long mappings. Only selector128 can publish the fixed clock service. */
struct cixscmi_acpi_softc {
	device_t sc_dev;
	struct acpi_devnode *sc_node;
	struct acpi_devnode *sc_refs[4];
	bus_space_tag_t sc_memt;
	bus_space_handle_t sc_io[2];
	bool sc_mapped[2];
	kmutex_t sc_lock;
	bool sc_once;
	bool sc_claimed;
	bool sc_query_started;
	bool sc_version_valid;
	uint32_t sc_version;
	unsigned int sc_selector;
	int sc_decode_error;
	bool sc_reviewed_version;
	int sc_version_policy_error;
	bool sc_attr_word_valid;
	uint32_t sc_attr;
	bool sc_attributes_valid;
	uint32_t sc_num_clocks;
	uint32_t sc_max_async;
	bool sc_id67_in_range;
	bool sc_entity_raw_valid;
	uint32_t sc_entity_attr;
	uint32_t sc_entity_name[4];
	uint32_t sc_entity_delay;
	bool sc_entity_valid;
	bool sc_rate_valid;
	uint64_t sc_rate_hz;
	uint32_t sc_ack_before;
	uint32_t sc_ack_after;
	bool sc_ack_after_valid;
	int sc_error;
	const char *sc_stage;
	const char *sc_role;
	struct scmi_smt_diag sc_wire;
	struct scmi_smt_diag_trace sc_trace;
	struct cixscmi_repeat_result sc_repeat[3];
	unsigned int sc_repeat_count;
	unsigned int sc_repeat_queries;
	bool sc_repeat_qualified;
	struct cixscmi_batch_result sc_batch[5];
	unsigned int sc_batch_count;
	unsigned int sc_batch_queries;
	bool sc_batch_qualified;
	struct cixscmi_clock67 sc_clock67;
	bool sc_service_fault;
	bool sc_wire_poisoned;
	bool sc_gate_reserved;
	unsigned int sc_remaining;
};

/* cold boot autoconfiguration serializes publication and the four aliases. */
static struct cixscmi_acpi_softc *cixscmi_lease;
static struct cixscmi_acpi_softc *cixscmi_service;
static struct cixscmi_acpi_softc *cixscmi_uart_cache;
static const char * const cixscmi_roles[] = {
	"parent", "MBX6", "MBX7", "SHM0", "SHM1"
};
static const struct device_compatible_entry cixscmi_compat[] = {
	{ .compat = "CIXHA006" }, DEVICE_COMPAT_EOL
};
static const struct {
	const char *hid;
	const char *uid;
	uint32_t address;
	uint32_t length;
	uint32_t irq;
} cixscmi_profiles[] = {
	{ "CIXHA001", "6", 0x06590000, 0x10000, 395 },
	{ "CIXHA001", "7", 0x065a0000, 0x10000, 391 },
	{ "CIXHA004", "0", 0x06590000, 0x80, 0 },
	{ "CIXHA005", "0", 0x065a0000, 0x80, 0 }
};

static bool
cixscmi_span(const void *base, size_t length, const void *p, size_t size)
{
	uintptr_t first = (uintptr_t)base, at = (uintptr_t)p;

	return base != NULL && p != NULL && length <= UINTPTR_MAX - first &&
	    at >= first && size <= length &&
	    at - first <= length - size;
}

static bool
cixscmi_object(const ACPI_BUFFER *b, const void *p, ACPI_OBJECT *out)
{
	if (!cixscmi_span(b->Pointer, b->Length, p, sizeof(*out)))
		return false;
	memcpy(out, p, sizeof(*out));
	return true;
}

static bool
cixscmi_element(const ACPI_BUFFER *b, const ACPI_OBJECT *package,
    unsigned int index, ACPI_OBJECT *out)
{
	uintptr_t elements;

	if (package->Type != ACPI_TYPE_PACKAGE ||
	    index >= package->Package.Count ||
	    package->Package.Count > b->Length / sizeof(*out) ||
	    !cixscmi_span(b->Pointer, b->Length, package->Package.Elements,
	    (size_t)package->Package.Count * sizeof(*out)))
		return false;
	elements = (uintptr_t)package->Package.Elements;
	return cixscmi_object(b,
	    (const void *)(elements + (size_t)index * sizeof(*out)), out);
}

static bool
cixscmi_string(const ACPI_BUFFER *b, const ACPI_OBJECT *o, const char *s)
{
	size_t size = strlen(s);

	return o->Type == ACPI_TYPE_STRING && o->String.Length == size &&
	    cixscmi_span(b->Pointer, b->Length, o->String.Pointer, size + 1) &&
	    memcmp(o->String.Pointer, s, size + 1) == 0;
}

/* Each reviewed role has exactly its listed properties, in either order. */
static int
cixscmi_properties(struct acpi_devnode *node, unsigned int role,
    ACPI_BUFFER *b, ACPI_OBJECT values[2])
{
	static const uint8_t uuid[16] = {
		0x14, 0xd8, 0xff, 0xda, 0xba, 0x6e, 0x8c, 0x4d,
		0x8a, 0x91, 0xbc, 0x9b, 0xbf, 0x4a, 0xa3, 0x01
	};
	const char *names[2];
	ACPI_OBJECT top, id, properties, pair, key;
	unsigned int count, seen = 0, i, j;

	if (role == 0) {
		names[0] = "mboxes";
		names[1] = "shmem";
	} else if (role <= 2) {
		names[0] = "cix,mbox_dir";
		names[1] = "#mbox-cells";
	} else {
		names[0] = "compatible";
		names[1] = NULL;
	}
	count = role <= 2 ? 2 : 1;
	if (ACPI_FAILURE(AcpiEvaluateObject(node->ad_handle, "_DSD", NULL, b)))
		return EIO;
	if (!cixscmi_object(b, b->Pointer, &top) ||
	    top.Type != ACPI_TYPE_PACKAGE || top.Package.Count != 2 ||
	    !cixscmi_element(b, &top, 0, &id) ||
	    id.Type != ACPI_TYPE_BUFFER || id.Buffer.Length != sizeof(uuid) ||
	    !cixscmi_span(b->Pointer, b->Length, id.Buffer.Pointer, sizeof(uuid)) ||
	    memcmp(id.Buffer.Pointer, uuid, sizeof(uuid)) != 0 ||
	    !cixscmi_element(b, &top, 1, &properties) ||
	    properties.Type != ACPI_TYPE_PACKAGE ||
	    properties.Package.Count != count)
		return ENOTSUP;
	for (i = 0; i < count; i++) {
		if (!cixscmi_element(b, &properties, i, &pair) ||
		    pair.Type != ACPI_TYPE_PACKAGE || pair.Package.Count != 2 ||
		    !cixscmi_element(b, &pair, 0, &key))
			return ENOTSUP;
		for (j = 0; j < count; j++)
			if (cixscmi_string(b, &key, names[j]))
				break;
		if (j == count || (seen & (1U << j)) != 0 ||
		    !cixscmi_element(b, &pair, 1, &values[j]))
			return ENOTSUP;
		seen |= 1U << j;
	}
	return 0;
}

/* Never give an unchecked Reference.Handle to an ACPICA handle API. */
static struct acpi_devnode *
cixscmi_resolve(struct cixscmi_acpi_softc *sc, const ACPI_OBJECT *o)
{
	const struct acpi_softc *acpi;
	struct acpi_devnode *node;

	if (o->Type != ACPI_TYPE_LOCAL_REFERENCE ||
	    o->Reference.ActualType != ACPI_TYPE_DEVICE ||
	    o->Reference.Handle == NULL || sc->sc_node->ad_root == NULL)
		return NULL;
	acpi = device_private(sc->sc_node->ad_root);
	if (acpi == NULL)
		return NULL;
	SIMPLEQ_FOREACH(node, &acpi->sc_head, ad_list) {
		if (node->ad_handle == o->Reference.Handle &&
		    node->ad_type == ACPI_TYPE_DEVICE &&
		    node->ad_root == sc->sc_node->ad_root)
			return node;
	}
	return NULL;
}

static bool
cixscmi_identity(struct acpi_devnode *node, const char *hid, const char *uid)
{
	ACPI_DEVICE_INFO *info = node->ad_devinfo;
	size_t hsize = strlen(hid) + 1, usize = strlen(uid) + 1;

	/* ACPICA integer UID length is buffer capacity, not decimal text length. */
	return node->ad_type == ACPI_TYPE_DEVICE && info != NULL &&
	    info->InfoSize >= sizeof(*info) && info->Type == ACPI_TYPE_DEVICE &&
	    (info->Valid & (ACPI_VALID_HID | ACPI_VALID_UID)) ==
	    (ACPI_VALID_HID | ACPI_VALID_UID) &&
	    info->HardwareId.Length == hsize && info->UniqueId.Length >= usize &&
	    cixscmi_span(info, info->InfoSize, info->HardwareId.String, hsize) &&
	    cixscmi_span(info, info->InfoSize, info->UniqueId.String,
	    info->UniqueId.Length) &&
	    memcmp(info->HardwareId.String, hid, hsize) == 0 &&
	    memcmp(info->UniqueId.String, uid, usize) == 0;
}

static bool
cixscmi_integer(const ACPI_OBJECT *o, uint64_t value)
{
	return o->Type == ACPI_TYPE_INTEGER && o->Integer.Value == value;
}

static int
cixscmi_references(struct cixscmi_acpi_softc *sc)
{
	ACPI_BUFFER b = { ACPI_ALLOCATE_BUFFER, NULL };
	ACPI_OBJECT values[2], o;
	unsigned int i, j;
	int error;

	sc->sc_stage = "properties";
	error = cixscmi_properties(sc->sc_node, 0, &b, values);
	if (error != 0)
		goto out;
	sc->sc_stage = "references";
	if (values[0].Type != ACPI_TYPE_PACKAGE ||
	    values[0].Package.Count != 4 || values[1].Type != ACPI_TYPE_PACKAGE ||
	    values[1].Package.Count != 2) {
		error = ENOTSUP;
		goto out;
	}
	for (i = 0; i < 4; i++) {
		if (!cixscmi_element(&b, &values[i / 2],
		    i < 2 ? i * 2 : i - 2, &o) ||
		    (sc->sc_refs[i] = cixscmi_resolve(sc, &o)) == NULL ||
		    sc->sc_refs[i] == sc->sc_node) {
			error = ENOTSUP;
			goto out;
		}
		for (j = 0; j < i; j++) {
			if (sc->sc_refs[i] == sc->sc_refs[j]) {
				error = ENOTSUP;
				goto out;
			}
		}
		if (i < 2 && (!cixscmi_element(&b, &values[0], i * 2 + 1, &o) ||
		    !cixscmi_integer(&o, 8))) {
			error = ENOTSUP;
			goto out;
		}
	}
 out:
	if (b.Pointer != NULL)
		AcpiOsFree(b.Pointer);
	return error;
}

/* Parse only bounded decoded descriptors; memcpy permits unaligned buffers. */
static int
cixscmi_resources(struct acpi_devnode *node, unsigned int role)
{
	ACPI_BUFFER b = { ACPI_ALLOCATE_BUFFER, NULL };
	ACPI_RESOURCE_FIXED_MEMORY32 mem;
	ACPI_RESOURCE_EXTENDED_IRQ irq;
	const uint8_t *p;
	uint32_t header[2];
	size_t offset = 0, size;
	unsigned int seen = 0, required = role < 2 ? 3 : 1;
	int error = ENOTSUP;

	if (ACPI_FAILURE(AcpiGetCurrentResources(node->ad_handle, &b))) {
		error = EIO;
		goto out;
	}
	if (!cixscmi_span(b.Pointer, b.Length, b.Pointer, b.Length))
		goto out;
	while (offset <= b.Length) {
		if (b.Pointer == NULL || b.Length - offset < sizeof(header))
			break;
		p = (const uint8_t *)b.Pointer + offset;
		memcpy(header, p, sizeof(header));
		if (header[1] < sizeof(header) || header[1] > b.Length - offset)
			break;
		if (header[0] == ACPI_RESOURCE_TYPE_FIXED_MEMORY32) {
			size = sizeof(header) + sizeof(mem);
			if ((seen & 1) != 0 || header[1] < size ||
			    header[1] > ((size + 7) & ~(size_t)7))
				break;
			memcpy(&mem, p + sizeof(header), sizeof(mem));
			if (mem.WriteProtect != ACPI_READ_WRITE_MEMORY ||
			    mem.Address != cixscmi_profiles[role].address ||
			    mem.AddressLength != cixscmi_profiles[role].length)
				break;
			seen |= 1;
		} else if (header[0] == ACPI_RESOURCE_TYPE_EXTENDED_IRQ) {
			size = sizeof(header) + sizeof(irq);
			if (role >= 2 || (seen & 2) != 0 || header[1] < size ||
			    header[1] > ((size + 7) & ~(size_t)7))
				break;
			memcpy(&irq, p + sizeof(header), sizeof(irq));
			if (irq.ProducerConsumer != ACPI_CONSUMER ||
			    irq.Triggering != ACPI_LEVEL_SENSITIVE ||
			    irq.Polarity != ACPI_ACTIVE_HIGH ||
			    irq.Shareable != ACPI_EXCLUSIVE || irq.WakeCapable != 0 ||
			    irq.InterruptCount != 1 ||
			    irq.ResourceSource.StringLength != 0 ||
			    irq.ResourceSource.StringPtr != NULL ||
			    irq.Interrupts[0] != cixscmi_profiles[role].irq)
				break;
			seen |= 2;
		} else if (header[0] == ACPI_RESOURCE_TYPE_END_TAG) {
			size = sizeof(header) + sizeof(ACPI_RESOURCE_END_TAG);
			if (seen == required && header[1] >= size &&
			    header[1] <= ((size + 7) & ~(size_t)7) &&
			    offset + header[1] == b.Length)
				error = 0;
			break;
		} else {
			break;
		}
		offset += header[1];
	}
 out:
	if (b.Pointer != NULL)
		AcpiOsFree(b.Pointer);
	return error;
}

static int
cixscmi_metadata(struct cixscmi_acpi_softc *sc)
{
	ACPI_BUFFER b;
	ACPI_OBJECT values[2];
	ACPI_INTEGER value;
	unsigned int i;
	int error;

	sc->sc_stage = "identity";
	sc->sc_role = cixscmi_roles[0];
	if (!cixscmi_identity(sc->sc_node, "CIXHA006", "0"))
		return ENOTSUP;
	sc->sc_stage = "sta";
	if (ACPI_FAILURE(acpi_eval_integer(sc->sc_node->ad_handle, "_STA", &value)) ||
	    (value & 0xb) != 0xb)
		return ENOTSUP;
	error = cixscmi_references(sc);
	if (error != 0)
		return error;
	for (i = 0; i < 4; i++) {
		sc->sc_stage = "identity";
		sc->sc_role = cixscmi_roles[i + 1];
		if (!cixscmi_identity(sc->sc_refs[i], cixscmi_profiles[i].hid,
		    cixscmi_profiles[i].uid))
			return ENOTSUP;
		sc->sc_stage = "sta";
		if (ACPI_FAILURE(acpi_eval_integer(
		    sc->sc_refs[i]->ad_handle, "_STA", &value)) ||
		    (value & 0xb) != 0xb)
			return ENOTSUP;
		sc->sc_stage = "cca";
		if (i >= 2 && (ACPI_FAILURE(acpi_eval_integer(
		    sc->sc_refs[i]->ad_handle, "_CCA", &value)) || value != 0))
			return ENOTSUP;
		b = (ACPI_BUFFER){ ACPI_ALLOCATE_BUFFER, NULL };
		sc->sc_stage = "properties";
		error = cixscmi_properties(sc->sc_refs[i], i + 1, &b, values);
		if (error == 0) {
			if (i < 2) {
				if (!cixscmi_integer(&values[0], i) ||
				    !cixscmi_integer(&values[1], 1))
					error = ENOTSUP;
			} else if (!cixscmi_string(&b, &values[0], "arm,scmi-shmem")) {
				error = ENOTSUP;
			}
		}
		if (b.Pointer != NULL)
			AcpiOsFree(b.Pointer);
		if (error == 0) {
			sc->sc_stage = "resources";
			error = cixscmi_resources(sc->sc_refs[i], i);
		}
		if (error != 0)
			return error;
	}
	/* Exact profiles prove SHM containment and disjoint TX/RX/PMMX spans. */
	return 0;
}

static void
cixscmi_context(struct cixscmi_acpi_softc *sc)
{
	KASSERT(mutex_owned(&sc->sc_lock));
	KASSERT(!cpu_intr_p() && !cpu_softintr_p());
}

static uint32_t
cixscmi_read(void *cookie, uint32_t offset)
{
	struct cixscmi_acpi_softc *sc = cookie;

	cixscmi_context(sc);
	KASSERT(offset <= 0x7c && (offset & 3) == 0);
	return bus_space_read_4(sc->sc_memt, sc->sc_io[0], offset);
}

static void
cixscmi_write(void *cookie, uint32_t offset, uint32_t value)
{
	struct cixscmi_acpi_softc *sc = cookie;

	cixscmi_context(sc);
	KASSERT((offset <= 0x7c || offset == 0x80) && (offset & 3) == 0);
	bus_space_write_4(sc->sc_memt, sc->sc_io[0], offset, value);
}

static void
cixscmi_barrier(void *cookie, enum scmi_smt_diag_barrier phase)
{
	struct cixscmi_acpi_softc *sc = cookie;

	cixscmi_context(sc);
	KASSERT(phase >= SCMI_SMT_DIAG_PUBLISH && phase <= SCMI_SMT_DIAG_RECEIVE);
	bus_space_barrier(sc->sc_memt, sc->sc_io[0], 0, 0x84,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	dsb(sy);
}

static uint64_t
cixscmi_now(void *cookie)
{
	struct cixscmi_acpi_softc *sc = cookie;
	struct timeval now;

	cixscmi_context(sc);
	getmicrouptime(&now);
	return (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_usec;
}

static void
cixscmi_pause(void *cookie)
{
	struct cixscmi_acpi_softc *sc = cookie;

	cixscmi_context(sc);
	delay(10);
}

static void
cixscmi_release(struct cixscmi_acpi_softc *sc)
{
	unsigned int i;

	KASSERT(!sc->sc_query_started);
	for (i = 0; i < 2; i++) {
		if (sc->sc_mapped[i]) {
			bus_space_unmap(sc->sc_memt, sc->sc_io[i],
			    cixscmi_profiles[i].length);
			sc->sc_mapped[i] = false;
		}
	}
	if (sc->sc_claimed) {
		for (i = 0; i < 4; i++)
			if (sc->sc_refs[i]->ad_device == sc->sc_dev)
				sc->sc_refs[i]->ad_device = NULL;
		sc->sc_claimed = false;
	}
	if (cixscmi_lease == sc)
		cixscmi_lease = NULL;
}

/* Reuse the initialized wire and its tokens. Stop before any fourth request. */
static void
cixscmi_repeat_base(struct cixscmi_acpi_softc *sc)
{
	unsigned int i;

	for (i = 0; i < __arraycount(sc->sc_repeat); i++) {
		struct cixscmi_repeat_result *r = &sc->sc_repeat[i];
		struct scmi_smt_diag_reply reply;

		r->ack_before = i == 0 ? sc->sc_ack_before :
		    bus_space_read_4(sc->sc_memt, sc->sc_io[0], 0xc8);
		sc->sc_repeat_count++;
		if ((r->ack_before & 2) != 0) {
			r->ack_error = EBUSY;
			sc->sc_error = EBUSY;
			break;
		}
		r->query_called = true;
		sc->sc_repeat_queries++;
		r->helper_return = scmi_smt_diag_query(&sc->sc_wire,
		    SCMI_SMT_DIAG_BASE_VERSION, 0, &reply, &r->trace);
		/* Match the single-query rule: no callback after a poisoned reply. */
		if (r->trace.stage == SCMI_SMT_DIAG_COMPLETE ||
		    r->trace.stage == SCMI_SMT_DIAG_FIRMWARE) {
			r->ack_after = bus_space_read_4(sc->sc_memt, sc->sc_io[0], 0xc8);
			r->ack_after_valid = true;
			if ((r->ack_after & 2) != 0)
				r->ack_error = EBUSY;
		}
		if (r->helper_return == 0) {
			if (r->trace.stage != SCMI_SMT_DIAG_COMPLETE ||
			    reply.data_length != 4) {
				r->decode_error = EPROTO;
			} else {
				r->version = (uint32_t)reply.data[0] |
				    (uint32_t)reply.data[1] << 8 |
				    (uint32_t)reply.data[2] << 16 |
				    (uint32_t)reply.data[3] << 24;
				r->version_valid = true;
				/* Require version2.0 for this batch only. */
				if (r->version != 0x20000)
					r->version_policy_error = ENOTSUP;
			}
		}
		sc->sc_trace = r->trace;
		sc->sc_error = r->helper_return != 0 ? r->helper_return :
		    r->decode_error != 0 ? r->decode_error :
		    r->version_policy_error != 0 ? r->version_policy_error : r->ack_error;
		if (sc->sc_error != 0)
			break;
	}
	/* Keep UART logging outside the consecutive request sequence. */
	for (i = 0; i < sc->sc_repeat_count; i++) {
		const struct cixscmi_repeat_result *r = &sc->sc_repeat[i];

		aprint_normal_dev(sc->sc_dev, "BASE_REPEAT index=%u query_called=%u helper_return=%d "
		    "decode_error=%d version_valid=%u version=%#x version_policy_error=%d ack_error=%d "
		    "stage=%u request_header=%#x response_header=%#x length=%u status_valid=%u status=%d "
		    "tx_status_before=%#x tx_status_after_valid=%u tx_status_after=%#x\n",
		    i + 1, r->query_called, r->helper_return, r->decode_error, r->version_valid,
		    r->version, r->version_policy_error, r->ack_error,
		    (unsigned int)r->trace.stage, r->trace.request_header,
		    r->trace.response_header, r->trace.response_length,
		    r->trace.firmware_status_valid, r->trace.firmware_status,
		    r->ack_before, r->ack_after_valid, r->ack_after);
	}
	sc->sc_repeat_qualified = sc->sc_repeat_queries == __arraycount(sc->sc_repeat) &&
	    sc->sc_error == 0;
	aprint_normal_dev(sc->sc_dev, "BASE_REPEAT attempted=%u qualified=%u records=%u error=%d; "
	    "boot-long lease retained\n", sc->sc_repeat_queries,
	    sc->sc_repeat_qualified, sc->sc_repeat_count, sc->sc_error);
}

/* One fixed sequence. There are no setters or extended-name queries. */
static void
cixscmi_mixed_batch(struct cixscmi_acpi_softc *sc)
{
	static const struct {
		enum scmi_smt_diag_cmd command;
		uint32_t argument;
		const char *name;
		size_t length;
	} requests[] = {
		{ SCMI_SMT_DIAG_BASE_VERSION, 0, "BASE_VERSION", 4 },
		{ SCMI_SMT_DIAG_CLOCK_VERSION, 0, "CLOCK_VERSION", 4 },
		{ SCMI_SMT_DIAG_CLOCK_ATTRIBUTES, 0, "CLOCK_PROTOCOL_ATTRIBUTES", 4 },
		{ SCMI_SMT_DIAG_CLOCK_MESSAGE_ATTRIBUTES, 7,
		    "CLOCK_MESSAGE_ATTRIBUTES7", 4 },
		{ SCMI_SMT_DIAG_CLOCK_RATE_GET, 67, "CLOCK_RATE_GET67", 8 }
	};
	unsigned int i;

	for (i = 0; i < __arraycount(requests); i++) {
		struct cixscmi_batch_result *r = &sc->sc_batch[i];
		struct scmi_smt_diag_reply reply;

		r->ack_before = i == 0 ? sc->sc_ack_before :
		    bus_space_read_4(sc->sc_memt, sc->sc_io[0], 0xc8);
		sc->sc_batch_count++;
		if ((r->ack_before & 2) != 0) {
			r->ack_error = EBUSY;
			sc->sc_error = EBUSY;
			break;
		}
		r->query_called = true;
		sc->sc_batch_queries++;
		r->helper_return = scmi_smt_diag_query(&sc->sc_wire,
		    requests[i].command, requests[i].argument, &reply, &r->trace);
		/* A poisoned attempt permits no further I/O, including status reads. */
		if (r->trace.stage == SCMI_SMT_DIAG_COMPLETE ||
		    r->trace.stage == SCMI_SMT_DIAG_FIRMWARE) {
			r->ack_after = bus_space_read_4(sc->sc_memt, sc->sc_io[0], 0xc8);
			r->ack_after_valid = true;
			if ((r->ack_after & 2) != 0)
				r->ack_error = EBUSY;
		}
		if (r->helper_return == 0) {
			if (r->trace.stage != SCMI_SMT_DIAG_COMPLETE ||
			    reply.data_length != requests[i].length) {
				r->decode_error = EPROTO;
			} else {
				r->raw_low = (uint32_t)reply.data[0] |
				    (uint32_t)reply.data[1] << 8 |
				    (uint32_t)reply.data[2] << 16 |
				    (uint32_t)reply.data[3] << 24;
				if (i == 4)
					r->raw_high = (uint32_t)reply.data[4] |
					    (uint32_t)reply.data[5] << 8 |
					    (uint32_t)reply.data[6] << 16 |
					    (uint32_t)reply.data[7] << 24;
				r->word_valid = true;
				if (i < 2) {
					r->value_valid = true;
					/* Exact2.0 is this batch's local review policy. */
					if (r->raw_low != 0x20000)
						r->policy_error = ENOTSUP;
				} else if (i == 2) {
					if ((r->raw_low & 0xff000000) != 0) {
						r->decode_error = EPROTO;
					} else {
						r->value_valid = true;
						r->num_clocks = r->raw_low & 0xffff;
						r->max_async = (r->raw_low >> 16) & 0xff;
						r->id67_in_range = r->num_clocks > 67;
						if (!r->id67_in_range)
							r->policy_error = ENOTSUP;
					}
				} else if (i == 3) {
					if (r->raw_low != 0)
						r->decode_error = EPROTO;
					else
						r->value_valid = true;
				} else {
					r->rate_hz = (uint64_t)r->raw_low |
					    (uint64_t)r->raw_high << 32;
					r->value_valid = true;
				}
			}
		}
		sc->sc_trace = r->trace;
		sc->sc_error = r->helper_return != 0 ? r->helper_return :
		    r->decode_error != 0 ? r->decode_error :
		    r->policy_error != 0 ? r->policy_error : r->ack_error;
		if (sc->sc_error != 0)
			break;
	}
	/* Do not insert UART logging between consecutive transactions. */
	for (i = 0; i < sc->sc_batch_count; i++) {
		const struct cixscmi_batch_result *r = &sc->sc_batch[i];

		aprint_normal_dev(sc->sc_dev, "CLOCK_BATCH index=%u command=%s "
		    "query_called=%u helper_return=%d decode_error=%d policy_error=%d ack_error=%d "
		    "word_valid=%u value_valid=%u raw_low=%#x raw_high=%#x "
		    "num_clocks=%u max_async=%u id67_in_range=%u rate_hz=%" PRIu64 " "
		    "stage=%u request_header=%#x response_header=%#x length=%u "
		    "status_valid=%u status=%d tx_status_before=%#x "
		    "tx_status_after_valid=%u tx_status_after=%#x\n",
		    i + 1, requests[i].name, r->query_called, r->helper_return,
		    r->decode_error, r->policy_error, r->ack_error,
		    r->word_valid, r->value_valid, r->raw_low, r->raw_high,
		    r->num_clocks, r->max_async, r->id67_in_range, r->rate_hz,
		    (unsigned int)r->trace.stage, r->trace.request_header,
		    r->trace.response_header, r->trace.response_length,
		    r->trace.firmware_status_valid, r->trace.firmware_status,
		    r->ack_before, r->ack_after_valid, r->ack_after);
	}
	sc->sc_batch_qualified = sc->sc_batch_queries == __arraycount(requests) &&
	    sc->sc_error == 0;
	aprint_normal_dev(sc->sc_dev,
	    "CLOCK_BATCH attempted=%u qualified=%u records=%u error=%d; "
	    "read-only batch; boot-long lease retained\n", sc->sc_batch_queries,
	    sc->sc_batch_qualified, sc->sc_batch_count, sc->sc_error);
}

/* Fixed UART functional-rate observations. No UART or clock control writes. */
static void
cixscmi_uart_rates(struct cixscmi_acpi_softc *sc)
{
	static const uint32_t ids[] = { 264, 266 };
	unsigned int i;

	for (i = 0; i < __arraycount(ids); i++) {
		struct cixscmi_batch_result *r = &sc->sc_batch[i];
		struct scmi_smt_diag_reply reply;

		r->ack_before = i == 0 ? sc->sc_ack_before :
		    bus_space_read_4(sc->sc_memt, sc->sc_io[0], 0xc8);
		sc->sc_batch_count++;
		if ((r->ack_before & 2) != 0) {
			r->ack_error = EBUSY;
			sc->sc_error = EBUSY;
			break;
		}
		r->query_called = true;
		sc->sc_batch_queries++;
		r->helper_return = scmi_smt_diag_query(&sc->sc_wire,
		    SCMI_SMT_DIAG_CLOCK_RATE_GET, ids[i], &reply, &r->trace);
		/* Poison forbids every later callback, including ACK reads. */
		if (r->trace.stage == SCMI_SMT_DIAG_COMPLETE ||
		    r->trace.stage == SCMI_SMT_DIAG_FIRMWARE) {
			r->ack_after = bus_space_read_4(sc->sc_memt, sc->sc_io[0], 0xc8);
			r->ack_after_valid = true;
			if ((r->ack_after & 2) != 0)
				r->ack_error = EBUSY;
		}
		if (r->helper_return == 0) {
			if (r->trace.stage != SCMI_SMT_DIAG_COMPLETE ||
			    reply.data_length != 8) {
				r->decode_error = EPROTO;
			} else {
				r->raw_low = (uint32_t)reply.data[0] |
				    (uint32_t)reply.data[1] << 8 |
				    (uint32_t)reply.data[2] << 16 |
				    (uint32_t)reply.data[3] << 24;
				r->raw_high = (uint32_t)reply.data[4] |
				    (uint32_t)reply.data[5] << 8 |
				    (uint32_t)reply.data[6] << 16 |
				    (uint32_t)reply.data[7] << 24;
				r->rate_hz = (uint64_t)r->raw_low |
				    (uint64_t)r->raw_high << 32;
				r->word_valid = true;
				r->value_valid = true;
			}
		}
		sc->sc_trace = r->trace;
		sc->sc_error = r->helper_return != 0 ? r->helper_return :
		    r->decode_error != 0 ? r->decode_error : r->ack_error;
		if (sc->sc_error != 0)
			break;
	}
	/* Log only after the consecutive transactions have ended. */
	for (i = 0; i < sc->sc_batch_count; i++) {
		const struct cixscmi_batch_result *r = &sc->sc_batch[i];

		aprint_normal_dev(sc->sc_dev,
		    "UART_RATE index=%u clock_id=%u rate_valid=%u rate_hz=%" PRIu64 " "
		    "raw_low=%#x raw_high=%#x query_called=%u helper_return=%d "
		    "decode_error=%d ack_error=%d selected_schema=0x20000 "
		    "version_queried=0 stage=%u request_header=%#x "
		    "response_header=%#x length=%u status_valid=%u status=%d "
		    "tx_status_before=%#x tx_status_after_valid=%u tx_status_after=%#x\n",
		    i + 1, ids[i], r->value_valid, r->rate_hz, r->raw_low,
		    r->raw_high, r->query_called, r->helper_return, r->decode_error,
		    r->ack_error, (unsigned int)r->trace.stage, r->trace.request_header,
		    r->trace.response_header, r->trace.response_length,
		    r->trace.firmware_status_valid, r->trace.firmware_status,
		    r->ack_before, r->ack_after_valid, r->ack_after);
	}
	sc->sc_batch_qualified = sc->sc_batch_queries == __arraycount(ids) &&
	    sc->sc_error == 0;
	aprint_normal_dev(sc->sc_dev,
	    "UART_RATE attempted=%u qualified=%u records=%u error=%d; "
	    "read-only observations; boot-long lease retained\n",
	    sc->sc_batch_queries, sc->sc_batch_qualified,
	    sc->sc_batch_count, sc->sc_error);
}

int
cixscmi_acpi_uart_rate(ACPI_HANDLE provider, uint32_t id, uint64_t *out)
{
	struct cixscmi_acpi_softc *sc = cixscmi_uart_cache;
	unsigned int i, index;
	int error = EIO;

	if (provider == NULL || out == NULL)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (id != 264 && id != 266)
		return ENOTSUP;
	if (sc == NULL || sc->sc_node->ad_handle != provider)
		return ENXIO;
	index = id == 264 ? 0 : 1;
	mutex_enter(&sc->sc_lock);
	if (sc->sc_selector != 256 || !sc->sc_batch_qualified ||
	    sc->sc_error != 0 || cixscmi_lease != sc || !sc->sc_claimed ||
	    !sc->sc_mapped[0] || !sc->sc_mapped[1] ||
	    sc->sc_node->ad_device != sc->sc_dev)
		goto out;
	for (i = 0; i < 4; i++)
		if (sc->sc_refs[i]->ad_device != sc->sc_dev)
			goto out;
	if (!sc->sc_batch[index].value_valid)
		goto out;
	*out = sc->sc_batch[index].rate_hz;
	error = 0;
out:
	mutex_exit(&sc->sc_lock);
	return error;
}

static bool
cixscmi_service_live(struct cixscmi_acpi_softc *sc)
{
	unsigned int i;

	if (sc->sc_selector != 128 || !sc->sc_batch_qualified ||
	    cixscmi_lease != sc || !sc->sc_claimed ||
	    !sc->sc_mapped[0] || !sc->sc_mapped[1] ||
	    sc->sc_node->ad_device != sc->sc_dev)
		return false;
	for (i = 0; i < 4; i++)
		if (sc->sc_refs[i]->ad_device != sc->sc_dev)
			return false;
	return true;
}

int
cixscmi_acpi_clock67_get(ACPI_HANDLE provider, ACPI_HANDLE logical,
    struct cixscmi_clock67 **out)
{
	struct cixscmi_acpi_softc *sc = cixscmi_service;
	struct acpi_devnode *node, *found = NULL;
	const struct acpi_softc *acpi;
	int error = ENOTSUP;

	if (provider == NULL || logical == NULL || out == NULL)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (sc == NULL || sc->sc_node->ad_handle != provider)
		return ENXIO;
	mutex_enter(&sc->sc_lock);
	if (!cixscmi_service_live(sc)) {
		sc->sc_service_fault = true;
		error = EIO;
	} else if (sc->sc_service_fault || sc->sc_wire_poisoned) {
		error = EIO;
	} else {
		acpi = device_private(sc->sc_node->ad_root);
		SIMPLEQ_FOREACH(node, &acpi->sc_head, ad_list) {
			if (node->ad_handle != logical)
				continue;
			if (found != NULL)
				goto out;
			found = node;
		}
		if (found != NULL && found->ad_root == sc->sc_node->ad_root &&
		    cixscmi_identity(found, "CIXHA010", "0")) {
			*out = &sc->sc_clock67;
			error = 0;
		}
	}
out:
	mutex_exit(&sc->sc_lock);
	return error;
}

static int
cixscmi_clock67_operation(struct cixscmi_clock67 *service, bool rate,
    uint32_t enable, uint64_t *value, struct cixscmi_clock67_result *result)
{
	struct cixscmi_acpi_softc *sc = cixscmi_service;
	struct cixscmi_clock67_result r = { 0 };
	struct scmi_smt_diag_reply reply;
	uint64_t decoded = 0;
	unsigned int i;
	int error;

	if (service == NULL || result == NULL || (rate && value == NULL) ||
	    enable > 1)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (sc == NULL || service != &sc->sc_clock67)
		return EINVAL;
	mutex_enter(&sc->sc_lock);
	if (!cixscmi_service_live(sc))
		sc->sc_service_fault = true;
	if (sc->sc_service_fault || sc->sc_wire_poisoned) {
		error = EIO;
		goto out;
	}
	/* Protect a disable call without depending on opaque wire state. */
	if (sc->sc_remaining == 0 ||
	    (((!rate && enable != 0) || (rate && sc->sc_gate_reserved)) &&
	    sc->sc_remaining < 2)) {
		error = EOVERFLOW;
		goto out;
	}
	r.ack_before = bus_space_read_4(sc->sc_memt, sc->sc_io[0], 0xc8);
	if ((r.ack_before & 2) != 0) {
		r.ack_error = EBUSY;
		sc->sc_service_fault = true;
		error = EBUSY;
		goto out;
	}
	sc->sc_remaining--;
	r.helper_called = true;
	if (rate) {
		r.helper_return = scmi_smt_diag_query(&sc->sc_wire,
		    SCMI_SMT_DIAG_CLOCK_RATE_GET, 67, &reply, &r.trace);
	} else {
		sc->sc_gate_reserved = true;
		r.helper_return = scmi_smt_clock67_config_set(&sc->sc_wire,
		    enable, &r.trace);
	}
	r.trace_valid = r.trace.request_header != 0;
	if (r.trace_valid && (r.trace.stage == SCMI_SMT_DIAG_COMPLETE ||
	    r.trace.stage == SCMI_SMT_DIAG_FIRMWARE)) {
		r.ack_after = bus_space_read_4(sc->sc_memt, sc->sc_io[0], 0xc8);
		r.ack_after_valid = true;
		if ((r.ack_after & 2) != 0) {
			r.ack_error = EBUSY;
			sc->sc_service_fault = true;
		}
	} else if (!r.trace_valid) {
		sc->sc_service_fault = true;
	} else if (!(r.helper_return == EBUSY &&
	    r.trace.stage == SCMI_SMT_DIAG_WAIT_FREE)) {
		sc->sc_wire_poisoned = true;
	}
	if (rate && r.helper_return == 0) {
		if (!r.trace_valid || r.trace.stage != SCMI_SMT_DIAG_COMPLETE ||
		    reply.data_length != 8) {
			r.decode_error = EPROTO;
		} else {
			for (i = 0; i < 8; i++)
				decoded |= (uint64_t)reply.data[i] << (i * 8);
		}
	}
	error = r.helper_return != 0 ? r.helper_return :
	    r.decode_error != 0 ? r.decode_error : r.ack_error;
	if (error == 0) {
		if (rate)
			*value = decoded;
		else
			sc->sc_gate_reserved = enable != 0;
	}
out:
	r.remaining = sc->sc_remaining;
	r.wire_poisoned = sc->sc_wire_poisoned;
	r.service_usable = !sc->sc_service_fault && !sc->sc_wire_poisoned;
	*result = r;
	aprint_normal_dev(sc->sc_dev, "CLOCK67_NATIVE rate=%u enable=%u error=%d "
	    "helper_called=%u helper_return=%d trace_valid=%u stage=%u "
	    "request_header=%#x response_header=%#x response_length=%u "
	    "status_valid=%u status=%d decode_error=%d ack_error=%d "
	    "ack_before=%#x ack_after_valid=%u ack_after=%#x "
	    "service_usable=%u wire_poisoned=%u remaining_calls=%u\n",
	    rate, enable, error, r.helper_called, r.helper_return, r.trace_valid,
	    (unsigned int)r.trace.stage, r.trace.request_header,
	    r.trace.response_header, r.trace.response_length, r.trace.firmware_status_valid,
	    r.trace.firmware_status, r.decode_error, r.ack_error, r.ack_before,
	    r.ack_after_valid, r.ack_after, r.service_usable, r.wire_poisoned,
	    r.remaining);
	mutex_exit(&sc->sc_lock);
	return error;
}

int
cixscmi_acpi_clock67_get_rate(struct cixscmi_clock67 *service, uint64_t *rate,
    struct cixscmi_clock67_result *result)
{

	return cixscmi_clock67_operation(service, true, 0, rate, result);
}

int
cixscmi_acpi_clock67_config_set(struct cixscmi_clock67 *service, uint32_t enable,
    struct cixscmi_clock67_result *result)
{

	return cixscmi_clock67_operation(service, false, enable, NULL, result);
}

static void
cixscmi_acpi_deferred(device_t self)
{
	struct cixscmi_acpi_softc *sc = device_private(self);
	struct scmi_smt_diag_reply reply;
	struct scmi_smt_diag_io io = {
		.cookie = sc, .read_4 = cixscmi_read, .write_4 = cixscmi_write,
		.barrier = cixscmi_barrier, .now_us = cixscmi_now,
		.pause = cixscmi_pause
	};
	unsigned int i;

	/* Binding owns the mutex and pinned thread context; callbacks cannot reenter. */
	if (sc->sc_once)
		return;
	if (cpu_intr_p() || cpu_softintr_p()) {
		sc->sc_once = true;
		sc->sc_error = EWOULDBLOCK;
		return;
	}
	mutex_enter(&sc->sc_lock);
	if (sc->sc_once)
		goto out;
	sc->sc_once = true;
	sc->sc_error = EBUSY;
	sc->sc_stage = "ownership";
	if (cold != 1 || sc->sc_node->ad_device != self || cixscmi_lease != NULL)
		goto failed;
	sc->sc_error = cixscmi_metadata(sc);
	if (sc->sc_error != 0)
		goto failed;
	sc->sc_stage = "ownership";
	for (i = 0; i < 4; i++) {
		sc->sc_role = cixscmi_roles[i + 1];
		if (sc->sc_refs[i]->ad_device != NULL) {
			sc->sc_error = EBUSY;
			goto failed;
		}
	}
	/* Reserve only these validated references, never the root's descendants. */
	cixscmi_lease = sc;
	for (i = 0; i < 4; i++)
		sc->sc_refs[i]->ad_device = self;
	sc->sc_claimed = true;
	for (i = 0; i < 2; i++) {
		sc->sc_stage = "map";
		sc->sc_role = cixscmi_roles[i + 1];
		sc->sc_error = bus_space_map(sc->sc_memt, cixscmi_profiles[i].address,
		    cixscmi_profiles[i].length, BUS_SPACE_MAP_NONPOSTED, &sc->sc_io[i]);
		if (sc->sc_error != 0)
			goto failed;
		sc->sc_mapped[i] = true;
	}
	sc->sc_stage = "interrupt-ownership";
	sc->sc_role = cixscmi_roles[0];
	if ((bus_space_read_4(sc->sc_memt, sc->sc_io[0], 0xbc) & 2) != 0 ||
	    (bus_space_read_4(sc->sc_memt, sc->sc_io[1], 0xc0) & 1) != 0) {
		sc->sc_error = EBUSY;
		goto failed;
	}
	sc->sc_ack_before = bus_space_read_4(sc->sc_memt, sc->sc_io[0], 0xc8);
	if ((sc->sc_selector == 32 || sc->sc_selector == 64 ||
	    sc->sc_selector == 128 || sc->sc_selector == 256) &&
	    (sc->sc_ack_before & 2) != 0) {
		sc->sc_stage = "ack-ownership";
		sc->sc_error = EBUSY;
		goto failed;
	}
	sc->sc_stage = "wire-init";
	sc->sc_error = scmi_smt_diag_init(&sc->sc_wire, &io);
	if (sc->sc_error != 0)
		goto failed;
	/* A request may be published from here. Retain mappings and leases forever. */
	sc->sc_query_started = true;
	sc->sc_stage = "query";
	if (sc->sc_selector == 256) {
		cixscmi_uart_rates(sc);
		if (sc->sc_batch_qualified)
			cixscmi_uart_cache = sc;
		goto out;
	}
	if (sc->sc_selector == 64 || sc->sc_selector == 128) {
		cixscmi_mixed_batch(sc);
		if (sc->sc_selector == 128 && sc->sc_batch_qualified) {
			sc->sc_clock67.provider = sc;
			sc->sc_remaining = 1023 - 5;
			cixscmi_service = sc;
		}
		goto out;
	}
	if (sc->sc_selector == 32) {
		cixscmi_repeat_base(sc);
		goto out;
	}
	sc->sc_error = scmi_smt_diag_query(&sc->sc_wire,
	    sc->sc_selector == 16 ? SCMI_SMT_DIAG_CLOCK_RATE_GET :
	    sc->sc_selector == 8 ? SCMI_SMT_DIAG_CLOCK_ENTITY_ATTRIBUTES :
	    sc->sc_selector == 4 ? SCMI_SMT_DIAG_CLOCK_ATTRIBUTES :
	    sc->sc_selector == 2 ? SCMI_SMT_DIAG_CLOCK_VERSION :
	    SCMI_SMT_DIAG_BASE_VERSION,
	    sc->sc_selector == 8 || sc->sc_selector == 16 ? 67 : 0,
	    &reply, &sc->sc_trace);
	/* Do not issue further I/O after a possibly poisoned wire attempt. */
	if (sc->sc_trace.stage == SCMI_SMT_DIAG_COMPLETE ||
	    sc->sc_trace.stage == SCMI_SMT_DIAG_FIRMWARE) {
		sc->sc_ack_after = bus_space_read_4(sc->sc_memt, sc->sc_io[0], 0xc8);
		sc->sc_ack_after_valid = true;
	}
	if (sc->sc_error == 0 && (sc->sc_selector == 2 || sc->sc_selector == 4) &&
	    reply.data_length != 4)
		sc->sc_decode_error = EPROTO;
	if (sc->sc_error == 0 && sc->sc_selector == 8 && reply.data_length != 24)
		sc->sc_decode_error = EPROTO;
	if (sc->sc_error == 0 && sc->sc_selector == 16 && reply.data_length != 8)
		sc->sc_decode_error = EPROTO;
	if (sc->sc_error == 0 && sc->sc_decode_error == 0) {
		uint32_t word = (uint32_t)reply.data[0] |
		    (uint32_t)reply.data[1] << 8 | (uint32_t)reply.data[2] << 16 |
		    (uint32_t)reply.data[3] << 24;

		if (sc->sc_selector == 16) {
			uint32_t high = (uint32_t)reply.data[4] |
			    (uint32_t)reply.data[5] << 8 | (uint32_t)reply.data[6] << 16 |
			    (uint32_t)reply.data[7] << 24;

			sc->sc_rate_hz = (uint64_t)high << 32 | word;
			sc->sc_rate_valid = true;
		} else if (sc->sc_selector == 8) {
			bool terminated = false;

			sc->sc_entity_attr = word;
			for (i = 0; i < 4; i++)
				sc->sc_entity_name[i] = (uint32_t)reply.data[4 + i * 4] |
				    (uint32_t)reply.data[5 + i * 4] << 8 |
				    (uint32_t)reply.data[6 + i * 4] << 16 |
				    (uint32_t)reply.data[7 + i * 4] << 24;
			sc->sc_entity_delay = (uint32_t)reply.data[20] |
			    (uint32_t)reply.data[21] << 8 | (uint32_t)reply.data[22] << 16 |
			    (uint32_t)reply.data[23] << 24;
			sc->sc_entity_raw_valid = true;
			if ((word & 0x1ffffffe) != 0) {
				sc->sc_decode_error = EPROTO;
			} else {
				for (i = 0; i < 16; i++) {
					if (reply.data[4 + i] == 0) {
						terminated = true;
						break;
					}
					if (reply.data[4 + i] >= 128)
						break;
				}
				if (!terminated)
					sc->sc_decode_error = EPROTO;
				else
					sc->sc_entity_valid = true;
			}
		} else if (sc->sc_selector == 4) {
			sc->sc_attr = word;
			sc->sc_attr_word_valid = true;
			if ((word & 0xff000000) != 0) {
				sc->sc_decode_error = EPROTO;
			} else {
				sc->sc_num_clocks = word & 0xffff;
				sc->sc_max_async = (word >> 16) & 0xff;
				sc->sc_id67_in_range = sc->sc_num_clocks > 67;
				sc->sc_attributes_valid = true;
			}
		} else {
			sc->sc_version = word;
			sc->sc_version_valid = true;
		}
		if (sc->sc_selector == 2) {
			/* Local review policy; same-major minor revisions are compatible. */
			sc->sc_reviewed_version = sc->sc_version == 0x00010000 ||
			    sc->sc_version == 0x00020000;
			sc->sc_version_policy_error = sc->sc_reviewed_version ? 0 : ENOTSUP;
		}
	}
	if (sc->sc_selector == 16) {
		aprint_normal_dev(self, "CLOCK67_RATE_GET rate_valid=%u rate_hz=%" PRIu64 " "
		    "rate_low=%#x rate_high=%#x helper_return=%d decode_error=%d "
		    "selected_schema=0x20000 version_queried=0 protocolattrs_queried=0 entityattrs_queried=0 clock_id=67 "
		    "stage=%u header=%#x length=%u status_valid=%u status=%d "
		    "tx_status_before=%#x tx_status_after_valid=%u tx_status_after=%#x; boot-long lease retained\n",
		    sc->sc_rate_valid, sc->sc_rate_hz, (uint32_t)sc->sc_rate_hz,
		    (uint32_t)(sc->sc_rate_hz >> 32), sc->sc_error, sc->sc_decode_error,
		    (unsigned int)sc->sc_trace.stage, sc->sc_trace.response_header,
		    sc->sc_trace.response_length, sc->sc_trace.firmware_status_valid,
		    sc->sc_trace.firmware_status, sc->sc_ack_before,
		    sc->sc_ack_after_valid, sc->sc_ack_after);
		goto out;
	}
	if (sc->sc_selector == 8) {
		if (sc->sc_error == 0 && reply.data_length == 20) {
			uint32_t prefix[5];

			for (i = 0; i < 5; i++)
				prefix[i] = (uint32_t)reply.data[i * 4] |
				    (uint32_t)reply.data[1 + i * 4] << 8 |
				    (uint32_t)reply.data[2 + i * 4] << 16 |
				    (uint32_t)reply.data[3 + i * 4] << 24;
			aprint_normal_dev(self, "CLOCK67_REJECTED_BODY20_PREFIX "
			    "available=1 expected_body=24 actual_body=20 "
			    "prefix_attr=%#x prefix_name_le32=%08x,%08x,%08x,%08x delay_present=0; rejection retained\n",
			    prefix[0], prefix[1], prefix[2], prefix[3], prefix[4]);
		}
		aprint_normal_dev(self, "CLOCK67_ATTRIBUTES raw_valid=%u raw_attr=%#x "
		    "name_le32=%08x,%08x,%08x,%08x "
		    "raw_delay_us=%u delay_supported=%u "
		    "helper_return=%d decode_error=%d entity_valid=%u enabled=%u extended_name=%u "
		    "selected_schema=0x20000 version_queried=0 protocolattrs_queried=0 clock_id=67 "
		    "stage=%u header=%#x length=%u status_valid=%u status=%d "
		    "tx_status_before=%#x tx_status_after_valid=%u tx_status_after=%#x; boot-long lease retained\n",
		    sc->sc_entity_raw_valid, sc->sc_entity_attr,
		    sc->sc_entity_name[0], sc->sc_entity_name[1],
		    sc->sc_entity_name[2], sc->sc_entity_name[3], sc->sc_entity_delay,
		    sc->sc_entity_valid && sc->sc_entity_delay != 0,
		    sc->sc_error, sc->sc_decode_error, sc->sc_entity_valid,
		    sc->sc_entity_valid && (sc->sc_entity_attr & 1) != 0,
		    sc->sc_entity_valid && (sc->sc_entity_attr & 0x20000000) != 0,
		    (unsigned int)sc->sc_trace.stage, sc->sc_trace.response_header,
		    sc->sc_trace.response_length, sc->sc_trace.firmware_status_valid,
		    sc->sc_trace.firmware_status, sc->sc_ack_before,
		    sc->sc_ack_after_valid, sc->sc_ack_after);
		goto out;
	}
	if (sc->sc_selector == 4) {
		aprint_normal_dev(self, "CLOCK_PROTOCOL_ATTRIBUTES word_valid=%u raw=%#x "
		    "helper_return=%d decode_error=%d attributes_valid=%u "
		    "num_clocks=%u max_async=%u id67_in_range=%u "
		    "version_queried=0 selected_schema=0x20000 "
		    "stage=%u header=%#x length=%u status_valid=%u status=%d "
		    "tx_status_before=%#x tx_status_after_valid=%u tx_status_after=%#x; boot-long lease retained\n",
		    sc->sc_attr_word_valid, sc->sc_attr, sc->sc_error,
		    sc->sc_decode_error, sc->sc_attributes_valid, sc->sc_num_clocks,
		    sc->sc_max_async, sc->sc_id67_in_range,
		    (unsigned int)sc->sc_trace.stage, sc->sc_trace.response_header,
		    sc->sc_trace.response_length, sc->sc_trace.firmware_status_valid,
		    sc->sc_trace.firmware_status, sc->sc_ack_before,
		    sc->sc_ack_after_valid, sc->sc_ack_after);
		goto out;
	}
	if (sc->sc_selector == 2) {
		aprint_normal_dev(self, "CLOCK_VERSION valid=%u value=%#x "
		    "helper_return=%d decode_error=%d reviewed_version=%u version_policy_error=%d "
		    "stage=%u header=%#x length=%u status_valid=%u status=%d "
		    "tx_status_before=%#x tx_status_after_valid=%u tx_status_after=%#x; boot-long lease retained\n",
		    sc->sc_version_valid, sc->sc_version, sc->sc_error,
		    sc->sc_decode_error, sc->sc_reviewed_version, sc->sc_version_policy_error,
		    (unsigned int)sc->sc_trace.stage, sc->sc_trace.response_header,
		    sc->sc_trace.response_length, sc->sc_trace.firmware_status_valid,
		    sc->sc_trace.firmware_status, sc->sc_ack_before,
		    sc->sc_ack_after_valid, sc->sc_ack_after);
		goto out;
	}
	aprint_normal_dev(self, "BASE_VERSION valid=%u value=%#x error=%d "
	    "stage=%u header=%#x length=%u status_valid=%u status=%d "
	    "tx_status_before=%#x tx_status_after_valid=%u tx_status_after=%#x; boot-long lease retained\n",
	    sc->sc_version_valid, sc->sc_version, sc->sc_error,
	    (unsigned int)sc->sc_trace.stage, sc->sc_trace.response_header,
	    sc->sc_trace.response_length, sc->sc_trace.firmware_status_valid,
	    sc->sc_trace.firmware_status, sc->sc_ack_before,
	    sc->sc_ack_after_valid, sc->sc_ack_after);
	goto out;
 failed:
	cixscmi_release(sc);
	aprint_error_dev(self, "single-request diagnostic rejected: "
	    "stage=%s role=%s error=%d\n", sc->sc_stage, sc->sc_role,
	    sc->sc_error);
 out:
	mutex_exit(&sc->sc_lock);
}

static int
cixscmi_acpi_match(device_t parent, cfdata_t cf, void *aux)
{
	(void)parent;
	(void)cf;
	return acpi_compatible_match(aux, cixscmi_compat);
}

static void
cixscmi_acpi_attach(device_t parent, device_t self, void *aux)
{
	struct cixscmi_acpi_softc *sc = device_private(self);
	const struct acpi_attach_args *aa = aux;
	unsigned int flags = device_cfdata(self)->cf_flags;

	(void)parent;
	sc->sc_dev = self;
	sc->sc_node = aa->aa_node;
	sc->sc_memt = aa->aa_memt;
	sc->sc_selector = flags;
	sc->sc_stage = "attach";
	sc->sc_role = cixscmi_roles[0];
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	aprint_naive("\n");
	if (flags == 256)
		aprint_normal(": CIX two-request UART rate diagnostic\n");
	else if (flags == 128)
		aprint_normal(": CIX fixed native clock67 service\n");
	else if (flags == 64)
		aprint_normal(": CIX five-request read-only SCMI diagnostic\n");
	else if (flags == 32)
		aprint_normal(": CIX three-request BASE SCMI diagnostic\n");
	else
		aprint_normal(": CIX single-request SCMI diagnostic\n");
	if (flags == 1 || flags == 2 || flags == 4 || flags == 8 ||
	    flags == 16 || flags == 32 || flags == 64 || flags == 128 || flags == 256)
		config_defer(self, cixscmi_acpi_deferred);
	else if (flags != 0)
		aprint_error_dev(self, "unsupported diagnostic flags %#x\n", flags);
	/* flags0 is passive: no AML, claims, mapping or mailbox access. */
}

CFATTACH_DECL_NEW(cixscmi_acpi, sizeof(struct cixscmi_acpi_softc),
    cixscmi_acpi_match, cixscmi_acpi_attach, NULL, NULL);
