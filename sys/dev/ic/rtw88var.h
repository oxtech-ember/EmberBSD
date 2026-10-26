/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8822CE resource lifecycle, AI-assisted. */
#ifndef _DEV_IC_RTW88VAR_H_
#define _DEV_IC_RTW88VAR_H_

#ifdef _KERNEL
#include "opt_rtw88_rfk.h"
#include "opt_rtw88_vht.h"
#include "opt_rtw88_dfs.h"
#endif
/* VHT 40/80 is an explicit experiment outside the HT-only RFK envelope. */
#if defined(RTW88_VHT_EXPERIMENT) && !defined(RTW88_RFK_EXPERIMENT)
#define RTW88_VHT80 1
#endif
/* Channel 64 slave operation is an explicit experiment outside RFK. */
#if defined(RTW88_DFS64_EXPERIMENT) && !defined(RTW88_RFK_EXPERIMENT)
#define RTW88_DFS64 1
#endif
#include <dev/ic/rtw88_hw.h>

/* TX power regulatory table index used by every channel program. */
#define RTW88_POWER_REGD 12

struct rtw8822c_rfk;

/* Queue order follows the pinned rtw88 PCI transport. */
#define RTW88_NTXRING 8
#define RTW88_RXRING 8
#define RTW88_NRING 9

struct rtw88_dma {
	void *cookie;		/* Bus-owned mapping; NULL until fully allocated. */
	void *vaddr;
	uint64_t addr;		/* Never narrow before validating the full range. */
	size_t size;
};

struct rtw88_bus_ops {
	/* On read failure, leave the caller's output unchanged. */
	int (*read_1)(void *, uint32_t, uint8_t *);
	int (*read_2)(void *, uint32_t, uint16_t *);
	int (*read_4)(void *, uint32_t, uint32_t *);
	int (*write_1)(void *, uint32_t, uint8_t);
	int (*write_2)(void *, uint32_t, uint16_t);
	int (*write_4)(void *, uint32_t, uint32_t);
	void (*delay_us)(void *, unsigned);
	/* Failure acquires nothing; success transfers one complete mapping. */
	int (*dma_alloc)(void *, struct rtw88_dma *, size_t);
	void (*dma_free)(void *, struct rtw88_dma *);
	void (*dma_sync)(void *, struct rtw88_dma *, int);
	int (*intr_establish)(void *);
	/* Mask and synchronously drain handlers before returning. */
	void (*intr_disestablish)(void *);
	/* Wait until no handler runs rtw88_intr; the line stays attached. */
	void (*intr_barrier)(void *);
	int (*firmware_load)(void *, const char *, const char *,
	    const uint8_t **, size_t *);
	void (*firmware_release)(void *);
	int (*bus_master)(void *, int);
};

#define RTW88_BOOTSTRAP_US	5000000U
#define RTW88_COMMAND_US	1000000U
#define RTW88_POLL_STEP_US	50U
#define RTW88_FW_CMD_MAX	7U
#define RTW88_FW_PAGE_SIZE	(4096U + 48U)
/* pci.h RTK_PCI_RX_BUF_SIZE. Fewer than the 512 reference buffers. */
#define RTW88_RX_BUFS		128U
#define RTW88_RX_BUF_SIZE	(11454U + 24U)
/* Bound on RX descriptors handled per interrupt. */
#define RTW88_RX_LIMIT		64U
#define RTW88_DEFAULT_CHANNEL	1U
/* Published TX entries per used queue; each has a bounce buffer. */
#define RTW88_TX_SLOTS		32U
#define RTW88_TX_BUF_SIZE	4096U
#define RTW88_LEGACY_RATES	12U
#define RTW88_TXQ_MGMT		0U
#define RTW88_TXQ_BE		1U
#define RTW88_TXQ_BK		2U
#define RTW88_TXQ_VI		3U
#define RTW88_TXQ_VO		4U
#define RTW88_NTXQ		5U

/* TX descriptor ring indices shared by the core and source contracts. */
enum rtw88_tx_ring_index {
	RTW88_TX_RING_BK = 0,
	RTW88_TX_RING_BE = 1,
	RTW88_TX_RING_VI = 2,
	RTW88_TX_RING_VO = 3,
	RTW88_TX_RING_MGMT = 5
};

struct rtw88_txq {
	struct rtw88_dma buf[RTW88_TX_SLOTS];
	unsigned nbuf;
	unsigned wp;		/* Next slot the host fills. */
	unsigned rp;		/* Oldest slot not yet completed. */
	unsigned queued;	/* Posted, completion not seen. */
};

#define RTW88_RA_LEGACY 0
#define RTW88_RA_HT20_1SS 1
#define RTW88_RA_VHT_1SS 2
/* Outbound local TX intersect peer RX: NSS1 MCS0-7 or MCS0-9, other NSS
 * unsupported. The local profile offers MCS0-9. */
#define RTW88_VHT_MAP_NSS1_MCS7 0xfffcU
#define RTW88_VHT_MAP_NSS1_MCS9 0xfffeU
#define RTW88_VHT_MAP_NSS2_MCS9 0xfffaU	/* 2T2R hardware only. */
#define RTW88_HT20_PROT_MF20_NO_RTS 1
#define RTW88_HT20_MTU_MIN 32U
#define RTW88_HT20_MTU_MAX 1500U
#define RTW88_HT20_TX_OVERHEAD 50U /* QoS26 + LLC8 + software CCMP16. */
#define RTW88_HT20_FCS_SIZE 4U
#define RTW88_HT20_THRESHOLD_MAX 2346U
/* Numeric protection and frame limits; no retained stack/peer pointer. */
struct rtw88_ht20_policy {
	uint16_t peer_cap, mtu, max_mpdu, rts_threshold;
	uint8_t secondary, qualification;
};
/* Numeric rate policy; no retained stack/peer pointer. */
struct rtw88_ra_config {
	uint64_t epoch;
	unsigned channel, common_rates;
	uint8_t mode, use_protection;
	uint8_t phy;
	uint16_t ht_mcs, ht_operation, vht_tx_map;
	uint32_t profile_generation;
	struct rtw88_ht20_policy ht;
	/* RA_INFO SGI_EN: the AP receives SGI at the committed width.
	 * LDPC: the AP receives LDPC (RA_INFO LDPC and the descriptor).
	 * HT20 and VHT entries of the VHT experiment only. */
	uint8_t sgi, ldpc;
};
struct rtw88_ra_state {
	uint64_t epoch;
	uint32_t generation;
	unsigned channel, common_rates, mask;
	uint8_t rate_id, phy;
	uint16_t ht_mcs, ht_operation, vht_tx_map;
	uint32_t profile_generation;
	struct rtw88_ht20_policy ht;
	struct rtw88_channel_tuple tuple;	/* Committed when configured. */
	uint8_t sgi, ldpc;
	uint8_t vht_seed;	/* Highest negotiated VHT descriptor rate. */
	int configured, active, protection_limited, error;
};

#ifdef RTW88_RFK_EXPERIMENT
/* Successful source programming, not a hardware TXAGC-table readback. */
struct rtw88_power_program {
	uint64_t serial;
	uint32_t generation, ht_diff[2];
	unsigned channel, regd;
	uint8_t cut, rfe, valid, ht_mcs, ht_ref[2];
};
struct rtw88_operating_envelope {
	struct rtw88_power_program power;
	uint64_t token;
	uint32_t generation, rf18[2], mac_bw, data_sc, tmac;
	unsigned channel;
	uint8_t cut, rfe, bandwidth, streams, prepared, sealed;
};
/* PCI-protected numeric mirror; only the owning process samples its context. */
struct rtw88_rfk_diag {
	uint64_t token;
	uint32_t generation, baseline_generation, flags, global;
	uint32_t kind, addr, size, raw, raw_valid, non_target;
	uint32_t arms[7], done[7], operations, requested_delay_us, sleeps;
	int error, cleanup_error;
};
#endif

/* Visible counters; errors are never silently dropped. */
struct rtw88_stats {
#ifdef RTW88_RFK_EXPERIMENT
	struct rtw88_rfk_diag rfk;
#endif
	struct rtw88_rx_signal signal;
	uint64_t signal_reasons[RTW88_SIGNAL_REASONS];
	uint64_t hw_requests, hw_reports, hw_missing, hw_foreign;
	uint64_t hw_unstable, hw_io_errors;

	uint64_t intr;
	uint64_t rx_frames;
	uint64_t rx_c2h;
	uint64_t rx_errors;	/* Descriptor rejected by the RX decoder. */
	uint64_t rx_short;	/* Shorter than an 802.11 ACK. */
	uint64_t rx_tag_errors;	/* RX tag mismatch: PCIe DMA timeout. */
	uint64_t rx_dma_errors;	/* Hardware index outside the ring. */
	uint64_t tx_queued;
	/* Successful producer writes per queue, not air ACKs or delivery. */
	uint64_t tx_posted[RTW88_NTXQ];
	uint64_t tx_done;	/* Counted only from the chip's read index. */
	uint64_t tx_full;
	uint64_t tx_dropped;	/* Posted but not completed before stop. */
	uint64_t tx_dma_errors;	/* Untrusted device read index or snapshot IO. */
	/* Successfully encoded unicast DATA, not delivery or air-rate reports. */
	uint64_t tx_data_encoded, tx_data_rates[RTW88_LEGACY_RATES];
	uint64_t tx_data_other;
	uint16_t tx_data_basic;
	uint8_t tx_data_rate;
	uint64_t tx_ra_encoded, tx_ra_protection_limited;
	uint64_t tx_ampdu_encoded;	/* Descriptors with AGG_EN set. */
	uint64_t ra_commands, ra_command_errors;
	uint64_t ra_reports, ra_unattributed, ra_malformed, ra_foreign;
	uint64_t ra_unsupported, ra_inactive, ra_trailing, ra_outside_mask;
	uint32_t ra_observed_generation;
	uint8_t ra_report_rate, ra_report_macid, ra_report_bw, ra_report_valid;
	uint64_t rx_beacons;
	uint8_t last_bssid[6];
	char last_ssid[33];	/* NUL-terminated; empty if malformed. */
	uint8_t last_ssid_len;
	uint8_t last_c2h_id;
	uint8_t last_c2h_seq;
};

/* One shared packet buffer. Only a confirmed new firmware epoch resets this. */
struct rtw88_packet_h2c {
	uint32_t generation;
	uint16_t wp, sequence;
	uint8_t active, initialized, attempted, busy, dirty;
	int error;		/* Sticky after an uncertain DMA operation. */
#ifdef RTW88_RFK_EXPERIMENT
	uint16_t managed_old, managed_next;
	uint8_t managed;
#endif
};

/* Boot generation, never a peer/association identity. */
struct rtw88_hw_feature_state {
	struct rtw88_hw_feature report;
	uint32_t requested_generation, observed_generation;
	uint8_t requested, observed;
};

/* Register-mailbox reservation, used only by the managed-state worker. */
struct rtw88_mailbox {
	uint32_t generation;
	uint8_t active, box, total, sent;
};

struct rtw88_softc {
	const struct rtw88_bus_ops *sc_ops;
	void *sc_bus;
	struct rtw88_dma sc_ring[RTW88_NRING];
	struct rtw88_dma sc_fw_dma;
	unsigned sc_nring;
	int sc_intr_established;
	int sc_resources_ready;
	int sc_fw_dma_owned;
	int sc_started;
	int sc_rf_enabled;
	int sc_dma_master;	/* Enabled, or an enable/disable is unconfirmed. */
	int sc_powered;
	int sc_power_dirty;	/* An off sequence is still required. */
	int sc_power_error;	/* Last failed off sequence. */
	int sc_dma_error;	/* Last unconfirmed bus-master disable. */
	int sc_bootstrapping;
	int sc_stop_requested;
	struct rtw88_mailbox sc_mailbox;
	uint16_t sc_vendor;
	uint16_t sc_product;
	uint8_t sc_mac[6];	/* Validated EFUSE identity. */
	uint8_t sc_rfe;
	uint8_t sc_cut;
	uint8_t sc_xtal;		/* EFUSE crystal cap, 7 bits. */
	uint8_t sc_efuse_phys[512];	/* Physical EFUSE read at identify. */
	uint32_t sc_sys_cfg1;
	uint32_t sc_generation;
	uint32_t sc_h2c_box;
	struct rtw88_packet_h2c sc_packet_h2c;
#ifdef RTW88_RFK_EXPERIMENT
	/* Original gain cache survives clean attempts; never the quarantine latch. */
	struct rtw8822c_rfk *sc_rfk;
	struct rtw88_power_program sc_power_program;
	struct rtw88_operating_envelope sc_operating;
#endif
	struct rtw88_hw_feature_state sc_hw_feature;
	struct rtw88_phy_state sc_phy;
	unsigned sc_pg_rows;
	unsigned sc_pwr_rows;
	struct rtw88_dma sc_rxbuf[RTW88_RX_BUFS];
	unsigned sc_nrxbuf;
	unsigned sc_rx_rp;	/* Next RX descriptor the host reads. */
	unsigned sc_rx_tag;
	uint64_t sc_rx_sequence;	/* Consumed slots in this generation. */
	int sc_running;		/* Interrupts unmasked; rtw88_intr owns RX. */
	int sc_edca_error;	/* TX quarantine until a full successful start. */
	unsigned sc_channel;	/* Last successful program; not proof after failure. */
	unsigned sc_basic_rates;	/* Negotiated legacy descriptor-rate bitmap. */
	int sc_linked;
	struct rtw88_ra_state sc_ra;
	/* DFS slave TX permit for one channel in one core generation. */
	struct {
		uint32_t generation;
		unsigned channel;
		int valid;
	} sc_dfs;
	struct rtw88_txq sc_txq[RTW88_NTXQ];
	/*
	 * Optional network consumers. Called under the bus interrupt lock;
	 * they must not sleep, call the stack, reenter the core, or alter
	 * callbacks. Borrowed RX bytes and info
	 * expire at return. packet_size equals len, with FCS removed. Stop
	 * drains interrupts before the owner clears these callbacks.
	 */
	void *sc_net_cookie;
	void (*sc_rx_frame)(void *, const uint8_t *, size_t,
	    const struct rtw88_rx_info *);
	/* Completed DMA entries, not acknowledged over-the-air frames. */
	void (*sc_tx_complete)(void *, unsigned);
	struct rtw88_stats sc_stats;
};

/* Attach only quiescent resources. No firmware, DMA publication or ifnet. */
int rtw88_core_attach(struct rtw88_softc *);
/* Failure retains resources; success drains IRQ before freeing ring mappings. */
int rtw88_core_detach(struct rtw88_softc *);
int rtw88_start(struct rtw88_softc *);
/*
 * Cold start at an admitted channel tuple, programmed before HCI start.
 * This is the only entry to a different RF width: rtw88_channel_apply
 * keeps the committed width. Rejection precedes any bus I/O.
 */
int rtw88_start_tuple(struct rtw88_softc *,
	const struct rtw88_channel_tuple *);
#ifdef RTW88_VHT80
/* Nonzero when an admitted 40/80 MHz PHY and power program is committed in
 * this generation. */
int rtw88_vht_committed(const struct rtw88_softc *);
/* Spatial streams the firmware hardware-feature report proves: 2 for a
 * two-stream, two-antenna report of this boot, else 1. */
unsigned rtw88_vht_nss(const struct rtw88_softc *);
/* Zero when this tuple passes the admission and TX-power validation of
 * rtw88_start_tuple() on this core; firmware, power-off state and bus
 * errors are not covered. No I/O or write, but a stopping core or one
 * without resources fails. Not a regulatory decision. */
int rtw88_tuple_check(const struct rtw88_softc *,
	const struct rtw88_channel_tuple *);
#endif
/*
 * Every DFS channel denies host TX before descriptor and DMA unless a
 * permit names this channel and core generation. Only the current DFS
 * primary, at an admitted 20, 40 or 80 MHz tuple with RTW88_DFS64, can be
 * opened. The caller holds the bus interrupt lock and must already hold
 * its master's current beacon.
 * Stop (and so every start) and every channel change withdraw it, and a
 * retune into a DFS channel is refused while host frames are posted.
 * Closing stops only new host frames: frames already posted need the
 * rtw88_tx_quiet() owner. Not covered: MAC ACK/BA/CTS responses and raw
 * H2C commands from the diagnostics path. The permit has no deadline;
 * the caller withdraws it when its beacon evidence ends.
 */
int rtw88_dfs_open(struct rtw88_softc *, unsigned, uint32_t);
void rtw88_dfs_close(struct rtw88_softc *);
/*
 * One startup call per firmware epoch. Process context without the PCI spin
 * lock, with the same external ownership contract as rtw88_fw_packet below.
 * The caller stops DMA on any error.
 */
int rtw88_fw_init(struct rtw88_softc *);
/*
 * Process context, without the PCI spin lock. The caller pins core lifetime
 * and exclusively owns command/channel/stop operations for the entire call.
 * busy rejects reentry; it is not a concurrent lifetime or locking mechanism.
 * Send exactly 32 bytes. Transport owns the 16-bit sequence field in the copy.
 * DMA completion does not mean firmware semantic acceptance. On an uncertain
 * error stop the core; no buffer reuse is allowed before a new firmware epoch.
 */
int rtw88_fw_packet(struct rtw88_softc *, const uint8_t *, size_t);
#ifdef RTW88_RFK_EXPERIMENT
/* Internal exclusive RFK IQK continuation. Each call is PCI-serialized. */
int rtw88_fw_iqk_begin(struct rtw88_softc *);
int rtw88_fw_iqk_poll(struct rtw88_softc *);
#endif
/* Caller serializes startup/stop. Errors retain unsafe ownership for a retry. */
int rtw88_stop(struct rtw88_softc *);
/* PCI-serialized, non-sleeping TX fence. Close host producers first, pause all
 * queues and disable MAC TX with readback. No DMA release or automatic resume.
 * Any error means hardware quiet is unproven; retain the fence and call stop. */
int rtw88_tx_quiet(struct rtw88_softc *);
int rtw88_fw_mailbox_begin(struct rtw88_softc *, unsigned, int *);
int rtw88_fw_mailbox_end(struct rtw88_softc *, int);
/* PCI-serialized outstanding DATA/MGMT DMA, not RF idle or air ACK.
 * Pending is valid only on success; a failed snapshot cannot release ownership. */
int rtw88_tx_drain_snapshot(struct rtw88_softc *, unsigned *);
int rtw88_fw_command(struct rtw88_softc *, uint8_t, const void *, size_t);
/*
 * Interrupt entry; the bus serializes it against rtw88_stop's barrier.
 * Returns 0 when no unmasked cause is pending (shared INTx) or the
 * device is not running, without touching any register.
 */
int rtw88_intr(struct rtw88_softc *);
/*
 * Caller holds the PCI lock. Snapshot the completed RX producer as a host
 * consumed-slot sequence in expected_generation. Never writes hardware or
 * synchronizes DMA. Zero *frontier on failure. EOVERFLOW means no later valid
 * sequence can be represented. This is not a radio-reception timestamp.
 */
int rtw88_rx_frontier(struct rtw88_softc *, uint32_t, uint64_t *);
/* Read-only snapshot: IRQ, RX index, PCI control, and direct RF18 A/B. */
#define RTW88_DIAG_REGS 8
int rtw88_diag(struct rtw88_softc *, uint32_t *);
/*
 * Process context. Caller exclusively owns channel/command/TX/stop operations.
 * Validate all four ACs before I/O, pause TX, then apply and restore TXPAUSE.
 * Any I/O error latches TX quarantine until a full successful rtw88_start;
 * no restore is attempted after a partial update. A failed bus operation
 * may have taken effect, so hardware pause is uncertain. Stop on error.
 * No callback or capability enables this API in the current legacy wrapper.
 */
int rtw88_edca_apply(struct rtw88_softc *,
    const struct rtw88_edca_params[RTW88_WME_NAC], unsigned);
/*
 * Copy one 802.11 frame into the next slot of a TX queue and kick it.
 * Lowest configured legacy basic rate, no fallback. Before station
 * setup, use 1M on 2.4 GHz or 6M on 5 GHz.
 * Management uses hardware sequence; all ACs retain the frame sequence.
 * At least a 24-byte header is required. The caller holds the bus
 * interrupt lock. ENOBUFS when the ring is full. QoS TIDs must match the
 * selected AC. Unsupported TIDs, HT Control and non-QoS on BK/VI/VO return
 * EOPNOTSUPP; EDCA quarantine returns its I/O error.
 */
int rtw88_tx_raw(struct rtw88_softc *, unsigned, const void *, size_t);
/*
 * Explicit legacy media context. The stack owns association and peer proof.
 * No node pointer escapes the caller. Rates use numeric 500 kbps units.
 * This opt-in API is not rate control. Caller holds the bus interrupt lock.
 * Only associated STA ToDS unicast payload DATA is eligible. Invalid metadata
 * and OFDM requiring ERP protection fail before DMA. No fallback is applied.
 */
#define RTW88_FIXED_11A 1U
#define RTW88_FIXED_11B 2U
#define RTW88_FIXED_11G 3U
struct rtw88_tx_fixed {
	uint16_t rate500, peer_mask, channel;
	uint8_t mode, associated, use_protection;
};
int rtw88_tx_fixed(struct rtw88_softc *, unsigned, const void *, size_t,
    const struct rtw88_tx_fixed *);

/* Caller holds the bus lock. Commands are posted, not firmware ACKs. */
void rtw88_ra_invalidate(struct rtw88_softc *);
int rtw88_ra_configure(struct rtw88_softc *, const struct rtw88_ra_config *);
int rtw88_tx_auto(struct rtw88_softc *, unsigned, const void *, size_t,
    uint64_t, unsigned);
/*
 * One QoS data MPDU of an established originator block-ack session, on
 * the current HT20 or VHT firmware-RA entry: the MAC may aggregate it
 * with its neighbours of the same TID into one A-MPDU PPDU. density is
 * the peer's minimum MPDU start spacing code (0-7); count is MAX_AGG_NUM
 * (1-31), at most 2 * count MPDUs, already bounded by the recipient
 * window and the peer's maximum A-MPDU length. Every other frame, the
 * legacy and protection-limited paths, and non-VHT builds never aggregate.
 */
struct rtw88_tx_agg {
	uint8_t tid, density, count;
};
#ifdef RTW88_VHT80
int rtw88_tx_ampdu(struct rtw88_softc *, unsigned, const void *, size_t,
    uint64_t, const struct rtw88_tx_agg *);
#endif

/* Read software ring capacity while holding the bus interrupt lock.
 * A DFS channel without a TX permit has no capacity. */
unsigned rtw88_tx_available(struct rtw88_softc *, unsigned);
/*
 * Legacy port0 station configuration, with software-only crypto. The
 * caller has switched channel and holds the bus interrupt lock. A NULL
 * BSSID and linked=0 select no-link with data RX paused. linked=1 requires
 * a unicast BSSID, AID1..2007, basic-rate bits0..11, and slot9 or20 us.
 * Input errors have no hardware effects. Hardware errors are returned;
 * the caller must not publish RUN after failure. Mailbox MAC ID is0.
 */
int rtw88_station_config(struct rtw88_softc *, const uint8_t *, unsigned,
    unsigned, unsigned, int);
/* Diagnostic loopback (rtlwifi LBMODE 0, 1, 3, 7 or 0x0b); 0 is normal. */
int rtw88_loopback(struct rtw88_softc *, int);
/*
 * Channel switch; caller holds the bus interrupt lock. A warm call keeps
 * the committed width; only rtw88_start_tuple() changes it. A failed
 * attempted mutation latches sc_stop_requested and closes host TX admission.
 * The lifecycle owner must stop outside that lock before restarting. An
 * initial read failure withdraws RX context but permits a safe warm retry.
 */
int rtw88_channel_apply(struct rtw88_softc *,
	const struct rtw88_channel_tuple *);
/* Narrow 20 MHz compatibility wrapper. */
int rtw88_channel(struct rtw88_softc *, unsigned);
/* Operator trigger. 1 starts, 0 stops. ENXIO until resources are ready. */
int rtwce_bootstrap(int);
int rtwce_started(void);
int rtwce_stats(char *, size_t);
int rtwce_h2c(uint32_t);
int rtwce_loopback(int);
int rtwce_lbmode(int);
int rtwce_channel(int *, int);

#endif /* _DEV_IC_RTW88VAR_H_ */
