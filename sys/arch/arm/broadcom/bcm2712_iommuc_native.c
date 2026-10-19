/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Optional ICAC owner, explicit read and private synchronous composition. */
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/systm.h>

#include <dev/acpi/acpivar.h>
#include <arm/broadcom/bcm2712_iommuc.h>
#include <arm/broadcom/bcm2712_iommuc_native.h>
#include <arm/broadcom/bcm2712_iommuc_executor.h>

struct bcm2712_iommuc_provider {
	device_t dev;
	ACPI_HANDLE handle;
	bus_space_tag_t bst;
	struct bcm2712_iommuc_contract contract;
	struct bcm2712_iommuc_core core;
	struct bcm2712_iommuc_observation observation;
	struct bcm2712_iommuc_execution mmu4;
	bool executing;
	bus_space_handle_t bsh;
	bool software_attached;
};

static ONCE_DECL(bcm2712_iommuc_once);
static kmutex_t bcm2712_iommuc_mutex;
static struct bcm2712_iommuc_provider *bcm2712_iommuc_owner;

static int bcm2712iommuc_acpi_match(device_t, cfdata_t, void *);
static void bcm2712iommuc_acpi_attach(device_t, device_t, void *);
static int bcm2712iommuc_acpi_detach(device_t, int);

CFATTACH_DECL_NEW(bcm2712iommuc_acpi,
    sizeof(struct bcm2712_iommuc_provider), bcm2712iommuc_acpi_match,
    bcm2712iommuc_acpi_attach, bcm2712iommuc_acpi_detach, NULL);

static int
bcm2712_iommuc_once_init(void)
{

	mutex_init(&bcm2712_iommuc_mutex, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

static int
bcm2712_iommuc_native_context(void)
{

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	return 0;
}

static int
bcm2712_iommuc_entry(void)
{
	int error;

	if ((error = bcm2712_iommuc_native_context()) != 0)
		return error;
	error = RUN_ONCE(&bcm2712_iommuc_once, bcm2712_iommuc_once_init);
	if (error != 0)
		return error;
	if (mutex_owned(&bcm2712_iommuc_mutex))
		return EDEADLK;
	return 0;
}

static int
bcm2712_iommuc_enter(void)
{
	int error;

	if ((error = bcm2712_iommuc_entry()) != 0)
		return error;
	mutex_enter(&bcm2712_iommuc_mutex);
	return 0;
}

static int
bcm2712iommuc_acpi_match(device_t parent, cfdata_t match, void *aux)
{
	struct acpi_attach_args *aa = aux;
	struct bcm2712_iommuc_contract contract;

	(void)parent;
	(void)match;
	if (aa == NULL || aa->aa_node == NULL || aa->aa_memt == NULL ||
	    aa->aa_node->ad_type != ACPI_TYPE_DEVICE)
		return 0;
	return bcm2712_iommuc_acpi_resources(aa->aa_node->ad_handle,
	    &contract) == 0;
}

static void
bcm2712iommuc_acpi_attach(device_t parent, device_t self, void *aux)
{
	struct acpi_attach_args *aa = aux;
	struct bcm2712_iommuc_provider *sc = device_private(self);
	struct bcm2712_iommuc_contract contract;
	int error;

	(void)parent;
	if (aa == NULL || aa->aa_node == NULL || aa->aa_memt == NULL ||
	    aa->aa_node->ad_type != ACPI_TYPE_DEVICE || sc == NULL)
		return;
	error = bcm2712_iommuc_acpi_resources(aa->aa_node->ad_handle,
	    &contract);
	if (error == 0)
		error = bcm2712_iommuc_enter();
	if (error != 0) {
		aprint_error_dev(self, "descriptor attachment failed: %d\n", error);
		return;
	}
	if (bcm2712_iommuc_owner != NULL) {
		mutex_exit(&bcm2712_iommuc_mutex);
		aprint_error_dev(self, "shared cache already has an owner\n");
		return;
	}
	memset(sc, 0, sizeof(*sc));
	sc->dev = self;
	sc->handle = aa->aa_node->ad_handle;
	sc->bst = aa->aa_memt;
	sc->contract = contract;
	/* Do not install fake callbacks or initialize an inaccessible core. */
	sc->software_attached = true;
	bcm2712_iommuc_owner = sc;
	mutex_exit(&bcm2712_iommuc_mutex);
	aprint_normal_dev(self, "descriptor attached; core uninitialized, no MMIO\n");
}

int
bcm2712_iommuc_lookup(ACPI_HANDLE handle,
    struct bcm2712_iommuc_provider **out)
{
	struct bcm2712_iommuc_contract contract;
	struct acpi_devnode *node;
	struct bcm2712_iommuc_provider *sc;
	int error;

	if (handle == NULL || out == NULL)
		return EINVAL;
	if ((error = bcm2712_iommuc_entry()) != 0)
		return error;
	error = bcm2712_iommuc_acpi_resources(handle, &contract);
	if (error != 0)
		return error;
	if ((error = bcm2712_iommuc_enter()) != 0)
		return error;
	error = ENXIO;
	node = acpi_match_node(handle);
	if (node == NULL || node->ad_type != ACPI_TYPE_DEVICE ||
	    node->ad_handle != handle || node->ad_device == NULL ||
	    !device_is_a(node->ad_device, "bcm2712iommuc"))
		goto done;
	sc = device_private(node->ad_device);
	if (sc == NULL || sc != bcm2712_iommuc_owner ||
	    !sc->software_attached || sc->dev != node->ad_device ||
	    sc->handle != handle || sc->bst == NULL ||
	    sc->contract.version != contract.version ||
	    sc->contract.address != contract.address ||
	    sc->contract.length != contract.length)
		goto done;
	*out = sc;
	error = 0;
done:
	mutex_exit(&bcm2712_iommuc_mutex);
	return error;
}

int
bcm2712_iommuc_cached_status(struct bcm2712_iommuc_provider *sc,
    struct bcm2712_iommuc_native_status *out)
{
	struct bcm2712_iommuc_native_status status;
	int error;

	if (sc == NULL || out == NULL)
		return EINVAL;
	if ((error = bcm2712_iommuc_enter()) != 0)
		return error;
	error = ENXIO;
	if (sc != bcm2712_iommuc_owner || !sc->software_attached)
		goto done;
	memset(&status, 0, sizeof(status));
	status.software_attached = true;
	status.core_initialized = sc->core.initialized;
	status.observation = sc->observation;
	status.mmu4 = sc->mmu4;
	if (status.core_initialized) {
		error = bcm2712_iommuc_core_status(&sc->core, &status.core);
		if (error != 0)
			goto done;
	}
	*out = status;
	error = 0;
done:
	mutex_exit(&bcm2712_iommuc_mutex);
	return error;
}

/* The fixed v1 CONTROL fields are ENABLE0, FLUSH1 and FLUSHING2. */
#define IOMMUC_CONTROL_FLUSHING	0x4U
#define IOMMUC_CONTROL_KNOWN	0x7U

int
bcm2712_iommuc_observe_control(struct bcm2712_iommuc_provider *sc)
{
	struct bcm2712_iommuc_observation *observation;
	uint32_t value;
	int error;

	if (sc == NULL)
		return EINVAL;
	if ((error = bcm2712_iommuc_enter()) != 0)
		return error;
	error = ENXIO;
	if (sc != bcm2712_iommuc_owner || !sc->software_attached ||
	    sc->bst == NULL || sc->contract.version != BCM2712_IOMMUC_VERSION ||
	    sc->contract.address != BCM2712_IOMMUC_ADDRESS ||
	    sc->contract.length != BCM2712_IOMMUC_LENGTH)
		goto done;
	observation = &sc->observation;
	if (observation->started) {
		error = observation->first_error != 0 ?
		    observation->first_error : EALREADY;
		goto done;
	}
	/* Do not introduce a new observation into a future active core. */
	if (sc->core.initialized) {
		error = EBUSY;
		goto done;
	}
	observation->started = true;
	observation->stage = BCM2712_IOMMUC_OBSERVE_MAP;
	error = bus_space_map(sc->bst, BCM2712_IOMMUC_ADDRESS,
	    BCM2712_IOMMUC_LENGTH, 0, &sc->bsh);
	if (error != 0) {
		observation->first_error = error;
		goto done;
	}
	observation->mapped = true;
	observation->stage = BCM2712_IOMMUC_OBSERVE_READ;
	observation->reads_attempted = 1;
	aprint_normal_dev(sc->dev, "ICAC one-shot CONTROL read: experimental "
	    "normal-active SoC assumption; firmware/MMU2/4/5 exclusion required; "
	    "bus stall/abort is not recoverable\n");
	value = bus_space_read_4(sc->bst, sc->bsh, 0);
	observation->reads_completed = 1;
	observation->raw_control = value;
	observation->raw_valid = true;
	if ((value & ~IOMMUC_CONTROL_KNOWN) != 0)
		error = EOPNOTSUPP;
	else if ((value & IOMMUC_CONTROL_FLUSHING) != 0)
		error = EBUSY;
	else
		error = 0;
	if (error != 0)
		observation->first_error = error;
	else
		observation->stage = BCM2712_IOMMUC_OBSERVE_COMPLETE;
done:
	mutex_exit(&bcm2712_iommuc_mutex);
	return error;
}

/* The initial private executor owns only logical member MMU4 (bit1). */
#define IOMMUC_MMU4_MEMBER	0x2U

static int bcm2712_iommuc_read_control(void *, uint32_t *);
static int bcm2712_iommuc_write_control(void *, uint32_t);
static void bcm2712_iommuc_control_barrier(void *);

static bool
bcm2712_iommuc_callbacks(const struct bcm2712_iommuc_provider *sc)
{

	return sc->core.initialized &&
	    sc->core.io.read_control == bcm2712_iommuc_read_control &&
	    sc->core.io.write_control == bcm2712_iommuc_write_control &&
	    sc->core.io.barrier == bcm2712_iommuc_control_barrier &&
	    sc->core.io.arg == sc;
}

static int
bcm2712_iommuc_native_fault(struct bcm2712_iommuc_provider *sc, int error)
{

	if (sc != NULL && mutex_owned(&bcm2712_iommuc_mutex) &&
	    sc == bcm2712_iommuc_owner && sc->software_attached &&
	    sc->mmu4.fault == 0)
		sc->mmu4.fault = error;
	return error;
}

static int
bcm2712_iommuc_backend(struct bcm2712_iommuc_provider *sc)
{

	if (sc == NULL)
		return EINVAL;
	if (!mutex_owned(&bcm2712_iommuc_mutex))
		return EDEADLK;
	if (sc != bcm2712_iommuc_owner || !sc->software_attached)
		return ENXIO;
	if (sc->mmu4.fault != 0)
		return EIO;
	if (sc->core.status.fault != 0) {
		(void)bcm2712_iommuc_native_fault(sc, sc->core.status.fault);
		return EIO;
	}
	if (!sc->executing || !sc->mmu4.attempted ||
	    !sc->observation.mapped || !bcm2712_iommuc_callbacks(sc))
		return bcm2712_iommuc_native_fault(sc, EINVAL);
	return 0;
}

static int
bcm2712_iommuc_read_control(void *arg, uint32_t *out)
{
	struct bcm2712_iommuc_provider *sc = arg;
	int error;

	if ((error = bcm2712_iommuc_backend(sc)) != 0)
		return error;
	if (out == NULL)
		return bcm2712_iommuc_native_fault(sc, EINVAL);
	*out = bus_space_read_4(sc->bst, sc->bsh, 0);
	return 0;
}

static int
bcm2712_iommuc_write_control(void *arg, uint32_t value)
{
	struct bcm2712_iommuc_provider *sc = arg;
	int error;

	if ((error = bcm2712_iommuc_backend(sc)) != 0)
		return error;
	if (value != 3U)
		return bcm2712_iommuc_native_fault(sc, EINVAL);
	bus_space_write_4(sc->bst, sc->bsh, 0, value);
	return 0;
}

static void
bcm2712_iommuc_control_barrier(void *arg)
{
	struct bcm2712_iommuc_provider *sc = arg;
	int error;

	error = bcm2712_iommuc_backend(sc);
	if (error != 0) {
		(void)bcm2712_iommuc_native_fault(sc, error);
		return;
	}
	bus_space_barrier(sc->bst, sc->bsh, 0, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

int
bcm2712_iommuc_execute_mmu4(struct bcm2712_iommuc_provider *sc,
    bcm2712_iommuc_mmu4_operation operation, void *arg)
{
	struct bcm2712_iommuc_status before, after;
	const struct bcm2712_iommuc_io io = {
		.read_control = bcm2712_iommuc_read_control,
		.write_control = bcm2712_iommuc_write_control,
		.barrier = bcm2712_iommuc_control_barrier,
		.arg = sc
	};
	int error, fault = 0;

	if (sc == NULL || operation == NULL)
		return EINVAL;
	if ((error = bcm2712_iommuc_enter()) != 0)
		return error;
	error = ENXIO;
	if (sc != bcm2712_iommuc_owner || !sc->software_attached ||
	    sc->bst == NULL || sc->contract.version != BCM2712_IOMMUC_VERSION ||
	    sc->contract.address != BCM2712_IOMMUC_ADDRESS ||
	    sc->contract.length != BCM2712_IOMMUC_LENGTH)
		goto done;
	/* The cached result precedes gates affected by our previous attempt. */
	if (sc->mmu4.attempted) {
		error = sc->mmu4.first_error != 0 ?
		    sc->mmu4.first_error : EALREADY;
		goto done;
	}
	error = EBUSY;
	if (!sc->observation.started || !sc->observation.mapped ||
	    !sc->observation.raw_valid || sc->observation.first_error != 0 ||
	    sc->observation.reads_attempted != 1 ||
	    sc->observation.reads_completed != 1 ||
	    sc->observation.stage != BCM2712_IOMMUC_OBSERVE_COMPLETE)
		goto done;
	error = EIO;
	if (sc->mmu4.fault != 0)
		goto done;
	if (sc->core.initialized) {
		if (!bcm2712_iommuc_callbacks(sc) || sc->core.status.fault != 0)
			goto done;
		error = EBUSY;
		if ((sc->core.status.reserved & IOMMUC_MMU4_MEMBER) != 0 ||
		    sc->core.status.stage == BCM2712_IOMMUC_CLOSED)
			goto done;
	}
	/* Consume the provider attempt before init, callback and operational IO. */
	sc->mmu4.attempted = true;
	if (!sc->core.initialized) {
		error = bcm2712_iommuc_core_init(&sc->core, &io);
		if (error != 0)
			goto result;
	}
	error = bcm2712_iommuc_core_status(&sc->core, &before);
	if (error != 0) {
		fault = error;
		goto result;
	}
	sc->executing = true;
	/* Trusted synchronous kernel composition, not a callback whitelist. */
	error = operation(arg, &sc->core);
	if (error < 0) {
		error = EINVAL;
		fault = EINVAL;
	}
	if (!bcm2712_iommuc_callbacks(sc) ||
	    sc->core.status.stage == BCM2712_IOMMUC_CLOSED) {
		fault = EPROTO;
		goto result;
	}
	if (bcm2712_iommuc_core_status(&sc->core, &after) != 0) {
		fault = EPROTO;
		goto result;
	}
	if ((before.reserved & ~IOMMUC_MMU4_MEMBER) !=
	    (after.reserved & ~IOMMUC_MMU4_MEMBER) ||
	    (before.retained & ~IOMMUC_MMU4_MEMBER) !=
	    (after.retained & ~IOMMUC_MMU4_MEMBER) ||
	    (after.retained & ~after.reserved) != 0)
		fault = EPROTO;
	if (after.fault != 0 && fault == 0)
		fault = after.fault;
	if (error != 0 && ((after.reserved | after.retained) &
	    IOMMUC_MMU4_MEMBER) != 0 && fault == 0)
		fault = error;
result:
	sc->executing = false;
	if (fault != 0 && sc->mmu4.fault == 0)
		sc->mmu4.fault = fault;
	if (error == 0 && sc->mmu4.fault != 0)
		error = sc->mmu4.fault;
	sc->mmu4.first_error = error;
done:
	mutex_exit(&bcm2712_iommuc_mutex);
	return error;
}

static int
bcm2712iommuc_acpi_detach(device_t self, int flags)
{

	(void)self;
	(void)flags;
	/* The one built-in owner and its serialization have boot lifetime. */
	return EBUSY;
}
