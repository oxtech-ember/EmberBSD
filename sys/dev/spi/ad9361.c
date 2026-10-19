/*
 * Copyright (c) 2026 EmberBSD contributors.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* Origin: EmberBSD AD9361 SPI/FDT attachment and exclusive RX/TX ADI no-OS adapter
 * ce7107f957d19858f2dffc85bf3d7f2c70367000. */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/queue.h>
#include <sys/condvar.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/gpio.h>
#include <dev/clk/clk_backend.h>
#include <external/bsd/ad9361/dist/ad9361_native.h>

#include <dev/fdt/fdtvar.h>
#include <dev/fdt/adi_axi_adcvar.h>
#include <dev/spi/spivar.h>
#include <dev/spi/ad9361reg.h>
#include <dev/spi/ad9361var.h>

struct ad9361_softc;
struct ad9361_tx {
	LIST_ENTRY(ad9361_tx) tx_next;
	struct ad9361_softc *tx_sc;
};

struct ad9361_softc {
	device_t sc_dev;
	struct spi_handle *sc_spi;
	uint32_t sc_spi_speed;
	int sc_spi_mode;
	kmutex_t sc_lock;
	struct sysctllog *sc_sysctllog;
	struct adi_axi_converter *sc_converter;
	int sc_phandle;
	bool sc_identified;
	bool sc_bootstrap_attempted;
	struct fdtbus_gpio_pin *sc_reset;
	struct clk *sc_refclk;
	struct ad9361_native_bus sc_native;
	struct ad9361_rf_phy *sc_phy;
	struct ad9361_tx sc_tx;
	AD9361_InitParam *sc_tx_params;
	void *sc_tx_owner;
	bool sc_tx_enabled;
	bool sc_tx_prepared;
	bool sc_tx_used;
	bool sc_tx_poisoned;
};

static LIST_HEAD(, ad9361_tx) ad9361_tx_providers =
    LIST_HEAD_INITIALIZER(ad9361_tx_providers);
static kmutex_t ad9361_tx_registry_lock;
static ONCE_DECL(ad9361_tx_registry_once);

static int ad9361_match(device_t, cfdata_t, void *);
static void ad9361_attach(device_t, device_t, void *);
static void ad9361_probe(device_t);
static int ad9361_configure(void *, const struct adi_axi_rx_config *,
			    const struct adi_axi_adc_io *);
static int ad9361_sysctl_snapshot(SYSCTLFN_ARGS);
static int ad9361_bootstrap(void *, const struct adi_axi_rx_config *);
static int ad9361_native_transfer(void *, uint8_t *, size_t);
static int ad9361_native_reset(void *, bool);
static void ad9361_tx_attach(device_t);

CFATTACH_DECL_NEW(ad9361phy, sizeof(struct ad9361_softc), ad9361_match,
		  ad9361_attach, NULL, NULL);

static const struct device_compatible_entry compat_data[] = {
    {.compat = "adi,ad9361"}, DEVICE_COMPAT_EOL};

static int ad9361_status(void *, struct adi_axi_rx_status *);
static int ad9361_retune_rx(void *, uint64_t);

static const struct adi_axi_converter_ops ad9361_ops = {
    .read_rx_status = ad9361_status, .retune_rx = ad9361_retune_rx,
    .configure = ad9361_configure, .bootstrap = ad9361_bootstrap};

static int ad9361_read(struct ad9361_softc *sc, uint16_t reg, uint8_t *value)
{
	uint8_t command[2];

	/* AD9361 read is bit 15 clear, count-one is bits 14:12 clear. */
	if (reg > AD9361_SPI_ADDRESS_MASK)
		return EINVAL;
	command[0] = reg >> 8;
	command[1] = reg;
	return spi_send_recv(sc->sc_spi, sizeof(command), command, 1, value);
}

static int ad9361_match(device_t parent, cfdata_t cf, void *aux)
{

	return spi_compatible_match(aux, cf, compat_data);
}

static void ad9361_attach(device_t parent, device_t self, void *aux)
{
	struct ad9361_softc *sc = device_private(self);
	struct spi_attach_args *sa = aux;
	uint32_t speed = 1000000;
	int error, mode = SPI_MODE_0;

	sc->sc_dev = self;
	sc->sc_spi = sa->sa_handle;
	sc->sc_phandle = sa->sa_cookie;
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	sc->sc_tx_enabled = sc->sc_phandle > 0 &&
	    of_hasprop(sc->sc_phandle, "adi,tx-enable");
	if (sc->sc_tx_enabled &&
	    of_hasprop(sc->sc_phandle, "adi,rx-only")) {
		aprint_error(": conflicting RX-only and TX-enable properties\n");
		return;
	}
	if (sc->sc_phandle != 0) {
		(void)of_getprop_uint32(sc->sc_phandle, "spi-max-frequency",
					&speed);
		if (of_hasprop(sc->sc_phandle, "spi-cpha"))
			mode |= SPI_MODE_CPHA;
		if (of_hasprop(sc->sc_phandle, "spi-cpol"))
			mode |= SPI_MODE_CPOL;
		if (of_hasprop(sc->sc_phandle, "spi-cs-high") ||
		    of_hasprop(sc->sc_phandle, "spi-3wire")) {
			aprint_error(": unsupported SPI wiring\n");
			return;
		}
	}
	if (speed == 0 || speed > 50000000) {
		aprint_error(": invalid spi-max-frequency\n");
		return;
	}
	/* Keep RX and the first TX probe at a conservative rate. */
	sc->sc_spi_speed = speed;
	sc->sc_spi_mode = mode;
	error = spi_configure(self, sc->sc_spi, mode, uimin(speed, 1000000));
	if (error != 0) {
		aprint_error(": SPI configuration failed: %d\n", error);
		return;
	}
	aprint_naive("\n");
	if (sc->sc_tx_enabled) {
		aprint_normal(": AD9361 transmitter (exclusive bootstrap required)\n");
		config_interrupts(self, ad9361_tx_attach);
		return;
	}
	aprint_normal(
	    ": AD9361 receiver (probe only until exclusive bootstrap)\n");
	config_interrupts(self, ad9361_probe);
}

int ad9361_read_status(device_t dev, struct ad9361_status *result)
{
	struct ad9361_softc *sc = device_private(dev);
	struct ad9361_status status;
	int error;

	if (result == NULL)
		return EINVAL;
	mutex_enter(&sc->sc_lock);
	if (!sc->sc_identified) {
		error = ENXIO;
		goto out;
	}
#define READ_STATUS(reg, member)                                               \
	do {                                                                   \
		error = ad9361_read(sc, (reg), &status.member);                \
		if (error != 0)                                                \
			goto out;                                              \
	} while (0)
	READ_STATUS(AD9361_REG_PRODUCT_ID, product_id);
	if ((status.product_id & AD9361_PRODUCT_ID_MASK) !=
	    AD9361_PRODUCT_ID_9361) {
		error = ENXIO;
		goto out;
	}
	READ_STATUS(AD9361_REG_TX_ENABLE, tx_enable);
	READ_STATUS(AD9361_REG_RX_ENABLE, rx_enable);
	READ_STATUS(AD9361_REG_ENSM_MODE, ensm_mode);
	READ_STATUS(AD9361_REG_ENSM_CONFIG1, ensm_config1);
	READ_STATUS(AD9361_REG_ENSM_CONFIG2, ensm_config2);
	READ_STATUS(AD9361_REG_STATE, state);
	READ_STATUS(AD9361_REG_TX_SYNTH_PD, tx_synth_powerdown);
	READ_STATUS(AD9361_REG_TX1_ATTEN0, tx1_atten[0]);
	READ_STATUS(AD9361_REG_TX1_ATTEN1, tx1_atten[1]);
	READ_STATUS(AD9361_REG_TX2_ATTEN0, tx2_atten[0]);
	READ_STATUS(AD9361_REG_TX2_ATTEN1, tx2_atten[1]);
#undef READ_STATUS
	*result = status;
out:
	mutex_exit(&sc->sc_lock);
	return error;
}

static int ad9361_sysctl_snapshot(SYSCTLFN_ARGS)
{
	struct ad9361_softc *sc = rnode->sysctl_data;
	struct ad9361_status status;
	struct sysctlnode node;
	char buffer[256];
	int error;

	if (newp != NULL)
		return EPERM;
	error = ad9361_read_status(sc->sc_dev, &status);
	if (error != 0)
		return error;
	snprintf(buffer, sizeof(buffer),
		 "id=0x%02x tx_enable=0x%02x rx_enable=0x%02x "
		 "ensm_mode=0x%02x ensm_config1=0x%02x ensm_config2=0x%02x "
		 "state=0x%02x tx_synth_pd=0x%02x "
		 "tx1_atten=0x%02x%02x tx2_atten=0x%02x%02x",
		 status.product_id, status.tx_enable, status.rx_enable,
		 status.ensm_mode, status.ensm_config1, status.ensm_config2,
		 status.state, status.tx_synth_powerdown, status.tx1_atten[1],
		 status.tx1_atten[0], status.tx2_atten[1], status.tx2_atten[0]);
	node = *rnode;
	node.sysctl_data = buffer;
	node.sysctl_size = strlen(buffer) + 1;
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static void ad9361_probe(device_t self)
{
	struct ad9361_softc *sc = device_private(self);
	struct ad9361_status status;
	const struct sysctlnode *node;
	uint8_t id;
	int error;

	error = ad9361_read(sc, AD9361_REG_PRODUCT_ID, &id);
	if (error != 0 ||
	    (id & AD9361_PRODUCT_ID_MASK) != AD9361_PRODUCT_ID_9361) {
		aprint_error_dev(self, "product ID probe failed (error %d)\n",
				 error != 0 ? error : ENXIO);
		return;
	}
	mutex_enter(&sc->sc_lock);
	sc->sc_identified = true;
	mutex_exit(&sc->sc_lock);
	error = ad9361_read_status(self, &status);
	if (error != 0) {
		aprint_error_dev(self, "status read failed: %d\n", error);
		return;
	}
	aprint_normal_dev(
	    self,
	    "ID 0x%02x, state 0x%02x, TX channels 0x%02x, TX LO PD 0x%02x\n",
	    id, status.state, status.tx_enable, status.tx_synth_powerdown);
	aprint_normal_dev(
	    self, "RX initialization requires exclusive capture bootstrap\n");
	if (sysctl_createv(&sc->sc_sysctllog, 0, NULL, &node, CTLFLAG_PERMANENT,
			   CTLTYPE_NODE, device_xname(self),
			   SYSCTL_DESCR("AD9361 SPI hardware status"), NULL, 0,
			   NULL, 0, CTL_HW, CTL_CREATE, CTL_EOL) == 0) {
		(void)sysctl_createv(
		    &sc->sc_sysctllog, 0, &node, NULL,
		    CTLFLAG_PERMANENT | CTLFLAG_READONLY, CTLTYPE_STRING,
		    "snapshot", SYSCTL_DESCR("Fresh raw SPI register snapshot"),
		    ad9361_sysctl_snapshot, 0, (char *)sc, 0, CTL_CREATE,
		    CTL_EOL);
	}
	error = adi_axi_converter_register(self, sc->sc_phandle, &ad9361_ops,
					   sc, &sc->sc_converter);
	if (error != 0)
		aprint_error_dev(self, "converter registration failed: %d\n",
				 error);
}

static int ad9361_native_transfer(void *cookie, uint8_t *data, size_t length)
{
	struct ad9361_softc *sc = cookie;
	struct spi_transfer transfer;
	struct spi_chunk chunk;
	uint8_t command[10];
	int error;

	KASSERT(mutex_owned(&sc->sc_lock));
	if (length < 3 || length > sizeof(command))
		return EINVAL;
	memcpy(command, data, length);
	spi_transfer_init(&transfer);
	spi_chunk_init(&chunk, length, command, data);
	spi_transfer_add(&transfer, &chunk);
	(void)spi_transfer(sc->sc_spi, &transfer);
	spi_wait(&transfer);
	error = transfer.st_errno;
	if (error != 0)
		aprint_error_dev(sc->sc_dev,
		    "SPI %s register 0x%03x failed: %d\n",
		    command[0] & 0x80 ? "write" : "read",
		    ((command[0] << 8) | command[1]) & 0x3ff, error);
	return error;
}

static int ad9361_native_reset(void *cookie, bool released)
{
	struct ad9361_softc *sc = cookie;
	int actual, level = released ? 1 : 0;

	KASSERT(mutex_owned(&sc->sc_lock));
	if (sc->sc_reset == NULL)
		return ENXIO;
	/* The no-OS callback uses physical RESETB levels, not GPIO polarity. */
	fdtbus_gpio_write_raw(sc->sc_reset, level);
	actual = fdtbus_gpio_read_raw(sc->sc_reset);
	if (actual == level)
		return 0;
	if (sc->sc_tx_enabled)
		sc->sc_tx_poisoned = true;
	aprint_error_dev(sc->sc_dev,
	    "RESETB readback mismatch: requested %d, observed %d\n",
	    level, actual);
	return EIO;
}

static int ad9361_bootstrap(void *cookie,
			    const struct adi_axi_rx_config *config)
{
	struct ad9361_softc *sc = cookie;
	AD9361_InitParam *params;
	uint32_t reference;
	int error;

	mutex_enter(&sc->sc_lock);
	if (!sc->sc_identified || sc->sc_bootstrap_attempted) {
		error = sc->sc_identified ? EALREADY : ENXIO;
		goto out;
	}
	sc->sc_bootstrap_attempted = true;
	sc->sc_refclk = fdtbus_clock_get_index(sc->sc_phandle, 0);
	if (sc->sc_refclk == NULL) {
		error = ENXIO;
		goto out;
	}
	reference = clk_get_rate(sc->sc_refclk);
	params = kmem_alloc(sizeof(*params), KM_SLEEP);
	error =
	    ad9361_native_params_from_fdt(sc->sc_phandle, reference, params);
	if (error != 0)
		goto free_params;
	/*
	 * GPIO acquisition changes direction and therefore belongs exclusively
	 * to bootstrap, after the capture owner has proved cold admission.
	 */
	sc->sc_reset =
	    fdtbus_gpio_acquire(sc->sc_phandle, "reset-gpios", GPIO_PIN_OUTPUT);
	if (sc->sc_reset == NULL) {
		error = ENXIO;
		goto free_params;
	}
	error = clk_enable(sc->sc_refclk);
	if (error != 0)
		goto free_params;
	ad9361_native_bus_init(&sc->sc_native, sc, ad9361_native_transfer,
			       ad9361_native_reset);
	error = ad9361_native_bootstrap(&sc->sc_native, params, config,
					&sc->sc_phy);
	if (error != 0)
		aprint_error_dev(sc->sc_dev,
		    "RX bootstrap failed: %d, transport error %d\n",
		    error, ad9361_native_bus_error(&sc->sc_native));
free_params:
	kmem_free(params, sizeof(*params));
out:
	mutex_exit(&sc->sc_lock);
	return error;
}

static int ad9361_configure(void *cookie,
			    const struct adi_axi_rx_config *config,
			    const struct adi_axi_adc_io *io)
{
	struct ad9361_softc *sc = cookie;
	int error;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_phy == NULL)
		error = ENXIO;
	else
		error = ad9361_native_configure(&sc->sc_native, sc->sc_phy,
						config, io);
	mutex_exit(&sc->sc_lock);
	return error;
}

static int
ad9361_status(void *cookie, struct adi_axi_rx_status *status)
{
	struct ad9361_softc *sc = cookie;
	int error;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_phy == NULL)
		error = ENXIO;
	else
		error = ad9361_native_read_rx_status(&sc->sc_native,
		    sc->sc_phy, status);
	mutex_exit(&sc->sc_lock);
	return error;
}

static int
ad9361_retune_rx(void *cookie, uint64_t frequency)
{
	struct ad9361_softc *sc = cookie;
	int error;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_phy == NULL)
		error = ENXIO;
	else
		error = ad9361_native_retune_rx(&sc->sc_native, sc->sc_phy, frequency);
	mutex_exit(&sc->sc_lock);
	return error;
}

static int
ad9361_tx_registry_init(void)
{

	mutex_init(&ad9361_tx_registry_lock, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

static int
ad9361_tx_register(struct ad9361_softc *sc)
{
	struct ad9361_tx *tx;

	RUN_ONCE(&ad9361_tx_registry_once, ad9361_tx_registry_init);
	mutex_enter(&ad9361_tx_registry_lock);
	LIST_FOREACH(tx, &ad9361_tx_providers, tx_next) {
		if (tx->tx_sc->sc_phandle == sc->sc_phandle) {
			mutex_exit(&ad9361_tx_registry_lock);
			return EEXIST;
		}
	}
	sc->sc_tx.tx_sc = sc;
	/* No detach method: the published provider and handle stay allocated. */
	device_acquire(sc->sc_dev);
	LIST_INSERT_HEAD(&ad9361_tx_providers, &sc->sc_tx, tx_next);
	mutex_exit(&ad9361_tx_registry_lock);
	return 0;
}

static void
ad9361_tx_attach(device_t self)
{
	struct ad9361_softc *sc = device_private(self);
	bool clock_enabled = false;
	uint32_t reference;
	int error;

	/* The independent hardware gate holds RESETB low: never probe SPI here. */
	sc->sc_refclk = fdtbus_clock_get_index(sc->sc_phandle, 0);
	if (sc->sc_refclk == NULL) {
		error = ENXIO;
		goto fail;
	}
	reference = clk_get_rate(sc->sc_refclk);
	sc->sc_tx_params = kmem_alloc(sizeof(*sc->sc_tx_params), KM_SLEEP);
	error = ad9361_native_params_from_fdt_tx(sc->sc_phandle, reference,
	    sc->sc_tx_params);
	if (error != 0)
		goto fail;
	sc->sc_reset = fdtbus_gpio_acquire(sc->sc_phandle, "reset-gpios",
	    GPIO_PIN_OUTPUT);
	if (sc->sc_reset == NULL) {
		error = ENXIO;
		goto fail;
	}
	mutex_enter(&sc->sc_lock);
	ad9361_native_bus_init_tx(&sc->sc_native, sc, ad9361_native_transfer,
	    ad9361_native_reset);
	error = ad9361_native_reset(sc, false);
	mutex_exit(&sc->sc_lock);
	if (error != 0)
		goto fail;
	error = clk_enable(sc->sc_refclk);
	if (error != 0)
		goto fail;
	clock_enabled = true;
	error = ad9361_tx_register(sc);
	if (error != 0)
		goto fail;
	aprint_normal_dev(self, "TX provider ready; reset held, SPI probe deferred\n");
	return;
fail:
	if (sc->sc_reset != NULL) {
		fdtbus_gpio_write_raw(sc->sc_reset, 0);
		fdtbus_gpio_release(sc->sc_reset);
		sc->sc_reset = NULL;
	}
	if (clock_enabled)
		clk_disable(sc->sc_refclk);
	if (sc->sc_tx_params != NULL) {
		kmem_free(sc->sc_tx_params, sizeof(*sc->sc_tx_params));
		sc->sc_tx_params = NULL;
	}
	aprint_error_dev(self, "TX attachment failed: %d\n", error);
}

int
ad9361_tx_acquire(int phandle, void *owner, struct ad9361_tx **result)
{
	struct ad9361_softc *sc;
	struct ad9361_tx *tx;
	int error = ENXIO;

	if (result == NULL)
		return EINVAL;
	*result = NULL;
	if (phandle <= 0 || owner == NULL)
		return EINVAL;
	RUN_ONCE(&ad9361_tx_registry_once, ad9361_tx_registry_init);
	mutex_enter(&ad9361_tx_registry_lock);
	LIST_FOREACH(tx, &ad9361_tx_providers, tx_next) {
		sc = tx->tx_sc;
		if (sc->sc_phandle != phandle)
			continue;
		mutex_enter(&sc->sc_lock);
		if (sc->sc_tx_poisoned)
			error = EIO;
		else if (sc->sc_tx_owner != NULL)
			error = EBUSY;
		else {
			ad9361_native_bus_init_tx(&sc->sc_native, sc,
			    ad9361_native_transfer, ad9361_native_reset);
			sc->sc_tx_used = false;
			sc->sc_tx_prepared = false;
			sc->sc_tx_owner = owner;
			*result = tx;
			error = 0;
		}
		mutex_exit(&sc->sc_lock);
		break;
	}
	mutex_exit(&ad9361_tx_registry_lock);
	return error;
}

static int
ad9361_tx_enter(struct ad9361_tx *tx, const void *owner,
    struct ad9361_softc **result)
{
	struct ad9361_softc *sc;

	if (tx == NULL || owner == NULL)
		return EINVAL;
	sc = tx->tx_sc;
	mutex_enter(&sc->sc_lock);
	if (sc->sc_tx_owner != owner) {
		mutex_exit(&sc->sc_lock);
		return EPERM;
	}
	*result = sc;
	return 0;
}

static int
ad9361_tx_invalidate(struct ad9361_softc *sc)
{
	int error;

	KASSERT(mutex_owned(&sc->sc_lock));
	error = ad9361_native_destroy_tx(&sc->sc_native, sc->sc_phy);
	sc->sc_phy = NULL;
	sc->sc_tx_prepared = false;
	if (error != 0)
		sc->sc_tx_poisoned = true;
	return error;
}

static int
ad9361_tx_prepare_spi(struct ad9361_softc *sc)
{
	uint8_t id, fast_id;
	int error;

	KASSERT(mutex_owned(&sc->sc_lock));
	/* A previous lease can leave this handle at the requested rate. */
	error = spi_configure(sc->sc_dev, sc->sc_spi, sc->sc_spi_mode,
	    uimin(sc->sc_spi_speed, 1000000));
	if (error != 0)
		return error;
	error = ad9361_native_reset(sc, true);
	if (error != 0)
		return error;
	/* Use the post-RESETB interval from the imported ADI reset sequence. */
	delay(1000);
	error = ad9361_read(sc, AD9361_REG_PRODUCT_ID, &id);
	if (error != 0)
		return error;
	if ((id & AD9361_PRODUCT_ID_MASK) != AD9361_PRODUCT_ID_9361)
		return ENXIO;
	if (sc->sc_spi_speed <= 1000000)
		return 0;
	error = spi_configure(sc->sc_dev, sc->sc_spi, sc->sc_spi_mode,
	    sc->sc_spi_speed);
	if (error != 0)
		return error;
	error = ad9361_read(sc, AD9361_REG_PRODUCT_ID, &fast_id);
	if (error != 0)
		return error;
	return fast_id == id ? 0 : ENXIO;
}

int
ad9361_tx_prepare_reset(struct ad9361_tx *tx, void *owner)
{
	struct ad9361_softc *sc;
	int error;

	error = ad9361_tx_enter(tx, owner, &sc);
	if (error != 0)
		return error;
	if (sc->sc_tx_poisoned)
		error = EIO;
	else if (sc->sc_phy != NULL || sc->sc_tx_prepared || sc->sc_tx_used)
		error = EALREADY;
	else {
		error = ad9361_tx_prepare_spi(sc);
		if (error == 0)
			sc->sc_tx_prepared = true;
		else {
			if (sc->sc_native.first_error == 0)
				sc->sc_native.first_error = error;
			sc->sc_tx_used = true;
			(void)ad9361_tx_invalidate(sc);
		}
	}
	mutex_exit(&sc->sc_lock);
	return error;
}

int
ad9361_tx_bootstrap(struct ad9361_tx *tx, void *owner, uint64_t frequency)
{
	struct ad9361_softc *sc;
	int error;

	error = ad9361_tx_enter(tx, owner, &sc);
	if (error != 0)
		return error;
	if (sc->sc_tx_poisoned)
		error = EIO;
	else if (sc->sc_phy != NULL || sc->sc_tx_used)
		error = EALREADY;
	else if (!sc->sc_tx_prepared)
		error = EINVAL;
	else {
		sc->sc_tx_used = true;
		error = ad9361_native_bootstrap_tx(&sc->sc_native,
		    sc->sc_tx_params, frequency, &sc->sc_phy);
		if (error != 0)
			(void)ad9361_tx_invalidate(sc);
	}
	mutex_exit(&sc->sc_lock);
	return error;
}

enum ad9361_tx_operation {
	AD9361_TX_RETUNE,
	AD9361_TX_FASTLOCK_STAGE,
	AD9361_TX_FASTLOCK_RECALL,
	AD9361_TX_ATTENUATION,
	AD9361_TX_MUTE,
	AD9361_TX_STATUS
};

static int
ad9361_tx_control(struct ad9361_tx *tx, void *owner,
    enum ad9361_tx_operation operation, uint64_t value,
    struct ad9361_tx_status *result)
{
	struct ad9361_native_tx_status status;
	struct ad9361_softc *sc;
	int error;

	error = ad9361_tx_enter(tx, owner, &sc);
	if (error != 0)
		return error;
	if (sc->sc_tx_poisoned) {
		error = EIO;
		goto out;
	}
	if (sc->sc_phy == NULL) {
		error = ENXIO;
		goto out;
	}
	switch (operation) {
	case AD9361_TX_RETUNE:
		error = ad9361_native_retune_tx(&sc->sc_native, sc->sc_phy, value);
		break;
	case AD9361_TX_FASTLOCK_STAGE:
		error = ad9361_native_fastlock_stage_tx(&sc->sc_native, sc->sc_phy,
		    (unsigned int)value);
		break;
	case AD9361_TX_FASTLOCK_RECALL:
		error = ad9361_native_fastlock_recall_tx(&sc->sc_native, sc->sc_phy,
		    (unsigned int)value);
		break;
	case AD9361_TX_ATTENUATION:
		error = ad9361_native_set_tx_attenuation(&sc->sc_native,
		    sc->sc_phy, (uint32_t)value);
		break;
	case AD9361_TX_MUTE:
		error = ad9361_native_mute_tx(&sc->sc_native, sc->sc_phy);
		break;
	case AD9361_TX_STATUS:
		error = ad9361_native_read_tx_status(&sc->sc_native, sc->sc_phy,
		    &status);
		if (error != 0)
			break;
		memset(result, 0, sizeof(*result));
		result->frequency_hz = status.frequency_hz;
		result->sample_rate_hz = status.sample_rate_hz;
		result->attenuation_mdB[0] = status.attenuation_mdB[0];
		result->attenuation_mdB[1] = status.attenuation_mdB[1];
		result->ensm_state = status.ensm_state;
		result->tx_enable = status.tx_enable;
		result->rx_delay = status.rx_delay;
		result->tx_delay = status.tx_delay;
		result->pll_locked = status.pll_locked;
		result->bbpll_locked = status.bbpll_locked;
		result->quadrature_calibrated = status.quadrature_calibrated;
		result->digital_tuning_valid = status.digital_tuning_valid;
		break;
	default:
		error = EINVAL;
		goto out;
	}
	if (error != 0)
		(void)ad9361_tx_invalidate(sc);
out:
	mutex_exit(&sc->sc_lock);
	return error;
}

int
ad9361_tx_lease_fastlock_prepare(struct ad9361_tx *tx, void *owner,
    const uint64_t *frequencies, size_t count)
{
	struct ad9361_softc *sc;
	int error;

	error = ad9361_tx_enter(tx, owner, &sc);
	if (error != 0)
		return error;
	if (sc->sc_tx_poisoned)
		error = EIO;
	else if (sc->sc_phy == NULL)
		error = ENXIO;
	else {
		error = ad9361_native_fastlock_prepare_tx(&sc->sc_native,
		    sc->sc_phy, frequencies, count);
		if (error != 0)
			(void)ad9361_tx_invalidate(sc);
	}
	mutex_exit(&sc->sc_lock);
	return error;
}

int
ad9361_tx_lease_fastlock_stage(struct ad9361_tx *tx, void *owner, unsigned int index)
{

	return ad9361_tx_control(tx, owner, AD9361_TX_FASTLOCK_STAGE, index, NULL);
}

int
ad9361_tx_lease_fastlock_recall(struct ad9361_tx *tx, void *owner, unsigned int index)
{

	return ad9361_tx_control(tx, owner, AD9361_TX_FASTLOCK_RECALL, index, NULL);
}

int
ad9361_tx_retune(struct ad9361_tx *tx, void *owner, uint64_t frequency)
{

	return ad9361_tx_control(tx, owner, AD9361_TX_RETUNE, frequency, NULL);
}

int
ad9361_tx_set_attenuation(struct ad9361_tx *tx, void *owner,
    uint32_t attenuation)
{

	return ad9361_tx_control(tx, owner, AD9361_TX_ATTENUATION, attenuation,
	    NULL);
}

int
ad9361_tx_lease_mute(struct ad9361_tx *tx, void *owner)
{

	return ad9361_tx_control(tx, owner, AD9361_TX_MUTE, 0, NULL);
}

int
ad9361_tx_read_status(struct ad9361_tx *tx, void *owner,
    struct ad9361_tx_status *result)
{

	if (result == NULL)
		return EINVAL;
	return ad9361_tx_control(tx, owner, AD9361_TX_STATUS, 0, result);
}

int
ad9361_tx_release(struct ad9361_tx *tx, void *owner)
{
	struct ad9361_softc *sc;
	int error;

	error = ad9361_tx_enter(tx, owner, &sc);
	if (error != 0)
		return error;
	error = ad9361_tx_invalidate(sc);
	sc->sc_tx_owner = NULL;
	mutex_exit(&sc->sc_lock);
	return error;
}
