/* SPDX-License-Identifier: BSD-2-Clause */
/* Original native RX allocation helper, AI-assisted. */
#ifndef _DEV_IC_RTW88_NET_MBUF_H_
#define _DEV_IC_RTW88_NET_MBUF_H_
#include <dev/ic/rtw88_net_policy.h>
struct mbuf;
struct ifnet;
struct mbuf *rtw88_net_rx_mbuf(const uint8_t *, size_t, struct ifnet *);
#endif
