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
	if (len < 24 || len > RTW88_NET_MPDU_LIMIT) {
		p->rx.size++;
		return EMSGSIZE;
	}
	/* Descriptor flags: CRC, ICV and C2H do not describe valid MPDUs. */
	if ((flags & 0x13) != 0) {
		p->rx.corrupt++;
		return EBADMSG;
	}
	/* Only version-zero, three-address management and data frames. */
	if ((frame[0] & 3) != 0 || (frame[0] & 0x0c) == 4 ||
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

uint32_t
rtw88_net_tu_to_ms(uint16_t tu)
{

	return (uint32_t)(((uint64_t)tu * 1024 + 999) / 1000);
}
