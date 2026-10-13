/* $NetBSD: fdt_powerdomain.c,v 1.2 2024/06/12 06:39:28 rin Exp $ */

/*-
 * Copyright (c) 2022 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Nick Hudson
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

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: fdt_powerdomain.c,v 1.2 2024/06/12 06:39:28 rin Exp $");

#include <sys/param.h>

#include <sys/bus.h>
#include <sys/kmem.h>
#include <sys/queue.h>

#include <libfdt.h>
#include <dev/fdt/fdtvar.h>


struct fdtbus_powerdomain_controller {
	device_t pdc_dev;
	int pdc_phandle;

	void *pdc_cookie;

	int pdc_cells;

	const struct fdtbus_powerdomain_controller_func *pdc_funcs;

	LIST_ENTRY(fdtbus_powerdomain_controller) pdc_next;
};

static LIST_HEAD(, fdtbus_powerdomain_controller) fdtbus_powerdomain_controllers =
    LIST_HEAD_INITIALIZER(fdtbus_powerdomain_controllers);

int
fdtbus_register_powerdomain_controller(device_t dev, int phandle,
    const struct fdtbus_powerdomain_controller_func *funcs)
{
	struct fdtbus_powerdomain_controller *pdc;

	uint32_t cells;
	if (of_getprop_uint32(phandle, "#power-domain-cells", &cells) != 0) {
		aprint_debug_dev(dev, "missing #power-domain-cells");
		return EINVAL;
	}
	if (cells > INT_MAX || funcs == NULL ||
	    (funcs->pdc_set == NULL && funcs->pdc_enable == NULL))
		return EINVAL;

	pdc = kmem_alloc(sizeof(*pdc), KM_SLEEP);
	pdc->pdc_dev = dev;
	pdc->pdc_phandle = phandle;
	pdc->pdc_funcs = funcs;
	pdc->pdc_cells = cells;

	LIST_INSERT_HEAD(&fdtbus_powerdomain_controllers, pdc, pdc_next);

	return 0;
}

static struct fdtbus_powerdomain_controller *
fdtbus_powerdomain_lookup(int phandle)
{
	struct fdtbus_powerdomain_controller *pdc;

	LIST_FOREACH(pdc, &fdtbus_powerdomain_controllers, pdc_next) {
		if (pdc->pdc_phandle == phandle)
			return pdc;
	}

	return NULL;
}

/* Origin: EmberBSD; checked power operations and read-only state queries. */
static int
fdtbus_powerdomain_enable_internal(int phandle, int index, bool enable,
    bool allow_unregistered)
{
	int len;
	const uint32_t *pds = fdtbus_get_prop(phandle, "power-domains", &len);

	if (pds == NULL || len <= 0 || len % sizeof(*pds) != 0)
		return EINVAL;

	for (const uint32_t *pd = pds; pd < pds + len / sizeof(*pd); index--) {
		const int pd_node =
		   fdtbus_get_phandle_from_native(be32toh(pd[0]));
		struct fdtbus_powerdomain_controller *pdc =
		    fdtbus_powerdomain_lookup(pd_node);
		int cells;

		if (pdc == NULL) {
			uint32_t ncells;

			if (!allow_unregistered)
				return ENXIO;
			/* Keep firmware-managed domains during automatic attach. */
			if (of_getprop_uint32(pd_node, "#power-domain-cells",
			    &ncells) != 0 || ncells > INT_MAX)
				return EINVAL;
			cells = ncells;
		} else {
			cells = pdc->pdc_cells;
		}

		/* Do not pass a truncated specifier to a controller. */
		if (cells < 0 || cells >= pds + len / sizeof(*pd) - pd)
			return EINVAL;

		if (index <= 0 && pdc != NULL) {
			if (pdc->pdc_funcs->pdc_set != NULL) {
				const int error = pdc->pdc_funcs->pdc_set(
				    pdc->pdc_dev, pd, enable);
				if (error != 0)
					return error;
			} else {
				pdc->pdc_funcs->pdc_enable(pdc->pdc_dev,
				    pd, enable);
			}
		}
		if (index == 0)
			break;

		pd += cells + 1;
	}

	return 0;
}

int
fdtbus_powerdomain_enable_index(int phandle, int index)
{
	return fdtbus_powerdomain_enable_internal(phandle, index, true, false);
}

int
fdtbus_powerdomain_disable_index(int phandle, int index)
{
	return fdtbus_powerdomain_enable_internal(phandle, index, false, false);
}

int
fdtbus_powerdomain_enable_on_attach(int phandle)
{
	return fdtbus_powerdomain_enable_internal(phandle, -1, true, true);
}

int
fdtbus_powerdomain_enable(int node)
{
	return fdtbus_powerdomain_enable_index(node, -1);
}

int
fdtbus_powerdomain_disable(int node)
{
	return fdtbus_powerdomain_disable_index(node, -1);
}

int
fdtbus_powerdomain_is_enabled_index(int phandle, int index, bool *enabled)
{
	const uint32_t *pds, *pd;
	int len;

	if (index < 0 || enabled == NULL)
		return EINVAL;
	pds = fdtbus_get_prop(phandle, "power-domains", &len);
	if (pds == NULL || len <= 0 || len % sizeof(*pds) != 0)
		return EINVAL;

	for (pd = pds; pd < pds + len / sizeof(*pd); index--) {
		const int pd_node =
		    fdtbus_get_phandle_from_native(be32toh(pd[0]));
		struct fdtbus_powerdomain_controller *pdc =
		    fdtbus_powerdomain_lookup(pd_node);
		uint32_t cells;
		bool state;
		int error;

		if (pdc != NULL) {
			cells = pdc->pdc_cells;
		} else if (of_getprop_uint32(pd_node, "#power-domain-cells",
		    &cells) != 0 || cells > INT_MAX) {
			return EINVAL;
		}
		if (cells >= (size_t)(pds + len / sizeof(*pd) - pd))
			return EINVAL;
		if (index == 0) {
			if (pdc == NULL)
				return ENXIO;
			if (pdc->pdc_funcs->pdc_get == NULL)
				return EOPNOTSUPP;
			error = pdc->pdc_funcs->pdc_get(pdc->pdc_dev, pd,
			    &state);
			if (error != 0)
				return error;
			*enabled = state;
			return 0;
		}
		pd += cells + 1;
	}

	return ENOENT;
}
