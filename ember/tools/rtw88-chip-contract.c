/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD chip selection and capability contracts, AI-assisted. */
#include <dev/ic/rtw88_chip.h>
#include <dev/ic/rtw88_calibration.h>
#define main rtw88_start_contract_main
#include "rtw88-start-contract.c"
#undef main

static unsigned selected_start, selected_stop, selected_channel, selected_tx;

static int
count_start(struct rtw88_softc *sc, const struct rtw88_channel_tuple *tuple)
{
	(void)sc;
	CHECK(tuple->primary == 1);
	selected_start++;
	return EAGAIN;
}

static int
count_stop(struct rtw88_softc *sc)
{
	(void)sc;
	selected_stop++;
	return 0;
}

static int
count_channel(struct rtw88_softc *sc, const struct rtw88_channel_tuple *tuple)
{
	(void)sc;
	CHECK(tuple->primary == 1);
	selected_channel++;
	return EAGAIN;
}

static int
count_tx(struct rtw88_softc *sc, uint8_t *out, size_t size,
    const struct rtw88_tx_info *info)
{
	(void)sc;
	(void)out;
	(void)size;
	(void)info;
	selected_tx++;
	return EAGAIN;
}

static void
test_chip_dispatch(void)
{
	struct rtw88_softc sc;
	struct rtw88_chip_ops chip;
	struct rtw88_tx_info info = { 0 };
	const struct rtw88_channel_tuple tuple = { 1, 1, 20 };
	uint8_t descriptor[64];

	reset_bus(&sc);
	chip = rtw8822c_chip_ops;
	chip.start = count_start;
	chip.stop = count_stop;
	chip.channel = count_channel;
	chip.tx_desc = count_tx;
	sc.sc_chip = &chip;
	selected_start = selected_stop = selected_channel = selected_tx = 0;
	CHECK(rtw88_start_tuple(&sc, &tuple) == EAGAIN);
	CHECK(rtw88_channel_apply(&sc, &tuple) == EAGAIN);
	CHECK(rtw88_chip_tx_desc(&sc, descriptor, sizeof(descriptor), &info) == EAGAIN);
	sc.sc_power_dirty = 1;
	CHECK(rtw88_stop(&sc) == 0);
	CHECK(selected_start == 1 && selected_stop == 1 &&
	    selected_channel == 1 && selected_tx == 1);
	CHECK(f.reads == 0 && f.writes == 0 && f.alloc_calls == 0);
}

static void
test_chip_required_operations(void)
{
	struct rtw88_softc sc;
	struct rtw88_chip_ops chip;

	reset_bus(&sc);
	sc.sc_chip = NULL;
	CHECK(rtw88_core_attach(&sc) == EINVAL);
	CHECK(f.alloc_calls == 0 && f.reads == 0 && f.writes == 0);
	reset_bus(&sc);
	chip = rtw8822c_chip_ops;
	chip.rx_desc = NULL;
	sc.sc_chip = &chip;
	CHECK(rtw88_core_attach(&sc) == EINVAL);
	CHECK(f.alloc_calls == 0 && f.reads == 0 && f.writes == 0);
}

static void
test_chip_capabilities(void)
{
	struct rtw88_softc sc;
	struct rtw88_chip_ops chip;
	struct rtw88_caps caps, saved;

	reset_bus(&sc);
	CHECK(rtw88_get_caps(&sc, &caps) == 0);
	CHECK((caps.bands & (RTW88_BAND_2G | RTW88_BAND_5G)) ==
	    (RTW88_BAND_2G | RTW88_BAND_5G));
	CHECK(caps.tx_desc_size == RTW8822C_TX_PACKET_DESC_SIZE &&
	    caps.rx_desc_size == RTW8822C_RX_PACKET_DESC_SIZE);
	chip = rtw8822c_chip_ops;
	chip.caps.bands = RTW88_BAND_2G;
	chip.caps.widths = RTW88_WIDTH_20;
	chip.caps.nss = 1;
	chip.caps.features &= ~RTW88_CAP_VHT;
	sc.sc_chip = &chip;
	CHECK(rtw88_get_caps(&sc, &caps) == 0);
	CHECK(caps.bands == RTW88_BAND_2G && caps.widths == RTW88_WIDTH_20 &&
	    caps.nss == 1 && (caps.features & RTW88_CAP_VHT) == 0);
	memset(&caps, 0xa5, sizeof(caps));
	memcpy(&saved, &caps, sizeof(saved));
	sc.sc_chip = NULL;
	CHECK(rtw88_get_caps(&sc, &caps) == EINVAL);
	CHECK(memcmp(&caps, &saved, sizeof(caps)) == 0);
}

static void
test_chip_absent_calibration(void)
{
	struct rtw88_softc sc;
	struct rtw88_chip_ops chip;
	struct rtw88_calibration_services services = { 0 };
	uint64_t sentinel[64], saved[64];

	reset_bus(&sc);
	chip = rtw8822c_chip_ops;
	chip.calibration = NULL;
	sc.sc_chip = &chip;
	CHECK(rtw88_calibration_active(&sc) == 0);
	CHECK(rtw88_calibration_blocked(&sc) == 0);
	CHECK(rtw88_calibration_begin(&sc, &services) == EOPNOTSUPP);
	CHECK(rtw88_calibration_validate(&sc) == EOPNOTSUPP);
	CHECK(rtw88_calibration_run(&sc) == EOPNOTSUPP);
	CHECK(rtw88_calibration_retire(&sc) == EOPNOTSUPP);
	CHECK(rtw88_calibration_seal(&sc) == EOPNOTSUPP);
	/* An absent service cannot inspect or modify an opaque output. */
	memset(sentinel, 0xa5, sizeof(sentinel));
	memcpy(saved, sentinel, sizeof(saved));
	CHECK(rtw88_calibration_progress(&sc,
	    (struct rtw88_rfk_diag *)(void *)sentinel) == EOPNOTSUPP);
	CHECK(memcmp(sentinel, saved, sizeof(saved)) == 0);
	CHECK(f.reads == 0 && f.writes == 0 && f.alloc_calls == 0);
}

int
main(void)
{
	if (rtw88_start_contract_main() != EXIT_SUCCESS)
		return EXIT_FAILURE;
	test_chip_required_operations();
	test_chip_absent_calibration();
	test_chip_dispatch();
	test_chip_capabilities();
	printf("rtw88 chip contract: %u assertions, %u failures\n", assertions, failures);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
