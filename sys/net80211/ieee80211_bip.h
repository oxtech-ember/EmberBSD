/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Bounded BIP-CMAC-128 verification. This is an independent implementation.
 * CMAC follows RFC 4493. BIP authenticates masked FC, A1/A2/A3 and the body,
 * with the eight-octet MMIE MIC set to zero. The block callback uses AES-128.
 * The kernel adapter must use aes_setenckey128()/aes_enc(), not a local AES.
 *
 * This helper does not install keys, trust firmware status, or retire a link.
 * The caller owns key selection, lifetime locking and the replay counter.
 */
#ifndef _NET80211_IEEE80211_BIP_H_
#define _NET80211_IEEE80211_BIP_H_

#define IEEE80211_BIP_MAX_FRAME 1536
#define IEEE80211_BIP_MAX_PN UINT64_C(0xffffffffffff)
#define IEEE80211_BIP_MMIE_LEN 18

/*
 * IEEE 802.11 robust Action categories exclude these unprotected categories.
 * Reserved categories remain protected here: classification must not grant
 * an unauthenticated action permission to reach a future category handler.
 * This is classification, not validation of the category-specific payload.
 */
static inline bool
ieee80211_is_robust_action(uint8_t category)
{
	switch (category) {
	case 4:		/* Public. */
	case 7:		/* HT. */
	case 11:	/* Unprotected WNM. */
	case 15:	/* Self-protected. */
	case 20:	/* Unprotected DMG. */
	case 21:	/* VHT. */
	case 22:	/* Unprotected S1G (category 23 is protected S1G). */
	case 127:	/* Vendor-specific (unprotected). */
		return false;
	default:
		return true;
	}
}

/*
 * Classify a plaintext management MPDU with a 24-octet header. The caller
 * validates header format/length and decrypts protected unicast frames first;
 * the Protected bit alone never authorizes a frame or a category. Malformed
 * deauth/disassoc bodies still classify as robust and require authentication.
 */
static inline bool
ieee80211_is_robust_mgmt(const uint8_t *frame, size_t len)
{
	if (frame == NULL || len < 24)
		return false;
	switch (frame[0]) {
	case 0xa0:
	case 0xc0:
		return true;
	case 0xd0:
		return len >= 25 && ieee80211_is_robust_action(frame[24]);
	default:
		return false;
	}
}

/* The callback must support distinct input/output buffers and cannot fail. */
typedef void (*ieee80211_bip_block_fn)(const void *, const uint8_t[16], uint8_t[16]);

struct ieee80211_bip_part {
	const uint8_t	*data;
	size_t		len;
};

static inline void
ieee80211_bip_clear(void *p, size_t len)
{
	volatile uint8_t *q = p;

	while (len-- != 0)
		*q++ = 0;
}

static inline void
ieee80211_bip_double(uint8_t key[16])
{
	const uint8_t high = key[0] >> 7;
	unsigned i;

	for (i = 0; i < 15; i++)
		key[i] = (uint8_t)((key[i] << 1) | (key[i + 1] >> 7));
	key[15] = (uint8_t)((key[15] << 1) ^ (0x87 & (0U - high)));
}

/* At most three bounded segments. Leave the result unchanged on failure. */
static inline bool
ieee80211_bip_cmac(ieee80211_bip_block_fn encrypt, const void *ctx,
    const struct ieee80211_bip_part *parts, size_t count, uint8_t result[16])
{
	uint8_t key[16], block[16] = { 0 }, mac[16] = { 0 };
	size_t total = 0, consumed = 0, off = 0, part = 0;
	unsigned i;

	if (encrypt == NULL || ctx == NULL || result == NULL ||
	    count > 3 || (count != 0 && parts == NULL))
		return false;
	for (size_t n = 0; n < count; n++) {
		if ((parts[n].len != 0 && parts[n].data == NULL) ||
		    parts[n].len > IEEE80211_BIP_MAX_FRAME - total)
			return false;
		total += parts[n].len;
	}
	encrypt(ctx, block, key);
	ieee80211_bip_double(key);
	for (;;) {
		const size_t take = total - consumed < 16 ?
		    total - consumed : 16;

		memset(block, 0, sizeof(block));
		for (size_t n = 0; n < take; n++) {
			while (part < count && off == parts[part].len) {
				part++;
				off = 0;
			}
			/* The validated total guarantees a nonempty segment. */
			block[n] = parts[part].data[off++];
		}
		consumed += take;
		if (consumed == total) {
			if (take < 16) {
				block[take] = 0x80;
				ieee80211_bip_double(key);
			}
			for (i = 0; i < 16; i++)
				block[i] ^= key[i];
		}
		for (i = 0; i < 16; i++)
			block[i] ^= mac[i];
		encrypt(ctx, block, mac);
		if (consumed == total)
			break;
	}
	memcpy(result, mac, sizeof(mac));
	ieee80211_bip_clear(key, sizeof(key));
	ieee80211_bip_clear(block, sizeof(block));
	ieee80211_bip_clear(mac, sizeof(mac));
	return true;
}

/*
 * Accept complete group-addressed deauth/disassoc or robust Action MPDUs.
 * The input excludes FCS and includes the intact, final 18-octet MMIE.
 * The caller validates action-specific fields after authentication.
 * Optional header formats and extended disconnect bodies are unsupported.
 * Initial/previous IPN and key ID belong to the supplied AES key context.
 * A successful call returns a candidate PN. Commit it under the key lock.
 * Failure leaves the candidate unchanged. This function changes no state.
 */
static inline bool
ieee80211_bip_verify(ieee80211_bip_block_fn encrypt, const void *ctx,
    const uint8_t *frame, size_t len, const uint8_t source[6],
    const uint8_t bssid[6], uint16_t keyid, uint64_t previous,
    uint64_t *candidate)
{
	static const uint8_t zero[8] = { 0 };
	struct ieee80211_bip_part parts[3];
	uint8_t aad[20], mic[16], different = 0;
	const uint8_t *mmie;
	uint64_t pn = 0;
	size_t body_len;
	unsigned i;
	bool valid;

	if (frame == NULL || source == NULL || bssid == NULL ||
	    candidate == NULL || len < 44 || len > IEEE80211_BIP_MAX_FRAME ||
	    (keyid != 4 && keyid != 5) || previous > IEEE80211_BIP_MAX_PN)
		return false;
	/* No DS, fragmentation, protection or HT-control header. */
	if ((frame[1] & ~0x38) != 0 || (frame[22] & 0x0f) != 0 ||
	    (frame[4] & 1) == 0 || (source[0] & 1) != 0 ||
	    (bssid[0] & 1) != 0 || memcmp(source, zero, 6) == 0 ||
	    memcmp(bssid, zero, 6) == 0 ||
	    memcmp(frame + 10, source, 6) != 0 ||
	    memcmp(frame + 16, bssid, 6) != 0)
		return false;
	body_len = len - 24 - IEEE80211_BIP_MMIE_LEN;
	switch (frame[0]) {
	case 0xa0:		/* Disassociation. */
	case 0xc0:		/* Deauthentication. */
		if (body_len != 2)
			return false;
		break;
	case 0xd0:		/* Category and action code; caller checks the rest. */
		if (body_len < 2 || !ieee80211_is_robust_action(frame[24]))
			return false;
		break;
	default:
		return false;
	}
	mmie = frame + len - IEEE80211_BIP_MMIE_LEN;
	if (mmie[0] != 76 || mmie[1] != 16 ||
	    mmie[2] != keyid || mmie[3] != 0)
		return false;
	for (i = 0; i < 6; i++)
		pn |= (uint64_t)mmie[4 + i] << (8 * i);
	if (pn <= previous)
		return false;

	aad[0] = frame[0];
	aad[1] = frame[1] & ~0x38;
	memcpy(aad + 2, frame + 4, 18);
	parts[0].data = aad;
	parts[0].len = sizeof(aad);
	parts[1].data = frame + 24;
	parts[1].len = len - 24 - sizeof(zero);
	parts[2].data = zero;
	parts[2].len = sizeof(zero);
	if (!ieee80211_bip_cmac(encrypt, ctx, parts, 3, mic))
		return false;
	/* Compare all truncated MIC octets without an early exit. */
	for (i = 0; i < 8; i++)
		different |= mic[i] ^ mmie[10 + i];
	valid = different == 0;
	ieee80211_bip_clear(mic, sizeof(mic));
	if (valid)
		*candidate = pn;
	return valid;
}

#endif /* _NET80211_IEEE80211_BIP_H_ */
