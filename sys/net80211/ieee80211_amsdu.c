/*	$OpenBSD: ieee80211_input.c,v 1.263 2026/05/28 10:50:47 kirill Exp $	*/
/*	$NetBSD: ieee80211_input.c,v 1.24 2004/05/31 11:12:24 dyoung Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 EmberBSD contributors
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002, 2003 Sam Leffler, Errno Consulting
 * Copyright (c) 2007-2009 Damien Bergamini
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Origin: bounded adaptation of the OpenBSD A-MSDU validation policies.
 * Source: OpenBSD ieee80211_input.c revision 1.263, 2026-05-28.
 * Original bounds, reader API and strict SNAP policy are AI-assisted Codex work.
 * See ember/development/net80211-amsdu.md for pinned sources and limits.
 */
#include <sys/types.h>
#include <sys/systm.h>
#include <net80211/ieee80211_amsdu.h>

int
ieee80211_amsdu_next(ieee80211_amsdu_read read, void *arg, size_t total,
    const uint8_t station[6], size_t *offset,
    struct ieee80211_amsdu_subframe *sub)
{
	static const uint8_t rfc1042[6] = { 0xaa, 0xaa, 3, 0, 0, 0 };
	static const uint8_t zero[6];
	uint8_t *h = sub->header;
	size_t len, end, pad;
	unsigned type;

	if (total > IEEE80211_AMSDU_MAXLEN || total < 22 || *offset > total)
		return -1;
	if (*offset == total)
		return 0;
	if (total - *offset < sizeof(sub->header) ||
	    read(arg, *offset, sizeof(sub->header), h) != 0)
		return -1;
	len = ((unsigned)h[12] << 8) | h[13];
	if (len < 8 || len > 1508 || len > total - *offset - 14)
		return -1;
	/* Reject forged A-MSDU bits and addresses outside this STA profile. */
	if (memcmp(h, rfc1042, 6) == 0 || memcmp(h, station, 6) != 0 ||
	    (h[6] & 1) != 0 || memcmp(h + 6, zero, 6) == 0 ||
	    memcmp(h + 6, station, 6) == 0)
		return -1;
	/* Only Ethernet II SNAP; preserve the separate legacy EAPOL path. */
	if (h[14] != 0xaa || h[15] != 0xaa || h[16] != 3 ||
	    h[17] != 0 || h[18] != 0 || (h[19] != 0 && h[19] != 0xf8))
		return -1;
	type = ((unsigned)h[20] << 8) | h[21];
	if (type < 0x600 || type == 0x888e)
		return -1;
	end = *offset + 14 + len;
	pad = (4 - ((14 + len) & 3)) & 3;
	if (end != total) {
		/* Padding is unspecified, but must precede a complete subframe. */
		if (total - end < pad + sizeof(sub->header))
			return -1;
		end += pad;
	}
	sub->offset = *offset;
	sub->length = len;
	*offset = end;
	return 1;
}
