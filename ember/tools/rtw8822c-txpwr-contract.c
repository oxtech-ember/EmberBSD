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
static unsigned delta_failures;
static unsigned accesses, fail_at, cancel_at;
static struct rtw88_softc sc;
static struct rtw8822c_efuse efuse;

/* Link the shared pure tuple geometry helper from the PHY translation unit. */
int
rtw8822c_dack(struct rtw88_softc *softc, struct rtw8822c_dack_result *result)
{
	(void)softc;
	memset(result, 0, sizeof(*result));
	return 0;
}

#ifdef RTW88_TXPWR_DIFFERENTIAL_LIMIT
/* Distinct synthetic 20/40/80 limits expose the constituent-width minima. */
static const struct rtw_txpwr_lmt_cfg_pair differential_rows[] = {
	{ 2, 1, 0, 1, 64, 30 }, { 2, 1, 0, 2, 64, 30 },
	{ 2, 1, 0, 3, 64, 30 }, { 2, 1, 0, 4, 64, 30 },
	{ 2, 1, 0, 5, 64, 30 },
	{ 2, 1, 1, 2, 62, 20 }, { 2, 1, 1, 3, 62, 24 },
	{ 2, 1, 1, 4, 62, 22 }, { 2, 1, 1, 5, 62, 26 },
	{ 2, 1, 2, 4, 58, 10 }, { 2, 1, 2, 5, 58, 32 }
};
const struct rtw8822c_table rtw8822c_txpwr_lmt_type0_tbl = {
	.data = differential_rows,
	.count = sizeof(differential_rows) / sizeof(differential_rows[0]),
	.kind = RTW8822C_TBL_TXPWR_LMT
};
#elif defined(RTW88_TXPWR_MISSING_LIMIT)
/* Real PG data with selectable missing sections in the modeled limit table. */
static struct rtw_txpwr_lmt_cfg_pair missing_rows[] = {
	{ 2, 0, 0, 0, 1, 60 },
	{ 2, 0, 0, 1, 1, 60 },
	{ 2, 0, 0, 2, 1, 60 },
	{ 2, 1, 0, 1, 36, 60 }, { 2, 1, 0, 2, 36, 60 },
	{ 2, 1, 0, 3, 36, 60 }, { 2, 1, 0, 4, 36, 60 },
	{ 2, 1, 0, 5, 36, 60 },
	{ 2, 1, 1, 2, 38, 60 }, { 2, 1, 1, 3, 38, 60 },
	{ 2, 1, 1, 4, 38, 60 }, { 2, 1, 1, 5, 38, 60 },
	{ 2, 1, 2, 4, 42, 60 }, { 2, 1, 2, 5, 42, 60 }
};
const struct rtw8822c_table rtw8822c_txpwr_lmt_type0_tbl = {
	.data = missing_rows,
	.count = sizeof(missing_rows) / sizeof(missing_rows[0]),
	.kind = RTW8822C_TBL_TXPWR_LMT
};
static void
missing_width_rows(unsigned bw, int missing)
{
	unsigned i, channel = bw == 0 ? 36 : bw == 1 ? 38 : 42;

	for (i = 0; i < sizeof(missing_rows) / sizeof(missing_rows[0]); i++) {
		if (missing_rows[i].band == 1 && missing_rows[i].bw == bw)
			missing_rows[i].ch = missing ? 255 : channel;
	}
}
#endif

#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); \
	exit(EXIT_FAILURE); } } while (0)
#define EXPECT(x) do { checks++; if (!(x)) { \
	delta_failures++; \
	fprintf(stderr, "%s:%d: expected %s\n", __func__, __LINE__, #x); \
} } while (0)

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

static void
wide_tuples(void)
{
	const struct rtw88_channel_tuple tuples[] = {
		{ 64, 64, 20 }, { 64, 62, 40 },
		{ 64, 58, 80 }, { 36, 42, 80 }
	};
	static const unsigned rfe[] = { 0, 0, 0, 5 };
	unsigned i;

	for (i = 0; i < sizeof(tuples) / sizeof(tuples[0]); i++) {
		int error;

		reset(1, rfe[i]);
		error = rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuples[i], 2);
		if (error != 0)
			fprintf(stderr, "tuple %u/%u/%u: error %d\n",
			    tuples[i].primary, tuples[i].center,
			    tuples[i].width_mhz, error);
		CHECK(error == 0);
		CHECK(accesses != 0 && !sc.sc_phy.channel_valid);
	}
	{
		const struct rtw88_channel_tuple invalid = { 64, 60, 40 };

		reset(1, 0);
		CHECK(rtw8822c_set_tx_power_tuple(&sc, &efuse, &invalid, 2) == EINVAL);
		CHECK(accesses == 0);
	}
	{
		const struct rtw88_channel_tuple tuple = { 64, 58, 80 };
		int error;

		reset(1, 0);
		efuse.txpower[0][37] = 0xff;
		error = rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple, 2);
		EXPECT(error == 0 && accesses != 0);
		reset(1, 0);
		efuse.txpower[0][38] = 0xff;
		error = rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple, 2);
		EXPECT(error == EINVAL && accesses == 0);
		reset(1, 0);
		efuse.txpower[0][39] = 0xff;
		error = rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple, 2);
		EXPECT(error == EINVAL && accesses == 0);
	}
	{
		const struct rtw88_channel_tuple tuple20 = { 64, 64, 20 };
		const struct rtw88_channel_tuple tuple40 = { 64, 62, 40 };
		uint32_t before;

		reset(1, 0);
		CHECK(rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple20, 2) == 0);
		before = model_read(0x18e8, 4);
		reset(1, 0);
		efuse.txpower[0][32] = 0xf0;
		CHECK(rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple20, 2) == 0);
		CHECK(model_read(0x18e8, 4) == before - 0x800);

		reset(1, 0);
		CHECK(rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple40, 2) == 0);
		before = model_read(0x3a14, 4);
		reset(1, 0);
		efuse.txpower[0][33] = 0xf0;
		CHECK(rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple40, 2) == 0);
		CHECK((model_read(0x3a14, 4) & 0x7f) ==
		    (((before & 0x7f) - 2) & 0x7f));

	}
}

static unsigned
rate_field(uint32_t address, unsigned shift)
{
	return (model_read(address, 4) >> shift) & 0x7f;
}

static void
width_efuse_deltas(void)
{
	const struct rtw88_channel_tuple tuple40 = { 64, 62, 40 };
	const struct rtw88_channel_tuple tuple80 = { 64, 58, 80 };
	unsigned before, after, ref_before, ref_after;
	int error;

	/* HT40 2SS efuse deltas also apply to the VHT2SS rate section. */
	reset(1, 0);
	error = rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple40, 2);
	EXPECT(error == 0);
	before = rate_field(0x3a34, 16); /* VHT2SS MCS0, rate54. */
	reset(1, 0);
	efuse.txpower[0][33] = 0xf0; /* HT40 2SS = -1; two TXGI steps. */
	error = rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple40, 2);
	EXPECT(error == 0);
	after = rate_field(0x3a34, 16);
	EXPECT(after == ((before - 2) & 0x7f));

	/* BW80 uses the VHT80 1SS delta for the HT 1SS reference as well. */
	reset(1, 0);
	error = rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple80, 2);
	EXPECT(error == 0);
	ref_before = rate_field(0x18e8, 10);
	reset(1, 0);
	efuse.txpower[0][38] = 0xf0; /* VHT80 1SS = -1. */
	error = rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple80, 2);
	EXPECT(error == 0);
	ref_after = rate_field(0x18e8, 10);
	EXPECT(ref_after == ((ref_before - 2) & 0x7f));

	/* VHT80 1SS also shifts VHT1SS against the HT reference, so it cancels. */
	reset(1, 0);
	error = rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple80, 2);
	EXPECT(error == 0);
	before = rate_field(0x3a2c, 0); /* VHT1SS MCS0, rate44. */
	reset(1, 0);
	efuse.txpower[0][37] = 0xf0; /* OFDM multi-SS byte is not VHT80. */
	error = rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple80, 2);
	EXPECT(error == 0);
	after = rate_field(0x3a2c, 0);
	EXPECT(after == before);

	/* The 2SS width80 delta is added for HT and VHT 2SS rates. */
	reset(1, 0);
	error = rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple80, 2);
	EXPECT(error == 0);
	before = rate_field(0x3a14, 0); /* HT2SS MCS0, rate20. */
	reset(1, 0);
	efuse.txpower[0][39] = 0xf0; /* VHT80 2SS = -1. */
	error = rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple80, 2);
	EXPECT(error == 0);
	after = rate_field(0x3a14, 0);
	EXPECT(after == ((before - 2) & 0x7f));

	reset(1, 0);
	error = rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple80, 2);
	EXPECT(error == 0);
	before = rate_field(0x3a34, 16); /* VHT2SS MCS0. */
	reset(1, 0);
	efuse.txpower[0][38] = efuse.txpower[1][38] = 0xf0;
	error = rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple80, 2);
	EXPECT(error == 0);
	after = rate_field(0x3a34, 16);
	EXPECT(after == before); /* The shared 1SS delta shifts the reference too. */

	reset(1, 0);
	error = rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple80, 2);
	EXPECT(error == 0);
	before = rate_field(0x3a34, 16);
	reset(1, 0);
	efuse.txpower[0][39] = efuse.txpower[1][39] = 0xf0;
	error = rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple80, 2);
	EXPECT(error == 0);
	after = rate_field(0x3a34, 16);
	EXPECT(after == ((before - 2) & 0x7f));

	/* BW80 averages its MCS base groups but leaves OFDM on the lower group. */
	reset(1, 0);
	error = rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple80, 2);
	EXPECT(error == 0);
	ref_before = rate_field(0x18e8, 10);
	before = rate_field(0x3a04, 0); /* OFDM6M, rate4. */
	reset(1, 0);
	efuse.txpower[0][21] += 4; /* upper of groups2 and3 for center58. */
	error = rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple80, 2);
	EXPECT(error == 0);
	ref_after = rate_field(0x18e8, 10);
	after = rate_field(0x3a04, 0);
	EXPECT(ref_after == ((ref_before + 2) & 0x1ff));
	EXPECT(after == ((before - 2) & 0x7f));
}

#if defined(RTW88_RFK_EXPERIMENT) && \
	!defined(RTW88_TXPWR_DIFFERENTIAL_LIMIT) && \
	!defined(RTW88_TXPWR_MISSING_LIMIT)
static void
rfk_power_proof_width(void)
{
	const struct rtw88_channel_tuple tuple20 = { 64, 64, 20 };
	const struct rtw88_channel_tuple tuple40 = { 64, 62, 40 };
	const struct rtw88_channel_tuple tuple80 = { 64, 58, 80 };
	uint64_t serial;

	reset(1, 0);
	CHECK(rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple20, 2) == 0);
	CHECK(sc.sc_power_program.valid && sc.sc_power_program.ht_mcs == 0xff);
	serial = sc.sc_power_program.serial;
	CHECK(rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple40, 2) == 0);
	CHECK(!sc.sc_power_program.valid);
	CHECK(sc.sc_power_program.serial == serial + 1);
	CHECK(rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple20, 2) == 0);
	CHECK(sc.sc_power_program.valid && sc.sc_power_program.ht_mcs == 0xff);
	serial = sc.sc_power_program.serial;
	CHECK(rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple80, 2) == 0);
	CHECK(!sc.sc_power_program.valid);
	CHECK(sc.sc_power_program.serial == serial + 1);
}
#endif

static int
raw_limit(const struct rtw8822c_table *table, unsigned regd, unsigned bw,
	unsigned rs, unsigned channel)
{
	const struct rtw_txpwr_lmt_cfg_pair *rows = table->data;
	unsigned i;

	for (i = 0; i < table->count; i++)
		if (rows[i].regd == regd && rows[i].band == 1 &&
		    rows[i].bw == bw && rows[i].rs == rs &&
		    rows[i].ch == channel)
			return rows[i].txpwr_lmt;
	return -128;
}

#ifdef RTW88_TXPWR_DIFFERENTIAL_LIMIT
static void
differential_width_limits(void)
{
	const struct rtw88_channel_tuple tuple20 = { 64, 64, 20 };
	const struct rtw88_channel_tuple tuple40 = { 64, 62, 40 };
	const struct rtw88_channel_tuple tuple80 = { 64, 58, 80 };
	uint32_t power20, power40, power80, rate40, rate80;

	reset(1, 0);
	CHECK(rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple20, 2) == 0);
	power20 = model_read(0x18e8, 4);
	reset(1, 0);
	CHECK(rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple40, 2) == 0);
	power40 = model_read(0x18e8, 4);
	rate40 = model_read(0x3a14, 4);
	reset(1, 0);
	CHECK(rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple80, 2) == 0);
	power80 = model_read(0x18e8, 4);
	rate80 = model_read(0x3a2c, 4);
	printf("differential TX-power registers: 20=0x%08x 40=0x%08x "
	    "80=0x%08x 40-rate=0x%08x 80-rate=0x%08x\n",
	    power20, power40, power80, rate40, rate80);
	CHECK(power20 == 0x02dcf893);
	CHECK(power40 == 0x02dcd093);
	CHECK(power80 == 0x02dcd093);
	CHECK(rate40 == 0x04047878);
	CHECK(rate80 == 0x76766a6a);
}
#endif

static void
wide_raw_limits(void)
{
	const unsigned ch64[] = { 1, 2, 3 };
	const int type0_20[] = { 62, 62, 38 };
	const int type5_20[] = { 58, 58, 36 };
	unsigned i;

	for (i = 0; i < 3; i++) {
		CHECK(raw_limit(&rtw8822c_txpwr_lmt_type0_tbl, 2, 0,
		    ch64[i], 64) == type0_20[i]);
		CHECK(raw_limit(&rtw8822c_txpwr_lmt_type5_tbl, 2, 0,
		    ch64[i], 64) == type5_20[i]);
	}
	CHECK(raw_limit(&rtw8822c_txpwr_lmt_type0_tbl, 2, 1, 2, 62) == 64);
	CHECK(raw_limit(&rtw8822c_txpwr_lmt_type0_tbl, 2, 1, 3, 62) == 40);
	CHECK(raw_limit(&rtw8822c_txpwr_lmt_type5_tbl, 2, 1, 2, 62) == 64);
	CHECK(raw_limit(&rtw8822c_txpwr_lmt_type5_tbl, 2, 1, 3, 62) == 40);
	CHECK(raw_limit(&rtw8822c_txpwr_lmt_type0_tbl, 2, 2, 4, 58) == 64);
	CHECK(raw_limit(&rtw8822c_txpwr_lmt_type0_tbl, 2, 2, 5, 58) == 40);
	CHECK(raw_limit(&rtw8822c_txpwr_lmt_type5_tbl, 2, 2, 4, 58) == 64);
	CHECK(raw_limit(&rtw8822c_txpwr_lmt_type5_tbl, 2, 2, 5, 58) == 40);
}

int
main(int argc, char **argv)
{
	static const unsigned invalid_channels[] = {0, 15, 35, 37, 178, 256};
	unsigned i;
	uint32_t before;

	CHECK(argc == 2);
#if defined(RTW88_RFK_EXPERIMENT) && \
	!defined(RTW88_TXPWR_DIFFERENTIAL_LIMIT) && \
	!defined(RTW88_TXPWR_MISSING_LIMIT)
	rfk_power_proof_width();
#endif
#ifdef RTW88_TXPWR_DIFFERENTIAL_LIMIT
	differential_width_limits();
	printf("%u differential-width assertions passed\n", checks);
	return EXIT_SUCCESS;
#endif
#ifdef RTW88_TXPWR_MISSING_LIMIT
	const struct rtw88_channel_tuple tuple40 = { 36, 38, 40 };
	const struct rtw88_channel_tuple tuple80 = { 36, 42, 80 };

	missing_width_rows(1, 1);
	reset(1, 0);
	CHECK(rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple40, 2) == ENOTSUP);
	CHECK(accesses == 0);
	missing_width_rows(1, 0);
	missing_width_rows(2, 1);
	reset(1, 0);
	CHECK(rtw8822c_set_tx_power_tuple(&sc, &efuse, &tuple80, 2) == ENOTSUP);
	CHECK(accesses == 0);
	missing_width_rows(2, 0);

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
	wide_tuples();
	width_efuse_deltas();
	CHECK(delta_failures == 0);
	wide_raw_limits();
	traces(argv[1]);
	printf("%u RTL8822C TX power assertions passed\n", checks);
	return EXIT_SUCCESS;
}
