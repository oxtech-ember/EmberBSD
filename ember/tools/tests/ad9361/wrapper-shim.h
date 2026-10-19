/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD host facilities for testing the AD9361 TX provider. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include "ad9361reg.h"
typedef void *device_t;
typedef int kmutex_t;
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define KM_SLEEP 0
#define GPIO_PIN_OUTPUT 1
#define SPI_MODE_0 0
#define SPI_MODE_CPHA 1
#define SPI_MODE_CPOL 2
#define KASSERT assert
#define ONCE_DECL(n) int n
#define RUN_ONCE(p, fn) do { if (!*(p)) { assert((fn)() == 0); *(p) = 1; } } while (0)
#define uimin(a, b) ((a) < (b) ? (a) : (b))
#define aprint_error(...) do { } while (0)
#define aprint_error_dev(...) do { } while (0)
#define aprint_naive(...) do { } while (0)
#define aprint_normal(...) do { } while (0)
#define aprint_normal_dev(...) do { } while (0)
static void mutex_init(int *p, int kind, int level) { (void)kind; (void)level; *p = 0; }
static void mutex_enter(int *p) { assert(*p == 0); *p = 1; }
static void mutex_exit(int *p) { assert(*p == 1); *p = 0; }
static int mutex_owned(int *p) { return *p == 1; }
static void *device_private(device_t dev) { return dev; }
static void device_acquire(device_t dev) { assert(dev != NULL); }
static void *kmem_alloc(size_t size, int flags) { (void)flags; void *p = calloc(1, size); assert(p); return p; }
static void kmem_free(void *p, size_t size) { (void)size; free(p); }
struct spi_handle { int mode, speed; };
struct spi_attach_args { struct spi_handle *sa_handle; int sa_cookie; };
struct fdtbus_gpio_pin { int level; bool acquired; };
struct clk { int enabled; };
struct ad9361_rf_phy { uint64_t frequency; uint32_t attenuation; };
typedef struct { uint32_t reference_clk_rate; } AD9361_InitParam;
struct ad9361_native_bus {
    void *cookie;
    int (*transfer)(void *, uint8_t *, size_t);
    int (*reset)(void *, bool);
    int first_error;
};
struct ad9361_native_tx_status {
    uint64_t frequency_hz;
    uint32_t sample_rate_hz, attenuation_mdB[2];
    uint8_t ensm_state, tx_enable, rx_delay, tx_delay;
    bool pll_locked, bbpll_locked, quadrature_calibrated, digital_tuning_valid;
};
static struct fdtbus_gpio_pin pins[32];
static struct clk reference_clock;
static bool tx_property = true, rx_property, clock_missing, gpio_missing;
static bool reset_mismatch, bad_wiring;
static int spi_error, params_error, clock_error, native_error;
static unsigned int probes, configurations, bootstraps, destroys, resets, transfers;
static uint32_t dt_speed = 10000000;
static int dt_mode = SPI_MODE_CPHA | SPI_MODE_CPOL;
static int configured_speeds[128], configured_modes[128], read_speeds[128];
static unsigned int configure_fail_at, read_fail_at, bad_id_at;
static uint8_t bad_id_value;
static int reset_level;
static bool reset_settled;
static int of_hasprop(int phandle, const char *name)
{
    assert(phandle > 0);
    if (!strcmp(name, "adi,tx-enable")) return tx_property;
    if (!strcmp(name, "adi,rx-only")) return rx_property;
    if (!strcmp(name, "spi-3wire")) return bad_wiring;
    if (!strcmp(name, "spi-cpha")) return dt_mode & SPI_MODE_CPHA;
    if (!strcmp(name, "spi-cpol")) return dt_mode & SPI_MODE_CPOL;
    return 0;
}
static int of_getprop_uint32(int phandle, const char *name, uint32_t *value)
{ assert(phandle > 0 && !strcmp(name, "spi-max-frequency")); *value = dt_speed; return 0; }
static int spi_configure(device_t dev, struct spi_handle *spi, int mode, int speed)
{
    assert(dev && configurations < 128 && speed > 0 && speed <= 50000000);
    configured_speeds[configurations] = speed;
    configured_modes[configurations] = mode;
    ++configurations;
    if (spi_error) return spi_error;
    if (configurations == configure_fail_at) return EIO;
    spi->mode = mode;
    spi->speed = speed;
    return 0;
}
static int spi_send_recv(struct spi_handle *spi, size_t count,
    const uint8_t *command, size_t received, uint8_t *value)
{
    assert(count == 2 && command[0] == 0 && command[1] == AD9361_REG_PRODUCT_ID);
    assert(received == 1 && reset_level == 1 && reset_settled && transfers < 128);
    read_speeds[transfers++] = spi->speed;
    if (transfers == read_fail_at) return ETIMEDOUT;
    *value = transfers == bad_id_at ? bad_id_value : AD9361_PRODUCT_ID_9361 | 2;
    return 0;
}
static void delay(unsigned int usec) { assert(usec == 1000 && reset_level == 1); reset_settled = true; }
static void ad9361_probe(device_t dev) { (void)dev; ++probes; }
static void config_interrupts(device_t dev, void (*fn)(device_t)) { fn(dev); }
static struct clk *fdtbus_clock_get_index(int phandle, int index)
{ (void)phandle; assert(index == 0); return clock_missing ? NULL : &reference_clock; }
static uint32_t clk_get_rate(struct clk *clk) { assert(clk); return 40000000; }
static int clk_enable(struct clk *clk) { if (clock_error) return clock_error; ++clk->enabled; return 0; }
static void clk_disable(struct clk *clk) { assert(clk->enabled > 0); --clk->enabled; }
static struct fdtbus_gpio_pin *fdtbus_gpio_acquire(int phandle, const char *name, int flags)
{
    assert(phandle < 32 && !strcmp(name, "reset-gpios") && flags == GPIO_PIN_OUTPUT);
    if (gpio_missing) return NULL;
    assert(!pins[phandle].acquired);
    pins[phandle].acquired = true;
    return &pins[phandle];
}
static void fdtbus_gpio_release(struct fdtbus_gpio_pin *pin) { assert(pin->acquired); pin->acquired = false; }
static void fdtbus_gpio_write_raw(struct fdtbus_gpio_pin *pin, int level)
{ assert(pin->acquired); pin->level = level; reset_level = level; reset_settled = false; ++resets; }
static int fdtbus_gpio_read_raw(struct fdtbus_gpio_pin *pin)
{ assert(pin->acquired); return reset_mismatch ? !pin->level : pin->level; }
static int ad9361_native_transfer(void *cookie, uint8_t *data, size_t size)
{ (void)cookie; (void)data; (void)size; ++transfers; return 0; }
static void ad9361_native_bus_init_tx(struct ad9361_native_bus *bus, void *cookie,
    int (*transfer)(void *, uint8_t *, size_t), int (*reset)(void *, bool))
{ memset(bus, 0, sizeof(*bus)); bus->cookie = cookie; bus->transfer = transfer; bus->reset = reset; }
static int ad9361_native_params_from_fdt_tx(int phandle, uint32_t reference, AD9361_InitParam *params)
{ assert(phandle > 0 && tx_property && !rx_property); params->reference_clk_rate = reference; return params_error; }
static int ad9361_native_destroy_tx(struct ad9361_native_bus *bus, struct ad9361_rf_phy *phy)
{ int error = bus->reset(bus->cookie, false); free(phy); ++destroys; return error; }
static int ad9361_native_bootstrap_tx(struct ad9361_native_bus *bus,
    const AD9361_InitParam *params, uint64_t hz, struct ad9361_rf_phy **phy)
{
    assert(*phy == NULL && params->reference_clk_rate == 40000000 && !bus->first_error);
    ++bootstraps;
    int error = bus->reset(bus->cookie, false);
    if (error == 0) error = bus->reset(bus->cookie, true);
    if (error) return error;
    if (native_error) return native_error;
    *phy = calloc(1, sizeof(**phy)); assert(*phy);
    (*phy)->frequency = hz;
    (*phy)->attenuation = 89750;
    return 0;
}
static int ad9361_native_retune_tx(struct ad9361_native_bus *bus, struct ad9361_rf_phy *phy, uint64_t hz)
{ (void)bus; phy->frequency = hz; return native_error; }
static unsigned int fastlock_calls, fastlock_index;
static int ad9361_native_fastlock_prepare_tx(struct ad9361_native_bus *bus,
    struct ad9361_rf_phy *phy, const uint64_t *frequencies, size_t count)
{ (void)bus; assert(count == 2); ++fastlock_calls; phy->frequency = frequencies[0]; return native_error; }
static int ad9361_native_fastlock_stage_tx(struct ad9361_native_bus *bus,
    struct ad9361_rf_phy *phy, unsigned int index)
{ (void)bus; (void)phy; ++fastlock_calls; fastlock_index = index; return native_error; }
static int ad9361_native_fastlock_recall_tx(struct ad9361_native_bus *bus,
    struct ad9361_rf_phy *phy, unsigned int index)
{ (void)bus; (void)phy; ++fastlock_calls; fastlock_index = index; return native_error; }
static int ad9361_native_set_tx_attenuation(struct ad9361_native_bus *bus, struct ad9361_rf_phy *phy, uint32_t attenuation)
{ (void)bus; phy->attenuation = attenuation; return native_error; }
static int ad9361_native_mute_tx(struct ad9361_native_bus *bus, struct ad9361_rf_phy *phy)
{ return ad9361_native_set_tx_attenuation(bus, phy, 89750); }
static int ad9361_native_read_tx_status(struct ad9361_native_bus *bus,
    struct ad9361_rf_phy *phy, struct ad9361_native_tx_status *status)
{
    (void)bus;
    *status = (struct ad9361_native_tx_status){ .frequency_hz = phy->frequency,
        .sample_rate_hz = 13000000, .attenuation_mdB = {phy->attenuation, phy->attenuation},
        .ensm_state = 10, .tx_enable = 0xc0, .rx_delay = 0x62, .tx_delay = 0x74,
        .pll_locked = true, .bbpll_locked = true, .quadrature_calibrated = true,
        .digital_tuning_valid = false };
    return native_error;
}
