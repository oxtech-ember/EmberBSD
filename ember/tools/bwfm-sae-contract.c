/* SPDX-License-Identifier: BSD-2-Clause */
/* Run: cc -std=c99 -Wall -Wextra -Werror bwfm-sae-contract.c -o /tmp/sae-check */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../sys/dev/ic/bwfm_sae.h"

static unsigned checks;
static void
check(bool pass, const char *name)
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
	const uint8_t good[] = {
		48, 20, 1, 0, 0, 15, 172, 4, 1, 0, 0, 15, 172, 4,
		1, 0, 0, 15, 172, 8, 0xc0, 0
	};
	uint8_t ie[64];
	uint8_t request[60] = { 0 };
	const uint8_t peer[] = { 2, 0, 0, 0, 0, 1 };
	const uint8_t ssid[] = "test-device";
	size_t n;

	check(fullmac_sae_rsn_valid(good, sizeof(good)), "SAE/CCMP/required PMF");
	for (n = 0; n < sizeof(good); n++)
		check(!fullmac_sae_rsn_valid(good, n), "truncated RSN rejected");
	memcpy(ie, good, sizeof(good));
	ie[20] = 0x80;
	check(!fullmac_sae_rsn_valid(ie, 22), "optional PMF rejected");
	ie[20] = 0x40;
	check(!fullmac_sae_rsn_valid(ie, 22), "missing PMF capability rejected");
	ie[20] = 0xc0;
	ie[19] = 2;
	check(!fullmac_sae_rsn_valid(ie, 22), "PSK cannot enter SAE path");
	ie[19] = 8;
	ie[7] = 2;
	check(!fullmac_sae_rsn_valid(ie, 22), "TKIP group rejected");
	ie[7] = 4;
	ie[13] = 2;
	check(!fullmac_sae_rsn_valid(ie, 22), "TKIP pairwise rejected");
	memcpy(ie, good, sizeof(good));
	ie[1] = 26;
	ie[22] = ie[23] = 0;
	memcpy(ie + 24, "\x00\x0f\xac\x06", 4);
	check(fullmac_sae_rsn_valid(ie, 28), "explicit BIP-CMAC-128");
	for (n = 22; n < 28; n++)
		check(!fullmac_sae_rsn_valid(ie, n), "truncated management suite");
	ie[27] = 11;
	check(!fullmac_sae_rsn_valid(ie, 28), "unsupported BIP suite rejected");
	ie[22] = 255;
	ie[23] = 255;
	check(!fullmac_sae_rsn_valid(ie, 28), "oversized PMKID count rejected");
	check(bwfm_sae_cap_token("ap mfp sae_ext fbt ", "sae_ext"), "CM5 external SAE");
	check(!bwfm_sae_cap_token("ap mfp sae_ext fbt ", "sae"), "external is not firmware SAE offload");
	check(!bwfm_sae_cap_token("not_sae_ext sae_ext_extra", "sae_ext"), "whole tokens only");
	check(bwfm_sae_cap_token("sae_ext", "sae_ext"), "single token");
	memcpy(request + 2, peer, sizeof(peer));
	request[8] = sizeof(ssid) - 1;
	memcpy(request + 12, ssid, sizeof(ssid) - 1);
	check(bwfm_sae_request_valid(request, sizeof(request), peer, ssid,
	    sizeof(ssid) - 1), "zero request flags accepted");
	for (n = 0; n < sizeof(request); n++)
		check(!bwfm_sae_request_valid(request, n, peer, ssid,
		    sizeof(ssid) - 1), "truncated firmware auth request");
	request[0] = 1;
	check(bwfm_sae_request_valid(request, sizeof(request), peer, ssid,
	    sizeof(ssid) - 1), "nonzero request flags accepted");
	request[2] ^= 2;
	check(!bwfm_sae_request_valid(request, sizeof(request), peer, ssid,
	    sizeof(ssid) - 1), "wrong authentication peer rejected");
	request[2] ^= 2;
	request[12] ^= 1;
	check(!bwfm_sae_request_valid(request, sizeof(request), peer, ssid,
	    sizeof(ssid) - 1), "wrong authentication SSID rejected");
	request[12] ^= 1;
	request[11] = 1;
	check(!bwfm_sae_request_valid(request, sizeof(request), peer, ssid,
	    sizeof(ssid) - 1), "oversized firmware SSID rejected");
	printf("PASS: %u SAE security boundary checks\n", checks);
	return EXIT_SUCCESS;
}
