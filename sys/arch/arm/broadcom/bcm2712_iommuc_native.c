/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Descriptor attachment only. No native MMIO backend exists here. */
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

struct bcm2712_iommuc_provider {
	device_t dev;
	ACPI_HANDLE handle;
	bus_space_tag_t bst;
	struct bcm2712_iommuc_contract contract;
	struct bcm2712_iommuc_core core;
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
bcm2712_iommuc_enter(void)
{
	int error;

	if ((error = bcm2712_iommuc_native_context()) != 0)
		return error;
	error = RUN_ONCE(&bcm2712_iommuc_once, bcm2712_iommuc_once_init);
	if (error != 0)
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
	if ((error = bcm2712_iommuc_native_context()) != 0)
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

static int
bcm2712iommuc_acpi_detach(device_t self, int flags)
{

	(void)self;
	(void)flags;
	/* The one built-in owner and its serialization have boot lifetime. */
	return EBUSY;
}
