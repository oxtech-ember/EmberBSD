/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted SD/MMC DMA regression assertions. */
static unsigned int test_failures;

#define CHECK(label, expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "FAIL: %s\n", (label)); \
		test_failures++; \
	} \
} while (0)

static const struct sunxi_mmc_config *
lookup_config(const char *name)
{
	const struct device_compatible_entry *entry;

	for (entry = compat_data; entry->compat != NULL; entry++) {
		if (strcmp(entry->compat, name) == 0)
			return entry->data;
	}
	return NULL;
}

static void
prepare_map(struct sunxi_mmc_softc *sc, struct fake_registers *registers,
	struct bus_dmamap *idma_map, struct sunxi_mmc_idma_descriptor *descriptors,
	const struct sunxi_mmc_config *config, struct bus_dmamap *transfer_map)
{
	memset(sc, 0, sizeof(*sc));
	memset(registers, 0, sizeof(*registers));
	memset(idma_map, 0, sizeof(*idma_map));
	memset(descriptors, 0, sizeof(*descriptors) * 8);
	idma_map->dm_nsegs = 1;
	idma_map->dm_segs[0].ds_addr = 0x8000;
	sc->sc_bst = registers;
	sc->sc_idma_map = idma_map;
	sc->sc_idma_ndesc = 8;
	sc->sc_idma_desc = descriptors;
	sc->sc_idma_size = sizeof(*descriptors) * 8;
	sc->sc_config = config;
	(void)transfer_map;
}

static void
check_dma(const char *label, const char *compatible,
	const struct sunxi_mmc_config *expected_config, unsigned int shift,
	uint32_t address0, uint32_t address1, uint32_t link0, uint32_t dlba,
	unsigned int expected_descriptors)
{
	struct sunxi_mmc_softc sc;
	struct fake_registers registers;
	struct bus_dmamap idma_map;
	struct bus_dmamap transfer_map;
	struct sunxi_mmc_idma_descriptor descriptors[8];
	struct sdmmc_command cmd;
	const struct sunxi_mmc_config *config = lookup_config(compatible);
	int error;

	CHECK("compatible resolves", config != NULL);
	if (config == NULL)
		return;
	CHECK("expected config object", config == expected_config);
	CHECK("expected descriptor address shift", config->idma_shift == shift);
	prepare_map(&sc, &registers, &idma_map, descriptors, config, &transfer_map);
	transfer_map.dm_nsegs = 1;
	transfer_map.dm_segs[0].ds_addr = 0x12340000;
	transfer_map.dm_segs[0].ds_len = 16384;
	memset(&cmd, 0, sizeof(cmd));
	cmd.c_dmamap = &transfer_map;
	cmd.c_datalen = 16384;
	cmd.c_resid = 16384;
	sync_calls = 0;
	error = sunxi_mmc_dma_prepare(&sc, &cmd);
	CHECK("DMA prepare succeeds", error == 0);
	CHECK("all transfer bytes described", cmd.c_resid == 0);
	CHECK("first descriptor payload length", descriptors[0].dma_buf_size ==
	    htole32(expected_descriptors == 2 ? 8192 : 16384));
	CHECK("first buffer address", descriptors[0].dma_buf_addr ==
	    htole32(address0));
	CHECK("descriptor link address", descriptors[0].dma_next ==
	    htole32(expected_descriptors == 2 ? link0 : 0));
	CHECK("DLBA address", registers.words[SUNXI_MMC_DLBA / 4] == dlba);
	CHECK("DMA descriptor sync happened", sync_calls == 1);
	if (expected_descriptors == 2) {
		CHECK("second descriptor payload length", descriptors[1].dma_buf_size ==
		    htole32(8192));
		CHECK("second buffer address", descriptors[1].dma_buf_addr ==
		    htole32(address1));
		CHECK("last descriptor terminates chain", descriptors[1].dma_next == 0);
	}
	(void)label;
}

int
main(void)
{
	const struct sunxi_mmc_config *a100 =
	    lookup_config("allwinner,sun50i-a100-mmc");
	const struct sunxi_mmc_config *a100_emmc =
	    lookup_config("allwinner,sun50i-a100-emmc");

	CHECK("A100 MMC maps to D1 configuration", a100 == &sun20i_d1_mmc_config);
	CHECK("A100 DMA limit is 8192", a100 != NULL && a100->idma_xferlen == 8192);
	CHECK("A100 uses word addresses", a100 != NULL && a100->idma_shift == 2);
	check_dma("A100", "allwinner,sun50i-a100-mmc", &sun20i_d1_mmc_config,
	    2, 0x048d0000, 0x048d0800, 0x2004, 0x2000, 2);
	CHECK("H6 mapping remains unchanged",
	    lookup_config("allwinner,sun50i-h6-mmc") == &sun50i_h6_mmc_config);
	check_dma("H6", "allwinner,sun50i-h6-mmc", &sun50i_h6_mmc_config,
	    0, 0x12340000, 0, 0, 0x8000, 1);
	CHECK("A100 eMMC mapping remains unchanged",
	    a100_emmc == &sun50i_a100_emmc_config);
	CHECK("A100 eMMC transfer limit remains 8192",
	    a100_emmc != NULL && a100_emmc->idma_xferlen == 8192);
	CHECK("A100 eMMC remains word-addressed",
	    a100_emmc != NULL && a100_emmc->idma_shift == 2);
	if (test_failures == 0)
		puts("PASS: A100 MMC DMA format, H6 stability, A100 eMMC stability");
	else
		fprintf(stderr, "%u fixture checks failed\n", test_failures);
	return test_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
