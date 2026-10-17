/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD RTL8822C TX power source contract, AI-assisted. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8822c.h>
#include <dev/ic/rtw8822c_table.h>
#include "rtw8822c-mac-model.h"

static unsigned checks;
static unsigned accesses, fail_at, cancel_at;
static struct rtw88_softc sc;
static struct rtw8822c_efuse efuse;

#ifdef RTW88_TXPWR_MISSING_LIMIT
/* Real PG data, but a literal limit table with no HT2/VHT2 section. */
static const struct rtw_txpwr_lmt_cfg_pair missing_rows[] = {
	{ 2, 0, 0, 0, 1, 60 },
	{ 2, 0, 0, 1, 1, 60 },
	{ 2, 0, 0, 2, 1, 60 }
};
const struct rtw8822c_table rtw8822c_txpwr_lmt_type0_tbl = {
	.data = missing_rows,
	.count = sizeof(missing_rows) / sizeof(missing_rows[0]),
	.kind = RTW8822C_TBL_TXPWR_LMT
};
#endif

#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); \
	exit(EXIT_FAILURE); } } while (0)

static int
read4(void *cookie, uint32_t address, uint32_t *out)
{

	(void)cookie;
	accesses++;
	if (accesses == cancel_at)
		sc.sc_stop_requested = 1;
	if (accesses == fail_at)
		return EIO;
	*out = model_read(address, 4);
	return 0;
}

static int
write4(void *cookie, uint32_t address, uint32_t value)
{

	(void)cookie;
	accesses++;
	if (accesses == fail_at)
		return EIO;
	model_write(address, 4, value);
	return 0;
}

static const struct rtw88_bus_ops ops = { .read_4 = read4, .write_4 = write4 };

static void
reset(int valid, unsigned rfe)
{
	unsigned p, i;

	memset(&sc, 0, sizeof(sc));
	memset(&efuse, 0, sizeof(efuse));
	sc.sc_ops = &ops;
	sc.sc_rfe = efuse.rfe_option = (uint8_t)rfe;
	for (p = 0; p < 4; p++) {
		for (i = 0; i < 42; i++) {
			if (valid)
				efuse.txpower[p][i] = (i < 11 ||
				    (i >= 18 && i < 32)) ? 96 : 0;
			else
				efuse.txpower[p][i] = (uint8_t)((p * 42 + i) * 13 + 5);
		}
	}
	accesses = fail_at = cancel_at = 0;
	model_reset();
}

static void
traces(const char *path)
{
	FILE *fp = fopen(path, "r");
	char line[128], name[80];
	unsigned n = 0, seen = 0, sections = 0, channel = 0, rfe, regd;
	int result = 0;

	CHECK(fp != NULL);
	while (fgets(line, sizeof(line), fp) != NULL) {
		unsigned next_count;

		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == '#' || line[0] == '\0')
			continue;
		if (sscanf(line, "section %79s writes %u", name, &next_count) == 2) {
			const char *coordinates = strstr(name, "-rfe");

			if (sections != 0)
				CHECK(seen == n);
			n = next_count;
			sections++;
			seen = 0;
			CHECK(coordinates != NULL);
			CHECK(sscanf(coordinates, "-rfe%u-ch%u",
			    &rfe, &channel) == 2);
			regd = strstr(name, "etsi-") ? 2 :
			    strstr(name, "ww-") ? 12 : 0;
			reset(strncmp(name, "valid-", 6) == 0, rfe);
			result = rtw8822c_set_tx_power(&sc, &efuse, channel, regd);
			if (regd == 0) {
				CHECK(result == ENOTSUP);
				CHECK(accesses == 0);
			} else if (strncmp(name, "valid-", 6) == 0) {
				CHECK(result == 0);
				CHECK(model_ntrace == n);
			} else {
				/* The reference wraps unsafe signed arithmetic to 127. */
				CHECK(result == ERANGE);
				CHECK(accesses == 0);
			}
		} else {
			CHECK(sections != 0);
			CHECK(seen < n);
			if (result == 0) {
				unsigned address;

				CHECK(sscanf(line, "w4 0x%x", &address) == 1);
				/* 2G VHT now inherits real HT limits; keep reference intact. */
				if (!(channel <= 14 && address >= 0x3a2c))
					CHECK(strcmp(line, model_trace[seen]) == 0);
			}
			seen++;
		}
	}
	CHECK(!ferror(fp));
	CHECK(fclose(fp) == 0);
	CHECK(sections >= 33);
	CHECK(seen == n);
}

int
main(int argc, char **argv)
{
	static const unsigned invalid_channels[] = {0, 15, 35, 37, 178, 256};
	unsigned i;
	uint32_t before;

	CHECK(argc == 2);
#ifdef RTW88_TXPWR_MISSING_LIMIT
	reset(1, 0);
	CHECK(rtw8822c_set_tx_power(&sc, &efuse, 1, 2) == ENOTSUP);
	CHECK(accesses == 0);
	reset(1, 0);
	CHECK(rtw8822c_set_tx_power(&sc, &efuse, 1, 12) == ENOTSUP);
	CHECK(accesses == 0);
	printf("%u missing-section policy assertions passed\n", checks);
	return EXIT_SUCCESS;
#endif
	reset(1, 0);
	CHECK(rtw8822c_set_tx_power(NULL, &efuse, 1, 12) == EINVAL);
	CHECK(rtw8822c_set_tx_power(&sc, NULL, 1, 12) == EINVAL);
	CHECK(accesses == 0);
	reset(1, 0);
	CHECK(rtw8822c_set_tx_power(&sc, &efuse, 173, 12) == ENOTSUP);
	CHECK(accesses == 0);
	reset(1, 0);
	CHECK(rtw8822c_set_tx_power(&sc, &efuse, 1, 2) == 0);
	/* Absolute HT1=60, HT2=36; VHT section bases=64/64, MCS7 ref=92. */
	CHECK(model_read(0x3a2c, 4) == 0x00007474);
	CHECK(model_read(0x3a30, 4) == 0x00000000);
	CHECK(model_read(0x3a34, 4) == 0x5c5c7c00);
	CHECK(model_read(0x3a38, 4) == 0x68686868);
	CHECK(model_read(0x3a3c, 4) == 0x68686868);
	for (i = 0; i <= 13; i++) {
		if (i == 2 || i == 12)
			continue;
		reset(1, 0);
		CHECK(rtw8822c_set_tx_power(&sc, &efuse, 1, i) == ENOTSUP);
		CHECK(accesses == 0);
	}
	for (i = 0; i < sizeof(invalid_channels) / sizeof(invalid_channels[0]); i++) {
		reset(1, 0);
		CHECK(rtw8822c_set_tx_power(&sc, &efuse, invalid_channels[i], 12) == EINVAL);
		CHECK(accesses == 0);
	}
	reset(1, 7);
	CHECK(rtw8822c_set_tx_power(&sc, &efuse, 1, 12) == ENOTSUP);
	CHECK(accesses == 0);
	reset(1, 0);
	efuse.txpower[0][0] = 0xff;
	CHECK(rtw8822c_set_tx_power(&sc, &efuse, 1, 12) == EINVAL);
	CHECK(accesses == 0);
	reset(1, 0);
	efuse.txpower[0][0] = 0;
	CHECK(rtw8822c_set_tx_power(&sc, &efuse, 1, 12) == ERANGE);
	CHECK(accesses == 0);
	reset(1, 0);
	efuse.txpower[0][0] = 254;
	CHECK(rtw8822c_set_tx_power(&sc, &efuse, 1, 12) == ERANGE);
	CHECK(accesses == 0);
	reset(1, 0);
	sc.sc_stop_requested = 1;
	CHECK(rtw8822c_set_tx_power(&sc, &efuse, 1, 12) == EINTR);
	CHECK(accesses == 0);
	reset(1, 0);
	CHECK(rtw8822c_set_tx_power(&sc, &efuse, 1, 12) == 0);
	before = model_read(0x18a0, 4);
	reset(1, 0);
	efuse.txpower[0][0]++;
	CHECK(rtw8822c_set_tx_power(&sc, &efuse, 1, 12) == 0);
	CHECK(model_read(0x18a0, 4) == before + 0x10000);
	reset(1, 0);
	CHECK(rtw8822c_set_tx_power(&sc, &efuse, 36, 12) == 0);
	before = model_read(0x18e8, 4);
	reset(1, 0);
	efuse.txpower[0][18]++;
	CHECK(rtw8822c_set_tx_power(&sc, &efuse, 36, 12) == 0);
	CHECK(model_read(0x18e8, 4) == before + 0x400);
	reset(1, 0);
	CHECK(rtw8822c_set_tx_power(&sc, &efuse, 1, 12) == 0);
	before = model_read(0x18e8, 4);
	reset(1, 0);
	efuse.txpower[0][11] = 0x20;
	CHECK(rtw8822c_set_tx_power(&sc, &efuse, 1, 12) == 0);
	CHECK(model_read(0x18e8, 4) == before + 0x1000);
	reset(1, 0);
	efuse.txpower[0][11] = 0xe0;
	CHECK(rtw8822c_set_tx_power(&sc, &efuse, 1, 12) == 0);
	CHECK(model_read(0x18e8, 4) == before - 0x1000);
	reset(1, 0);
	CHECK(rtw8822c_set_tx_power(&sc, &efuse, 1, 12) == 0);
	before = model_read(0x3a14, 4);
	reset(1, 0);
	efuse.txpower[0][12] = 0x0f;
	CHECK(rtw8822c_set_tx_power(&sc, &efuse, 1, 12) == 0);
	CHECK(model_read(0x3a14, 4) == before - 0x02020202);
	for (i = 1; i <= 64; i++) {
		int result;

		reset(1, 0);
		fail_at = i;
		result = rtw8822c_set_tx_power(&sc, &efuse, 1, 12);
		if (result != EIO)
			fprintf(stderr, "access %u: expected EIO, got %d\n", i, result);
		CHECK(result == EIO);
		CHECK(accesses == i);
	}
	for (i = 1; i < 64; i += 2) {
		reset(1, 0);
		cancel_at = i;
		CHECK(rtw8822c_set_tx_power(&sc, &efuse, 1, 12) == EINTR);
		CHECK(accesses == i);
	}
	traces(argv[1]);
	printf("%u RTL8822C TX power assertions passed\n", checks);
	return EXIT_SUCCESS;
}
