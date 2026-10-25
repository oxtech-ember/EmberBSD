/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original native implementation, AI-assisted. */
#ifndef _DEV_IC_RTW88_NET_POLICY_H_
#define _DEV_IC_RTW88_NET_POLICY_H_
#include <sys/types.h>
#ifdef _KERNEL
#include <sys/null.h>
#else
#include <stdint.h>
#include <stddef.h>
#endif
#define RTW88_NET_RX_LIMIT 64U
#define RTW88_NET_MPDU_LIMIT 4096U
#define RTW88_NET_HT20_READY_RADIO       0x01U
#define RTW88_NET_HT20_READY_POWER       0x02U
#define RTW88_NET_HT20_READY_PROTECTION  0x04U
#define RTW88_NET_HT20_READY_RXBA        0x08U
#define RTW88_NET_HT20_READY_SCHEDULER   0x10U
#define RTW88_NET_HT20_READY_AMSDU       0x20U
#define RTW88_NET_HT20_READY_ALL (RTW88_NET_HT20_READY_RADIO | \
    RTW88_NET_HT20_READY_POWER | RTW88_NET_HT20_READY_PROTECTION | \
    RTW88_NET_HT20_READY_RXBA | RTW88_NET_HT20_READY_SCHEDULER | \
    RTW88_NET_HT20_READY_AMSDU)
/* Fields packed into one atomic u64 so readers cannot observe mixed epochs. */
struct rtw88_net_ht20_snapshot {
	uint32_t generation;
	uint16_t rx_mcs, tx_mcs;
	uint8_t ready, ampdu;
};
/*
 * HT/VHT negotiation, read-only numeric diagnostics. The ht20_* fields
 * mirror net80211's ic_ht20_diag (offers, responses, beacon resets and
 * the last reset reason). prepare_* is the ASSOC profile provider, whose
 * ready mask (RTW88_NET_HT20_READY_* bits) is also kept on failure;
 * vht_* is the channel provider with the last asked primary/center/width;
 * run_* is the node at the latest RUN worker pass, zero in other states.
 */
struct rtw88_net_neg {
	uint32_t ht20_offers, ht20_vht_offers, ht20_ht, ht20_vht;
	uint32_t ht20_legacy, ht20_unoffered, ht20_bad, ht20_resets;
	uint32_t ht20_status, ht20_norate, ht20_failures;
	uint16_t ht20_last_status;
	uint32_t prepare_ok, prepare_fail, vht_admit, vht_refuse;
	int32_t prepare_error, vht_error;
	uint8_t ht20_reset_reason, ht20_failure_reason, prepare_ready;
	uint8_t vht_chan[3];
	uint8_t run_ht, run_vht, run_chan[3];
};
/* Mirror ic_ht20_diag; Giant owns d, the caller holds the neg lock. */
#define RTW88_NET_NEG_HT20(n, d) do {					\
	(n)->ht20_offers = (d)->offers;					\
	(n)->ht20_vht_offers = (d)->vht_offers;				\
	(n)->ht20_ht = (d)->ht;						\
	(n)->ht20_vht = (d)->vht;					\
	(n)->ht20_legacy = (d)->legacy;					\
	(n)->ht20_unoffered = (d)->unoffered;				\
	(n)->ht20_bad = (d)->bad;					\
	(n)->ht20_resets = (d)->resets;					\
	(n)->ht20_status = (d)->status;					\
	(n)->ht20_norate = (d)->norate;					\
	(n)->ht20_failures = (d)->failures;				\
	(n)->ht20_failure_reason = (d)->failure_reason;			\
	(n)->ht20_last_status = (d)->last_status;			\
	(n)->ht20_reset_reason = (d)->reset_reason;			\
} while (0)
struct rtw88_net_rx_counts {
	uint64_t accepted, inactive, epoch, invalid, size;
	uint64_t corrupt, format, crypto, full;
	uint64_t delivered, delivery_inactive, delivery_epoch;
};
struct rtw88_net_policy {
	uint64_t epoch;
	unsigned queued;
	int accepting;
	int dying;
	uint64_t beacon_epoch;
	uint32_t beacon_tick;
	uint32_t beacon_report_tick;
	int beacon_valid;
	/* Giant publishes only a current authorized RX BA peer under the bus lock. */
	uint8_t rxba_mask, rxba_ra[6], rxba_ta[6];
	struct rtw88_net_rx_counts rx;
};
/* Numeric failure evidence. No station address or network name is stored. */
#define RTW88_NET_STAGE_CHANNEL 1U
#define RTW88_NET_STAGE_STATION 2U
#define RTW88_NET_STAGE_CORE 3U
#define RTW88_NET_STAGE_EDCA 4U
#define RTW88_NET_STAGE_RA 5U
#define RTW88_NET_STAGE_RESTART 6U
struct rtw88_net_state_diag {
	int state, error;
	unsigned stage, channel, core_channel, basic, slot, aid, raw_aid;
	unsigned bssid_flags, nrates, flags, started, running;
};
int		rtw88_net_state_format(char *, size_t,
    const struct rtw88_net_state_diag *);
uint64_t	rtw88_net_request(struct rtw88_net_policy *);
int		rtw88_net_commit(struct rtw88_net_policy *, uint64_t, int, int);
void		rtw88_net_close(struct rtw88_net_policy *);
int		rtw88_net_rx_admit(struct rtw88_net_policy *, uint64_t,
    const uint8_t *, size_t, unsigned, unsigned);
void		rtw88_net_rx_release(struct rtw88_net_policy *);
int		rtw88_net_rx_ready(struct rtw88_net_policy *, uint64_t);
int		rtw88_net_beacon_start(struct rtw88_net_policy *, uint64_t, uint32_t);
/* The caller validates the complete body and current STA peer/channel.
 * The beacon timestamp is the local IRQ arrival tick, not delivery time. */
int		rtw88_net_beacon_note(struct rtw88_net_policy *, uint64_t, uint32_t,
    const uint8_t *, size_t, const uint8_t[6]);
int		rtw88_net_beacon_poll(struct rtw88_net_policy *, uint64_t, uint32_t,
    uint32_t);
uint32_t	rtw88_net_tu_to_ms(uint16_t);
int	rtw88_net_ht20_snapshot_encode(const struct rtw88_net_ht20_snapshot *,
	    uint64_t *);
int	rtw88_net_ht20_snapshot_decode(uint64_t,
	    struct rtw88_net_ht20_snapshot *);
int	rtw88_net_ht20_snapshot_admit(uint64_t,
	    struct rtw88_net_ht20_snapshot *);
#endif
