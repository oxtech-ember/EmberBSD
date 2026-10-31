/* SPDX-License-Identifier: BSD-2-Clause */
#define CELL(pin, in, out) ((pin) | ((in) << 6) | ((out) << 15))
int
main(void)
{
	struct esp_gpio_softc sc = {.gpio = 0, .iomux = 1};
	uint32_t ref = __builtin_bswap32(100);
	ncells = 1;
	props = INPUT | OUTPUT;
	cells[0] = __builtin_bswap32(CELL(51, 54, 54));
	reg[0][GPIO_INSEL(54) / 4] = 0xf0000000;
	reg[0][GPIO_OUTSEL(51) / 4] = 0xf0000000;
	assert(esp_gpio_set_config(&sc, &ref, 4) == 0);
	assert(reg[0][GPIO_INSEL(54) / 4] ==
	       (0xf0000000 | GPIO_IN_MATRIX | 51));
	assert(reg[0][GPIO_OUTSEL(51) / 4] == (0xf0000000 | 54));
	assert((reg[1][51] & (IOMUX_FUNC | IOMUX_INPUT)) ==
	       (IOMUX_GPIO | IOMUX_INPUT));
	unsigned int before = writes;
	cells[0] = __builtin_bswap32(CELL(29, 54, 54));
	assert(esp_gpio_set_config(&sc, &ref, 4) == EINVAL && writes == before);
	cells[0] = __builtin_bswap32(CELL(51, 54, 54));
	props = INPUT | OUTPUT | DISOUT;
	assert(esp_gpio_set_config(&sc, &ref, 4) == EINVAL && writes == before);
	props = INPUT | OUTPUT;
	sc.pin_owner[51] = 101;
	assert(esp_gpio_set_config(&sc, &ref, 4) == EBUSY && writes == before);
	/* A previous peripheral OE must not keep driving an input-only pad. */
	props = INPUT;
	cells[0] = __builtin_bswap32(CELL(40, 55, 511));
	reg[0][GPIO_OUTSEL(40) / 4] = 7;
	assert(esp_gpio_set_config(&sc, &ref, 4) == 0);
	assert((reg[0][GPIO_OUTSEL(40) / 4] & GPIO_OUT_FIELDS) ==
	    (GPIO_OUT_GPIO | GPIO_OUT_OEN_GPIO));
	assert(reg[0][GPIO_EN1_CLR / 4] == (1u << 8));
	return 0;
}
