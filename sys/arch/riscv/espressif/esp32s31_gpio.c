/* SPDX-License-Identifier: BSD-2-Clause */
/* ESP32-S31 GPIO matrix and IO_MUX. Register layout: ESP-IDF v6.1,
 * components/soc/esp32s31/register/soc/{gpio,io_mux,hp_sys_clkrst,lp_system}_reg.h
 * (Apache-2.0 OR MIT); GPIO validity from soc_caps.h. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/gpio.h>
#include <sys/intr.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <dev/fdt/fdtvar.h>
#include <dev/fdt/syscon.h>
#include "esp32s31_gpio_logic.h"

#define GPIO_OUT 0x004
#define GPIO_OUT_SET 0x008
#define GPIO_OUT_CLR 0x00c
#define GPIO_OUT1 0x010
#define GPIO_OUT1_SET 0x014
#define GPIO_OUT1_CLR 0x018
#define GPIO_EN 0x034
#define GPIO_EN_SET 0x038
#define GPIO_EN_CLR 0x03c
#define GPIO_EN1 0x040
#define GPIO_EN1_SET 0x044
#define GPIO_EN1_CLR 0x048
#define GPIO_IN 0x064
#define GPIO_IN1 0x068
#define GPIO_PIN(n) (0x0f4 + 4 * (n))
#define GPIO_INSEL(n) (0x2f4 + 4 * (n))
#define GPIO_OUTSEL(n) (0xaf4 + 4 * (n))
#define GPIO_PAD_OD __BIT(2)
#define GPIO_IN_MATRIX __BIT(9)
#define GPIO_IN_FIELDS (__BITS(7, 0) | __BIT(8) | GPIO_IN_MATRIX)
#define GPIO_OUT_GPIO 256
#define GPIO_OUT_OEN_GPIO __BIT(10)
#define GPIO_OUT_FIELDS (__BITS(8, 0) | __BIT(9) | GPIO_OUT_OEN_GPIO | __BIT(11))
#define IOMUX_PULLDOWN __BIT(7)
#define IOMUX_PULLUP __BIT(8)
#define IOMUX_INPUT __BIT(9)
#define IOMUX_FUNC __BITS(14, 12)
#define IOMUX_GPIO __SHIFTIN(1, IOMUX_FUNC)
#define IOMUX_APB_CLK 0x15c
#define IOMUX_APB_EN __BIT(0)
#define LP_PADCTRL_REG 0x1c4
#define LP_HP_GPIO_O_HOLD_CTRL1_REG 0x270
#define LP_HP_GPIO_O_HOLD_CTRL0_REG 0x274
#define ROUTE_NONE 511
#define PIN_MAX 62

struct esp_gpio_softc {
	device_t dev;
	bus_space_tag_t bst;
	bus_space_handle_t gpio, iomux;
	struct syscon *lp_syscon;
	kmutex_t lock;
	uint64_t gpio_used;
	int pin_owner[PIN_MAX];
	int input_owner[256];
	struct sysctllog *sysctllog;
};
struct esp_gpio_pin {
	unsigned int pin;
	bool active_low;
};
struct esp_pinconf {
	struct esp32s31_route route;
	unsigned int conf;
};
#define PC_INPUT __BIT(0)
#define PC_OUTPUT __BIT(1)
#define PC_PULLUP __BIT(2)
#define PC_PULLDOWN __BIT(3)
#define PC_OD __BIT(4)
#define PC_HIGH __BIT(5)
#define PC_LOW __BIT(6)

static void *esp_gpio_acquire(device_t, const void *, size_t, int);
static void esp_gpio_release(device_t, void *);
static int esp_gpio_read(device_t, void *, bool);
static void esp_gpio_write(device_t, void *, int, bool);
static int esp_gpio_set_config(device_t, const void *, size_t);
static const struct fdtbus_gpio_controller_func gpio_funcs = {
    .acquire = esp_gpio_acquire,
    .release = esp_gpio_release,
    .read = esp_gpio_read,
    .write = esp_gpio_write,
};
static const struct fdtbus_pinctrl_controller_func pinctrl_funcs = {
    .set_config = esp_gpio_set_config,
};
static const struct device_compatible_entry compat[] = {
    {.compat = "espressif,esp32s31-gpio"}, DEVICE_COMPAT_EOL};

/* OUT/EN/mux fields describe programming. IN samples can be disabled by
 * IO_MUX FUN_IE and none of these fields directly measures pad voltage. */
static int
esp_gpio_sysctl_registers(SYSCTLFN_ARGS)
{
	struct esp_gpio_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	uint32_t iomux[PIN_MAX], outsel[PIN_MAX], pinreg[PIN_MAX];
	uint32_t out, out1, en, en1, in, in1;
	uint32_t hold0, hold1, padctrl;
	const size_t capacity = 4096;
	char *buf;
	size_t used;
	int error, n;

	buf = kmem_alloc(capacity, KM_SLEEP);
	mutex_enter(&sc->lock);
	out = bus_space_read_4(sc->bst, sc->gpio, GPIO_OUT);
	out1 = bus_space_read_4(sc->bst, sc->gpio, GPIO_OUT1);
	en = bus_space_read_4(sc->bst, sc->gpio, GPIO_EN);
	en1 = bus_space_read_4(sc->bst, sc->gpio, GPIO_EN1);
	in = bus_space_read_4(sc->bst, sc->gpio, GPIO_IN);
	in1 = bus_space_read_4(sc->bst, sc->gpio, GPIO_IN1);
	for (unsigned int pin = 0; pin < PIN_MAX; pin++) {
		if (!esp32s31_pin_valid(pin))
			continue;
		iomux[pin] = bus_space_read_4(sc->bst, sc->iomux, pin * 4);
		outsel[pin] = bus_space_read_4(sc->bst, sc->gpio, GPIO_OUTSEL(pin));
		pinreg[pin] = bus_space_read_4(sc->bst, sc->gpio, GPIO_PIN(pin));
	}
	mutex_exit(&sc->lock);
	if (sc->lp_syscon != NULL) {
		/* Snapshot LP_SYS hold state; this path does not alter registers. */
		syscon_lock(sc->lp_syscon);
		padctrl = syscon_read_4(sc->lp_syscon, LP_PADCTRL_REG);
		hold1 = syscon_read_4(sc->lp_syscon,
		    LP_HP_GPIO_O_HOLD_CTRL1_REG);
		hold0 = syscon_read_4(sc->lp_syscon,
		    LP_HP_GPIO_O_HOLD_CTRL0_REG);
		syscon_unlock(sc->lp_syscon);
		used = snprintf(buf, capacity,
		    "out=%08x,%08x en=%08x,%08x in=%08x,%08x "
		    "hp_hold1=%08x hp_hold0=%08x lp_padctrl=%08x\n",
		    out, out1, en, en1, in, in1, hold1, hold0, padctrl);
	} else {
		used = snprintf(buf, capacity,
		    "out=%08x,%08x en=%08x,%08x in=%08x,%08x "
		    "lp_syscon=unavailable\n",
		    out, out1, en, en1, in, in1);
	}
	for (unsigned int pin = 0; pin < PIN_MAX; pin++) {
		if (!esp32s31_pin_valid(pin))
			continue;
		n = snprintf(buf + used, capacity - used,
		    "%u: mux=%08x outsel=%08x pin=%08x\n", pin,
		    iomux[pin], outsel[pin], pinreg[pin]);
		if (n < 0 || (size_t)n >= capacity - used) {
			kmem_free(buf, capacity);
			return EOVERFLOW;
		}
		used += (size_t)n;
	}
	node.sysctl_data = buf;
	node.sysctl_size = used + 1;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	kmem_free(buf, capacity);
	return error;
}

static void
esp_gpio_sysctl_init(struct esp_gpio_softc *sc)
{
	const struct sysctlnode *node;
	int error;

	error = sysctl_createv(&sc->sysctllog, 0, NULL, &node,
	    CTLFLAG_READONLY, CTLTYPE_NODE, device_xname(sc->dev),
	    SYSCTL_DESCR("ESP32-S31 GPIO controller"), NULL, 0, NULL, 0,
	    CTL_HW, CTL_CREATE, CTL_EOL);
	if (error == 0)
		error = sysctl_createv(&sc->sysctllog, 0, NULL, NULL,
		    CTLFLAG_READONLY, CTLTYPE_STRING, "registers",
		    SYSCTL_DESCR("GPIO, IO_MUX and LP_SYS hold register snapshot"),
		    esp_gpio_sysctl_registers, 0, (char *)sc, 0,
		    CTL_HW, node->sysctl_num, CTL_CREATE, CTL_EOL);
	if (error != 0) {
		sysctl_teardown(&sc->sysctllog);
		aprint_error_dev(sc->dev, "cannot publish registers: %d\n", error);
	}
}

static void
esp_gpio_level(struct esp_gpio_softc *sc, unsigned int pin, bool high)
{
	bus_size_t reg = pin < 32 ? (high ? GPIO_OUT_SET : GPIO_OUT_CLR)
	                          : (high ? GPIO_OUT1_SET : GPIO_OUT1_CLR);
	bus_space_write_4(sc->bst, sc->gpio, reg, __BIT(pin % 32));
}
static void
esp_gpio_enable(struct esp_gpio_softc *sc, unsigned int pin, bool enable)
{
	bus_size_t reg = pin < 32 ? (enable ? GPIO_EN_SET : GPIO_EN_CLR)
	                          : (enable ? GPIO_EN1_SET : GPIO_EN1_CLR);
	bus_space_write_4(sc->bst, sc->gpio, reg, __BIT(pin % 32));
}
static void
esp_gpio_pad(struct esp_gpio_softc *sc, unsigned int pin, unsigned int conf)
{
	uint32_t v = bus_space_read_4(sc->bst, sc->iomux, pin * 4);
	v &= ~(IOMUX_PULLDOWN | IOMUX_PULLUP | IOMUX_INPUT | IOMUX_FUNC);
	v |= IOMUX_GPIO;
	if (conf & PC_INPUT)
		v |= IOMUX_INPUT;
	if (conf & PC_PULLUP)
		v |= IOMUX_PULLUP;
	if (conf & PC_PULLDOWN)
		v |= IOMUX_PULLDOWN;
	bus_space_write_4(sc->bst, sc->iomux, pin * 4, v);
	v = bus_space_read_4(sc->bst, sc->gpio, GPIO_PIN(pin));
	v = conf & PC_OD ? v | GPIO_PAD_OD : v & ~GPIO_PAD_OD;
	bus_space_write_4(sc->bst, sc->gpio, GPIO_PIN(pin), v);
}

static void
esp_gpio_route_output(struct esp_gpio_softc *sc, unsigned int pin,
    unsigned int signal)
{
	uint32_t v = bus_space_read_4(sc->bst, sc->gpio, GPIO_OUTSEL(pin));

	v &= ~GPIO_OUT_FIELDS;
	v |= signal;
	if (signal == GPIO_OUT_GPIO)
		v |= GPIO_OUT_OEN_GPIO;
	bus_space_write_4(sc->bst, sc->gpio, GPIO_OUTSEL(pin), v);
}

static void
esp_gpio_route_input(struct esp_gpio_softc *sc, unsigned int signal,
    unsigned int pin)
{
	uint32_t v = bus_space_read_4(sc->bst, sc->gpio, GPIO_INSEL(signal));

	v &= ~GPIO_IN_FIELDS;
	v |= GPIO_IN_MATRIX | pin;
	bus_space_write_4(sc->bst, sc->gpio, GPIO_INSEL(signal), v);
}
static void *
esp_gpio_acquire(device_t dev, const void *data, size_t len, int flags)
{
	struct esp_gpio_softc *sc = device_private(dev);
	const uint32_t *cells = data;
	struct esp_gpio_pin *p;
	unsigned int pin, dtflags, conf = 0;

	if (len != 3 * sizeof(uint32_t))
		return NULL;
	pin = be32toh(cells[1]);
	dtflags = be32toh(cells[2]);
	if (!esp32s31_pin_valid(pin) || (dtflags & ~7U) ||
	    ((dtflags & 6U) != 0 && (dtflags & 6U) != 6U) ||
	    !(flags & (GPIO_PIN_INPUT | GPIO_PIN_OUTPUT)))
		return NULL;
	p = kmem_zalloc(sizeof(*p), KM_SLEEP);
	p->pin = pin;
	p->active_low = dtflags & 1;
	if (flags & GPIO_PIN_INPUT)
		conf |= PC_INPUT;
	if ((flags & GPIO_PIN_OPENDRAIN) || (dtflags & 6U))
		conf |= PC_OD;
	if (flags & GPIO_PIN_PULLUP)
		conf |= PC_PULLUP;
	if (flags & GPIO_PIN_PULLDOWN)
		conf |= PC_PULLDOWN;
	if ((conf & (PC_PULLUP | PC_PULLDOWN)) == (PC_PULLUP | PC_PULLDOWN)) {
		kmem_free(p, sizeof(*p));
		return NULL;
	}
	mutex_enter(&sc->lock);
	if ((sc->gpio_used & (UINT64_C(1) << pin)) || sc->pin_owner[pin]) {
		mutex_exit(&sc->lock);
		kmem_free(p, sizeof(*p));
		return NULL;
	}
	sc->gpio_used |= UINT64_C(1) << pin;
	esp_gpio_enable(sc, pin, false);
	esp_gpio_pad(sc, pin, conf);
	esp_gpio_route_output(sc, pin, GPIO_OUT_GPIO);
	if (flags & GPIO_PIN_OUTPUT) {
		esp_gpio_level(sc, pin, (conf & PC_OD) != 0 || p->active_low);
		esp_gpio_enable(sc, pin, true);
	}
	mutex_exit(&sc->lock);
	return p;
}
static void
esp_gpio_release(device_t dev, void *priv)
{
	struct esp_gpio_softc *sc = device_private(dev);
	struct esp_gpio_pin *p = priv;
	mutex_enter(&sc->lock);
	esp_gpio_enable(sc, p->pin, false);
	sc->gpio_used &= ~(UINT64_C(1) << p->pin);
	mutex_exit(&sc->lock);
	kmem_free(p, sizeof(*p));
}
static int
esp_gpio_read(device_t dev, void *priv, bool raw)
{
	struct esp_gpio_softc *sc = device_private(dev);
	struct esp_gpio_pin *p = priv;
	uint32_t v = bus_space_read_4(sc->bst, sc->gpio,
	                              p->pin < 32 ? GPIO_IN : GPIO_IN1);
	int result = !!(v & __BIT(p->pin % 32));
	return !raw && p->active_low ? !result : result;
}
static void
esp_gpio_write(device_t dev, void *priv, int value, bool raw)
{
	struct esp_gpio_softc *sc = device_private(dev);
	struct esp_gpio_pin *p = priv;
	if (!raw && p->active_low)
		value = !value;
	esp_gpio_level(sc, p->pin, value != 0);
}

static int
esp_gpio_set_config(device_t dev, const void *data, size_t len)
{
	struct esp_gpio_softc *sc = device_private(dev);
	struct esp_pinconf entries[PIN_MAX];
	uint64_t pins = 0;
	uint32_t inputs[8] = {0};
	unsigned int n = 0;
	int phandle, group;

	if (len != sizeof(uint32_t))
		return EINVAL;
	phandle = fdtbus_get_phandle_from_native(be32dec(data));
	if (phandle < 0)
		return EINVAL;
	for (group = OF_child(phandle); group; group = OF_peer(group)) {
		const uint32_t *cells;
		unsigned int conf = 0;
		int bytes;
		cells = fdtbus_get_prop(group, "pinmux", &bytes);
		if (cells == NULL || bytes <= 0 || bytes % 4 != 0)
			return EINVAL;
		if (of_hasprop(group, "input-enable"))
			conf |= PC_INPUT;
		if (of_hasprop(group, "output-enable"))
			conf |= PC_OUTPUT;
		if (of_hasprop(group, "bias-pull-up"))
			conf |= PC_PULLUP;
		if (of_hasprop(group, "bias-pull-down"))
			conf |= PC_PULLDOWN;
		if (of_hasprop(group, "drive-open-drain"))
			conf |= PC_OD;
		if (of_hasprop(group, "output-high"))
			conf |= PC_OUTPUT | PC_HIGH;
		if (of_hasprop(group, "output-low"))
			conf |= PC_OUTPUT | PC_LOW;
		if ((of_hasprop(group, "input-disable") && (conf & PC_INPUT)) ||
		    (of_hasprop(group, "output-disable") && (conf & PC_OUTPUT)) ||
		    (of_hasprop(group, "drive-push-pull") && (conf & PC_OD)) ||
		    (of_hasprop(group, "bias-disable") &&
		    (conf & (PC_PULLUP | PC_PULLDOWN))))
			return EINVAL;
		if (((conf & (PC_PULLUP | PC_PULLDOWN)) ==
		     (PC_PULLUP | PC_PULLDOWN)) ||
		    ((conf & (PC_HIGH | PC_LOW)) == (PC_HIGH | PC_LOW)))
			return EINVAL;
		for (int i = 0; i < bytes / 4; i++) {
			struct esp32s31_route route;
			if (n == PIN_MAX ||
			    !esp32s31_route_decode(be32toh(cells[i]), &route) ||
			    (pins & (UINT64_C(1) << route.pin)))
				return EINVAL;
			if (route.input != ROUTE_NONE) {
				if (!(conf & PC_INPUT) ||
				    (inputs[route.input / 32] &
				     __BIT(route.input % 32)))
					return EINVAL;
				inputs[route.input / 32] |=
				    __BIT(route.input % 32);
			}
			if ((conf & PC_OUTPUT) && route.output == ROUTE_NONE)
				return EINVAL;
			if ((conf & (PC_HIGH | PC_LOW)) &&
			    route.output != GPIO_OUT_GPIO)
				return EINVAL;
			pins |= UINT64_C(1) << route.pin;
			entries[n++] =
			    (struct esp_pinconf){.route = route, .conf = conf};
		}
	}
	if (n == 0)
		return EINVAL;
	mutex_enter(&sc->lock);
	for (unsigned int i = 0; i < n; i++) {
		unsigned int pin = entries[i].route.pin;
		if ((sc->gpio_used & (UINT64_C(1) << pin)) ||
		    (sc->pin_owner[pin] && sc->pin_owner[pin] != phandle) ||
		    (entries[i].route.input != ROUTE_NONE &&
		     sc->input_owner[entries[i].route.input] &&
		     sc->input_owner[entries[i].route.input] != phandle)) {
			mutex_exit(&sc->lock);
			return EBUSY;
		}
	}
	for (unsigned int i = 0; i < n; i++) {
		struct esp_pinconf *p = &entries[i];
		unsigned int pin = p->route.pin;
		esp_gpio_enable(sc, pin, false);
		if (p->conf & (PC_HIGH | PC_LOW))
			esp_gpio_level(sc, pin, p->conf & PC_HIGH);
		esp_gpio_pad(sc, pin, p->conf);
		if (p->route.input != ROUTE_NONE) {
			esp_gpio_route_input(sc, p->route.input, pin);
			sc->input_owner[p->route.input] = phandle;
		}
		if (p->route.output != ROUTE_NONE)
			esp_gpio_route_output(sc, pin, p->route.output);
		else
			/* Do not retain a previous peripheral output-enable. */
			esp_gpio_route_output(sc, pin, GPIO_OUT_GPIO);
		if (p->conf & PC_OUTPUT)
			esp_gpio_enable(sc, pin, true);
		sc->pin_owner[pin] = phandle;
	}
	mutex_exit(&sc->lock);
	return 0;
}

static int
esp_gpio_match(device_t parent, cfdata_t match, void *aux)
{
	struct fdt_attach_args *faa = aux;
	return of_compatible_match(faa->faa_phandle, compat);
}
static void
esp_gpio_attach(device_t parent, device_t self, void *aux)
{
	struct esp_gpio_softc *sc = device_private(self);
	struct fdt_attach_args *faa = aux;
	struct syscon *syscon;
	bus_addr_t addr;
	bus_size_t size;
	bus_size_t gpio_size;
	int phandle = faa->faa_phandle;
	uint32_t v;

	sc->dev = self;
	sc->bst = faa->faa_bst;
	sc->lp_syscon = fdtbus_syscon_acquire(phandle,
	    "espressif,lp-syscon");
	syscon = fdtbus_syscon_acquire(phandle, "espressif,syscon");
	if (syscon == NULL) {
		aprint_error(": missing clock syscon\n");
		return;
	}
	if (fdtbus_get_reg(phandle, 0, &addr, &size) || size < 0xbf0 ||
	    bus_space_map(sc->bst, addr, size, 0, &sc->gpio)) {
		aprint_error(": cannot map GPIO registers\n");
		return;
	}
	gpio_size = size;
	if (fdtbus_get_reg(phandle, 1, &addr, &size) || size < PIN_MAX * 4 ||
	    bus_space_map(sc->bst, addr, size, 0, &sc->iomux)) {
		aprint_error(": cannot map IO_MUX registers\n");
		bus_space_unmap(sc->bst, sc->gpio, gpio_size);
		return;
	}
	syscon_lock(syscon);
	v = syscon_read_4(syscon, IOMUX_APB_CLK);
	syscon_write_4(syscon, IOMUX_APB_CLK, v | IOMUX_APB_EN);
	syscon_unlock(syscon);
	mutex_init(&sc->lock, MUTEX_DEFAULT, IPL_VM);
	esp_gpio_sysctl_init(sc);
	aprint_naive("\n");
	aprint_normal(": GPIO and pin controller\n");
	fdtbus_register_gpio_controller(self, phandle, &gpio_funcs);
	for (int child = OF_child(phandle); child; child = OF_peer(child))
		fdtbus_register_pinctrl_config(self, child, &pinctrl_funcs);
}
CFATTACH_DECL_NEW(esp32s31_gpio, sizeof(struct esp_gpio_softc), esp_gpio_match,
                  esp_gpio_attach, NULL, NULL);
