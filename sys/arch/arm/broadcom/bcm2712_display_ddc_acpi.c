/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Explicit polled DDC0 owner. No mapping or automatic attachment. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/cpu.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/systm.h>
#include <sys/timevar.h>
#include <dev/i2c/brcmstb_i2creg.h>
#include <dev/videomode/edidvar.h>
#include <arm/broadcom/bcm2712_display_ddc.h>

struct bcm2712_ddc_provider {
	struct brcmstb_i2c core;
	struct i2c_controller tag;
	bus_space_tag_t bst;
	bus_space_handle_t bsc, ddc;
	uint32_t bsc_status, bsc_mask;
	bool status_valid, mask_valid;
	int fault;
};

static ONCE_DECL(bcm2712_ddc_once);
static kmutex_t bcm2712_ddc_lock;
static struct bcm2712_ddc_provider *bcm2712_ddc_owner;

static int
bcm2712_ddc_once_init(void)
{

	mutex_init(&bcm2712_ddc_lock, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

static int
bcm2712_ddc_enter(void)
{
	int error;

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	error = RUN_ONCE(&bcm2712_ddc_once, bcm2712_ddc_once_init);
	if (error != 0)
		return error;
	mutex_enter(&bcm2712_ddc_lock);
	return 0;
}

static int
bcm2712_ddc_owned(const struct bcm2712_ddc_provider *sc)
{
	int error;

	error = bcm2712_ddc_enter();
	if (error != 0)
		return error;
	if (sc == NULL || sc != bcm2712_ddc_owner) {
		mutex_exit(&bcm2712_ddc_lock);
		return ENXIO;
	}
	return 0;
}

static int
bcm2712_ddc_read(void *arg, unsigned int offset, uint32_t *out)
{
	struct bcm2712_ddc_provider *sc = arg;

	if ((offset & 3) != 0 || (offset != BRCMSTB_I2C_COUNT &&
	    offset != BRCMSTB_I2C_CTL && offset != BRCMSTB_I2C_ENABLE &&
	    offset != BRCMSTB_I2C_CTLHI &&
	    (offset < BRCMSTB_I2C_DATA_OUT(0) ||
	    offset > BRCMSTB_I2C_DATA_OUT(7))))
		return EINVAL;
	/* Native bus_space cannot recover a stalled transaction or abort. */
	*out = bus_space_read_4(sc->bst, sc->ddc, offset);
	return 0;
}

static int
bcm2712_ddc_write(void *arg, unsigned int offset, uint32_t value)
{
	struct bcm2712_ddc_provider *sc = arg;

	if ((offset & 3) != 0 || (offset != BRCMSTB_I2C_ADDRESS &&
	    offset != BRCMSTB_I2C_COUNT && offset != BRCMSTB_I2C_CTL &&
	    offset != BRCMSTB_I2C_ENABLE && offset != BRCMSTB_I2C_CTLHI &&
	    (offset < BRCMSTB_I2C_DATA_IN(0) ||
	    offset > BRCMSTB_I2C_DATA_IN(7))))
		return EINVAL;
	bus_space_write_4(sc->bst, sc->ddc, offset, value);
	return 0;
}

static void
bcm2712_ddc_barrier(void *arg)
{
	struct bcm2712_ddc_provider *sc = arg;

	bus_space_barrier(sc->bst, sc->ddc, 0, 0x58,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

static uint64_t
bcm2712_ddc_time(void *arg)
{
	struct timeval now;

	(void)arg;
	microuptime(&now);
	return (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_usec;
}

static void
bcm2712_ddc_delay(void *arg, unsigned int us)
{

	(void)arg;
	delay(us);
}

static int
bcm2712_ddc_guard(void *arg, bool idle)
{
	struct bcm2712_ddc_provider *sc = arg;

	sc->bsc_mask = bus_space_read_4(sc->bst, sc->bsc, 4);
	sc->mask_valid = true;
	sc->status_valid = false;
	if (idle) {
		sc->bsc_status = bus_space_read_4(sc->bst, sc->bsc, 0);
		sc->status_valid = true;
	}
	if ((sc->bsc_mask & 2U) == 0 ||
	    (idle && (sc->bsc_status & 2U) != 0)) {
		if (sc->fault == 0)
			sc->fault = EBUSY;
		return sc->fault;
	}
	return 0;
}

int
bcm2712_display_ddc_create(device_t dev,
    const struct bcm2712_display_contract *contract,
    const struct bcm2712_display_memory *bsc, bus_space_handle_t bsc_handle,
    const struct bcm2712_display_memory *ddc, bus_space_handle_t ddc_handle,
    bus_space_tag_t bst, struct bcm2712_ddc_provider **out)
{
	struct bcm2712_display_contract checked;
	struct bcm2712_ddc_provider *sc;
	struct brcmstb_i2c_io io;
	int error;

	if (dev == NULL || contract == NULL || bsc == NULL || ddc == NULL ||
	    bst == NULL || out == NULL)
		return EINVAL;
	error = bcm2712_ddc_enter();
	if (error != 0)
		return error;
	if (bcm2712_ddc_owner != NULL) {
		error = EBUSY;
		goto unlock;
	}
	error = bcm2712_display_decode(contract->version, contract->memory,
	    BCM2712_DISPLAY_NMEM, contract->irq, BCM2712_DISPLAY_NIRQ, &checked);
	if (error != 0)
		goto unlock;
	if (bsc->address != checked.memory[13].address ||
	    bsc->length != checked.memory[13].length ||
	    ddc->address != checked.memory[14].address ||
	    ddc->length != checked.memory[14].length) {
		error = EINVAL;
		goto unlock;
	}
	sc = kmem_zalloc(sizeof(*sc), KM_SLEEP);
	sc->bst = bst;
	sc->bsc = bsc_handle;
	sc->ddc = ddc_handle;
	iic_tag_init(&sc->tag);
	sc->tag.ic_cookie = &sc->core;
	sc->tag.ic_exec = brcmstb_i2c_exec;
	io = (struct brcmstb_i2c_io) {
		.read = bcm2712_ddc_read, .write = bcm2712_ddc_write,
		.barrier = bcm2712_ddc_barrier, .time_us = bcm2712_ddc_time,
		.delay_us = bcm2712_ddc_delay, .guard = bcm2712_ddc_guard,
		.arg = sc
	};
	error = brcmstb_i2c_init(&sc->core, &io);
	if (error == 0)
		error = bcm2712_ddc_guard(sc, true);
	if (error != 0) {
		iic_tag_fini(&sc->tag);
		kmem_free(sc, sizeof(*sc));
		goto unlock;
	}
	bcm2712_ddc_owner = sc;
	*out = sc;
unlock:
	mutex_exit(&bcm2712_ddc_lock);
	return error;
}

int
bcm2712_display_ddc_read_edid(struct bcm2712_ddc_provider *sc, uint8_t *out,
    size_t length)
{
	uint8_t data[BCM2712_DDC_EDID_LENGTH], command = 0;
	int error;

	if (out == NULL || length != sizeof(data))
		return EINVAL;
	error = bcm2712_ddc_owned(sc);
	if (error != 0)
		return error;
	if (sc->fault != 0) {
		error = sc->fault;
		goto unlock;
	}
	error = iic_acquire_bus(&sc->tag, I2C_F_POLL);
	if (error != 0)
		goto unlock;
	error = iic_exec(&sc->tag, I2C_OP_READ_WITH_STOP, 0x50, &command, 1,
	    data, sizeof(data), I2C_F_POLL);
	if (error != 0 && sc->fault == 0)
		sc->fault = error;
	if (error == 0)
		error = bcm2712_ddc_guard(sc, true);
	/* Release the iic mutex even after failure. This does not reset hardware. */
	iic_release_bus(&sc->tag, I2C_F_POLL);
	if (error != 0)
		goto unlock;
	if (edid_is_valid(data) != 0) {
		error = EBADMSG;
		goto unlock;
	}
	memcpy(out, data, sizeof(data));
unlock:
	mutex_exit(&bcm2712_ddc_lock);
	return error;
}

int
bcm2712_display_ddc_status(struct bcm2712_ddc_provider *sc,
    struct bcm2712_ddc_status *out)
{
	struct bcm2712_ddc_status status;
	int error;

	if (out == NULL)
		return EINVAL;
	error = bcm2712_ddc_owned(sc);
	if (error != 0)
		return error;
	memset(&status, 0, sizeof(status));
	error = brcmstb_i2c_status(&sc->core, &status.core);
	if (error == 0) {
		status.bsc_status = sc->bsc_status;
		status.bsc_mask = sc->bsc_mask;
		status.status_valid = sc->status_valid;
		status.mask_valid = sc->mask_valid;
		status.fault = sc->fault;
		*out = status;
	}
	mutex_exit(&bcm2712_ddc_lock);
	return error;
}

int
bcm2712_display_ddc_destroy(struct bcm2712_ddc_provider *sc)
{
	int error;

	error = bcm2712_ddc_owned(sc);
	if (error != 0)
		return error;
	error = sc->fault != 0 ? sc->fault : brcmstb_i2c_fini(&sc->core);
	if (error == 0) {
		bcm2712_ddc_owner = NULL;
		iic_tag_fini(&sc->tag);
		kmem_free(sc, sizeof(*sc));
	}
	mutex_exit(&bcm2712_ddc_lock);
	return error;
}
