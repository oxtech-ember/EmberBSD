/* Origin: EmberBSD - distinguish GPIO flags from legacy mux selections. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SUNXI_GPIO_CELLS_H_
#define _SUNXI_GPIO_CELLS_H_

static inline uint32_t
sunxi_gpio_cell(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
	    (uint32_t)p[2] << 8 | p[3];
}

static inline bool
sunxi_gpio_cells(const void *data, size_t len, uint8_t *port, uint8_t *pin,
    bool *actlo)
{
	const uint8_t *p = data;
	uint32_t mode;

	if (len != 16 && len != 28)
		return false;
	mode = sunxi_gpio_cell(p + 12);
	if (len == 28 && mode > 1)
		return false;
	*port = sunxi_gpio_cell(p + 4) & 0xff;
	*pin = sunxi_gpio_cell(p + 8) & 0xff;
	/* Legacy cell 3 is input/output mux, never an active-low flag. */
	*actlo = len == 16 && (mode & 1) != 0;
	return true;
}
#endif
