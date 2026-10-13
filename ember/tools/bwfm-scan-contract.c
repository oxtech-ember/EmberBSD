/* SPDX-License-Identifier: BSD-2-Clause */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "bwfm-scan-constants.h"
/* Match the kernel namespace when checking the shared helpers. */
extern const char version[];
#include "../../sys/dev/ic/bwfm_scan.h"

static unsigned int checks;
static void
check(int pass, const char *name)
{
	checks++;
	if (!pass) {
		fprintf(stderr, "FAIL: %s\n", name);
		exit(EXIT_FAILURE);
	}
}

int
main(void)
{
	unsigned int i;
	int dbm;

	/* A scan begun on channel 11 must preserve a 5 GHz BSS on channel 60. */
	check(bwfm_scan_channel(60, 0xe23a, 2) == 60, "firmware control channel");
	check(bwfm_scan_channel(0, 0xe23a, 2) == 60, "80 MHz fallback primary");
	check(bwfm_scan_channel(0, 0x100b, 2) == 11, "2.4 GHz channel 11");
	check(bwfm_scan_channel(0, 0xd024, 2) == 36, "5 GHz 20 MHz");
	for (i = 0; i < 4; i++)
		check(bwfm_scan_channel(0, 0xe02a | (i << 8), 2) == 36 + 4 * i,
		    "all 80 MHz primary channels");
	for (i = 0; i < 8; i++)
		check(bwfm_scan_channel(0, 0xe832 | (i << 8), 2) == 36 + 4 * i,
		    "all 160 MHz primary channels");
	check(bwfm_scan_channel(0, 0xd826, 2) == 36, "40 MHz lower");
	check(bwfm_scan_channel(0, 0xd926, 2) == 40, "40 MHz upper");
	check(bwfm_scan_channel(0, 0x2b06, 1) == 6, "legacy 20 MHz");
	check(bwfm_scan_channel(0, 0x1d26, 1) == 36, "legacy 40 MHz lower");
	check(bwfm_scan_channel(0, 0x1e26, 1) == 40, "legacy 40 MHz upper");
	check(bwfm_scan_channel(0, 0xe001, 2) == 0, "channel underflow rejected");
	check(bwfm_scan_channel(0, 0xd9ff, 2) == 0, "channel overflow rejected");
	check(bwfm_scan_channel(0, 0xe42a, 2) == 0, "invalid 80 MHz sideband");
	check(bwfm_scan_channel(0, 0xf02a, 2) == 0, "unknown 80+80 mapping");
	check(bwfm_scan_channel(0, 0xd024, 3) == 0, "unknown firmware format");
	check(bwfm_scan_channel(0, 0x5024, 2) == 0, "unsupported 3 GHz band");
	check(bwfm_scan_channel(0, 0x9024, 2) == 0, "unsupported 4 GHz band");
	check(bwfm_scan_channel(0, 0x1024, 2) == 0, "2 GHz band with channel 36");
	check(bwfm_scan_channel(0, 0xd00b, 2) == 0, "5 GHz band with channel 11");
	check(bwfm_scan_channel(0, 0x200a, 2) == 0, "80 MHz in 2 GHz band");
	check(bwfm_scan_channel(0, 0x2906, 1) == 0, "legacy 20 MHz sideband");
	check(bwfm_scan_channel(0, 0x0b24, 1) == 0, "legacy unknown band");
	check(bwfm_scan_channel(0, 0x2b24, 1) == 0, "legacy band mismatch");
	check(bwfm_scan_rssi(-62) == 38, "negative RSSI does not wrap to 194");
	check(bwfm_scan_rssi(-88) == 12, "weak 5 GHz quality");
	check(bwfm_scan_rssi(INT16_MIN) == 1, "RSSI lower bound");
	check(bwfm_scan_rssi(INT16_MAX) == 100, "RSSI upper bound");
	for (dbm = -128; dbm < 0; dbm++)
		check(bwfm_scan_rssi(dbm) <= bwfm_scan_rssi(dbm + 1),
		    "signal ranking is monotonic");
	printf("PASS: %u firmware scan metadata checks\n", checks);
	return EXIT_SUCCESS;
}
