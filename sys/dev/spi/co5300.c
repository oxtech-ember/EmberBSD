/*
 * Copyright (c) 2026 EmberBSD contributors.
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 * CO5300 QSPI command and colour opcodes follow Espressif esp-iot-solution
 * commit 8eee17c20da66048faa20c683ffeb80bfa85d723,
 * components/display/lcd/esp_lcd_co5300/esp_lcd_co5300_spi.c.
 * Panel-specific setup is supplied by the device tree.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$EmberBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/mutex.h>
#include <sys/mman.h>
#include <sys/condvar.h>
#include <sys/proc.h>
#include <sys/intr.h>
#include <sys/bus.h>
#include <sys/sysctl.h>
#include <uvm/uvm_extern.h>
#include <dev/fdt/fdtvar.h>
#include <dev/spi/spivar.h>
#include <dev/wsfb/genfbvar.h>

#define CO5300_MAX_WIDTH 480
#define CO5300_MAX_HEIGHT 480
#define CO5300_RATE 20

struct co5300_softc {
	/* genfb passes its own softc to the bus callbacks. */
	struct genfb_softc sc_gen;
	device_t sc_dev;
	struct spi_handle *sc_spi;
	bus_space_tag_t sc_bst;
	struct fdtbus_gpio_pin *sc_reset;
	struct fdtbus_regulator *sc_power;
	int sc_phandle;
	uint32_t sc_width, sc_height;
	vaddr_t sc_fb;
	size_t sc_size, sc_alloc, sc_rowbytes;
	uint8_t *sc_shadow, *sc_snapshot, *sc_wire, *sc_dirty;
	kmutex_t sc_lock;
	kcondvar_t sc_cv;
	struct sysctllog *sc_sysctllog;
	uint32_t sc_rows_changed, sc_rows_sent, sc_row_errors;
	int sc_last_error;
	lwp_t *sc_thread;
	bool sc_ready, sc_stop;
};

static int co5300_match(device_t, cfdata_t, void *);
static void co5300_attach(device_t, device_t, void *);
static void co5300_deferred(device_t);
static int co5300_ioctl(void *, void *, u_long, void *, int, struct lwp *);
static paddr_t co5300_mmap(void *, void *, off_t, int);
static void co5300_worker(void *);

CFATTACH_DECL_NEW(cofb, sizeof(struct co5300_softc),
    co5300_match, co5300_attach, NULL, NULL);

static const struct device_compatible_entry co5300_compat[] = {
	{ .compat = "chipone,co5300" },
	DEVICE_COMPAT_EOL
};

static int
co5300_match(device_t parent, cfdata_t match, void *aux)
{
	return spi_compatible_match(aux, match, co5300_compat);
}

/* A complete transaction holds CS from the opcode through its data. */
static int
co5300_send(struct co5300_softc *sc, uint8_t opcode, uint8_t command,
    const uint8_t *data, size_t len, unsigned int width)
{
	struct spi_transfer transfer;
	struct spi_chunk header_chunk, data_chunk;
	uint8_t header[4] = { opcode, 0, command, 0 };

	if (len > INT_MAX)
		return EINVAL;
	spi_transfer_init(&transfer);
	spi_chunk_init(&header_chunk, sizeof(header), header, NULL);
	spi_transfer_add(&transfer, &header_chunk);
	if (len != 0) {
		spi_chunk_init(&data_chunk, (int)len, data, NULL);
		data_chunk.chunk_bus_width = width;
		spi_transfer_add(&transfer, &data_chunk);
	}
	(void)spi_transfer(sc->sc_spi, &transfer);
	spi_wait(&transfer);
	return (transfer.st_flags & SPI_F_ERROR) ? transfer.st_errno : 0;
}

/* Round up and include one tick for the current partial clock interval. */
static int
co5300_ticks(unsigned int ms)
{
	uint64_t ticks = ((uint64_t)ms * hz + 999) / 1000;

	return ticks >= INT_MAX ? INT_MAX : (int)ticks + 1;
}

/* Validate all records before sending any command. */
static bool
co5300_init_valid(const uint8_t *data, size_t len)
{
	size_t pos = 0;
	unsigned int total_ms = 0;

	if (len == 0 || len > 1024)
		return false;
	while (pos < len) {
		if (len - pos < 4 || data[pos + 1] > len - pos - 4)
			return false;
		total_ms += ((unsigned int)data[pos + 2] << 8) | data[pos + 3];
		if (total_ms > 5000)
			return false;
		pos += 4 + data[pos + 1];
	}
	return true;
}

static int
co5300_init_sequence(struct co5300_softc *sc, const uint8_t *data, size_t len)
{
	size_t pos = 0;
	int error;

	if (!co5300_init_valid(data, len))
		return EINVAL;
	while (pos < len) {
		unsigned int count = data[pos + 1];
		unsigned int ms = (data[pos + 2] << 8) | data[pos + 3];
		error = co5300_send(sc, 0x02, data[pos], data + pos + 4,
		    count, 1);
		if (error != 0)
			return error;
		if (ms != 0)
			(void)kpause("coinit", false, co5300_ticks(ms), NULL);
		pos += 4 + count;
	}
	return 0;
}

static void
co5300_to_wire(const uint8_t *source, uint8_t *wire, size_t width)
{
	for (size_t i = 0; i < width; i++) {
		uint16_t pixel;
		memcpy(&pixel, source + i * 2, sizeof(pixel));
		wire[i * 2] = pixel >> 8;
		wire[i * 2 + 1] = pixel;
	}
}

/* Commit the exact snapshot only after all three commands succeed. */
static int
co5300_row(struct co5300_softc *sc, unsigned int y)
{
	uint8_t xwin[4] = { 0, 0, (sc->sc_width - 1) >> 8,
	    sc->sc_width - 1 };
	uint8_t ywin[4] = { y >> 8, y, y >> 8, y };
	uint8_t *shadow = sc->sc_shadow + y * sc->sc_rowbytes;
	int error;

	memcpy(sc->sc_snapshot, (void *)(sc->sc_fb + y * sc->sc_rowbytes),
	    sc->sc_rowbytes);
	if (!sc->sc_dirty[y] && memcmp(shadow, sc->sc_snapshot,
	    sc->sc_rowbytes) == 0)
		return 0;
	mutex_enter(&sc->sc_lock);
	sc->sc_rows_changed++;
	mutex_exit(&sc->sc_lock);
	co5300_to_wire(sc->sc_snapshot, sc->sc_wire, sc->sc_width);
	error = co5300_send(sc, 0x02, 0x2a, xwin, sizeof(xwin), 1);
	if (error == 0)
		error = co5300_send(sc, 0x02, 0x2b, ywin, sizeof(ywin), 1);
	if (error == 0)
		error = co5300_send(sc, 0x32, 0x2c, sc->sc_wire,
		    sc->sc_rowbytes, 4);
	if (error == 0) {
		memcpy(shadow, sc->sc_snapshot, sc->sc_rowbytes);
		sc->sc_dirty[y] = 0;
	}
	mutex_enter(&sc->sc_lock);
	if (error == 0)
		sc->sc_rows_sent++;
	else {
		sc->sc_row_errors++;
		sc->sc_last_error = error;
	}
	mutex_exit(&sc->sc_lock);
	return error;
}

static int
co5300_sysctl_status(SYSCTLFN_ARGS)
{
	struct co5300_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	char status[128];
	uint32_t changed, sent, errors;
	int last_error;

	mutex_enter(&sc->sc_lock);
	changed = sc->sc_rows_changed;
	sent = sc->sc_rows_sent;
	errors = sc->sc_row_errors;
	last_error = sc->sc_last_error;
	mutex_exit(&sc->sc_lock);
	(void)snprintf(status, sizeof(status),
	    "rows_changed=%u rows_sent=%u errors=%u last_error=%d",
	    changed, sent, errors, last_error);
	node.sysctl_data = status;
	node.sysctl_size = strlen(status) + 1;
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static void
co5300_sysctl_init(struct co5300_softc *sc)
{
	const struct sysctlnode *node;
	int error;

	error = sysctl_createv(&sc->sc_sysctllog, 0, NULL, &node,
	    CTLFLAG_READONLY, CTLTYPE_NODE, device_xname(sc->sc_dev),
	    SYSCTL_DESCR("CO5300 display"), NULL, 0, NULL, 0,
	    CTL_HW, CTL_CREATE, CTL_EOL);
	if (error == 0)
		error = sysctl_createv(&sc->sc_sysctllog, 0, NULL, NULL,
		    CTLFLAG_READONLY, CTLTYPE_STRING, "status",
		    SYSCTL_DESCR("Framebuffer refresh counters"),
		    co5300_sysctl_status, 0, (char *)sc, 0,
		    CTL_HW, node->sysctl_num, CTL_CREATE, CTL_EOL);
	if (error != 0) {
		sysctl_teardown(&sc->sc_sysctllog);
		aprint_error_dev(sc->sc_dev, "cannot publish status: %d\n", error);
	}
}

static bool
co5300_mmap_valid(off_t offset, size_t size)
{
	return offset >= 0 && ((uint64_t)offset & PAGE_MASK) == 0 &&
	    (uint64_t)offset < size;
}

static paddr_t
co5300_mmap(void *v, void *vs, off_t offset, int prot)
{
	struct co5300_softc *sc = v;
	paddr_t pa;

	(void)vs;
	if ((prot & PROT_EXEC) != 0 ||
	    !co5300_mmap_valid(offset, sc->sc_alloc) ||
	    !pmap_extract(pmap_kernel(), sc->sc_fb + (vaddr_t)offset, &pa))
		return (paddr_t)-1;
	return bus_space_mmap(sc->sc_bst, pa, 0, prot,
	    BUS_SPACE_MAP_LINEAR | BUS_SPACE_MAP_CACHEABLE);
}

static int
co5300_ioctl(void *v, void *vs, u_long cmd, void *data, int flag, struct lwp *l)
{
	return EPASSTHROUGH;
}

static void
co5300_worker(void *arg)
{
	struct co5300_softc *sc = arg;
	bool reported = false;

	mutex_enter(&sc->sc_lock);
	while (!sc->sc_ready && !sc->sc_stop)
		cv_wait(&sc->sc_cv, &sc->sc_lock);
	while (!sc->sc_stop) {
		mutex_exit(&sc->sc_lock);
		bool failed = false;
		for (u_int y = 0; y < sc->sc_height; y++) {
			int error = co5300_row(sc, y);
			if (error != 0) {
				failed = true;
				if (!reported)
					aprint_error_dev(sc->sc_dev,
					    "row transfer failed: %d (retrying)\n", error);
				reported = true;
				break;
			}
		}
		if (!failed)
			reported = false;
		mutex_enter(&sc->sc_lock);
		if (!sc->sc_stop)
			(void)cv_timedwait(&sc->sc_cv, &sc->sc_lock,
			    co5300_ticks(1000 / CO5300_RATE));
	}
	mutex_exit(&sc->sc_lock);
	kthread_exit(0);
}

static void
co5300_attach(device_t parent, device_t self, void *aux)
{
	struct co5300_softc *sc = device_private(self);
	struct spi_attach_args *sa = aux;

	sc->sc_dev = self;
	sc->sc_gen.sc_dev = self;
	sc->sc_spi = sa->sa_handle;
	sc->sc_phandle = sa->sa_cookie;
	aprint_naive("\n");
	aprint_normal(": CO5300 QSPI framebuffer\n");
	config_interrupts(self, co5300_deferred);
}

static void
co5300_deferred(device_t self)
{
	struct co5300_softc *sc = device_private(self);
	const uint8_t *init;
	int initlen, error;
	uint32_t speed;
	prop_dictionary_t props = device_properties(self);
	struct genfb_ops ops = { .genfb_ioctl = co5300_ioctl,
	    .genfb_mmap = co5300_mmap };

	init = fdtbus_get_prop(sc->sc_phandle, "chipone,init-sequence", &initlen);
	if (of_getprop_uint32(sc->sc_phandle, "width", &sc->sc_width) != 0 ||
	    of_getprop_uint32(sc->sc_phandle, "height", &sc->sc_height) != 0 ||
	    of_getprop_uint32(sc->sc_phandle, "spi-max-frequency", &speed) != 0 ||
	    sc->sc_width == 0 || sc->sc_width > CO5300_MAX_WIDTH ||
	    (sc->sc_width & 3) != 0 || sc->sc_height == 0 ||
	    sc->sc_height > CO5300_MAX_HEIGHT || speed == 0 || speed > INT_MAX ||
	    init == NULL || initlen <= 0 || !co5300_init_valid(init, initlen)) {
		aprint_error_dev(self, "invalid geometry, speed or init sequence\n");
		return;
	}
	sc->sc_bst = fdtbus_bus_tag_create(sc->sc_phandle, 0);
	if (sc->sc_bst == NULL) {
		aprint_error_dev(self, "cannot obtain framebuffer bus tag\n");
		return;
	}
	sc->sc_power = fdtbus_regulator_acquire(sc->sc_phandle, "power-supply");
	if (sc->sc_power == NULL || fdtbus_regulator_enable(sc->sc_power) != 0) {
		aprint_error_dev(self, "cannot enable power-supply\n");
		goto out_power;
	}
	if (of_hasprop(sc->sc_phandle, "reset-gpios")) {
		sc->sc_reset = fdtbus_gpio_acquire(sc->sc_phandle, "reset-gpios",
		    GPIO_PIN_OUTPUT);
		if (sc->sc_reset == NULL) {
			aprint_error_dev(self, "cannot acquire reset-gpios\n");
			goto out_power;
		}
		fdtbus_gpio_write(sc->sc_reset, 1);
		(void)kpause("corst", false, co5300_ticks(10), NULL);
		fdtbus_gpio_write(sc->sc_reset, 0);
		(void)kpause("corst", false, co5300_ticks(150), NULL);
	}
	if ((error = spi_configure(self, sc->sc_spi, SPI_MODE_0, speed)) != 0)
		goto out_reset;
	if (sc->sc_reset == NULL) {
		error = co5300_send(sc, 0x02, 0x01, NULL, 0, 1);
		if (error != 0)
			goto out_reset;
		(void)kpause("corst", false, co5300_ticks(80), NULL);
	}
	if ((error = co5300_init_sequence(sc, init, initlen)) != 0)
		goto out_reset;

	sc->sc_rowbytes = sc->sc_width * 2;
	sc->sc_size = sc->sc_rowbytes * sc->sc_height;
	sc->sc_alloc = round_page(sc->sc_size);
	sc->sc_fb = uvm_km_alloc(kernel_map, sc->sc_alloc, 0,
	    UVM_KMF_WIRED | UVM_KMF_ZERO);
	if (sc->sc_fb == 0) {
		error = ENOMEM;
		goto out_reset;
	}
	sc->sc_shadow = kmem_zalloc(sc->sc_size, KM_SLEEP);
	sc->sc_snapshot = kmem_alloc(sc->sc_rowbytes, KM_SLEEP);
	sc->sc_wire = kmem_alloc(sc->sc_rowbytes, KM_SLEEP);
	sc->sc_dirty = kmem_alloc(sc->sc_height, KM_SLEEP);
	memset(sc->sc_dirty, 1, sc->sc_height);
	if (!prop_dictionary_set_uint32(props, "width", sc->sc_width) ||
	    !prop_dictionary_set_uint32(props, "height", sc->sc_height) ||
	    !prop_dictionary_set_uint32(props, "depth", 16) ||
	    !prop_dictionary_set_uint32(props, "linebytes", sc->sc_rowbytes) ||
	    !prop_dictionary_set_uint64(props, "address", 0) ||
	    !prop_dictionary_set_uint64(props, "virtual_address", sc->sc_fb) ||
	    !prop_dictionary_set_bool(props, "enable_shadowfb", false) ||
	    !prop_dictionary_set_bool(props, "is_console", false)) {
		error = ENOMEM;
		goto out_mem;
	}
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	cv_init(&sc->sc_cv, "co5300");
	error = kthread_create(PRI_NONE, KTHREAD_MPSAFE | KTHREAD_MUSTJOIN,
	    NULL, co5300_worker, sc, &sc->sc_thread, "%s", device_xname(self));
	if (error != 0)
		goto out_lock;
	genfb_init(&sc->sc_gen);
	error = genfb_attach(&sc->sc_gen, &ops);
	if (error != 0) {
		mutex_enter(&sc->sc_lock);
		sc->sc_stop = true;
		cv_signal(&sc->sc_cv);
		mutex_exit(&sc->sc_lock);
		(void)kthread_join(sc->sc_thread);
		cv_destroy(&sc->sc_cv);
		mutex_destroy(&sc->sc_lock);
		/* genfb has no detach/unwind API and may retain these buffers. */
		aprint_error_dev(self, "genfb attachment failed: %d\n", error);
		return;
	}
	co5300_sysctl_init(sc);
	mutex_enter(&sc->sc_lock);
	sc->sc_ready = true;
	cv_signal(&sc->sc_cv);
	mutex_exit(&sc->sc_lock);
	return;

out_lock:
	cv_destroy(&sc->sc_cv);
	mutex_destroy(&sc->sc_lock);
out_mem:
	prop_dictionary_remove(props, "virtual_address");
	kmem_free(sc->sc_dirty, sc->sc_height);
	kmem_free(sc->sc_wire, sc->sc_rowbytes);
	kmem_free(sc->sc_snapshot, sc->sc_rowbytes);
	kmem_free(sc->sc_shadow, sc->sc_size);
	uvm_km_free(kernel_map, sc->sc_fb, sc->sc_alloc, UVM_KMF_WIRED);
out_reset:
	aprint_error_dev(self, "initialization failed: %d\n", error);
	if (sc->sc_reset != NULL)
		fdtbus_gpio_release(sc->sc_reset);
out_power:
	if (sc->sc_power != NULL)
		fdtbus_regulator_release(sc->sc_power);
}
