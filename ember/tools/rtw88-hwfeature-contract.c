/* SPDX-License-Identifier: BSD-2-Clause */
/* Synthetic bus/FW services; execute the complete actual core startup. */
#define RTW88_START_CORE_ONLY 1
#define main hwfeature_prior_start_main
#include "rtw88-start-contract.c"
#undef main

static struct {
	unsigned accesses, requests, reads, releases;
	unsigned fail_at, fence_at;
	int after, generation, ready, unstable, hci;
	uint8_t id, payload[13];
} h;
static struct rtw88_bus_ops hwops;

static int
hw_event(int after)
{
	if (!after)
		h.accesses++;
	if (h.after != after)
		return 0;
	if (h.accesses == h.fence_at) {
		if (h.generation)
			f.sc->sc_generation++;
		else
			f.sc->sc_stop_requested = 1;
	}
	return h.accesses == h.fail_at ? EIO : 0;
}

static int
hw_read1(void *cookie, uint32_t reg, uint8_t *out)
{
	int error;
	if (reg < 0x1a0 || reg > 0x1ae)
		return read_1(cookie, reg, out);
	CHECK(h.ready && !h.hci && !f.sc->sc_running);
	CHECK(reg != 0x1a1); /* The reference does not define a length here. */
	if ((error = hw_event(0)) != 0)
		return error;
	h.reads++;
	if (reg == 0x1a0 && h.unstable && h.reads > 1)
		f.mmio[reg] = 0x42;
	error = read_1(cookie, reg, out);
	return error != 0 ? error : hw_event(1);
}

static int
hw_write1(void *cookie, uint32_t reg, uint8_t value)
{
	int error;
	if (reg != 0x1a0) {
		if (reg == 0x303 && h.ready && !f.sc->sc_stop_requested)
			h.hci = 1;
		return write_1(cookie, reg, value);
	}
	if ((error = hw_event(0)) != 0)
		return error;
	if (value == 0xfd) {
		CHECK(f.power_post_seen && !h.ready);
		h.requests++;
	} else {
		CHECK(value == 0 && h.ready && h.reads == 15);
		h.releases++;
	}
	error = write_1(cookie, reg, value);
	return error != 0 ? error : hw_event(1);
}

static int
hw_read4(void *cookie, uint32_t reg, uint32_t *out)
{
	int error = read_4(cookie, reg, out);
	if (!error && reg == 0x80 && (*out & 0x8000) && !h.ready) {
		h.ready = 1;
		if (h.requests) {
			f.mmio[0x1a0] = h.id;
			memcpy(f.mmio + 0x1a2, h.payload, sizeof(h.payload));
		}
	}
	return error;
}

static void
hw_reset(struct rtw88_softc *sc)
{
	reset_bus(sc);
	memset(&h, 0, sizeof(h));
	h.id = 0x19;
	/* LE word: HCI9, BW7, NSS2, antennas2, protocol3. */
	h.payload[4] = 9; h.payload[6] = 0x57; h.payload[7] = 0x0c;
	hwops = ops;
	hwops.read_1 = hw_read1; hwops.write_1 = hw_write1;
	hwops.read_4 = hw_read4;
	sc->sc_ops = &hwops;
	CHECK(attach_ok(sc));
}

static void
test_transport(void)
{
	struct rtw88_softc sc;
	hw_reset(&sc);
	CHECK(rtw88_start(&sc) == 0);
	CHECK(h.requests == 1 && h.reads == 15 && h.releases == 1);
	CHECK(f.mmio[0x1a0] == 0);
	CHECK(rtw88_stop(&sc) == 0);
	rtw88_core_detach(&sc);
}

static void
test_decoder(void)
{
	uint8_t bytes[15] = { 0 };
	struct rtw88_hw_feature value, saved;
	memset(&saved, 0xa5, sizeof(saved));
	for (size_t len = 0; len <= 14; len++) {
		value = saved;
		CHECK(rtw88_hw_feature_decode(bytes + 1, len, &value) ==
		    (len == 13 ? 0 : EINVAL));
		if (len != 13)
			CHECK(memcmp(&value, &saved, sizeof(value)) == 0);
	}
	CHECK(rtw88_hw_feature_decode(NULL, 13, &value) == EINVAL);
	CHECK(rtw88_hw_feature_decode(bytes, 13, NULL) == EINVAL);
	for (unsigned bw = 0; bw < 8; bw++)
	for (unsigned nss = 0; nss < 4; nss++)
	for (unsigned ant = 0; ant < 8; ant++)
	for (unsigned proto = 0; proto < 4; proto++) {
		uint32_t word = 0xf000fff9u | bw << 16 | nss << 19 |
		    ant << 21 | proto << 26;
		uint8_t *p = bytes + 1; /* Intentionally unaligned. */
		unsigned known = (bw >= 6 ? 1 : 0) |
		    (nss == 1 || nss == 2 ? 2 : 0) |
		    (ant == 1 || ant == 2 ? 4 : 0) | (proto == 3 ? 8 : 0);
		for (unsigned i = 0; i < 4; i++) p[4+i] = word >> (8*i);
		CHECK(rtw88_hw_feature_decode(p, 13, &value) == 0);
		CHECK(value.known == known && value.hci == 9 && value.bw == bw);
		CHECK(value.nss == nss && value.antennas == ant && value.protocol == proto);
		CHECK(value.bandwidths == (bw == 6 ? 3 : bw == 7 ? 7 : 0));
		CHECK(memcmp(value.raw, p, 13) == 0);
	}
}

static void
test_optional(void)
{
	const uint8_t ids[] = {0, 0xfd, 0x42, 0x19};
	for (unsigned i = 0; i < sizeof(ids); i++) {
		struct rtw88_softc sc;
		hw_reset(&sc); h.id = ids[i];
		memset(h.payload, 0, sizeof(h.payload));
		CHECK(rtw88_start(&sc) == 0 && sc.sc_started);
		CHECK(sc.sc_hw_feature.requested && sc.sc_hw_feature.requested_generation == sc.sc_generation);
		CHECK(sc.sc_hw_feature.observed == (ids[i] == 0x19));
		CHECK(sc.sc_hw_feature.report.known == 0);
		CHECK(sc.sc_stats.hw_missing == (ids[i] == 0 || ids[i] == 0xfd));
		CHECK(sc.sc_stats.hw_foreign == (ids[i] == 0x42));
		CHECK(h.releases == (ids[i] == 0x19));
		CHECK(f.mmio[0x1a0] == (ids[i] == 0x19 ? 0 : ids[i]));
		rtw88_core_detach(&sc);
	}
	{
		struct rtw88_softc sc;
		hw_reset(&sc); h.unstable = 1;
		CHECK(rtw88_start(&sc) == 0);
		CHECK(!sc.sc_hw_feature.observed && sc.sc_stats.hw_unstable == 1);
		CHECK(h.releases == 0 && f.mmio[0x1a0] == 0x42);
		rtw88_core_detach(&sc);
	}
}

static void
test_startup_fields(void)
{
	for (unsigned bw = 6; bw <= 7; bw++)
	for (unsigned nss = 1; nss <= 2; nss++) {
		struct rtw88_softc sc;
		hw_reset(&sc);
		h.payload[6] = bw | nss << 3 | nss << 5;
		CHECK(rtw88_start(&sc) == 0 && h.hci);
		CHECK(sc.sc_hw_feature.report.known == 15);
		CHECK(sc.sc_hw_feature.report.nss == nss);
		CHECK(sc.sc_hw_feature.report.antennas == nss);
		CHECK(sc.sc_hw_feature.report.bandwidths == (bw == 6 ? 3 : 7));
		rtw88_core_detach(&sc);
	}
}

static void
test_faults(void)
{
	for (unsigned kind = 0; kind < 3; kind++)
	for (unsigned after = 0; after < 2; after++)
	for (unsigned access = 1; access <= 17; access++) {
		struct rtw88_softc sc;
		hw_reset(&sc); h.after = after;
		if (kind == 0) h.fail_at = access;
		else { h.fence_at = access; h.generation = kind == 2; }
		CHECK(rtw88_start(&sc) == (kind == 0 ? EIO : EINTR));
		CHECK(!sc.sc_started && !sc.sc_running && !sc.sc_dma_master);
		CHECK(!sc.sc_power_dirty && !sc.sc_fw_dma_owned);
		CHECK(!sc.sc_hw_feature.requested && !sc.sc_hw_feature.observed);
		CHECK(sc.sc_stats.hw_reports == 0);
		CHECK(sc.sc_stats.hw_io_errors == (kind == 0));
		CHECK(h.accesses == access);
		rtw88_core_detach(&sc);
	}
}

static void
test_restart(void)
{
	struct rtw88_softc sc;
	uint32_t gen;
	hw_reset(&sc);
	CHECK(rtw88_start(&sc) == 0);
	gen = sc.sc_generation;
	CHECK(sc.sc_hw_feature.observed && sc.sc_hw_feature.report.known == 15);
	CHECK(sc.sc_hw_feature.report.bandwidths == 7);
	CHECK(sc.sc_hw_feature.observed_generation == gen);
	CHECK(rtw88_stop(&sc) == 0);
	CHECK(!sc.sc_hw_feature.observed && !sc.sc_hw_feature.report.known);
	/* Model a fresh FW boot that provides no new report, retaining old payload. */
	h.ready = h.hci = 0; h.id = 0; h.reads = h.requests = h.releases = 0;
	CHECK(rtw88_start(&sc) == 0);
	CHECK(sc.sc_generation != gen && !sc.sc_hw_feature.observed);
	CHECK(sc.sc_hw_feature.requested_generation == sc.sc_generation);
	CHECK(!sc.sc_hw_feature.report.known && h.requests == 1 && !h.releases);
	CHECK(sc.sc_stats.hw_reports == 1 && sc.sc_stats.hw_requests == 2);
	rtw88_core_detach(&sc);
}

int
main(void)
{
	test_transport();
	test_decoder();
	test_optional();
	test_faults();
	test_startup_fields();
	test_restart();
	printf("rtw88 HW feature contract: %u assertions, %u failures\n", assertions, failures);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
