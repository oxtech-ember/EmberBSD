/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 EmberBSD contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP32-S31 GPSPI PIO controller. Peripheral sequencing follows ESP-IDF
 * v6.1 spi_ll.h (fff9895c82d744c7237be8847347bdd1b07c6643).
 * The FDT/SPI integration is specific to EmberBSD, not an IDF application.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$EmberBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <machine/sysreg.h>
#include <dev/clk/clk_backend.h>
#include <dev/fdt/fdtvar.h>
#include <dev/fdt/syscon.h>
#include <dev/spi/spivar.h>
#include <riscv/espressif/esp_spireg.h>

#define ESPSPI_TRACE_COMMANDS 24
#define ESPSPI_TRACE_SAMPLE 8
#define ESPSPI_TRACE_TEXT_SIZE 16384

struct esp_spi_trace_block {
	u_int block;
	u_int count;
	u_int width;
	uint32_t ctrl, user, misc, dlen;
	uint32_t dma_before_reset, dma_asserted, dma_released;
	uint32_t raw_before_clear, raw_after_clear, raw_done;
	uint32_t cmd_before_update, cmd_after_update, cmd_after_usr;
	int error;
	uint8_t first[4], last[4];
};

struct esp_spi_trace_chunk {
	bool valid, hash_valid, time_valid;
	u_int sequence, index, len, width;
	uint32_t hash;
	uint32_t csr_start, csr_end;
	uint64_t mono_start_us, mono_end_us;
	uint8_t first[ESPSPI_TRACE_SAMPLE], last[ESPSPI_TRACE_SAMPLE];
	struct esp_spi_trace_block first_block, last_block;
};

struct esp_spi_trace_snapshot {
	u_int sequence, command_count;
	struct esp_spi_trace_chunk commands[ESPSPI_TRACE_COMMANDS];
	struct esp_spi_trace_chunk pixel, latest;
	struct esp_spi_trace_block error_block;
	bool error_block_valid;
	int last_error;
};

struct esp_spi_softc {
	device_t sc_dev;
	bus_space_tag_t sc_bst;
	bus_space_handle_t sc_bsh;
	struct syscon *sc_syscon;
	struct spi_controller sc_spi;
	bus_size_t sc_clkrst;
	u_int sc_source_hz;
	u_int sc_speed;
	uint32_t sc_clock;
	uint32_t sc_user;
	uint32_t sc_misc;
	struct sysctllog *sc_sysctllog;
	kmutex_t sc_trace_lock;
	u_int sc_trace_sequence, sc_trace_command_count;
	struct esp_spi_trace_chunk sc_trace_commands[ESPSPI_TRACE_COMMANDS];
	struct esp_spi_trace_chunk sc_trace_pixel, sc_trace_latest;
	struct esp_spi_trace_block sc_trace_error_block;
	bool sc_trace_error_block_valid;
	int sc_trace_last_error;
};

#define ESPSPI_READ(sc, reg) \
	bus_space_read_4((sc)->sc_bst, (sc)->sc_bsh, (reg))
#define ESPSPI_WRITE(sc, reg, value) \
	bus_space_write_4((sc)->sc_bst, (sc)->sc_bsh, (reg), (value))

static int esp_spi_match(device_t, cfdata_t, void *);
static void esp_spi_attach(device_t, device_t, void *);
static int esp_spi_configure(void *, int, int, int);
static int esp_spi_transfer(void *, struct spi_transfer *);
static struct spi_controller *esp_spi_get_controller(device_t);

static uint32_t
esp_spi_trace_hash(const uint8_t *data, size_t len)
{
	uint32_t hash = UINT32_C(2166136261);

	for (size_t i = 0; i < len; i++) {
		hash = (hash ^ data[i]) * UINT32_C(16777619);
	}
	return hash;
}

static uint64_t
esp_spi_uptime_us(void)
{
	struct timespec ts;

	getnanouptime(&ts);
	return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void
esp_spi_trace_time_start(struct esp_spi_trace_chunk *trace)
{
	trace->csr_start = csr_time_read();
	trace->mono_start_us = esp_spi_uptime_us();
	trace->time_valid = true;
}

static void
esp_spi_trace_time_end(struct esp_spi_trace_chunk *trace)
{
	trace->mono_end_us = esp_spi_uptime_us();
	trace->csr_end = csr_time_read();
}

CFATTACH_DECL_NEW(espspi, sizeof(struct esp_spi_softc),
    esp_spi_match, esp_spi_attach, NULL, NULL);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "espressif,esp32s31-spi2", .value = ESPSPI2_CLKRST },
	{ .compat = "espressif,esp32s31-spi3", .value = ESPSPI3_CLKRST },
	DEVICE_COMPAT_EOL
};

static const struct fdtbus_spi_controller_func esp_spi_funcs = {
	.get_controller = esp_spi_get_controller,
};

/* This is a best-effort snapshot; a transfer may change registers between reads. */
static int
esp_spi_sysctl_registers(SYSCTLFN_ARGS)
{
	struct esp_spi_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	uint32_t clkrst;
	char status[256];

	syscon_lock(sc->sc_syscon);
	clkrst = syscon_read_4(sc->sc_syscon, sc->sc_clkrst);
	syscon_unlock(sc->sc_syscon);
	(void)snprintf(status, sizeof(status),
	    "clkrst=%08x gate=%08x cmd=%08x ctrl=%08x clock=%08x "
	    "user=%08x user1=%08x user2=%08x misc=%08x int_raw=%08x",
	    clkrst, ESPSPI_READ(sc, ESPSPI_CLK_GATE),
	    ESPSPI_READ(sc, ESPSPI_CMD), ESPSPI_READ(sc, ESPSPI_CTRL),
	    ESPSPI_READ(sc, ESPSPI_CLOCK), ESPSPI_READ(sc, ESPSPI_USER),
	    ESPSPI_READ(sc, ESPSPI_USER1), ESPSPI_READ(sc, ESPSPI_USER2),
	    ESPSPI_READ(sc, ESPSPI_MISC), ESPSPI_READ(sc, ESPSPI_INT_RAW));
	node.sysctl_data = status;
	node.sysctl_size = strlen(status) + 1;
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static int
esp_spi_sysctl_trace(SYSCTLFN_ARGS)
{
	struct esp_spi_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	struct esp_spi_trace_snapshot *snapshot;
	char *status;
	size_t off = 0;
	int error = 0;

	snapshot = kmem_alloc(sizeof(*snapshot), KM_SLEEP);
	status = kmem_alloc(ESPSPI_TRACE_TEXT_SIZE, KM_SLEEP);
	mutex_enter(&sc->sc_trace_lock);
	snapshot->sequence = sc->sc_trace_sequence;
	snapshot->command_count = sc->sc_trace_command_count;
	memcpy(snapshot->commands, sc->sc_trace_commands,
	    sizeof(snapshot->commands));
	snapshot->pixel = sc->sc_trace_pixel;
	snapshot->latest = sc->sc_trace_latest;
	snapshot->error_block = sc->sc_trace_error_block;
	snapshot->error_block_valid = sc->sc_trace_error_block_valid;
	snapshot->last_error = sc->sc_trace_last_error;
	mutex_exit(&sc->sc_trace_lock);

#define TRACE_APPEND(...) do { \
	if (off >= ESPSPI_TRACE_TEXT_SIZE) { error = EOVERFLOW; goto out; } \
	int n = snprintf(status + off, ESPSPI_TRACE_TEXT_SIZE - off, \
	    __VA_ARGS__); \
	if (n < 0 || (size_t)n >= ESPSPI_TRACE_TEXT_SIZE - off) { \
		error = EOVERFLOW; goto out; \
	} \
	off += (size_t)n; \
	} while (0)

	TRACE_APPEND("seq=%u error=%d", snapshot->sequence,
	    snapshot->last_error);
	for (u_int i = 0; i < snapshot->command_count; i++) {
		const struct esp_spi_trace_chunk *c = &snapshot->commands[i];
		const struct esp_spi_trace_block *b = &c->first_block;
		TRACE_APPEND(
		    " cmd%u=%u.%u/%u/w%u/h%u:%08x/%02x%02x%02x%02x%02x%02x%02x%02x-"
		    "%02x%02x%02x%02x%02x%02x%02x%02x/"
		    "c%08x-u%08x-m%08x-d%u-t%u:%08x>%08x/%llu>%llu-"
		    "dma%08x>%08x>%08x-irq%08x>%08x/%08x-cmd%08x>%08x>%08x-e%d",
		    i, c->sequence, c->index, c->len, c->width,
		    c->hash_valid, c->hash,
		    c->first[0], c->first[1], c->first[2], c->first[3],
		    c->first[4], c->first[5], c->first[6], c->first[7],
		    c->last[0], c->last[1], c->last[2], c->last[3],
		    c->last[4], c->last[5], c->last[6], c->last[7],
		    b->ctrl, b->user, b->misc, b->dlen, c->time_valid, c->csr_start,
		    c->csr_end, (unsigned long long)c->mono_start_us,
		    (unsigned long long)c->mono_end_us,
		    b->dma_before_reset, b->dma_asserted, b->dma_released,
		    b->raw_before_clear,
		    b->raw_after_clear, b->raw_done, b->cmd_before_update,
		    b->cmd_after_update, b->cmd_after_usr, b->error);
	}
	if (snapshot->pixel.valid) {
		const struct esp_spi_trace_chunk *c = &snapshot->pixel;
		const struct esp_spi_trace_block *f = &c->first_block;
		const struct esp_spi_trace_block *last = &c->last_block;
		TRACE_APPEND(
		    " pixel=%u.%u/%u/w%u/h%u:%08x/%02x%02x%02x%02x%02x%02x%02x%02x-"
		    "%02x%02x%02x%02x%02x%02x%02x%02x/"
		    "t%u:%08x>%08x/%llu>%llu/"
		    "f%u+%u/w%u:%02x%02x%02x%02x-%02x%02x%02x%02x/"
		    "c%08x-u%08x-m%08x-d%u-dma%08x>%08x>%08x-"
		    "irq%08x>%08x/%08x-cmd%08x>%08x>%08x-e%d/"
		    "l%u+%u/w%u:%02x%02x%02x%02x-%02x%02x%02x%02x/"
		    "c%08x-u%08x-m%08x-d%u-dma%08x>%08x>%08x-"
		    "irq%08x>%08x/%08x-cmd%08x>%08x>%08x-e%d",
		    c->sequence, c->index, c->len, c->width,
		    c->hash_valid, c->hash,
		    c->first[0], c->first[1], c->first[2], c->first[3],
		    c->first[4], c->first[5], c->first[6], c->first[7],
		    c->last[0], c->last[1], c->last[2], c->last[3],
		    c->last[4], c->last[5], c->last[6], c->last[7],
		    c->time_valid, c->csr_start, c->csr_end,
		    (unsigned long long)c->mono_start_us,
		    (unsigned long long)c->mono_end_us,
		    f->block, f->count, f->width,
		    f->first[0], f->first[1], f->first[2], f->first[3],
		    f->last[0], f->last[1], f->last[2], f->last[3],
		    f->ctrl, f->user, f->misc, f->dlen,
		    f->dma_before_reset, f->dma_asserted, f->dma_released,
		    f->raw_before_clear,
		    f->raw_after_clear, f->raw_done, f->cmd_before_update,
		    f->cmd_after_update, f->cmd_after_usr, f->error,
		    last->block, last->count, last->width,
		    last->first[0], last->first[1], last->first[2], last->first[3],
		    last->last[0], last->last[1], last->last[2], last->last[3],
		    last->ctrl, last->user, last->misc, last->dlen,
		    last->dma_before_reset, last->dma_asserted, last->dma_released,
		    last->raw_before_clear,
		    last->raw_after_clear, last->raw_done, last->cmd_before_update,
		    last->cmd_after_update, last->cmd_after_usr, last->error);
	}
	if (snapshot->error_block_valid) {
		const struct esp_spi_trace_block *b = &snapshot->error_block;
		TRACE_APPEND(" fail=%u+%u/w%u:%02x%02x%02x%02x-"
		    "%02x%02x%02x%02x/c%08x-u%08x-m%08x-d%u-"
		    "dma%08x-irq%08x-cmd%08x-e%d", b->block, b->count,
		    b->width, b->first[0], b->first[1], b->first[2], b->first[3],
		    b->last[0], b->last[1], b->last[2], b->last[3],
		    b->ctrl, b->user, b->misc, b->dlen,
		    b->dma_released, b->raw_done, b->cmd_after_usr, b->error);
	}
	if (snapshot->latest.valid) {
		const struct esp_spi_trace_chunk *c = &snapshot->latest;
		TRACE_APPEND(
		    " latest=%u.%u/%u/w%u/h%u:%08x/%02x%02x%02x%02x%02x%02x%02x%02x-"
		    "%02x%02x%02x%02x%02x%02x%02x%02x",
		    c->sequence, c->index, c->len, c->width,
		    c->hash_valid, c->hash,
		    c->first[0], c->first[1], c->first[2], c->first[3],
		    c->first[4], c->first[5], c->first[6], c->first[7],
		    c->last[0], c->last[1], c->last[2], c->last[3],
		    c->last[4], c->last[5], c->last[6], c->last[7]);
	}

	out:
	if (error == 0) {
		node.sysctl_data = status;
		node.sysctl_size = strlen(status) + 1;
		error = sysctl_lookup(SYSCTLFN_CALL(&node));
	}
	kmem_free(status, ESPSPI_TRACE_TEXT_SIZE);
	kmem_free(snapshot, sizeof(*snapshot));
#undef TRACE_APPEND
	return error;
}

static void
esp_spi_sysctl_init(struct esp_spi_softc *sc)
{
	const struct sysctlnode *node;
	int error;

	error = sysctl_createv(&sc->sc_sysctllog, 0, NULL, &node,
	    CTLFLAG_READONLY, CTLTYPE_NODE, device_xname(sc->sc_dev),
	    SYSCTL_DESCR("ESP32-S31 SPI controller"), NULL, 0, NULL, 0,
	    CTL_HW, CTL_CREATE, CTL_EOL);
	if (error == 0)
		error = sysctl_createv(&sc->sc_sysctllog, 0, NULL, NULL,
		    CTLFLAG_READONLY, CTLTYPE_STRING, "registers",
		    SYSCTL_DESCR("SPI register snapshot"),
		    esp_spi_sysctl_registers, 0, (char *)sc, 0,
		    CTL_HW, node->sysctl_num, CTL_CREATE, CTL_EOL);
	if (error == 0)
		error = sysctl_createv(&sc->sc_sysctllog, 0, NULL, NULL,
		    CTLFLAG_READONLY | CTLFLAG_PRIVATE, CTLTYPE_STRING, "trace",
		    SYSCTL_DESCR("Bounded SPI command and pixel transfer trace"),
		    esp_spi_sysctl_trace, 0, (char *)sc, 0,
		    CTL_HW, node->sysctl_num, CTL_CREATE, CTL_EOL);
	if (error != 0) {
		sysctl_teardown(&sc->sc_sysctllog);
		aprint_error_dev(sc->sc_dev, "cannot publish registers: %d\n", error);
	}
}

static struct spi_controller *
esp_spi_get_controller(device_t dev)
{
	struct esp_spi_softc *sc = device_private(dev);

	return &sc->sc_spi;
}

static int
esp_spi_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

/* Reset only this GPSPI instance; shared clock registers use syscon locking. */
static void
esp_spi_reset(struct esp_spi_softc *sc)
{
	uint32_t control;

	syscon_lock(sc->sc_syscon);
	control = syscon_read_4(sc->sc_syscon, sc->sc_clkrst);
	control &= ~(ESPSPI_CLKRST_DIVSEL | ESPSPI_CLKRST_RESET);
	control |= ESPSPI_CLKRST_ENABLE;
	syscon_write_4(sc->sc_syscon, sc->sc_clkrst,
	    control | ESPSPI_CLKRST_RESET);
	(void)syscon_read_4(sc->sc_syscon, sc->sc_clkrst);
	syscon_write_4(sc->sc_syscon, sc->sc_clkrst, control);
	syscon_unlock(sc->sc_syscon);

	/* XTAL source, both upstream dividers one, no DMA or interrupts. */
	ESPSPI_WRITE(sc, ESPSPI_CLK_GATE, ESPSPI_CLK_ENABLE);
	ESPSPI_WRITE(sc, ESPSPI_SLAVE, 0);
	ESPSPI_WRITE(sc, ESPSPI_CTRL, ESPSPI_CTRL_IDLE_HIGH);
	/* Keep CS active for at least one SPI clock after the final edge. */
	ESPSPI_WRITE(sc, ESPSPI_USER1, ESPSPI_CS_HOLD_TIME(1));
	ESPSPI_WRITE(sc, ESPSPI_USER2, 0);
	ESPSPI_WRITE(sc, ESPSPI_DMA_CONF, ESPSPI_DMA_BASE);
	ESPSPI_WRITE(sc, ESPSPI_INT_ENA, 0);
	ESPSPI_WRITE(sc, ESPSPI_INT_CLR, UINT32_MAX);
	ESPSPI_WRITE(sc, ESPSPI_CLOCK, sc->sc_clock);
	ESPSPI_WRITE(sc, ESPSPI_USER, sc->sc_user);
	ESPSPI_WRITE(sc, ESPSPI_MISC, sc->sc_misc);
}

static void
esp_spi_attach(device_t parent, device_t self, void *aux)
{
	struct esp_spi_softc *sc = device_private(self);
	struct fdt_attach_args *faa = aux;
	const struct device_compatible_entry *dce;
	struct clk *xtal;
	bus_addr_t addr;
	bus_size_t size;
	int phandle = faa->faa_phandle;

	sc->sc_dev = self;
	sc->sc_bst = faa->faa_bst;
	dce = of_compatible_lookup(phandle, compat_data);
	sc->sc_clkrst = dce->value;
	sc->sc_syscon = fdtbus_syscon_acquire(phandle, "espressif,syscon");
	xtal = fdtbus_clock_get(phandle, "xtal");
	if (sc->sc_syscon == NULL || xtal == NULL ||
	    (sc->sc_source_hz = clk_get_rate(xtal)) == 0) {
		aprint_error(": missing clock/reset controller or XTAL clock\n");
		return;
	}
	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0 ||
	    size < ESPSPI_REG_SIZE ||
	    bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_bsh) != 0) {
		aprint_error(": cannot map registers\n");
		return;
	}
	sc->sc_clock = ESPSPI_CLOCK_DIRECT;
	sc->sc_misc = ESPSPI_CS_DISABLE;
	esp_spi_reset(sc);
	if (fdtbus_pinctrl_set_config_index(phandle, 0) != 0) {
		aprint_error(": cannot configure pins\n");
		bus_space_unmap(sc->sc_bst, sc->sc_bsh, size);
		return;
	}
	mutex_init(&sc->sc_trace_lock, MUTEX_DEFAULT, IPL_NONE);
	sc->sc_spi.sct_cookie = sc;
	sc->sc_spi.sct_nslaves = sc->sc_clkrst == ESPSPI2_CLKRST ? 6 : 3;
	sc->sc_spi.sct_configure = esp_spi_configure;
	sc->sc_spi.sct_transfer = esp_spi_transfer;
	sc->sc_spi.sct_caps = SPI_CAP_DUAL_TX | SPI_CAP_QUAD_TX |
	    SPI_CAP_3WIRE;
	aprint_normal(": GPSPI PIO, XTAL %u Hz\n", sc->sc_source_hz);
	esp_spi_sysctl_init(sc);
	fdtbus_register_spi_controller(self, phandle, &esp_spi_funcs);
	fdtbus_attach_spibus(self, phandle, spibus_print);
}

/* Select the fastest divider that does not exceed the peripheral's limit. */
static int
esp_spi_clock(u_int source, u_int limit, uint32_t *value, u_int *actual)
{
	u_int best = 0;
	uint32_t selected = 0;

	if (source == 0 || limit == 0)
		return EINVAL;
	if (limit >= source) {
		*value = ESPSPI_CLOCK_DIRECT;
		*actual = source;
		return 0;
	}
	for (u_int n = 2; n <= 64; n++) {
		for (u_int pre = 1; pre <= 16; pre++) {
			u_int candidate_hz = source / (pre * n);
			if (source > (uint64_t)limit * pre * n ||
			    candidate_hz <= best)
				continue;
			best = candidate_hz;
			selected = ((pre - 1) << 18) | ((n - 1) << 12) |
			    ((n / 2 - 1) << 6) | (n - 1);
		}
	}
	if (best == 0)
		return EINVAL;
	*value = selected;
	*actual = best;
	return 0;
}

static int
esp_spi_configure(void *cookie, int slave, int mode, int speed)
{
	struct esp_spi_softc *sc = cookie;
	int error;

	if (slave < 0 || slave >= sc->sc_spi.sct_nslaves ||
	    mode < SPI_MODE_0 || mode > SPI_MODE_3 || speed <= 0)
		return EINVAL;
	error = esp_spi_clock(sc->sc_source_hz, speed,
	    &sc->sc_clock, &sc->sc_speed);
	if (error != 0)
		return error;
	sc->sc_misc = ESPSPI_CS_DISABLE & ~__BIT(slave);
	if (mode & SPI_MODE_CPOL)
		sc->sc_misc |= ESPSPI_CK_IDLE_EDGE;
	sc->sc_user = ESPSPI_CS_HOLD |
	    ((mode == SPI_MODE_1 || mode == SPI_MODE_2) ?
	    ESPSPI_CK_OUT_EDGE : 0);
	ESPSPI_WRITE(sc, ESPSPI_CLOCK, sc->sc_clock);
	ESPSPI_WRITE(sc, ESPSPI_MISC, sc->sc_misc);
	return 0;
}

static int
esp_spi_wait(struct esp_spi_softc *sc, bus_size_t reg, uint32_t mask,
    bool set, u_int timeout)
{
	for (u_int i = 0; i < timeout; i++) {
		if (((ESPSPI_READ(sc, reg) & mask) != 0) == set)
			return 0;
		delay(1);
	}
	return ETIMEDOUT;
}

static void
esp_spi_trace_init(struct esp_spi_trace_chunk *trace, u_int sequence,
	 u_int index, const uint8_t *tx, u_int len, u_int width,
	 bool fingerprint)
{
	memset(trace, 0, sizeof(*trace));
	trace->valid = true;
	trace->sequence = sequence;
	trace->index = index;
	trace->len = len;
	trace->width = width;
	if (tx == NULL || len == 0)
		return;
	if (fingerprint) {
		trace->hash = esp_spi_trace_hash(tx, len);
		trace->hash_valid = true;
	}
	memcpy(trace->first, tx, MIN(len, sizeof(trace->first)));
	memcpy(trace->last, tx + len - MIN(len, sizeof(trace->last)),
	    MIN(len, sizeof(trace->last)));
}

static int
esp_spi_block(struct esp_spi_softc *sc, const uint8_t *tx, uint8_t *rx,
	 u_int count, u_int width, bool more, bool sio,
	 struct esp_spi_trace_block *trace,
	 struct esp_spi_trace_block *error_trace, u_int block_index)
{
	uint32_t user = sc->sc_user, word;
	uint32_t ctrl = ESPSPI_READ(sc, ESPSPI_CTRL) &
	    ~ESPSPI_CTRL_DATA_MODE_MASK;
	uint32_t raw_before_clear = 0, raw_after_clear = 0;
	uint32_t cmd_before_update = 0, cmd_after_update = 0;
	uint32_t cmd_after_usr = 0, raw_done = 0;
	uint32_t dma_before_reset = 0, dma_asserted = 0, dma_released = 0;
	int error;

	KASSERT(count > 0 && count <= ESPSPI_FIFO_SIZE);
	if (tx != NULL || rx == NULL)
		user |= ESPSPI_USR_MOSI;
	if (rx != NULL)
		user |= ESPSPI_USR_MISO;
	if (tx != NULL && rx != NULL)
		user |= ESPSPI_DOUTDIN;
	if (sio)
		user |= ESPSPI_SIO;
	if (width == 2) {
		user |= ESPSPI_FWRITE_DUAL;
		ctrl |= ESPSPI_FREAD_DUAL;
	}
	if (width == 4) {
		user |= ESPSPI_FWRITE_QUAD;
		ctrl |= ESPSPI_FREAD_QUAD;
	}

	if (trace != NULL)
		dma_before_reset = ESPSPI_READ(sc, ESPSPI_DMA_CONF);
	ESPSPI_WRITE(sc, ESPSPI_DMA_CONF, ESPSPI_DMA_BASE |
	    ESPSPI_TX_FIFO_RESET | ESPSPI_RX_FIFO_RESET);
	if (trace != NULL)
		dma_asserted = ESPSPI_READ(sc, ESPSPI_DMA_CONF);
	ESPSPI_WRITE(sc, ESPSPI_DMA_CONF, ESPSPI_DMA_BASE);
	if (trace != NULL)
		dma_released = ESPSPI_READ(sc, ESPSPI_DMA_CONF);
	for (u_int n = 0; n < count; n += 4) {
		word = UINT32_MAX;
		if (tx != NULL)
			memcpy(&word, tx + n, MIN(4, count - n));
		ESPSPI_WRITE(sc, ESPSPI_W0 + n, word);
	}
	ESPSPI_WRITE(sc, ESPSPI_CTRL, ctrl);
	ESPSPI_WRITE(sc, ESPSPI_USER, user);
	ESPSPI_WRITE(sc, ESPSPI_DLEN, count * 8 - 1);
	ESPSPI_WRITE(sc, ESPSPI_MISC, sc->sc_misc |
	    (more ? ESPSPI_CS_KEEP_ACTIVE : 0));
	if (trace != NULL) {
		trace->block = block_index;
		trace->count = count;
		trace->width = width;
		trace->ctrl = ESPSPI_READ(sc, ESPSPI_CTRL);
		trace->user = ESPSPI_READ(sc, ESPSPI_USER);
		trace->misc = ESPSPI_READ(sc, ESPSPI_MISC);
		trace->dlen = ESPSPI_READ(sc, ESPSPI_DLEN);
		raw_before_clear = ESPSPI_READ(sc, ESPSPI_INT_RAW);
		cmd_before_update = ESPSPI_READ(sc, ESPSPI_CMD);
	}
	ESPSPI_WRITE(sc, ESPSPI_INT_CLR, UINT32_MAX);
	if (trace != NULL)
		raw_after_clear = ESPSPI_READ(sc, ESPSPI_INT_RAW);
	ESPSPI_WRITE(sc, ESPSPI_CMD, ESPSPI_UPDATE);
	error = esp_spi_wait(sc, ESPSPI_CMD, ESPSPI_UPDATE, false, 1000);
	if (trace != NULL)
		cmd_after_update = ESPSPI_READ(sc, ESPSPI_CMD);
	if (error != 0)
		goto out;
	ESPSPI_WRITE(sc, ESPSPI_CMD, ESPSPI_USR);
	error = esp_spi_wait(sc, ESPSPI_INT_RAW, ESPSPI_TRANS_DONE, true,
	    1000 + 2 * (ESPSPI_FIFO_SIZE * 8000000U / sc->sc_speed));
	if (trace != NULL) {
		cmd_after_usr = ESPSPI_READ(sc, ESPSPI_CMD);
		raw_done = ESPSPI_READ(sc, ESPSPI_INT_RAW);
	}
	if (error == 0 && rx != NULL) {
		for (u_int n = 0; n < count; n += 4) {
			word = ESPSPI_READ(sc, ESPSPI_W0 + n);
			memcpy(rx + n, &word, MIN(4, count - n));
		}
	}
	out:
	if (trace != NULL) {
		trace->raw_before_clear = raw_before_clear;
		trace->raw_after_clear = raw_after_clear;
		trace->cmd_before_update = cmd_before_update;
		trace->cmd_after_update = cmd_after_update;
		trace->cmd_after_usr = cmd_after_usr;
		trace->raw_done = raw_done;
		trace->dma_before_reset = dma_before_reset;
		trace->dma_asserted = dma_asserted;
		trace->dma_released = dma_released;
		trace->error = error;
		if (tx != NULL && count != 0) {
			memcpy(trace->first, tx,
			    MIN(count, sizeof(trace->first)));
			memcpy(trace->last,
			    tx + count - MIN(count, sizeof(trace->last)),
			    MIN(count, sizeof(trace->last)));
		}
	}
	if (error != 0 && error_trace != NULL) {
		memset(error_trace, 0, sizeof(*error_trace));
		error_trace->block = block_index;
		error_trace->count = count;
		error_trace->width = width;
		error_trace->ctrl = ESPSPI_READ(sc, ESPSPI_CTRL);
		error_trace->user = ESPSPI_READ(sc, ESPSPI_USER);
		error_trace->misc = ESPSPI_READ(sc, ESPSPI_MISC);
		error_trace->dlen = ESPSPI_READ(sc, ESPSPI_DLEN);
		error_trace->dma_released = ESPSPI_READ(sc, ESPSPI_DMA_CONF);
		error_trace->raw_done = ESPSPI_READ(sc, ESPSPI_INT_RAW);
		error_trace->cmd_after_usr = ESPSPI_READ(sc, ESPSPI_CMD);
		error_trace->error = error;
		if (tx != NULL) {
			memcpy(error_trace->first, tx,
			    MIN(count, sizeof(error_trace->first)));
			memcpy(error_trace->last,
			    tx + count - MIN(count, sizeof(error_trace->last)),
			    MIN(count, sizeof(error_trace->last)));
		}
	}
	return error;
}

static int
esp_spi_transfer(void *cookie, struct spi_transfer *st)
{
	struct esp_spi_softc *sc = cookie;
	struct spi_chunk *chunk, *last = NULL;
	u_int sequence, chunk_index = 0;
	int error = 0;

	mutex_enter(&sc->sc_trace_lock);
	sequence = ++sc->sc_trace_sequence;
	mutex_exit(&sc->sc_trace_lock);

	/* Conservative GPIO-matrix receive timing; multi-line RX is not exposed. */
	for (chunk = st->st_chunks; chunk; chunk = chunk->chunk_next) {
		if (chunk->chunk_count < 0 || (chunk->chunk_read != NULL &&
		    sc->sc_speed > 10000000)) {
			mutex_enter(&sc->sc_trace_lock);
			sc->sc_trace_last_error = EINVAL;
			mutex_exit(&sc->sc_trace_lock);
			spi_done(st, EINVAL);
			return 0;
		}
		if (chunk->chunk_count > 0)
			last = chunk;
	}
	for (chunk = st->st_chunks; chunk && error == 0;
	    chunk = chunk->chunk_next) {
		struct esp_spi_trace_chunk trace;
		struct esp_spi_trace_block block_trace = { 0 };
		struct esp_spi_trace_block error_trace = { 0 };
		bool trace_init = false, trace_pixel = false;
		u_int original_len = chunk->chunk_count > 0 ?
		    (u_int)chunk->chunk_count : 0;
		const uint8_t *original_tx = chunk->chunk_write;
		bool nonzero = false;

		if (original_tx != NULL && original_len != 0) {
			mutex_enter(&sc->sc_trace_lock);
			trace_init = chunk->chunk_bus_width == 1 &&
			    original_len <= 32 && sc->sc_trace_command_count <
			    ESPSPI_TRACE_COMMANDS;
			nonzero = chunk->chunk_bus_width > 1 &&
			    !sc->sc_trace_pixel.valid;
			mutex_exit(&sc->sc_trace_lock);
			if (nonzero) {
				for (u_int i = 0; i < original_len; i++)
					if (original_tx[i] != 0) {
						trace_pixel = true;
						break;
					}
			}
			esp_spi_trace_init(&trace, sequence, chunk_index,
			    original_tx, original_len, chunk->chunk_bus_width,
			    trace_init || trace_pixel);
			if (trace_init || trace_pixel)
				esp_spi_trace_time_start(&trace);
		} else {
			memset(&trace, 0, sizeof(trace));
		}
		memset(&trace.first_block, 0, sizeof(trace.first_block));
		memset(&trace.last_block, 0, sizeof(trace.last_block));
		u_int block_index = 0;
		while (chunk->chunk_wresid > 0) {
			u_int count = MIN(chunk->chunk_wresid, ESPSPI_FIFO_SIZE);
			bool more = chunk != last || chunk->chunk_wresid > count;
			bool capture_block = trace_init || (trace_pixel &&
			    (block_index == 0 || chunk->chunk_wresid <= count));
			struct esp_spi_trace_block *block =
			    capture_block ? &block_trace : NULL;
			if (block != NULL)
				memset(block, 0, sizeof(*block));
			error = esp_spi_block(sc, chunk->chunk_wptr,
			    chunk->chunk_rptr, count, chunk->chunk_bus_width, more,
			    (st->st_flags & SPI_F_3WIRE) != 0, block,
			    &error_trace, block_index);
			if (block != NULL) {
				if (block_index == 0)
					trace.first_block = block_trace;
				trace.last_block = block_trace;
			}
			if (error != 0)
			{
				mutex_enter(&sc->sc_trace_lock);
				sc->sc_trace_error_block = error_trace;
				sc->sc_trace_error_block_valid = true;
				mutex_exit(&sc->sc_trace_lock);
				break;
			}
			chunk->chunk_wresid -= count;
			chunk->chunk_rresid -= count;
			if (chunk->chunk_wptr != NULL)
				chunk->chunk_wptr += count;
			if (chunk->chunk_rptr != NULL)
				chunk->chunk_rptr += count;
			block_index++;
		}
		trace.valid = original_tx != NULL && original_len != 0;
		if (trace.valid) {
			if (trace.time_valid)
				esp_spi_trace_time_end(&trace);
			mutex_enter(&sc->sc_trace_lock);
			sc->sc_trace_latest = trace;
			if (trace_init && sc->sc_trace_command_count <
			    ESPSPI_TRACE_COMMANDS)
				sc->sc_trace_commands[sc->sc_trace_command_count++] = trace;
			if (trace_pixel && !sc->sc_trace_pixel.valid)
				sc->sc_trace_pixel = trace;
			mutex_exit(&sc->sc_trace_lock);
		}
		chunk_index++;
	}
	if (error != 0) {
		mutex_enter(&sc->sc_trace_lock);
		sc->sc_trace_last_error = error;
		mutex_exit(&sc->sc_trace_lock);
		esp_spi_reset(sc); /* Release CS and restore the selected configuration. */
	}
	spi_done(st, error);
	return 0;
}
