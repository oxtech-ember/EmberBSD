/* SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../sys/dev/sdmmc/if_aicwf_sae.h"
#include "../../sys/dev/ic/fullmac_sae.h"

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
	const uint8_t peer[6] = {2, 0, 0, 0, 0, 1};
	const uint8_t host[6] = {2, 0, 0, 0, 0, 2};
	const uint8_t ssid[] = "test-device";
	uint8_t ind[44] = {0}, frame[1537] = {0}, key[24] = {4};
	size_t n;

	check(aicwf_sae_peer_valid(peer, 1), "unicast peer and generation");
	check(!aicwf_sae_peer_valid(peer, 0), "zero generation");
	check(!aicwf_sae_peer_valid(frame, 1), "zero peer with valid generation");
	frame[0] = 1;
	check(!aicwf_sae_peer_valid(frame, 1), "multicast peer");
	frame[0] = 0;
	ind[0] = 1;
	ind[1] = sizeof(ssid) - 1;
	memcpy(ind + 2, ssid, sizeof(ssid) - 1);
	memcpy(ind + 34, peer, 6);
	memcpy(ind + 40, "\x08\xac\x0f\x00", 4);
#define REQUEST() aicwf_sae_request_valid(ind, sizeof(ind), 1, peer, ssid, sizeof(ssid) - 1)
	check(REQUEST(), "external auth request");
	memcpy(ind + 40, "\x00\x0f\xac\x08", 4);
	check(REQUEST(), "legacy firmware selector byte order");
	memcpy(ind + 40, "\x08\xac\x0f\x00", 4);
	for (n = 0; n < sizeof(ind); n++)
		check(!aicwf_sae_request_valid(ind, n, 1, peer, ssid,
		    sizeof(ssid) - 1), "truncated external auth request");
	ind[0] = 2;
	check(!REQUEST(), "wrong interface");
	ind[0] = 1;
	ind[1] = 33;
	check(!REQUEST(), "oversized SSID");
	ind[1] = sizeof(ssid) - 1;
	ind[2] ^= 1;
	check(!REQUEST(), "wrong SSID");
	ind[2] ^= 1;
	ind[34] ^= 2;
	check(!REQUEST(), "wrong peer");
	ind[34] ^= 2;
	ind[40] = 2;
	check(!REQUEST(), "PSK cannot request SAE");

	frame[0] = 0xb0;
	frame[24] = 3;
	memcpy(frame + 4, host, 6);
	memcpy(frame + 10, peer, 6);
	memcpy(frame + 16, peer, 6);
#define FRAME(n) aicwf_sae_frame_valid(frame, (n), host, peer, peer)
	check(FRAME(30), "SAE frame");
	check(FRAME(1536), "maximum frame");
	check(!FRAME(1537), "oversized frame");
	for (n = 0; n < 30; n++)
		check(!FRAME(n), "truncated frame");
	frame[1] = 8;
	check(FRAME(30), "retry frame");
	for (n = 0; n < 8; n++) {
		if (n == 3)
			continue;
		frame[1] = 1U << n;
		check(!FRAME(30), "non-authentication flags");
	}
	frame[1] = 0;
	frame[0] = 0xc0;
	check(!FRAME(30), "deauthentication is not SAE");
	frame[0] = 0xb0;
	for (n = 4; n < 22; n++) {
		frame[n] ^= 1;
		check(!FRAME(30), "foreign frame address");
		frame[n] ^= 1;
	}
	frame[22] = 1;
	check(!FRAME(30), "fragmented authentication");
	frame[22] = 0;
	frame[24] = 0;
	check(!FRAME(30), "open authentication is not SAE");

	check(aicwf_sae_igtk_valid(key, sizeof(key)), "zero-IPN IGTK 4");
	key[0] = 5;
	check(aicwf_sae_igtk_valid(key, sizeof(key)), "zero-IPN IGTK 5");
	for (n = 0; n < sizeof(key); n++)
		check(!aicwf_sae_igtk_valid(key, n), "truncated IGTK");
	key[0] = 3;
	check(!aicwf_sae_igtk_valid(key, sizeof(key)), "data key index rejected");
	key[0] = 4;
	for (n = 1; n < 8; n++) {
		key[n] = 1;
		check(!aicwf_sae_igtk_valid(key, sizeof(key)), "unsupported index/IPN rejected");
		key[n] = 0;
	}
	printf("PASS: %u AIC SAE wire boundary checks\n", checks);
	return EXIT_SUCCESS;
}
