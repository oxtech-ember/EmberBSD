/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/intr.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>
#include <dev/powervr/pvr_job_irq_fdt.h>

static void *
pvr_irq_establish(void *arg, int (*intr)(void *), void *cookie)
{
	struct pvr_job_irq_fdt *fdt = arg;

	return fdtbus_intr_establish_xname(fdt->phandle, 0, IPL_VM,
	    FDT_INTR_MPSAFE | FDT_INTR_EXCLUSIVE_MASKED, intr, cookie,
	    device_xname(fdt->irq.dev));
}

static void
pvr_irq_mask(void *arg, void *cookie)
{
	struct pvr_job_irq_fdt *fdt = arg;

	fdtbus_intr_mask(fdt->phandle, cookie);
}

static void
pvr_irq_unmask(void *arg, void *cookie)
{
	struct pvr_job_irq_fdt *fdt = arg;

	fdtbus_intr_unmask(fdt->phandle, cookie);
}

static void
pvr_irq_release(void *arg, void *cookie)
{
	struct pvr_job_irq_fdt *fdt = arg;

	fdtbus_intr_disestablish(fdt->phandle, cookie);
}

int
pvr_job_irq_fdt_start(struct pvr_job_irq_fdt *fdt, int node,
    bus_space_tag_t bst, bus_space_handle_t bsh,
    enum pvr_job_irq_profile profile)
{
	static const struct pvr_job_irq_route route = {
		.establish = pvr_irq_establish,
		.mask = pvr_irq_mask,
		.unmask = pvr_irq_unmask,
		.release = pvr_irq_release,
	};
	static const struct device_compatible_entry gicv2[] = {
		{ .compat = "arm,gic-400" },
		{ .compat = "arm,cortex-a15-gic" },
		{ .compat = "arm,cortex-a9-gic" },
		{ .compat = "arm,cortex-a7-gic" },
		DEVICE_COMPAT_EOL
	};
	static const struct device_compatible_entry gicv3[] = {
		{ .compat = "arm,gic-v3" },
		DEVICE_COMPAT_EOL
	};
	uint32_t spec[3];
	int parent, error;
	const bool a733 = profile == PVR_JOB_IRQ_A733;

	/* The GIC binding encodes the 988 ordinary SPIs starting at 0. */
	parent = fdtbus_intr_parent(node);
	if (OF_getproplen(node, "interrupts-extended") >= 0 ||
	    OF_getproplen(node, "interrupts") != sizeof(spec) ||
	    OF_getprop(node, "interrupts", spec, sizeof(spec)) != sizeof(spec) ||
	    be32toh(spec[0]) != 0 || be32toh(spec[1]) >= 988 ||
	    be32toh(spec[2]) != 4 || parent <= 0 ||
	    OF_getproplen(parent, "interrupt-map") >= 0 ||
	    !of_compatible_match(parent, a733 ? gicv3 : gicv2)) {
		error = EOPNOTSUPP;
	} else {
		fdt->phandle = node;
		error = pvr_job_irq_start(&fdt->irq, bst, bsh, &route, fdt, a733);
	}
	if (error != 0)
		aprint_error_dev(fdt->irq.dev,
		    "firmware IRQ unavailable (%d); using bounded job polling\n",
		    error);
	return error;
}
