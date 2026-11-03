/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD; native A133 FDT power-domain provider. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/intr.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <dev/clk/clk.h>
#include <dev/fdt/fdtvar.h>
#include <dev/fdt/syscon.h>

#include <arm/sunxi/sun50i_a133_ppu_power.h>

struct a133_ppu_softc {
	device_t sc_dev;
	bus_space_tag_t sc_bst;
	bus_space_handle_t sc_bsh;
	struct clk *sc_clk;
	struct fdtbus_reset *sc_reset;
	void *sc_ih;
	kmutex_t sc_gate;
	kmutex_t sc_lock;
	struct a133_ppu_power sc_power;
	bool sc_prepared;
	bool sc_preparing;
};

static int a133_ppu_match(device_t, cfdata_t, void *);
static void a133_ppu_attach(device_t, device_t, void *);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun50i-a133-ppu" },
	DEVICE_COMPAT_EOL
};

/* FDT controller references live until shutdown; there is no unregister API. */
CFATTACH_DECL_NEW(sun50i_a133_ppu, sizeof(struct a133_ppu_softc),
    a133_ppu_match, a133_ppu_attach, NULL, NULL);

static int
a133_ppu_read(void *cookie, unsigned reg, uint32_t *value)
{
	struct a133_ppu_softc *sc = cookie;

	if (reg > A133_PPU_IRQ_MASK || reg % 4 != 0)
		return EINVAL;
	return bus_space_peek_4(sc->sc_bst, sc->sc_bsh, reg, value) == 0 ? 0 : EIO;
}

static int
a133_ppu_write(void *cookie, unsigned reg, uint32_t value)
{
	struct a133_ppu_softc *sc = cookie;

	if (reg > A133_PPU_IRQ_MASK || reg % 4 != 0)
		return EINVAL;
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, reg, value);
	bus_space_barrier(sc->sc_bst, sc->sc_bsh, reg, 4, BUS_SPACE_BARRIER_WRITE);
	return 0;
}

static void
a133_ppu_delay(void *cookie __unused, unsigned us)
{

	delay(us);
}

static int
a133_ppu_interrupt(void *cookie)
{
	struct a133_ppu_softc *sc = cookie;
	bool handled = false;
	int error;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_preparing || !sc->sc_prepared || sc->sc_power.uncertain) {
		mutex_exit(&sc->sc_lock);
		return 0;
	}
	error = a133_ppu_service_request(&sc->sc_power, &handled);
	if (error != 0) {
		/* Keep the controller mapped and powered after an uncertain flow. */
		sc->sc_power.uncertain = true;
		(void)a133_ppu_write(sc, A133_PPU_IRQ_MASK, 0);
		aprint_error_dev(sc->sc_dev, "power request failed: %d\n", error);
	}
	mutex_exit(&sc->sc_lock);
	return handled || error != 0;
}

static int
a133_ppu_get(device_t dev, const uint32_t *specifier __unused, bool *enabled)
{
	struct a133_ppu_softc *sc = device_private(dev);
	int error;

	mutex_enter(&sc->sc_lock);
	error = sc->sc_preparing ? EBUSY :
	    a133_ppu_power_get(&sc->sc_power, enabled);
	mutex_exit(&sc->sc_lock);
	return error;
}

static int
a133_ppu_set(device_t dev, const uint32_t *specifier __unused, bool enable)
{
	struct a133_ppu_softc *sc = device_private(dev);
	uint32_t control, readback;
	bool change;
	int error;

	/* Infrastructure clock/reset operations can sleep. */
	ASSERT_SLEEPABLE;
	mutex_enter(&sc->sc_gate);
	mutex_enter(&sc->sc_lock);
	error = a133_ppu_power_prepare(&sc->sc_power, enable, &change);
	if (error != 0 || !change)
		goto out;
	if (!sc->sc_prepared) {
		sc->sc_preparing = true;
		mutex_exit(&sc->sc_lock);
		error = clk_enable(sc->sc_clk);
		if (error == 0)
			error = fdtbus_reset_deassert(sc->sc_reset);
		mutex_enter(&sc->sc_lock);
		sc->sc_preparing = false;
		if (error != 0) {
			sc->sc_power.uncertain = true;
			goto out;
		}
		sc->sc_prepared = true;
		/* Keep PPU infrastructure until shutdown, including failed flows. */
	}
	error = a133_ppu_read(sc, A133_PPU_CONTROL, &control);
	if (error == 0)
		error = a133_ppu_write(sc, A133_PPU_CONTROL, (control & ~3U) | 1U);
	if (error == 0)
		error = a133_ppu_read(sc, A133_PPU_CONTROL, &readback);
	if (error == 0 && (readback & 3) != 1)
		error = EIO;
	if (error != 0) {
		sc->sc_power.uncertain = true;
		goto out;
	}
	error = a133_ppu_power_set(&sc->sc_power, enable);
	if (error == 0 && enable) {
		error = a133_ppu_write(sc, A133_PPU_IRQ_MASK, 1);
		if (error == 0)
			error = a133_ppu_read(sc, A133_PPU_IRQ_MASK, &readback);
		if (error == 0 && (readback & 1) == 0)
			error = EIO;
		if (error != 0)
			sc->sc_power.uncertain = true;
	}
	if (error != 0 && sc->sc_power.uncertain)
		(void)a133_ppu_write(sc, A133_PPU_IRQ_MASK, 0);
out:
	mutex_exit(&sc->sc_lock);
	mutex_exit(&sc->sc_gate);
	return error;
}

static const struct fdtbus_powerdomain_controller_func a133_ppu_funcs = {
	.pdc_set = a133_ppu_set,
	.pdc_get = a133_ppu_get,
};

static int
a133_ppu_match(device_t parent __unused, cfdata_t cf __unused, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
a133_ppu_attach(device_t parent __unused, device_t self, void *aux)
{
	struct a133_ppu_softc *sc = device_private(self);
	struct fdt_attach_args *faa = aux;
	struct syscon *syscon;
	bus_addr_t address;
	bus_size_t size;
	uint32_t cells, mask;
	bool enabled;
	int error;

	sc->sc_dev = self;
	sc->sc_bst = faa->faa_bst;
	if (of_getprop_uint32(faa->faa_phandle, "#power-domain-cells", &cells) != 0 ||
	    cells != 0 || fdtbus_get_reg(faa->faa_phandle, 0, &address, &size) != 0 ||
	    size != 0x40) {
		aprint_error_dev(self, "invalid power-domain binding\n");
		return;
	}
	syscon = fdtbus_syscon_acquire(faa->faa_phandle, "syscon");
	sc->sc_clk = fdtbus_clock_get(faa->faa_phandle, "ppu");
	sc->sc_reset = fdtbus_reset_get(faa->faa_phandle, "ppu");
	if (syscon == NULL || sc->sc_clk == NULL || sc->sc_reset == NULL) {
		aprint_error_dev(self, "missing syscon, clock or reset provider\n");
		goto release;
	}
	syscon_lock(syscon);
	sc->sc_power.revision = syscon_read_4(syscon, 0x24) & 7;
	syscon_unlock(syscon);
	if (sc->sc_power.revision != 6) {
		aprint_error_dev(self, "unqualified PPU revision %u\n",
		    sc->sc_power.revision);
		goto release;
	}
	if (bus_space_map(sc->sc_bst, address, size, 0, &sc->sc_bsh) != 0) {
		aprint_error_dev(self, "cannot map PPU registers\n");
		goto release;
	}
	sc->sc_power.io = (struct a133_ppu_io) {
		sc, a133_ppu_read, a133_ppu_write, a133_ppu_delay
	};
	error = a133_ppu_power_get(&sc->sc_power, &enabled);
	if (error == 0)
		error = a133_ppu_read(sc, A133_PPU_IRQ_MASK, &mask);
	if (error != 0 || mask != 0) {
		aprint_error_dev(self, "PPU state is unavailable or already managed\n");
		goto unmap;
	}
	mutex_init(&sc->sc_gate, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_VM);
	sc->sc_ih = fdtbus_intr_establish_xname(faa->faa_phandle, 0, IPL_VM,
	    FDT_INTR_MPSAFE, a133_ppu_interrupt, sc, device_xname(self));
	if (sc->sc_ih == NULL) {
		aprint_error_dev(self, "cannot establish PPU interrupt\n");
		goto destroy;
	}
	error = fdtbus_register_powerdomain_controller(self, faa->faa_phandle,
	    &a133_ppu_funcs);
	if (error != 0) {
		fdtbus_intr_disestablish(faa->faa_phandle, sc->sc_ih);
		goto destroy;
	}
	aprint_normal_dev(self, "A133 PPU revision %u, %s\n",
	    sc->sc_power.revision, enabled ? "3D" : "off/host");
	return;
destroy:
	mutex_destroy(&sc->sc_lock);
	mutex_destroy(&sc->sc_gate);
unmap:
	bus_space_unmap(sc->sc_bst, sc->sc_bsh, size);
release:
	if (sc->sc_reset != NULL)
		fdtbus_reset_put(sc->sc_reset);
	if (sc->sc_clk != NULL)
		clk_put(sc->sc_clk);
}
