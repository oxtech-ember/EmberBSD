/* $NetBSD: plcom_acpi.c,v 1.4 2023/01/24 06:56:40 mlelstv Exp $ */

/*-
 * Copyright (c) 2018 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jared McNeill <jmcneill@invisible.ca>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "cixscmi_acpi.h"
#ifdef _KERNEL_OPT
#include "opt_wsdisplay_bootfb.h"
#endif
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: plcom_acpi.c,v 1.4 2023/01/24 06:56:40 mlelstv Exp $");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/device.h>
#include <sys/termios.h>
#include <sys/systm.h>
#include <sys/kernel.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpi_intr.h>
#include <dev/acpi/acpi_util.h>
#include <dev/acpi/cixscmi_acpi.h>

#include <evbarm/dev/plcomreg.h>
#include <evbarm/dev/plcomvar.h>
#ifdef WSDISPLAY_BOOTFB_HANDOFF
#include <evbarm/dev/plcom_consolevar.h>
#endif

struct plcom_acpi_softc {
	struct plcom_softc pa_sc;
	struct acpi_attach_args pa_aa;
	device_t pa_root;
	ACPI_HANDLE pa_provider;
	uint32_t pa_clock_id;
	bus_size_t pa_span;
	bool pa_once;
};

static int	plcom_acpi_match(device_t, cfdata_t, void *);
static void	plcom_acpi_attach(device_t, device_t, void *);

#ifdef WSDISPLAY_BOOTFB_HANDOFF
CFATTACH_DECL_NEW(plcom_acpi, sizeof(struct plcom_acpi_softc),
    plcom_acpi_match, plcom_acpi_attach, NULL, plcom_activate);
#else
CFATTACH_DECL_NEW(plcom_acpi, sizeof(struct plcom_acpi_softc),
    plcom_acpi_match, plcom_acpi_attach, NULL, NULL);
#endif

enum plcom_acpi_variant {
	PLCOM_ACPI_GENERIC,
	PLCOM_ACPI_PL011,
	PLCOM_ACPI_BCM2837
};

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "BCM2837",		.value = PLCOM_ACPI_BCM2837 },
	{ .compat = "ARMH0011",		.value = PLCOM_ACPI_PL011 },
	{ .compat = "ARMHB000",		.value = PLCOM_ACPI_GENERIC },
	DEVICE_COMPAT_EOL
};

/* Closed opt-in CIX profile. Generic and DEBUG UART2 paths stay synchronous. */
static bool
plcom_acpi_span(const void *base, size_t length, const void *p, size_t size)
{
	uintptr_t first = (uintptr_t)base, at = (uintptr_t)p;

	return base != NULL && p != NULL && length <= UINTPTR_MAX - first &&
	    at >= first && size <= length && at - first <= length - size;
}

static bool
plcom_acpi_identity(const struct acpi_devnode *node, const char *hid,
    const char *uid)
{
	const ACPI_DEVICE_INFO *p = node->ad_devinfo;
	size_t hs = strlen(hid) + 1, us = uid != NULL ? strlen(uid) + 1 : 0;

	if (node->ad_type != ACPI_TYPE_DEVICE || p == NULL ||
	    p->InfoSize < sizeof(*p) || p->Type != ACPI_TYPE_DEVICE ||
	    (p->Valid & ACPI_VALID_HID) == 0 || p->HardwareId.Length != hs ||
	    !plcom_acpi_span(p, p->InfoSize, p->HardwareId.String, hs) ||
	    memcmp(p->HardwareId.String, hid, hs) != 0)
		return false;
	return uid == NULL || ((p->Valid & ACPI_VALID_UID) != 0 &&
	    p->UniqueId.Length >= us &&
	    plcom_acpi_span(p, p->InfoSize, p->UniqueId.String,
	    p->UniqueId.Length) && memcmp(p->UniqueId.String, uid, us) == 0);
}

static int
plcom_acpi_provider(struct acpi_devnode *consumer, ACPI_HANDLE *out)
{
	const struct acpi_softc *acpi = device_private(consumer->ad_root);
	struct acpi_devnode *node, *provider = NULL;
	unsigned int count = 0, own = 0;

	SIMPLEQ_FOREACH(node, &acpi->sc_head, ad_list) {
		if (node->ad_handle == consumer->ad_handle)
			own++;
		if (node->ad_root == consumer->ad_root &&
		    plcom_acpi_identity(node, "CIXHA006", NULL)) {
			provider = node;
			count++;
		}
	}
	if (count == 0)
		return ENOENT;
	if (count != 1 || own != 1 ||
	    !plcom_acpi_identity(provider, "CIXHA006", "0"))
		return ENOTSUP;
	*out = provider->ad_handle;
	return 0;
}

static bool
plcom_acpi_object(const ACPI_BUFFER *b, const void *p, ACPI_OBJECT *out)
{

	if (!plcom_acpi_span(b->Pointer, b->Length, p, sizeof(*out)))
		return false;
	memcpy(out, p, sizeof(*out));
	return true;
}

static bool
plcom_acpi_element(const ACPI_BUFFER *b, const ACPI_OBJECT *p,
    unsigned int i, ACPI_OBJECT *out)
{
	uintptr_t first;

	if (p->Type != ACPI_TYPE_PACKAGE || i >= p->Package.Count ||
	    p->Package.Count > b->Length / sizeof(*out) ||
	    !plcom_acpi_span(b->Pointer, b->Length, p->Package.Elements,
	    (size_t)p->Package.Count * sizeof(*out)))
		return false;
	first = (uintptr_t)p->Package.Elements;
	return plcom_acpi_object(b, (void *)(first + i * sizeof(*out)), out);
}

static int
plcom_acpi_clock_table(struct acpi_devnode *node, uint32_t id)
{
	static const char * const names[] = { "apb_pclk", "uartclk" };
	ACPI_BUFFER buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	ACPI_OBJECT top, pair, value, name, reference;
	ACPI_HANDLE table;
	ACPI_OBJECT_TYPE type;
	unsigned int i;
	int error = ENOTSUP;

	if (ACPI_FAILURE(AcpiGetHandle(node->ad_handle, "CLKT", &table)) ||
	    ACPI_FAILURE(AcpiGetType(table, &type)))
		return EIO;
	if (type != ACPI_TYPE_PACKAGE)
		return ENOTSUP;
	if (ACPI_FAILURE(AcpiEvaluateObject(node->ad_handle, "CLKT", NULL,
	    &buffer))) {
		error = EIO;
		goto out;
	}
	if (!plcom_acpi_object(&buffer, buffer.Pointer, &top) ||
	    top.Type != ACPI_TYPE_PACKAGE || top.Package.Count != 2)
		goto out;
	for (i = 0; i < 2; i++) {
		size_t size = strlen(names[i]) + 1;

		if (!plcom_acpi_element(&buffer, &top, i, &pair) ||
		    pair.Type != ACPI_TYPE_PACKAGE || pair.Package.Count != 3 ||
		    !plcom_acpi_element(&buffer, &pair, 0, &value) ||
		    value.Type != ACPI_TYPE_INTEGER ||
		    value.Integer.Value != (i == 0 ? id - 17 : id) ||
		    !plcom_acpi_element(&buffer, &pair, 1, &name) ||
		    name.Type != ACPI_TYPE_STRING || name.String.Length != size - 1 ||
		    !plcom_acpi_span(buffer.Pointer, buffer.Length,
		    name.String.Pointer, size) ||
		    memcmp(name.String.Pointer, names[i], size) != 0 ||
		    !plcom_acpi_element(&buffer, &pair, 2, &reference) ||
		    reference.Type != ACPI_TYPE_LOCAL_REFERENCE ||
		    reference.Reference.ActualType != ACPI_TYPE_DEVICE ||
		    reference.Reference.Handle != node->ad_handle)
			goto out;
	}
	error = 0;
out:
	AcpiOsFree(buffer.Pointer);
	return error;
}

static void
plcom_acpi_cix_deferred(device_t self)
{
	struct plcom_acpi_softc *psc = device_private(self);
	struct plcom_softc *sc = &psc->pa_sc;
	ACPI_HANDLE provider;
	uint64_t rate = 0;
	void *ih;
	int error;

	if (psc->pa_once)
		return;
	psc->pa_once = true;
	if (cpu_intr_p() || cpu_softintr_p()) {
		error = EWOULDBLOCK;
		goto failed;
	}
	if (psc->pa_aa.aa_node->ad_device != self ||
	    psc->pa_aa.aa_node->ad_root != psc->pa_root) {
		error = EIO;
		goto failed;
	}
	error = plcom_acpi_provider(psc->pa_aa.aa_node, &provider);
	if (error != 0 || provider != psc->pa_provider) {
		if (error == 0)
			error = EIO;
		goto failed;
	}
#if NCIXSCMI_ACPI > 0
	error = cixscmi_acpi_uart_rate(provider, psc->pa_clock_id, &rate);
#else
	error = ENOTSUP;
#endif
	if (error != 0)
		goto failed;
	if (rate == 0 || rate > INT_MAX) {
		error = ERANGE;
		goto failed;
	}
	sc->sc_frequency = (int)rate;
	if (bus_space_map(sc->sc_pi.pi_iot, sc->sc_pi.pi_iobase,
	    psc->pa_span, 0, &sc->sc_pi.pi_ioh) != 0) {
		error = EIO;
		goto failed;
	}
	plcom_attach_subr(sc);
	ih = acpi_intr_establish(self,
	    (uint64_t)psc->pa_aa.aa_node->ad_handle, IPL_SERIAL, true,
	    plcomintr, sc, device_xname(self));
	if (ih == NULL) {
		aprint_error_dev(self, "couldn't install interrupt handler\n");
		return;
	}
#ifdef WSDISPLAY_BOOTFB_HANDOFF
	plcom_console_attach_complete(sc, ih);
#endif
	aprint_normal_dev(self, "native cached clock %u frequency %d Hz\n",
	    psc->pa_clock_id, sc->sc_frequency);
	return;
failed:
	aprint_error_dev(self, "native UART rate attach rejected: error=%d\n",
	    error);
}

static int
plcom_acpi_cix(device_t self, const struct acpi_attach_args *aa,
    const struct acpi_resources *res, const struct acpi_mem *mem,
    const struct acpi_irq *irq, bool *selected)
{
	struct plcom_acpi_softc *psc = device_private(self);
	ACPI_HANDLE provider;
	uint32_t id, number;
	bus_addr_t address;
	int error;

	*selected = false;
	if (device_cfdata(self)->cf_flags != 1 ||
	    psc->pa_sc.sc_pi.pi_type != PLCOM_TYPE_PL011 ||
	    plcom_acpi_identity(aa->aa_node, "BCM2837", NULL) ||
	    plcom_acpi_identity(aa->aa_node, "ARMH0011", "3"))
		return 0;
	error = plcom_acpi_provider(aa->aa_node, &provider);
	if (error == ENOENT)
		return 0;
	*selected = true;
	if (error != 0)
		return error;
	if (cold != 1 || cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (plcom_acpi_identity(aa->aa_node, "ARMH0011", "2")) {
		id = 264;
		address = 0x040c0000;
		number = 329;
	} else if (plcom_acpi_identity(aa->aa_node, "ARMH0011", "4")) {
		id = 266;
		address = 0x040e0000;
		number = 331;
	} else {
		return ENOTSUP;
	}
	if (res->ar_nmem != 1 || res->ar_nirq != 1 || res->ar_nmemrange != 0 ||
	    res->ar_nio != 0 || res->ar_niorange != 0 ||
	    mem->ar_base != address || mem->ar_xbase != address ||
	    mem->ar_length != 0x1000 || irq->ar_irq != number ||
	    irq->ar_type != ACPI_LEVEL_SENSITIVE)
		return ENOTSUP;
	error = plcom_acpi_clock_table(aa->aa_node, id);
	if (error != 0)
		return error;
	psc->pa_aa = *aa;
	psc->pa_root = aa->aa_node->ad_root;
	psc->pa_provider = provider;
	psc->pa_clock_id = id;
	psc->pa_span = mem->ar_length;
	config_interrupts(self, plcom_acpi_cix_deferred);
	return 0;
}

static int
plcom_acpi_match(device_t parent, cfdata_t cf, void *aux)
{
	struct acpi_attach_args *aa = aux;

	(void)parent;
	(void)cf;
	if (aa->aa_node->ad_type != ACPI_TYPE_DEVICE)
		return 0;

	return acpi_compatible_match(aa, compat_data);
}

static void
plcom_acpi_attach(device_t parent, device_t self, void *aux)
{
	struct plcom_softc * const sc = device_private(self);
	struct acpi_attach_args *aa = aux;
	struct acpi_resources res;
	struct acpi_mem *mem;
	struct acpi_irq *irq;
	ACPI_STATUS rv;
	void *ih;
	ACPI_INTEGER shared_rp1_irq;
	int ipl = IPL_SERIAL, error;
	bool selected;

	(void)parent;
	sc->sc_dev = self;

	rv = acpi_resource_parse(sc->sc_dev, aa->aa_node->ad_handle, "_CRS",
	    &res, &acpi_resource_parse_ops_default);
	if (ACPI_FAILURE(rv))
		return;

	mem = acpi_res_mem(&res, 0);
	if (mem == NULL) {
		aprint_error(": couldn't find mem resource\n");
		goto done;
	}

	irq = acpi_res_irq(&res, 0);
	if (irq == NULL) {
		aprint_error(": couldn't find irq resource\n");
		goto done;
	}

#ifdef WSDISPLAY_BOOTFB_HANDOFF
	if (!plcom_console_attach_begin(self, aa->aa_memt, mem->ar_base))
		goto done;
#endif
	sc->sc_hwflags = 0;
	sc->sc_swflags = 0;
	sc->sc_fifolen = 0;
	sc->sc_pi.pi_type = PLCOM_TYPE_PL011;
	sc->sc_pi.pi_flags = PLC_FLAG_32BIT_ACCESS;

	switch (acpi_compatible_lookup(aa, compat_data)->value) {
	case PLCOM_ACPI_BCM2837:
		sc->sc_fifolen = 16;
		break;
	case PLCOM_ACPI_GENERIC:
		sc->sc_pi.pi_type = PLCOM_TYPE_GENERIC_UART;
		break;
	}

	sc->sc_pi.pi_iot = aa->aa_memt;
	sc->sc_pi.pi_iobase = mem->ar_base;
	error = plcom_acpi_cix(self, aa, &res, mem, irq, &selected);
	if (selected) {
		if (error != 0)
			aprint_error_dev(self, "native UART metadata rejected: error=%d\n",
			    error);
		goto done;
	}
	if (bus_space_map(aa->aa_memt, mem->ar_base, mem->ar_length, 0, &sc->sc_pi.pi_ioh) != 0) {
		aprint_error(": couldn't map registers\n");
		goto done;
	}

	plcom_attach_subr(sc);
#ifdef WSDISPLAY_BOOTFB_HANDOFF
	/* Common attach can refuse or leave incomplete console/tty storage. */
	if ((sc->sc_hwflags & PLCOM_HW_DEV_OK) == 0 || sc->sc_si == NULL)
		goto done;
#endif

	/* RP1 shares PCIe INTA with USB and Ethernet at IPL_VM. */
	if (ACPI_SUCCESS(acpi_dsd_integer(aa->aa_node->ad_handle,
	    "raspberrypi,shared-rp1-irq", &shared_rp1_irq)) &&
	    shared_rp1_irq == 1) {
		/* plcomintr protects UART registers with its spin mutex. */
		ipl = IPL_VM;
		aprint_verbose_dev(self, "shared RP1 interrupt at IPL_VM\n");
	}

	ih = acpi_intr_establish(self, (uint64_t)aa->aa_node->ad_handle,
	    ipl, true, plcomintr, sc, device_xname(self));
	if (ih == NULL) {
		aprint_error_dev(self, "couldn't install interrupt handler\n");
		return;
	}

#ifdef WSDISPLAY_BOOTFB_HANDOFF
	plcom_console_attach_complete(sc, ih);
#endif
done:
	acpi_resource_cleanup(&res);
}
