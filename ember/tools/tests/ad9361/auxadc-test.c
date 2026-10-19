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
/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: Actual-source AD9361 AUXADC result and transport-failure regression. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "registers.h"

struct no_os_spi_desc {
	unsigned int calls, fail_at;
	int32_t error;
	uint16_t value;
};

struct ad9361_rf_phy {
	struct no_os_spi_desc *spi;
};

static int32_t
ad9361_spi_writef(struct no_os_spi_desc *spi, uint32_t reg, uint32_t mask,
    uint32_t value)
{
	++spi->calls;
	assert(spi->calls == 1 || spi->calls == 3);
	assert(reg == REG_AUXADC_CONFIG && mask == AUXADC_POWER_DOWN);
	assert(value == (spi->calls == 1 ? 1U : 0U));
	if (spi->calls == spi->fail_at) {
		assert(spi->error < 0);
		return spi->error;
	}
	return 0;
}

static int32_t
ad9361_spi_readm(struct no_os_spi_desc *spi, uint32_t reg, uint8_t *data,
    uint32_t size)
{
	++spi->calls;
	assert(spi->calls == 2 && reg == REG_AUXADC_LSB && size == 2);
	if (spi->calls == spi->fail_at) {
		assert(spi->error < 0);
		return spi->error; /* A failed transfer does not initialize data. */
	}
	data[0] = 0xf0U | (spi->value & 0x0fU);
	data[1] = spi->value >> 4;
	return 0;
}

#include "auxadc.inc"

int
main(void)
{
	const uint16_t values[] = { 0, 1, 0x123, 0xabc, 0xfff };
	const unsigned int failures[] = { 2, 1, 3 };
	const int32_t errors[] = { -EIO, -ETIMEDOUT, -EINVAL };
	unsigned int cases = 0;

	for (unsigned int i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		struct no_os_spi_desc spi = { .value = values[i] };
		struct ad9361_rf_phy phy = { .spi = &spi };
		assert(ad9361_get_auxadc(&phy) == values[i]);
		assert(spi.calls == 3);
		cases++;
	}
	for (unsigned int i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
		for (unsigned int j = 0; j < sizeof(errors) / sizeof(errors[0]); j++) {
			struct no_os_spi_desc spi = {
			    .value = 0x123, .fail_at = failures[i], .error = errors[j]
			};
			struct ad9361_rf_phy phy = { .spi = &spi };
			int32_t result = ad9361_get_auxadc(&phy);
			if (result != errors[j])
				fprintf(stderr, "AUXADC returned %d, expected %d\n",
				    result, errors[j]);
			assert(result == errors[j]);
			assert(spi.calls == failures[i]);
			cases++;
		}
	}
	printf("PASS: %u AUXADC values and first-error propagation cases\n", cases);
	return 0;
}
