/* SPDX-License-Identifier: BSD-2-Clause */
/* Portable regression for the net80211 RSN parser. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../ieee80211_rsn.h"

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
	uint8_t assoc[64];
	uint8_t rsnxe_capab;
	const uint8_t *rsn;
	size_t n, rsn_len, rsn_offset;

	check(!ieee80211_sae_rsn_valid(NULL, 0), "null empty input rejected");
	check(!ieee80211_sae_rsn_valid(NULL, 22), "null sized input rejected");
	check(ieee80211_sae_rsn_valid(good, sizeof(good)), "SAE/CCMP/required PMF");
	{
		const uint8_t h2e[] = { 244, 1, 0x20 };
		const uint8_t future[] = { 244, 2, 0x31, 0x80 };
		const uint8_t outer_mismatch[] = { 244, 2, 0x20 };
		const uint8_t truncated[] = { 244, 1 };
		const uint8_t encoded_mismatch[] = { 244, 2, 0x00, 0x80 };
		const uint8_t zero_payload[] = { 244, 0 };
		const uint8_t wrong_id[] = { 48, 1, 0x20 };
		uint8_t oversized[19] = { 244, 17, 0x10 };

		check(ieee80211_rsnxe_parse(h2e, sizeof(h2e), &rsnxe_capab) &&
		    rsnxe_capab == 0x20, "standalone SAE H2E RSNXE");
		check(ieee80211_rsnxe_parse(future, sizeof(future),
		    &rsnxe_capab) && rsnxe_capab == 0x31,
		    "multi-octet RSNXE first capability octet");
		rsnxe_capab = 0xff;
		check(!ieee80211_rsnxe_parse(outer_mismatch,
		    sizeof(outer_mismatch), &rsnxe_capab) && rsnxe_capab == 0,
		    "RSNXE outer length mismatch rejected and output cleared");
		check(!ieee80211_rsnxe_parse(truncated, sizeof(truncated), NULL),
		    "truncated RSNXE capability length rejected");
		check(!ieee80211_rsnxe_parse(encoded_mismatch,
		    sizeof(encoded_mismatch), NULL),
		    "encoded RSNXE capability length rejected");
		check(!ieee80211_rsnxe_parse(zero_payload,
		    sizeof(zero_payload), NULL), "empty RSNXE rejected");
		check(!ieee80211_rsnxe_parse(wrong_id, sizeof(wrong_id), NULL),
		    "non-RSNXE element rejected");
		check(!ieee80211_rsnxe_parse(oversized, sizeof(oversized), NULL),
		    "oversized RSNXE rejected");
	}
	memcpy(assoc, good, sizeof(good));
	assoc[22] = 244;
	assoc[23] = 1;
	assoc[24] = 0x20;
	check(ieee80211_assoc_rsn_parse(assoc, 25, &rsn, &rsn_len,
	    &rsn_offset), "RSN plus RSNXE association stream");
	check(rsn == assoc && rsn_len == sizeof(good) && rsn_offset == 0,
	    "association parser returns borrowed RSN view");
	check(ieee80211_sae_rsn_valid(rsn, rsn_len),
	    "SAE validation uses RSN element, not RSNXE tail");
	memcpy(assoc + sizeof(good), good, sizeof(good));
	check(!ieee80211_assoc_rsn_parse(assoc, 2 * sizeof(good), &rsn,
	    &rsn_len, &rsn_offset), "duplicate RSN elements rejected");
	check(rsn == NULL && rsn_len == 0 && rsn_offset == 0,
	    "failed stream parse clears output view");
	memcpy(assoc, good, sizeof(good));
	assoc[22] = 244;
	assoc[23] = 1;
	assoc[24] = 0x21;
	check(!ieee80211_assoc_rsn_parse(assoc, 25, &rsn, &rsn_len,
	    &rsn_offset), "inconsistent RSNXE length bits rejected");
	assoc[24] = 1;
	assoc[25] = 244;
	assoc[26] = 1;
	assoc[27] = 0x20;
	check(!ieee80211_assoc_rsn_parse(assoc, 28, &rsn, &rsn_len,
	    &rsn_offset), "duplicate RSNXE elements rejected");
	assoc[25] = 221;
	assoc[26] = 2;
	check(!ieee80211_assoc_rsn_parse(assoc, 27, &rsn, &rsn_len,
	    &rsn_offset), "truncated association element rejected");
	for (n = 0; n < sizeof(good); n++)
		check(!ieee80211_sae_rsn_valid(good, n), "truncated RSN rejected");
	memcpy(ie, good, sizeof(good));
	ie[20] = 0x80;
	check(!ieee80211_sae_rsn_valid(ie, 22), "optional PMF rejected");
	ie[20] = 0x40;
	check(!ieee80211_sae_rsn_valid(ie, 22), "missing PMF capability rejected");
	ie[20] = 0xc0;
	ie[19] = 2;
	check(!ieee80211_sae_rsn_valid(ie, 22), "PSK cannot enter SAE path");
	ie[19] = 8;
	ie[7] = 2;
	check(!ieee80211_sae_rsn_valid(ie, 22), "TKIP group rejected");
	ie[7] = 4;
	ie[13] = 2;
	check(!ieee80211_sae_rsn_valid(ie, 22), "TKIP pairwise rejected");
	memcpy(ie, good, sizeof(good));
	ie[1] = 26;
	ie[22] = ie[23] = 0;
	memcpy(ie + 24, "\x00\x0f\xac\x06", 4);
	check(ieee80211_sae_rsn_valid(ie, 28), "explicit BIP-CMAC-128");
	for (n = 22; n < 28; n++)
		check(!ieee80211_sae_rsn_valid(ie, n), "truncated management suite");
	ie[27] = 11;
	check(!ieee80211_sae_rsn_valid(ie, 28), "unsupported BIP suite rejected");
	ie[22] = 255;
	ie[23] = 255;
	check(!ieee80211_sae_rsn_valid(ie, 28), "oversized PMKID count rejected");
	printf("PASS: %u RSN security boundary checks\n", checks);
	return EXIT_SUCCESS;
}
