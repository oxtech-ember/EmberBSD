/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original native implementation, AI-assisted. */
#include <sys/types.h>
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <stdio.h>
#include <string.h>
#endif
#include "rtw88_net_policy.h"

int
rtw88_net_state_format(char *buf, size_t len,
    const struct rtw88_net_state_diag *d)
{

	return snprintf(buf, len, "station state %d failed: %d stage %u "
	    "channel %u core_channel %u basic %#x slot %u aid %u raw_aid %#x "
	    "bssid_flags %#x nrates %u flags %#x started %u running %u\n",
	    d->state, d->error, d->stage, d->channel, d->core_channel,
	    d->basic, d->slot, d->aid, d->raw_aid, d->bssid_flags,
	    d->nrates, d->flags, d->started, d->running);
}

uint64_t
rtw88_net_request(struct rtw88_net_policy *p)
{

	p->accepting = 0;
	p->rxba_mask = 0;
	p->beacon_valid = 0;
	if (++p->epoch == 0)
		++p->epoch;
	return p->epoch;
}

int
rtw88_net_commit(struct rtw88_net_policy *p, uint64_t epoch, int error,
    int accepting)
{

	if (p->dying || epoch != p->epoch)
		return ECANCELED;
	if (error != 0)
		return error;
	p->accepting = accepting;
	return 0;
}

void
rtw88_net_close(struct rtw88_net_policy *p)
{

	p->dying = 1;
	(void)rtw88_net_request(p);
}

int
rtw88_net_rx_admit(struct rtw88_net_policy *p, uint64_t epoch,
    const uint8_t *frame, size_t len, unsigned flags, unsigned security)
{
	unsigned tid;
	int bar;

	if (p->dying || !p->accepting) {
		p->rx.inactive++;
		return ENXIO;
	}
	if (epoch != p->epoch) {
		p->rx.epoch++;
		return ECANCELED;
	}
	if (frame == NULL) {
		p->rx.invalid++;
		return EINVAL;
	}
	bar = len == 20 && frame[0] == 0x84 && (frame[1] & ~0x08) == 0 &&
	    frame[16] == 4 && (frame[17] & 0x0f) == 0 && (frame[18] & 15) == 0;
	if ((len < 24 && !bar) || len > RTW88_NET_MPDU_LIMIT) {
		p->rx.size++;
		return EMSGSIZE;
	}
	/* Descriptor flags: CRC, ICV and C2H do not describe valid MPDUs. */
	if ((flags & 0x13) != 0) {
		p->rx.corrupt++;
		return EBADMSG;
	}
	/* No generic control-frame admission. BAR has a current Giant-owned peer
	 * snapshot and still goes through the shared session/sequence validator. */
	if (bar) {
		tid = frame[17] >> 4;
		if (tid >= 8 || (p->rxba_mask & (1U << tid)) == 0 || security != 0 ||
		    memcmp(frame + 4, p->rxba_ra, 6) != 0 ||
		    memcmp(frame + 10, p->rxba_ta, 6) != 0) {
			p->rx.format++;
			return EPROTONOSUPPORT;
		}
	}
	/* Only version-zero, three-address management and data frames. */
	if ((frame[0] & 3) != 0 || ((frame[0] & 0x0c) == 4 && !bar) ||
	    (frame[0] & 0x0c) == 12 || (frame[1] & 3) == 3) {
		p->rx.format++;
		return EPROTONOSUPPORT;
	}
	/* Encrypted input must retain its software-decrypt descriptor flag. */
	if (security != 0 && (flags & 8) == 0) {
		p->rx.crypto++;
		return EACCES;
	}
	if (p->queued >= RTW88_NET_RX_LIMIT) {
		p->rx.full++;
		return ENOBUFS;
	}
	p->queued++;
	p->rx.accepted++;
	return 0;
}

void
rtw88_net_rx_release(struct rtw88_net_policy *p)
{

	if (p->queued != 0)
		p->queued--;
}

int
rtw88_net_rx_ready(struct rtw88_net_policy *p, uint64_t epoch)
{

	if (p->dying || !p->accepting) {
		p->rx.delivery_inactive++;
		return 0;
	}
	if (epoch != p->epoch) {
		p->rx.delivery_epoch++;
		return 0;
	}
	p->rx.delivered++;
	return 1;
}

int
rtw88_net_beacon_start(struct rtw88_net_policy *p, uint64_t epoch, uint32_t now)
{

	if (p->dying || !p->accepting)
		return ENXIO;
	if (epoch != p->epoch)
		return ECANCELED;
	p->beacon_epoch = epoch;
	p->beacon_tick = p->beacon_report_tick = now;
	p->beacon_valid = 1;
	return 0;
}

int
rtw88_net_beacon_note(struct rtw88_net_policy *p, uint64_t epoch, uint32_t now,
    const uint8_t *frame, size_t len, const uint8_t bssid[6])
{
	unsigned i;

	if (p->dying || !p->accepting)
		return ENXIO;
	if (epoch != p->epoch)
		return ECANCELED;
	if (!p->beacon_valid || epoch != p->beacon_epoch)
		return ENXIO;
	/* Fixed beacon body: timestamp, interval and capability follow the header. */
	if (frame == NULL || bssid == NULL || len < 36 || frame[0] != 0x80 ||
	    (frame[1] & 0x43) != 0)
		return ENOENT;
	for (i = 0; i < 6; i++) {
		if (frame[16 + i] != bssid[i])
			return ENOENT;
	}
	p->beacon_tick = p->beacon_report_tick = now;
	return 0;
}

int
rtw88_net_beacon_poll(struct rtw88_net_policy *p, uint64_t epoch, uint32_t now,
    uint32_t timeout)
{
	uint32_t elapsed, reported;

	if (p->dying || !p->accepting || !p->beacon_valid ||
	    epoch != p->epoch || epoch != p->beacon_epoch ||
	    timeout == 0 || timeout > UINT32_MAX / 2)
		return 0;
	elapsed = now - p->beacon_tick;
	reported = now - p->beacon_report_tick;
	/* Unsigned subtraction spans one wrap. A backward clock is not a miss. */
	if (elapsed < timeout || elapsed > UINT32_MAX / 2 ||
	    reported < timeout || reported > UINT32_MAX / 2)
		return 0;
	p->beacon_report_tick = now;
	return 1;
}

/*
 * One 64-bit publication word: generation[31:0], RX MCS[39:32],
 * TX MCS[47:40], A-MPDU[55:48], readiness[61:56], reserved[63:62].
 * The caller uses release-store/acquire-load around this codec.
 */
int
rtw88_net_ht20_snapshot_encode(const struct rtw88_net_ht20_snapshot *p,
    uint64_t *word)
{

	if (p == NULL || word == NULL || p->generation == 0 ||
	    p->rx_mcs > 0xff || p->tx_mcs > 0xff ||
	    (p->ready & ~RTW88_NET_HT20_READY_ALL) != 0 ||
	    (p->ampdu & 0xe3) != 0)
		return EINVAL;
	*word = (uint64_t)p->generation |
	    (uint64_t)p->rx_mcs << 32 | (uint64_t)p->tx_mcs << 40 |
	    (uint64_t)p->ampdu << 48 | (uint64_t)p->ready << 56;
	return 0;
}

int
rtw88_net_ht20_snapshot_decode(uint64_t word,
    struct rtw88_net_ht20_snapshot *p)
{
	struct rtw88_net_ht20_snapshot candidate;

	if (p == NULL)
		return EINVAL;
	if (word == 0)
		return ENOENT;
	if ((word >> 62) != 0)
		return EINVAL;
	candidate.generation = (uint32_t)word;
	candidate.rx_mcs = (uint16_t)(word >> 32) & 0xff;
	candidate.tx_mcs = (uint16_t)(word >> 40) & 0xff;
	candidate.ampdu = (uint8_t)(word >> 48);
	candidate.ready = (uint8_t)(word >> 56) & RTW88_NET_HT20_READY_ALL;
	if (candidate.generation == 0 || (candidate.ampdu & 0xe3) != 0)
		return EINVAL;
	*p = candidate;
	return 0;
}

int
rtw88_net_ht20_snapshot_admit(uint64_t word,
    struct rtw88_net_ht20_snapshot *p)
{
	struct rtw88_net_ht20_snapshot candidate;
	int error;

	if (p == NULL)
		return EINVAL;
	error = rtw88_net_ht20_snapshot_decode(word, &candidate);
	if (error == ENOENT)
		return EOPNOTSUPP;
	if (error != 0)
		return error;
	if (candidate.ready != RTW88_NET_HT20_READY_ALL ||
	    candidate.rx_mcs != 0xff || candidate.tx_mcs != candidate.rx_mcs)
		return EOPNOTSUPP;
	*p = candidate;
	return 0;
}

uint32_t
rtw88_net_tu_to_ms(uint16_t tu)
{

	return (uint32_t)(((uint64_t)tu * 1024 + 999) / 1000);
}
