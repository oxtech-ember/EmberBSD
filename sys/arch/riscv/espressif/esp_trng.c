/*	$NetBSD$	*/
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * ESP32-S31 boot entropy source.  Register sequencing follows Espressif
 * ESP-IDF 6.1 components/hal/esp32s31/include/hal/trng_ll.h and
 * components/bootloader_support/src/bootloader_random_esp32s31.c,
 * Copyright 2026 Espressif Systems (Shanghai) CO LTD, Apache-2.0.
 * See external/apache2/LICENSE-2.0 for the complete license.
 *
 * The S31 kernel owns the TRNG exclusively during this early boot step,
 * before RF initialization.  The documented reset clears the TRNG's
 * internal state.  Clock and unrelated control bits are restored; sampling
 * and noise are disabled before the radio starts, as in the IDF shutdown.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/rndsource.h>
#include <sys/systm.h>
#include <lib/libkern/libkern.h>
#include <riscv/espressif/esp_trng.h>

#define CLOCK_REG	0x2071003cU
#define TRNG_DATE	0x208140fcU
#define TRNG_CONF	0x20814000U
#define TRNG_DATA	0x20814048U
#define CLOCK_EN	(1U << 30)
#define CLOCK_RST	(1U << 31)
#define DATE_CLK	(1U << 28)
#define CONF_SAMPLE	(1U << 31)
#define CONF_NOISE	(1U << 30)
#define TRNG_SAMPLES	320
#define TRNG_MAX_BITS	256

struct esp_trng_io {
	uint32_t (*read)(uint32_t);
	void (*write)(uint32_t, uint32_t);
	void (*pause_us)(unsigned);
};

static int
esp_trng_collect(const struct esp_trng_io *io, uint32_t *samples)
{
	const uint32_t clock = io->read(CLOCK_REG);
	uint32_t date, conf, run_date, run_conf;
	unsigned unique = 0;

	if (clock & CLOCK_RST)
		return -1;
	/* The TRNG registers are only accessible after its LP bus clock. */
	io->write(CLOCK_REG, clock | CLOCK_EN);
	date = io->read(TRNG_DATE);
	conf = io->read(TRNG_CONF);
	/* Do not disturb an already active entropy user. */
	if (conf & CONF_SAMPLE) {
		io->write(CLOCK_REG, clock);
		return -1;
	}
	io->write(CLOCK_REG, clock | CLOCK_EN | CLOCK_RST);
	io->write(CLOCK_REG, (clock | CLOCK_EN) & ~CLOCK_RST);
	run_date = io->read(TRNG_DATE);
	run_conf = io->read(TRNG_CONF);
	io->write(TRNG_DATE, run_date | DATE_CLK);
	io->write(TRNG_CONF, run_conf | CONF_SAMPLE);
	io->write(TRNG_CONF, run_conf | CONF_SAMPLE | CONF_NOISE);
	for (unsigned i = 0; i < TRNG_SAMPLES; i++) {
		unsigned j;

		/* A full millisecond is far slower than IDF's 16 APB cycles. */
		io->pause_us(1000);
		samples[i] = io->read(TRNG_DATA);
		for (j = 0; j < i; j++)
			if (samples[j] == samples[i])
				break;
		if (j == i && unique < TRNG_MAX_BITS)
			unique++;
	}
	io->write(TRNG_CONF, conf & ~(CONF_SAMPLE | CONF_NOISE));
	io->write(TRNG_DATE, date);
	io->write(CLOCK_REG, clock);

	/* A changing register is only a health check, not an entropy estimate.
	 * Credit the documented active noise source conservatively only after
	 * at least 256 non-repeated, individually spaced observations. */
	return unique == TRNG_MAX_BITS ? TRNG_MAX_BITS : 0;
}

static uint32_t
esp_trng_read(uint32_t reg)
{
	return *(volatile uint32_t *)(uintptr_t)reg;
}

static void
esp_trng_write(uint32_t reg, uint32_t value)
{
	*(volatile uint32_t *)(uintptr_t)reg = value;
}

static void
esp_trng_pause(unsigned usec)
{
	delay(usec);
}

bool
esp_trng_seed(void)
{
	static krndsource_t source;
	static const struct esp_trng_io io = {
		esp_trng_read, esp_trng_write, esp_trng_pause
	};
	uint32_t samples[TRNG_SAMPLES];
	int bits;

	rnd_attach_source(&source, "esp32s31-trng", RND_TYPE_RNG,
	    RND_FLAG_COLLECT_VALUE);
	bits = esp_trng_collect(&io, samples);
	if (bits > 0)
		rnd_add_data_sync(&source, samples, sizeof(samples), bits);
	else if (bits < 0)
		aprint_error("esp32s31-trng: hardware already in use\n");
	else
		aprint_error("esp32s31-trng: no usable boot entropy\n");
	explicit_memset(samples, 0, sizeof(samples));
	return bits >= 0;
}
