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

/* Origin: EmberBSD RX-only transport guard for ADI no-OS
 * ce7107f957d19858f2dffc85bf3d7f2c70367000. */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include "ad9361.h"
#include "ad9361_native.h"
#include "no_os_alloc.h"
#include "no_os_delay.h"
#include "no_os_util.h"

/*
 * Only one caller may use this context, under the converter's exclusive
 * configuration lease. Descriptors are embedded: vendor failure cleanup
 * cannot leak descriptors or release the caller-owned SPI/GPIO resources.
 */
static int ad9361_native_fail(struct ad9361_native_bus *bus, int error)
{

	if (error == 0)
		return 0;
	if (bus->first_error == 0)
		bus->first_error = error > 0 ? error : -error;
	return -bus->first_error;
}

void ad9361_native_bus_init(struct ad9361_native_bus *bus, void *cookie,
			    int (*transfer)(void *, uint8_t *, size_t),
			    int (*reset)(void *, bool))
{

	memset(bus, 0, sizeof(*bus));
	bus->cookie = cookie;
	bus->transfer = transfer;
	bus->reset = reset;
	bus->spi.extra = bus;
	bus->gpio.extra = bus;
	bus->gpio.number = 0;
}

void
ad9361_native_bus_init_tx(struct ad9361_native_bus *bus, void *cookie,
    int (*transfer)(void *, uint8_t *, size_t), int (*reset)(void *, bool))
{

	ad9361_native_bus_init(bus, cookie, transfer, reset);
	bus->tx_enabled = true;
}

int ad9361_native_bus_error(const struct ad9361_native_bus *bus)
{

	return bus->first_error;
}

int ad9361_native_bus_abort(struct ad9361_native_bus *bus)
{
	int error;

	/*
	 * Assert reset even after the first failure. The caller must continue
	 * to own capture and must not publish initialized/configured state.
	 */
	if (bus->reset == NULL)
		return ENXIO;
	error = bus->reset(bus->cookie, false);
	bus->reset_released = false;
	if (error != 0)
		(void)ad9361_native_fail(bus, error);
	return error;
}

int32_t no_os_spi_init(struct no_os_spi_desc **desc,
		       const struct no_os_spi_init_param *param)
{
	struct ad9361_native_bus *bus;

	if (desc == NULL || param == NULL || param->extra == NULL)
		return -EINVAL;
	*desc = NULL;
	bus = param->extra;
	if (bus->transfer == NULL || bus->reset == NULL)
		return ad9361_native_fail(bus, ENXIO);
	if (bus->first_error != 0)
		return -bus->first_error;
	*desc = &bus->spi;
	return 0;
}

int32_t no_os_spi_remove(struct no_os_spi_desc *desc)
{

	/* The enclosing native attachment owns the embedded descriptor. */
	return 0;
}

int ad9361_native_check_write(unsigned int reg, uint8_t value)
{

	switch (reg) {
	case REG_SPI_CONF:
		if (value != 0)
			return EPERM;
		break;
	case REG_TX_ENABLE_FILTER_CTRL:
		if (value & 0xc0)
			return EPERM;
		break;
	case REG_TX_SYNTH_POWER_DOWN_OVERRIDE:
		if ((value & 0x1f) != 0x1f)
			return EPERM;
		break;
	case REG_ENSM_CONFIG_2:
		if ((value & 0x21) != 0x21 || (value & (TXNRX_SPI_CTRL | SYNTH_ENABLE_PIN_CTRL_MODE |
		    FDD_EXTERNAL_CTRL_ENABLE)) != 0)
			return EPERM;
		break;
	case REG_ENSM_MODE:
		if (value != FDD_MODE)
			return EPERM;
		break;
	case REG_ENSM_CONFIG_1:
		if (value & (ENABLE_ENSM_PIN_CTRL |
			     ENABLE_RX_DATA_PORT_FOR_CAL | FORCE_RX_ON))
			return EPERM;
		break;
	case REG_CALIBRATION_CTRL:
		if (value & (TX_BB_TUNE_CAL | TX_QUAD_CAL))
			return EPERM;
		break;
	default:
		break;
	}
	return 0;
}

static int
ad9361_native_check_tx_write(unsigned int reg, uint8_t value)
{

	switch (reg) {
	case REG_SPI_CONF:
		return value == 0 ? 0 : EPERM;
	case REG_ENSM_MODE:
		return value == FDD_MODE ? 0 : EPERM;
	case REG_ENSM_CONFIG_1:
		return (value & ENABLE_ENSM_PIN_CTRL) == 0 ? 0 : EPERM;
	case REG_ENSM_CONFIG_2:
		return (value & (TXNRX_SPI_CTRL | SYNTH_ENABLE_PIN_CTRL_MODE |
		    FDD_EXTERNAL_CTRL_ENABLE)) == 0 ? 0 : EPERM;
	case REG_TX2_ATTEN_0:
		return value == (AD9361_NATIVE_TX_MAX_ATTENUATION / 250 % 256)
		    ? 0 : EPERM;
	case REG_TX2_ATTEN_1:
		return value == (AD9361_NATIVE_TX_MAX_ATTENUATION / 250 / 256)
		    ? 0 : EPERM;
	default:
		return 0;
	}
}

static int ad9361_native_read_raw(struct ad9361_native_bus *bus,
				  unsigned int reg, uint8_t *value)
{
	uint8_t data[3] = {reg >> 8, reg, 0};
	int error;

	error = bus->transfer(bus->cookie, data, sizeof(data));
	if (error != 0)
		return error;
	*value = data[2];
	return 0;
}

static int ad9361_native_inhibit_readback(struct ad9361_native_bus *bus)
{
	static const unsigned int regs[] = {REG_TX_ENABLE_FILTER_CTRL,
					    REG_TX_SYNTH_POWER_DOWN_OVERRIDE,
					    REG_ENSM_CONFIG_2};
	uint8_t value;
	unsigned int i;
	int error;

	/* Fresh hardware reads before every ENSM transition, never a cache. */
	for (i = 0; i < __arraycount(regs); i++) {
		error = ad9361_native_read_raw(bus, regs[i], &value);
		if (error != 0)
			return error;
		error = ad9361_native_check_write(regs[i], value);
		if (error != 0)
			return error;
	}
	return 0;
}

int32_t no_os_spi_write_and_read(struct no_os_spi_desc *desc, uint8_t *data,
				 uint16_t length)
{
	struct ad9361_native_bus *bus;
	unsigned int command, count, reg, i;
	bool transition = false;
	int error;

	if (desc == NULL || desc->extra == NULL)
		return -EINVAL;
	bus = desc->extra;
	if (bus->first_error != 0)
		return -bus->first_error;
	if (!bus->reset_released || data == NULL || length < 3)
		return ad9361_native_fail(bus, EINVAL);
	command = (data[0] << 8) | data[1];
	count = ((command >> 12) & 7) + 1;
	reg = command & 0x3ff;
	if (length != count + 2 || (command & 0x0c00) != 0 || reg < count - 1)
		return ad9361_native_fail(bus, EINVAL);
	if (command & 0x8000) {
		/* AD9361 multi-byte addresses descend. Validate all bytes
		 * first. */
		for (i = 0; i < count; i++) {
			error = bus->tx_enabled
			    ? ad9361_native_check_tx_write(reg - i, data[2 + i])
			    : ad9361_native_check_write(reg - i, data[2 + i]);
			if (error != 0)
				return ad9361_native_fail(bus, error);
			if (reg - i == REG_ENSM_CONFIG_1 ||
			    reg - i == REG_ENSM_MODE)
				transition = true;
		}
		if (transition && !bus->tx_enabled) {
			error = ad9361_native_inhibit_readback(bus);
			if (error != 0)
				return ad9361_native_fail(bus, error);
		}
	}
	error = bus->transfer(bus->cookie, data, length);
	return ad9361_native_fail(bus, error);
}

int32_t no_os_gpio_get(struct no_os_gpio_desc **desc,
		       const struct no_os_gpio_init_param *param)
{
	struct ad9361_native_bus *bus;

	if (desc == NULL || param == NULL || param->extra == NULL)
		return -EINVAL;
	*desc = NULL;
	bus = param->extra;
	if (bus->reset == NULL || param->number != 0)
		return ad9361_native_fail(bus, EINVAL);
	*desc = &bus->gpio;
	return 0;
}

int32_t no_os_gpio_get_optional(struct no_os_gpio_desc **desc,
				const struct no_os_gpio_init_param *param)
{

	if (desc == NULL || param == NULL)
		return -EINVAL;
	*desc = NULL;
	/* Optional sync/calibration GPIOs are deliberately unsupported. */
	return param->number < 0 ? 0 : -EOPNOTSUPP;
}

int32_t no_os_gpio_remove(struct no_os_gpio_desc *desc)
{

	return 0;
}

int32_t no_os_gpio_set_value(struct no_os_gpio_desc *desc, uint8_t value)
{
	struct ad9361_native_bus *bus;
	int error;

	if (desc == NULL || desc->extra == NULL)
		return -EINVAL;
	bus = desc->extra;
	if (value > 1)
		return ad9361_native_fail(bus, EINVAL);
	if (bus->first_error != 0)
		return -bus->first_error;
	error = bus->reset(bus->cookie, value != 0);
	if (error == 0)
		bus->reset_released = value != 0;
	return ad9361_native_fail(bus, error);
}

int32_t no_os_gpio_direction_output(struct no_os_gpio_desc *desc, uint8_t value)
{

	return no_os_gpio_set_value(desc, value);
}

int32_t no_os_gpio_direction_input(struct no_os_gpio_desc *desc)
{
	struct ad9361_native_bus *bus;

	if (desc == NULL || desc->extra == NULL)
		return -EINVAL;
	bus = desc->extra;
	return ad9361_native_fail(bus, EOPNOTSUPP);
}

/* Preserve at least 64-bit alignment, and remember the kmem allocation size. */
union ad9361_allocation {
	size_t size;
	uint64_t align;
};

void *no_os_malloc(size_t size)
{
	union ad9361_allocation *allocation;

	if (size > SIZE_MAX - sizeof(*allocation))
		return NULL;
	allocation = kmem_alloc(size + sizeof(*allocation), KM_SLEEP);
	allocation->size = size + sizeof(*allocation);
	return allocation + 1;
}

void *no_os_calloc(size_t count, size_t size)
{
	void *result;

	if (count != 0 && size > SIZE_MAX / count)
		return NULL;
	result = no_os_malloc(count * size);
	if (result != NULL)
		memset(result, 0, count * size);
	return result;
}

void no_os_free(void *ptr)
{
	union ad9361_allocation *allocation;

	if (ptr != NULL) {
		allocation = (union ad9361_allocation *)ptr - 1;
		kmem_free(allocation, allocation->size);
	}
}

void no_os_udelay(uint32_t usecs)
{

	while (usecs > 1000) {
		delay(1000);
		usecs -= 1000;
	}
	delay(usecs);
}

void no_os_mdelay(uint32_t msecs)
{

	while (msecs-- != 0)
		delay(1000);
}

uint64_t no_os_do_div(uint64_t *value, uint64_t divisor)
{
	uint64_t remainder;

	KASSERT(divisor != 0);
	remainder = *value % divisor;
	*value /= divisor;
	return remainder;
}
