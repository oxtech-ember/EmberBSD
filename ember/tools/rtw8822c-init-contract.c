/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: actual-source normal table-load call graph contract, AI-assisted. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <dev/ic/rtw8822c_table.h>

/* Table decoding has separate contracts. Record this caller's dispatches. */
struct rtw88_softc { unsigned sc_rfe; };
#define TABLE(name, id) const struct rtw8822c_table name = { .count = id }
TABLE(rtw8822c_mac_tbl, 1);
TABLE(rtw8822c_bb_tbl, 2);
TABLE(rtw8822c_agc_tbl, 3);
TABLE(rtw8822c_array_mp_cal_init_tbl, 4);
TABLE(rtw8822c_rf_b_tbl, 5);
TABLE(rtw8822c_rf_a_tbl, 6);
TABLE(rtw8822c_dpk_mac_bb_tbl, 7);
TABLE(rtw8822c_dpk_afe_no_dpk_tbl, 8);
TABLE(rtw8822c_dpk_afe_is_dpk_tbl, 9);
TABLE(rtw8822c_bb_pg_type0_tbl, 10);
TABLE(rtw8822c_txpwr_lmt_type0_tbl, 11);
TABLE(rtw8822c_txpwr_lmt_type5_tbl, 12);
#define RTW8822C_BIT(n) (1U << (n))

static FILE *expected;
static unsigned assertions, metadata, dispatches, fail_at;
#define CHECK(x) do { assertions++; if (!(x)) { \
	fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); exit(1); \
} } while (0)
static void
event(unsigned table, unsigned addr, unsigned mask, unsigned value)
{

	unsigned t, a, m, v;

	CHECK(fscanf(expected, "%u %x %x %x", &t, &a, &m, &v) == 4);
	CHECK(t == table && a == addr && m == mask && v == value);
}
static int
rtw8822c_apply_table(struct rtw88_softc *sc, const struct rtw8822c_table *tbl)
{

	if (++dispatches == fail_at)
		return EIO;
	if (tbl->count == 10 || tbl->count == 11 || tbl->count == 12) {
		metadata++;
		if (tbl->count != 10)
			CHECK(tbl->count == (sc->sc_rfe == 5 ? 12U : 11U));
	} else {
		event(tbl->count, 0, 0, 0);
	}
	return 0;
}
static int
rtw8822c_write32_mask(struct rtw88_softc *sc, uint32_t addr,
    uint32_t mask, uint32_t value)
{

	(void)sc;
	if (++dispatches == fail_at)
		return EIO;
	event(0, addr, mask, value);
	return 0;
}
#include "body.c"
int
main(int argc, char **argv)
{
	struct rtw88_softc sc;
	unsigned rfe, stop, count = 0;

	CHECK(argc == 2);
	for (rfe = 0; rfe <= 6; rfe++) {
		/* Fail every boundary once; no later table may run after EIO. */
		for (stop = 0; stop <= count; stop++) {
			expected = fopen(argv[1], "r");
			CHECK(expected != NULL);
			sc.sc_rfe = rfe;
			metadata = dispatches = 0;
			fail_at = stop;
			CHECK(rtw8822c_load_tables(&sc) == (stop ? EIO : 0));
			if (stop) {
				CHECK(dispatches == stop);
			} else {
				CHECK(fscanf(expected, "%*s") == EOF);
				CHECK(metadata == 2);
				count = dispatches;
			}
			CHECK(fclose(expected) == 0);
		}
	}
	printf("rtw8822c normal table graph: %u assertions, 0 failures\n", assertions);
	return 0;
}
