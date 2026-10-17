/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original native implementation, AI-assisted. */
#include <sys/types.h>
#ifdef _KERNEL
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include "rtw88_net_policy.h"

uint64_t
rtw88_net_request(struct rtw88_net_policy *p)
{

	p->accepting = 0;
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

	if (p->dying || !p->accepting)
		return ENXIO;
	if (epoch != p->epoch)
		return ECANCELED;
	if (frame == NULL)
		return EINVAL;
	if (len < 24 || len > RTW88_NET_MPDU_LIMIT)
		return EMSGSIZE;
	/* Descriptor flags: CRC, ICV and C2H do not describe valid MPDUs. */
	if ((flags & 0x13) != 0)
		return EBADMSG;
	/* Only version-zero, three-address management and data frames. */
	if ((frame[0] & 3) != 0 || (frame[0] & 0x0c) == 4 ||
	    (frame[0] & 0x0c) == 12 || (frame[1] & 3) == 3)
		return EPROTONOSUPPORT;
	/* Encrypted input must retain its software-decrypt descriptor flag. */
	if (security != 0 && (flags & 8) == 0)
		return EACCES;
	if (p->queued >= RTW88_NET_RX_LIMIT)
		return ENOBUFS;
	p->queued++;
	return 0;
}

void
rtw88_net_rx_release(struct rtw88_net_policy *p)
{

	if (p->queued != 0)
		p->queued--;
}
