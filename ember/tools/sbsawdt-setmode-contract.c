/*
 * Run the production SBSA watchdog setmode and tickle functions from
 * sys/arch/arm/acpi/sbsawdt_acpi.c against a recorded register bus.
 * ember/tests/sbsawdt-setmode.sh extracts them.  AI-assisted (Claude Code,
 * Claude Opus 5.5).
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#undef __BIT
#define __BIT(n)		(1U << (n))
#undef howmany
#define howmany(x, y)		(((x) + ((y) - 1)) / (y))
#define WDOG_MODE_MASK		0x03
#define WDOG_MODE_DISARMED	0
#define WDOG_MODE_KTICKLE	1
#define WDOG_PERIOD_DEFAULT	((u_int)-1)

typedef unsigned int u_int;
typedef void *device_t;
typedef int bus_space_tag_t;
typedef int bus_space_handle_t;		/* 1 refresh frame, 2 control frame */

struct sysmon_wdog {
	void	*smw_cookie;
	u_int	smw_mode;
	u_int	smw_period;
};

static struct { int frame; uint32_t reg, val; } wr[16];
static int nwr, notes;

/* Register model: system counter, WCS, WOR and the compare value. */
static struct {
	uint64_t	cnt, wcv;
	uint32_t	wcs, wor, hz;
	bool		wrr_refreshes, wor_refreshes;
} hw;

static void
hw_refresh(void)
{
	hw.wcv = hw.cnt + hw.wor;
}

static void
delay(unsigned us)
{
	hw.cnt += (uint64_t)us * hw.hz / 1000000;
}

static void
bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h, uint32_t reg,
    uint32_t val)
{
	(void)t;
	if (nwr < 16) {
		wr[nwr].frame = h;
		wr[nwr].reg = reg;
		wr[nwr].val = val;
	}
	nwr++;
	if (h == 2 && reg == 0x000) {		/* WCS: explicit refresh */
		hw.wcs = val;
		hw_refresh();
	} else if (h == 2 && reg == 0x008) {	/* WOR: explicit refresh */
		hw.wor = val;
		if (hw.wor_refreshes)
			hw_refresh();
	} else if (h == 1 && reg == 0x000 && hw.wrr_refreshes) {
		hw_refresh();
	}
}

static uint32_t
bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, uint32_t reg)
{
	(void)t;
	if (h == 2 && reg == 0x010)
		return (uint32_t)hw.wcv;
	return 0;
}

#define aprint_normal_dev(dev, ...)	((void)(dev), notes++)

#include "sbsawdt-setmode.h"

static int checks, failures;

#define CHECK(cond) do {						\
	checks++;							\
	if (!(cond)) {							\
		failures++;						\
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
	}								\
} while (0)

static struct sbsawdt_acpi_softc sc;
static struct sysmon_wdog smw;

static void
fresh(uint32_t cntfreq, u_int max_period, bool wrr_refreshes,
    bool wor_refreshes)
{
	memset(&sc, 0, sizeof(sc));
	memset(&hw, 0, sizeof(hw));
	sc.sc_bsh_r = 1;
	sc.sc_bsh_c = 2;
	sc.sc_cntfreq = cntfreq;
	sc.sc_max_period = max_period;
	hw.hz = cntfreq;
	hw.cnt = 633013328;
	hw.wrr_refreshes = wrr_refreshes;
	hw.wor_refreshes = wor_refreshes;
	notes = 0;
}

static int
setmode(u_int mode, u_int period)
{
	smw.smw_cookie = &sc;
	smw.smw_mode = mode;
	smw.smw_period = period;
	nwr = 0;
	return sbsawdt_acpi_setmode(&smw);
}

static void
check_armed(uint32_t cntfreq, u_int period, uint32_t wor)
{
	CHECK(nwr == 5);
	CHECK(wr[0].frame == 2 && wr[0].reg == C_WCS_REG && wr[0].val == 0);
	CHECK(wr[1].frame == 2 && wr[1].reg == C_WOR_REG && wr[1].val == wor);
	CHECK(wr[2].frame == 2 && wr[2].reg == C_WCS_REG &&
	    wr[2].val == C_WCS_EN);
	CHECK(wr[3].frame == 1 && wr[3].reg == R_WRR_REG);
	CHECK(wr[4].frame == 2 && wr[4].reg == C_WOR_REG && wr[4].val == wor);
	CHECK(hw.wcs == C_WCS_EN && hw.wor == wor);
	CHECK(sc.sc_wor == wor);
	/* Two offsets — WS0, then WS1 — make the whole period. */
	CHECK((uint64_t)wor * 2 == (uint64_t)period * cntfreq);
}

/* A tickle after some time must move the compare value forward. */
static void
check_tickle_refreshes(uint32_t wor)
{
	const uint64_t before = hw.wcv;

	delay(1000000);
	nwr = 0;
	CHECK(sbsawdt_acpi_tickle(&smw) == 0);
	CHECK(nwr == 2);
	CHECK(wr[0].frame == 1 && wr[0].reg == R_WRR_REG);
	CHECK(wr[1].frame == 2 && wr[1].reg == C_WOR_REG && wr[1].val == wor);
	CHECK(hw.wor == wor);
	CHECK(hw.wcv == hw.cnt + hw.wor && hw.wcv > before);
}

int
main(void)
{
	/* CIX P1: 1 GHz system counter, the driver's 5 s maximum. */
	const uint32_t ghz = 1000000000U;
	const u_int max = howmany((uint64_t)UINT32_MAX, ghz);

	CHECK(max == 5);

	/*
	 * Three implementations: both registers refresh (SBSA), WRR ignored
	 * (CIX P1, measured), WOR not refreshing (WRR only).  The tickle
	 * must refresh each of them.
	 */
	const bool impl[3][2] = { { true, true }, { false, true },
	    { true, false } };
	for (int i = 0; i < 3; i++) {
		fresh(ghz, max, impl[i][0], impl[i][1]);
		CHECK(setmode(WDOG_MODE_KTICKLE, 5) == 0);
		check_armed(ghz, 5, 2500000000U);
		check_tickle_refreshes(2500000000U);
		check_tickle_refreshes(2500000000U);
		CHECK(setmode(WDOG_MODE_KTICKLE, 1) == 0);
		check_armed(ghz, 1, 500000000U);
		check_tickle_refreshes(500000000U);
		CHECK(setmode(WDOG_MODE_KTICKLE, WDOG_PERIOD_DEFAULT) == 0);
		CHECK(smw.smw_period == 5);
		check_armed(ghz, 5, 2500000000U);
		check_tickle_refreshes(2500000000U);
		CHECK(notes == 0);
	}

	/* Older systems: 25 MHz counter, long periods. */
	const u_int max25 = howmany((uint64_t)UINT32_MAX, 25000000U);
	fresh(25000000U, max25, true, true);
	CHECK(setmode(WDOG_MODE_KTICKLE, max25) == 0);
	check_armed(25000000U, max25, max25 * 12500000U);

	/* Over the maximum: refused, nothing written. */
	fresh(ghz, max, true, true);
	CHECK(setmode(WDOG_MODE_KTICKLE, 6) == EINVAL);
	CHECK(nwr == 0);
	/* An offset beyond the 32-bit register: refused, nothing written. */
	fresh(ghz, 10, true, true);
	CHECK(setmode(WDOG_MODE_KTICKLE, 9) == EINVAL);
	CHECK(nwr == 0);

	/* Disarm stops the watchdog and nothing else. */
	fresh(ghz, max, true, true);
	CHECK(setmode(WDOG_MODE_DISARMED, 5) == 0);
	CHECK(nwr == 1 && wr[0].frame == 2 && wr[0].reg == C_WCS_REG &&
	    wr[0].val == 0);

	printf("sbsawdt setmode contract: %d checks, %d failures\n", checks,
	    failures);
	return failures != 0;
}
