/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8822CE resource lifecycle, AI-assisted. */
#ifndef _DEV_IC_RTW88VAR_H_
#define _DEV_IC_RTW88VAR_H_

#include <dev/ic/rtw88_hw.h>

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
#define RTW88_TXQ_MGMT		0U
#define RTW88_TXQ_BE		1U
#define RTW88_NTXQ		2U

struct rtw88_txq {
	struct rtw88_dma buf[RTW88_TX_SLOTS];
	unsigned nbuf;
	unsigned wp;		/* Next slot the host fills. */
	unsigned rp;		/* Oldest slot not yet completed. */
	unsigned queued;	/* Posted, completion not seen. */
};

/* Visible counters; errors are never silently dropped. */
struct rtw88_stats {
	uint64_t intr;
	uint64_t rx_frames;
	uint64_t rx_c2h;
	uint64_t rx_errors;	/* Descriptor rejected by the RX decoder. */
	uint64_t rx_short;	/* Shorter than an 802.11 ACK. */
	uint64_t rx_tag_errors;	/* RX tag mismatch: PCIe DMA timeout. */
	uint64_t rx_dma_errors;	/* Hardware index outside the ring. */
	uint64_t tx_queued;
	uint64_t tx_done;	/* Counted only from the chip's read index. */
	uint64_t tx_full;
	uint64_t tx_dropped;	/* Posted but not completed before stop. */
	uint64_t tx_dma_errors;	/* Read index outside the outstanding range. */
	uint64_t rx_beacons;
	uint8_t last_bssid[6];
	char last_ssid[33];	/* NUL-terminated; empty if malformed. */
	uint8_t last_ssid_len;
	uint8_t last_c2h_id;
	uint8_t last_c2h_seq;
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
	int sc_dma_master;
	int sc_powered;
	int sc_bootstrapping;
	int sc_stop_requested;
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
	unsigned sc_pg_rows;
	unsigned sc_pwr_rows;
	struct rtw88_dma sc_rxbuf[RTW88_RX_BUFS];
	unsigned sc_nrxbuf;
	unsigned sc_rx_rp;	/* Next RX descriptor the host reads. */
	unsigned sc_rx_tag;
	int sc_running;		/* Interrupts unmasked; rtw88_intr owns RX. */
	unsigned sc_channel;
	unsigned sc_basic_rates;	/* Negotiated legacy descriptor-rate bitmap. */
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
/* Caller serializes attach/detach; IRQ teardown precedes every DMA free. */
void rtw88_core_detach(struct rtw88_softc *);
int rtw88_start(struct rtw88_softc *);
void rtw88_stop(struct rtw88_softc *);
int rtw88_fw_command(struct rtw88_softc *, uint8_t, const void *, size_t);
/*
 * Interrupt entry; the bus serializes it against rtw88_stop's barrier.
 * Returns 0 when no unmasked cause is pending (shared INTx) or the
 * device is not running, without touching any register.
 */
int rtw88_intr(struct rtw88_softc *);
/* Diagnostic snapshot: HISR0, HIMR0, HISR3, HIMR3, RXBD_IDX, PCI_CTRL. */
#define RTW88_DIAG_REGS 6
int rtw88_diag(struct rtw88_softc *, uint32_t *);
/*
 * Copy one 802.11 frame into the next slot of a TX queue and kick it.
 * Lowest configured legacy basic rate, no fallback. Before station
 * setup, use 1M on 2.4 GHz or 6M on 5 GHz.
 * Management uses hardware sequence; BE retains the frame sequence.
 * At least a 24-byte header is required. The caller holds the bus
 * interrupt lock. ENOBUFS when the ring is full.
 */
int rtw88_tx_raw(struct rtw88_softc *, unsigned, const void *, size_t);
/* Read software ring capacity while holding the bus interrupt lock. */
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
/* 20 MHz channel switch; caller holds the bus interrupt lock. */
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
