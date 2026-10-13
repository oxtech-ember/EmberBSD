/*-
 * Copyright (c) 2026 Anton and EmberBSD contributors
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * AICSemi AIC8800D80 FullMAC wireless adapter on SDIO.
 *
 * The chip starts in a boot ROM that takes debug messages: read and
 * write memory, start a program. The driver uploads the firmware with
 * them and then talks to it in the same framing.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/condvar.h>
#include <sys/device.h>
#include <sys/atomic.h>
#include <sys/endian.h>
#include <sys/evcnt.h>
#include <sys/kernel.h>
#include <sys/kauth.h>
#include <sys/kmem.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/pool.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/workqueue.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_ether.h>
#include <net/if_media.h>
#include <net/route.h>

#include <netinet/in.h>

#include <net80211/ieee80211_var.h>

#include <dev/firmload.h>
#include <dev/ic/fullmac_sae.h>
#include <dev/sdmmc/if_aicwf_sae.h>
#include <dev/sdmmc/if_aicwf_vht.h>
#include <dev/sdmmc/if_aicwf_sta.h>
#include <dev/sdmmc/sdmmcchip.h>
#include <dev/sdmmc/sdmmcvar.h>

#define	AICWF_SDIO_VENDOR		0xc8a1
#define	AICWF_SDIO_PRODUCT_D80		0x0082

/* Function 1 registers. */
#define	AICWF_INTR_ENABLE		0x00
#define	 AICWF_INTR_ENABLE_ALL		0x07
#define	AICWF_INTR_PENDING		0x01
#define	 AICWF_INTR_PENDING_SOFT	__BIT(0)
#define	AICWF_FLOW_CTRL			0x03	/* free transmit buffers */
#define	AICWF_INTR_STATUS		0x04
#define	 AICWF_INTR_STATUS_OTHER	__BIT(7)
#define	 AICWF_INTR_STATUS_BLOCKS	__BITS(6,0)
#define	  AICWF_INTR_STATUS_BYTEMODE	120
#define	AICWF_BYTEMODE_LEN		0x05	/* in words */
#define	AICWF_BYTEMODE_ENABLE		0x07
#define	 AICWF_BYTEMODE_DISABLED	1
#define	AICWF_RD_FIFO			0x0f
#define	AICWF_WR_FIFO			0x10

/* Function 0 register that selects the pad settings the vendor uses. */
#define	AICWF_FN0_PAD			0xf2
#define	 AICWF_FN0_PAD_DEFAULT		0x7f

#define	AICWF_BLOCK_SIZE		512
#define	AICWF_TX_BUFFER_SIZE		1536	/* one unit of flow control */
#define	AICWF_RX_MAX			(119 * AICWF_BLOCK_SIZE)

/*
 * A frame starts with its length in 12 bits, a type and, to the chip, a
 * checksum of those three bytes. A message follows after one more word.
 */
#define	AICWF_HDR_LEN			4
#define	AICWF_TYPE_CFG			0x10	/* bit: not a data frame */
#define	AICWF_TYPE_MSG			0x11
#define	AICWF_TYPE_TX_CFM		0x12
#define	AICWF_TYPE_DATA			0x01	/* to the chip */
#define	AICWF_TX_MSG_OFFSET		8
#define	AICWF_RX_MSG_OFFSET		4

/* Message header: id, destination, source, parameter length. */
#define	AICWF_MSG_HDR_LEN		8
#define	AICWF_RX_MSG_PARAM		12	/* a pattern word comes first */
#define	AICWF_MSG_MAX_PARAM		1032
#define	AICWF_TASK(id)			((id) >> 10)
#define	AICWF_TASK_DRIVER		100

/* Debug task: what the boot ROM understands. */
#define	AICWF_DBG_MEM_READ_REQ		0x0400
#define	AICWF_DBG_MEM_READ_CFM		0x0401
#define	AICWF_DBG_MEM_WRITE_REQ		0x0402
#define	AICWF_DBG_MEM_WRITE_CFM		0x0403
#define	AICWF_DBG_MEM_BLOCK_WRITE_REQ	0x040b
#define	AICWF_DBG_MEM_BLOCK_WRITE_CFM	0x040c
#define	AICWF_DBG_START_APP_REQ		0x040d
#define	AICWF_DBG_START_APP_CFM		0x040e
#define	 AICWF_START_APP_AUTO		1

#define	AICWF_BLOCK_WRITE_MAX		1024

/* MAC management task of the running firmware. */
#define	AICWF_MM_RESET_REQ		0x0000
#define	AICWF_MM_RESET_CFM		0x0001
#define	AICWF_MM_START_REQ		0x0002
#define	AICWF_MM_START_CFM		0x0003
#define	AICWF_MM_VERSION_REQ		0x0004
#define	AICWF_MM_VERSION_CFM		0x0005
#define	AICWF_MM_ADD_IF_REQ		0x0006
#define	AICWF_MM_ADD_IF_CFM		0x0007
#define	 AICWF_IF_STA			0
#define	AICWF_MM_SET_RF_CALIB_REQ	0x0069
#define	AICWF_MM_SET_RF_CALIB_CFM	0x006a

/* Host-side link liveness: the AP beacons roughly every 102 ms; two
 * consecutive silent checks at a 2 s cadence mean the association is dead
 * even though the firmware never raised SM_DISCONNECT_IND. */
#define	AICWF_ALIVE_PERIOD_MS	2000
#define	AICWF_ALIVE_SILENT_MS	10000
#define	AICWF_MM_GET_MAC_ADDR_REQ	0x0073
#define	AICWF_MM_GET_MAC_ADDR_CFM	0x0074
#define	AICWF_MM_SET_STACK_START_REQ	0x007b
#define	AICWF_MM_SET_STACK_START_CFM	0x007c
#define	AICWF_MM_GET_FW_VERSION_REQ	0x0080
#define	AICWF_MM_GET_FW_VERSION_CFM	0x0081

#define	AICWF_MM_KEY_ADD_REQ		0x0024
#define	AICWF_MM_KEY_ADD_CFM		0x0025
#define	 AICWF_CIPHER_CCMP		2
#define	 AICWF_CIPHER_BIP		5
#define	 AICWF_STA_NONE			0xff	/* a group key */
#define	AICWF_MM_KEY_DEL_REQ		0x0026
#define	AICWF_MM_KEY_DEL_CFM		0x0027
#define	AICWF_MM_CONNECTION_LOSS_IND	0x0043
#define	AICWF_MM_CHANNEL_SWITCH_IND	0x0044
#define	AICWF_MM_CHANNEL_PRE_SWITCH_IND	0x0045
#define	AICWF_MM_CHANNEL_SURVEY_IND	0x004f

/* Scan task. */
#define	AICWF_SCANU_START_REQ		0x1000
#define	AICWF_SCANU_START_CFM		0x1001	/* the scan is over */
#define	AICWF_SCANU_RESULT_IND		0x1004
#define	AICWF_SCANU_START_ACK		0x1009	/* the scan has begun */
#define	AICWF_SCANU_CANCEL_REQ		0x100a
#define	AICWF_SCANU_CANCEL_CFM		0x100b

/* Management entity task. */
#define	AICWF_ME_CONFIG_REQ		0x1400
#define	AICWF_ME_CONFIG_CFM		0x1401
#define	AICWF_ME_CHAN_CONFIG_REQ	0x1402
#define	AICWF_ME_CHAN_CONFIG_CFM	0x1403

#define	AICWF_ME_SET_CONTROL_PORT_REQ	0x1404
#define	AICWF_ME_SET_CONTROL_PORT_CFM	0x1405

/* Station management task. */
#define	AICWF_SM_CONNECT_REQ		0x1800
#define	AICWF_SM_CONNECT_CFM		0x1801
#define	AICWF_SM_CONNECT_IND		0x1802
#define	AICWF_SM_DISCONNECT_REQ		0x1803
#define	AICWF_SM_DISCONNECT_CFM		0x1804
#define	AICWF_SM_DISCONNECT_IND		0x1805
#define	AICWF_SM_EXTERNAL_AUTH_IND	0x1806
#define	AICWF_SM_EXTERNAL_AUTH_RSP	0x1807
#define	AICWF_SM_EXTERNAL_AUTH_CFM	0x180c
#define	AICWF_ME_TRAFFIC_IND_REQ		0x140b
#define	 AICWF_CONNECT_MFP		__BIT(4)
#define	 AICWF_FEATURE_MFP		__BIT(13)
#define	 AICWF_AUTH_SAE			3
#define	 AICWF_CONNECT_PORT_HOST	__BIT(0) /* the host opens the port */
#define	 AICWF_CONNECT_WPA		__BIT(3)

/*
 * A frame to send starts with a descriptor: payload length, flags,
 * host tag, destination, source, Ethernet type, queue, traffic class,
 * interface, station, flags. The payload of the Ethernet frame follows.
 */
#define	AICWF_TXDESC_LEN		28
#define	AICWF_HWQ_BE			1
#define	AICWF_HWQ_VO			3
#define	AICWF_TX_MGMT			__BIT(3)
#define	AICWF_TID_NONE			0xff
#define	AICWF_TX_RESERVE		2	/* buffers left to messages */

/*
 * A received frame starts with 56 bytes from the radio: the length of
 * the 802.11 frame first, a status word and a flags word further on.
 * The 802.11 frame follows after one more word.
 */
#define	AICWF_RXHDR_LEN			60	/* with that word */
#define	AICWF_RXHDR_STATUS		36
#define	 AICWF_RX_DECR			__BITS(4,2)
#define	  AICWF_RX_DECR_WEP		1
#define	  AICWF_RX_DECR_TKIP		2
#define	  AICWF_RX_DECR_WAPI		7
#define	AICWF_RXHDR_FLAGS		48
#define	 AICWF_RX_FLAG_UPLOAD		__BIT(6)
#define	 AICWF_RX_FLAG_REORDER		__BIT(5)	/* part of an A-MPDU */
#define	 AICWF_RX_FLAG_AMSDU		__BIT(0)

/*
 * With 802.11n the firmware answers the block acknowledgement itself but
 * hands the frames up in the order they arrived; the host puts them in
 * sequence. The window and the wait for a missing frame are the vendor
 * driver's.
 */
#define	AICWF_REORDER_WINDOW		64
#define	AICWF_REORDER_WAIT_MS		50
#define	AICWF_NTID			16
#define	AICWF_SEQ_MASK			0xfff
#define	AICWF_SEQ_SUB(a, b)		(((a) - (b)) & AICWF_SEQ_MASK)
#define	AICWF_SEQ_BEFORE(a, b)		(AICWF_SEQ_SUB(a, b) >= 0x800)

#define	AICWF_SCAN_INTERVAL_MS		3000

/* A channel as the firmware describes it: 6 bytes. */
#define	AICWF_CHAN_LEN			6
#define	AICWF_CHAN_2G_MAX		14
#define	AICWF_CHAN_5G_MAX		28
#define	AICWF_CHAN_MAX			(AICWF_CHAN_2G_MAX + AICWF_CHAN_5G_MAX)
#define	AICWF_BAND_2G			0
#define	AICWF_BAND_5G			1
#define	AICWF_TX_POWER_DBM		20

/* Radio calibration, the vendor's defaults for this chip. */
#define	AICWF_CAL_CFG_2G		0x0f8f
#define	AICWF_CAL_CFG_5G		0x0f0f
#define	AICWF_CAL_PARAM_ALPHA		0x0c34c008
#define	AICWF_CAL_BT_PARAM		0x00264203

#define	AICWF_UAPSD_TIMEOUT_MS		300
#define	AICWF_LP_CLK_PPM		20

#define	AICWF_CHIP_ID_ADDR		0x40500000
#define	 AICWF_CHIP_ID_REV		__BITS(21,16)
#define	 AICWF_CHIP_ID_H		__BITS(23,22)

/*
 * Firmware. It runs from RAM at AICWF_FW_ADDR and reads its options from
 * a table of (offset, value) pairs that the driver writes into it.
 */
#define	AICWF_FW_NAME			"fmacfw_8800d80_u02.bin"
#define	AICWF_FW_NAME_H			"fmacfw_8800d80_h_u02.bin"
#define	AICWF_FW_ADDR			0x00120000
#define	AICWF_FW_VERSION		(AICWF_FW_ADDR + 0x01c)
#define	AICWF_FW_CONFIG_BASE		(AICWF_FW_ADDR + 0x198)
#define	AICWF_FW_PATCH_DESC		(AICWF_FW_ADDR + 0x1a0)
#define	AICWF_FW_PATCH_BUF		(AICWF_FW_ADDR + 0x1a4)
#define	AICWF_FW_PATCH_BUF_OLD		0x0016f800
#define	AICWF_FW_VERSION_PATCH_BUF	0x06090100
/*
 * Bluetooth. The controller half of the chip talks HCI on its own UART,
 * but its patches go in through the boot ROM, before the wireless
 * firmware starts. The patch table is a tagged list of entries, each a
 * name, a type and (address, value) pairs.
 */
#define	AICWF_BT_TABLE_NAME		"fw_patch_table_8800d80_u02.bin"
#define	AICWF_BT_ADID_NAME		"fw_adid_8800d80_u02.bin"
#define	AICWF_BT_PATCH_NAME		"fw_patch_8800d80_u02.bin"
#define	AICWF_BT_EXT_NAME		"fw_patch_8800d80_u02_ext%u.bin"
#define	AICWF_BT_TABLE_TAG		"AICBT_PT_TAG"
#define	AICWF_BT_TABLE_TAG_LEN		16
#define	AICWF_BT_ENTRY_HDR		24
#define	AICWF_BT_ENTRY_TYPE_OFF		16
#define	AICWF_BT_ENTRY_LEN_OFF		20
#define	AICWF_BT_PT_INF			0
#define	AICWF_BT_PT_BTMODE		3
#define	AICWF_BT_PT_PWRON		4
#define	AICWF_BT_PT_VER			6
#define	AICWF_BT_PT_NODATA		1000
/* Pairs of the information entry. */
#define	AICWF_BT_INF_ADID		0
#define	AICWF_BT_INF_PATCH		1
#define	AICWF_BT_INF_WRITES		4	/* pairs the chip is given */
#define	AICWF_BT_INF_EXT_COUNT		4
#define	AICWF_BT_INF_EXT		5
#define	AICWF_BT_ADID_ADDR		0x00201940
#define	AICWF_BT_PATCH_ADDR		0x0020b43c
/* Values of the mode entry, in the order of its pairs. */
#define	AICWF_BT_MODE_NO_SWITCH		5	/* shares the antenna */
#define	AICWF_BT_PORT_UART		2
#define	AICWF_BT_BAUD			1500000
#define	AICWF_BT_TXPWR			0x00006f2f

#define	AICWF_PATCH_MAGIC		0x48435450	/* "PTCH" */
#define	AICWF_PATCH_MAGIC_2		0x50544348	/* "HCTP" */
/* Offsets in the patch descriptor. */
#define	AICWF_PATCH_MAGIC_OFF		0x00
#define	AICWF_PATCH_PAIRS_OFF		0x04
#define	AICWF_PATCH_MAGIC_2_OFF		0x08
#define	AICWF_PATCH_COUNT_OFF		0x0c
#define	AICWF_PATCH_BLOCK_SIZE_OFF	0x30	/* four words */

#define	AICWF_CMD_TIMEOUT_MS		2000

/* The options the vendor driver gives this firmware by default. */
static const uint32_t aicwf_fw_options[][2] = {
	{ 0x00b4, 0xf3010001 },		/* 2.4 and 5 GHz */
	{ 0x0188, 0x00000001 },		/* power offsets cover calibration */
};

enum aicwf_task_cmd {
	AICWF_TASK_NEWSTATE,
	AICWF_TASK_KEY_SET,
	AICWF_TASK_KEY_DELETE,
	AICWF_TASK_STA_MGMT,
	AICWF_TASK_STA_CANCEL_SCAN,
};

struct aicwf_task {
	struct work		t_work;
	enum aicwf_task_cmd	t_cmd;
	enum ieee80211_state	t_state;
	int			t_arg;
	u_int			t_cipher;
	u_int			t_keyix;
	bool			t_group;
	uint64_t		t_epoch;
	u_int			t_keylen;
	uint8_t			t_key[IEEE80211_KEYBUF_SIZE];
	struct ieee80211req_sta_mgmt t_sta;
};

#define	AICWF_TASK_COUNT		16
#define	AICWF_KEY_SLOTS			(IEEE80211_WEP_NKID + 1)
#define	AICWF_KEY_SLOT_PAIRWISE		IEEE80211_WEP_NKID
#define	AICWF_HWKEY_NONE		0xff

struct aicwf_softc {
	device_t		sc_dev;
	struct sdmmc_function	*sc_sf;		/* function 1 */
	void			*sc_ih;

	kmutex_t		sc_lock;
	kmutex_t		sc_epoch_lock;	/* non-sleeping key callbacks */
	kmutex_t		sc_bus_lock;	/* the SDIO layer has none */
	size_t			sc_io_max;	/* bytes in one transfer */
	kcondvar_t		sc_cv;

	uint8_t			*sc_txbuf;
	uint8_t			*sc_rxbuf;

	/* The confirmation the sender of a request waits for. */
	uint16_t		sc_cfm_id;
	bool			sc_cfm_done;
	uint16_t		sc_cfm_len;
	uint8_t			sc_cfm[AICWF_MSG_MAX_PARAM];

	bool			sc_cmd_busy;

	uint8_t			sc_enaddr[ETHER_ADDR_LEN];
	bool			sc_5ghz;
	uint32_t		sc_fw_version;
	uint8_t			sc_vif;

	struct ieee80211com	sc_ic;
	struct ethercom		sc_ec;
#define	sc_if			sc_ec.ec_if
	bool			sc_if_attached;
	int			(*sc_newstate)(struct ieee80211com *,
				    enum ieee80211_state, int);
	struct workqueue	*sc_taskq;
	pool_cache_t		sc_taskpool;
	struct workqueue	*sc_txq;
	struct work		sc_txwork;
	volatile u_int		sc_txqueued;

	/* Frames of an A-MPDU waiting for the ones before them. */
	kmutex_t		sc_reorder_lock;
	callout_t		sc_reorder_ch;
	callout_t		sc_alive_ch;
	struct work		sc_alive_work;
	struct aicwf_ft_bss {
		uint8_t		bssid[ETHER_ADDR_LEN];
		uint16_t	mdid;
		uint8_t		r0kh_len;
		uint8_t		r0kh[45];
	}			sc_ft_bss[4];
	/* Host-managed 802.11k/v exchange (IEEE80211_IOC_STA_MGMT). */
	/* Station requests and firmware ownership are protected by sc_lock. */
	bool			sc_sta_enabled;
	bool			sc_sta_tx_pending, sc_sta_tx_sent;
	bool			sc_sta_tx_mismatch_reported;
	uint32_t		sc_sta_token;
	struct ieee80211req_sta_mgmt sc_sta_tx;
	bool			sc_sta_scan_pending, sc_sta_scan_started;
	bool			sc_scan_published, sc_sta_scan_draining;
	bool			sc_sta_cancel_queued;
	uint32_t		sc_sta_scan_serial;
	struct ieee80211req_sta_mgmt sc_sta_scan;
	uint16_t		sc_sta_assoc_len;
	uint8_t			sc_sta_assoc[804];
	volatile u_int		sc_alive_rx;	/* ticks of last RX frame */
	u_int			sc_alive_misses;
	bool			sc_alive_queued;
	uint64_t		sc_alive_epoch;
	struct aicwf_reorder {
		bool		started;
		uint16_t	head;	/* next sequence number to pass */
		u_int		held;
		struct mbuf	*slot[AICWF_REORDER_WINDOW];
	}			sc_reorder[AICWF_NTID];
	struct evcnt		sc_ev_ampdu;	/* frames of an A-MPDU */
	struct evcnt		sc_ev_amsdu;	/* frames with several packets */
	struct evcnt		sc_ev_gap;	/* waits that ran out */

	bool			sc_scanning;
	int			sc_scan_ticks;
	bool			sc_connecting;
	bool			sc_connected;
	bool			sc_qos;
	uint8_t			sc_ap;		/* station index of the AP */
	uint8_t			sc_hwkey[AICWF_KEY_SLOTS];
	uint16_t		sc_sae_caps;
	bool			sc_sae_enabled;
	bool			sc_sae_pending;
	bool			sc_sae_authenticated;
	uint32_t		sc_sae_generation;
	uint64_t		sc_sae_epoch;	/* changes on every join/leave */
	uint8_t			sc_sae_bssid[ETHER_ADDR_LEN];
	uint8_t			sc_igtk[2];
	/* Protected by sc_lock; valid only after a complete key confirmation. */
	bool			sc_ptk_valid;
	uint8_t			sc_ptk[16];
	uint64_t		sc_pmf_rx_pn;
	struct evcnt		sc_ev_sae_start;
	struct evcnt		sc_ev_sae_rx;
	struct evcnt		sc_ev_sae_tx;
	struct evcnt		sc_ev_sae_drop;
};

static int	aicwf_match(device_t, cfdata_t, void *);
static void	aicwf_attach(device_t, device_t, void *);
static void	aicwf_attachhook(device_t);
static int	aicwf_intr(void *);
static void	aicwf_ifattach(struct aicwf_softc *);
static void	aicwf_scan_result(struct aicwf_softc *, const uint8_t *,
		    size_t);
static void	aicwf_scan_done(struct aicwf_softc *, const uint8_t *, size_t);
static void	aicwf_connect_ind(struct aicwf_softc *, const uint8_t *,
		    size_t);
static void	aicwf_disconnect(struct aicwf_softc *);
static void	aicwf_disconnect_ind(struct aicwf_softc *, const uint8_t *, size_t);
static void	aicwf_alive_lost(struct aicwf_softc *);
static void	aicwf_alive_work(struct aicwf_softc *);
static void	aicwf_sae_event(struct aicwf_softc *, bool,
		    const uint8_t *, size_t);
static void	aicwf_rx_data(struct aicwf_softc *, const uint8_t *, size_t);
static void	aicwf_reorder_flush(struct aicwf_softc *, int, bool);
static void	aicwf_reorder_timeout(void *);

static void	aicwf_sta_tx_cfm(struct aicwf_softc *, const uint8_t *, size_t);
static bool	aicwf_sta_ready(struct aicwf_softc *);
static bool	aicwf_sta_scan_finish_locked(struct aicwf_softc *, bool, void *);
static void	aicwf_sta_retire_locked(struct aicwf_softc *, uint8_t *, bool *,
		    uint8_t *, bool *);
static int	aicwf_newstate(struct ieee80211com *, enum ieee80211_state, int);

CFATTACH_DECL_NEW(aicwf, sizeof(struct aicwf_softc),
    aicwf_match, aicwf_attach, NULL, NULL);

/* CRC-8, polynomial x^8 + x^2 + x + 1, as the chip checks it. */
static uint8_t
aicwf_crc8(const uint8_t *buf, size_t len)
{
	uint8_t crc = 0;
	u_int bit;

	while (len-- > 0) {
		for (bit = 0x80; bit != 0; bit >>= 1) {
			const bool carry = (crc & 0x80) != 0;

			crc <<= 1;
			if (carry)
				crc ^= 0x07;
			if ((*buf & bit) != 0)
				crc ^= 0x07;
		}
		buf++;
	}

	return crc;
}

/*
 * Move whole blocks between a buffer and the chip. A host controller
 * takes only so much in one transfer, so a long one goes in parts.
 */
static int
aicwf_fifo(struct aicwf_softc *sc, bool write, uint8_t *buf, size_t len)
{
	int error = 0;

	KASSERT(mutex_owned(&sc->sc_bus_lock));

	while (len > 0 && error == 0) {
		const size_t n = MIN(len, sc->sc_io_max);

		if (write)
			error = sdmmc_io_write_multi_1(sc->sc_sf,
			    AICWF_WR_FIFO, buf, n);
		else
			error = sdmmc_io_read_multi_1(sc->sc_sf,
			    AICWF_RD_FIFO, buf, n);
		buf += n;
		len -= n;
	}

	return error;
}

/* Wait until the chip has room for a frame of this many bytes. */
static int
aicwf_tx_wait(struct aicwf_softc *sc, size_t len, u_int reserve)
{
	u_int tries, buffers = 0;

	for (tries = 0; tries < 50; tries++) {
		mutex_enter(&sc->sc_bus_lock);
		/* D80 uses all eight bits, unlike the older 8801/DC/DW. */
		buffers = sdmmc_io_read_1(sc->sc_sf, AICWF_FLOW_CTRL);
		mutex_exit(&sc->sc_bus_lock);

		if (buffers > reserve &&
		    len <= (buffers - reserve) * AICWF_TX_BUFFER_SIZE)
			return 0;
		sdmmc_pause(tries < 30 ? 200 : 10000, NULL);
	}

	IEEE80211_DPRINTF(&sc->sc_ic, IEEE80211_MSG_STATE,
	    "SDIO transmit busy: buffers %u reserve %u length %zu\n",
	    buffers, reserve, len);
	return EBUSY;
}

/*
 * Write a frame of len bytes from the transmit buffer: whole words, and
 * a frame that ends inside a block gets a zero word after it.
 */
static int
aicwf_write_attempted(struct aicwf_softc *sc, size_t len, u_int reserve,
    bool *attempted)
{
	uint8_t * const buf = sc->sc_txbuf;
	size_t total;
	int error;

	KASSERT(mutex_owned(&sc->sc_lock));

	total = roundup(len, 4);
	if (total % AICWF_BLOCK_SIZE != 0)
		total = roundup(total + AICWF_HDR_LEN, AICWF_BLOCK_SIZE);
	memset(buf + len, 0, total - len);

	if (attempted != NULL)
		*attempted = false;
	error = aicwf_tx_wait(sc, total, reserve);
	if (error == 0) {
		mutex_enter(&sc->sc_bus_lock);
		if (attempted != NULL)
			*attempted = true;
		error = aicwf_fifo(sc, true, buf, total);
		mutex_exit(&sc->sc_bus_lock);
	}

	return error;
}

static int
aicwf_write(struct aicwf_softc *sc, size_t len, u_int reserve)
{
	return aicwf_write_attempted(sc, len, reserve, NULL);
}

/*
 * Send a request and wait for its confirmation. The parameters of the
 * confirmation are copied to cfm, which holds cfm_len bytes.
 */
static int
aicwf_cmd_reply(struct aicwf_softc *sc, uint16_t id, const void *param,
    size_t param_len, uint16_t cfm_id, void *cfm, size_t cfm_len, bool strict,
    uint64_t epoch)
{
	uint8_t * const buf = sc->sc_txbuf;
	size_t len;
	int error;

	KASSERT(param_len <= AICWF_MSG_MAX_PARAM);

	mutex_enter(&sc->sc_lock);
	while (sc->sc_cmd_busy)
		cv_wait(&sc->sc_cv, &sc->sc_lock);
	/* The caller may have retired while waiting for another command. */
	if (epoch != 0 && epoch != sc->sc_sae_epoch) {
		mutex_exit(&sc->sc_lock);
		return ESTALE;
	}
	if ((id == AICWF_SCANU_START_REQ && epoch != 0 &&
	    (!sc->sc_sta_scan_pending || !sc->sc_sta_scan_started ||
	    !aicwf_sta_ready(sc))) ||
	    (id == AICWF_SCANU_CANCEL_REQ &&
	    (!sc->sc_sta_scan_started || !sc->sc_sta_scan_draining ||
	    !sc->sc_scanning))) {
		mutex_exit(&sc->sc_lock);
		return ECANCELED;
	}
	sc->sc_cmd_busy = true;

	len = AICWF_TX_MSG_OFFSET + AICWF_MSG_HDR_LEN + param_len;
	memset(buf, 0, AICWF_TX_MSG_OFFSET + AICWF_MSG_HDR_LEN);
	le16enc(buf, (len - AICWF_HDR_LEN) & 0x0fff);
	buf[2] = AICWF_TYPE_MSG;
	buf[3] = aicwf_crc8(buf, 3);
	le16enc(buf + AICWF_TX_MSG_OFFSET + 0, id);
	le16enc(buf + AICWF_TX_MSG_OFFSET + 2, AICWF_TASK(id));
	le16enc(buf + AICWF_TX_MSG_OFFSET + 4, AICWF_TASK_DRIVER);
	le16enc(buf + AICWF_TX_MSG_OFFSET + 6, param_len);
	if (param_len != 0)
		memcpy(buf + AICWF_TX_MSG_OFFSET + AICWF_MSG_HDR_LEN, param,
		    param_len);

	sc->sc_cfm_id = cfm_id;
	sc->sc_cfm_done = false;

	error = aicwf_write_attempted(sc, len, 0,
	    id == AICWF_SCANU_START_REQ ? &sc->sc_scan_published : NULL);
	if (strict)
		explicit_memset(buf + AICWF_TX_MSG_OFFSET +
		    AICWF_MSG_HDR_LEN, 0, param_len);
	while (error == 0 && !sc->sc_cfm_done) {
		error = cv_timedwait(&sc->sc_cv, &sc->sc_lock,
		    mstohz(AICWF_CMD_TIMEOUT_MS));
	}
	if (error == 0 && strict && sc->sc_cfm_len < cfm_len)
		error = EPROTO;
	if (error == 0 && cfm_len != 0) {
		/* A shorter answer reads as zeros past its end. */
		memset(cfm, 0, cfm_len);
		memcpy(cfm, sc->sc_cfm, MIN(cfm_len, sc->sc_cfm_len));
	}
	sc->sc_cfm_id = 0;
	sc->sc_cmd_busy = false;
	cv_broadcast(&sc->sc_cv);

	mutex_exit(&sc->sc_lock);

	return error;
}

static int
aicwf_cmd(struct aicwf_softc *sc, uint16_t id, const void *param,
    size_t param_len, uint16_t cfm_id, void *cfm, size_t cfm_len)
{
	return aicwf_cmd_reply(sc, id, param, param_len, cfm_id, cfm,
	    cfm_len, false, 0);
}

static void
aicwf_rx_msg
(struct aicwf_softc *sc, const uint8_t *msg, size_t len)
{
	if (len < AICWF_RX_MSG_PARAM)
		return;

	const uint16_t id = le16dec(msg);
	const uint16_t param_len = le16dec(msg + 6);

	if (param_len > len - AICWF_RX_MSG_PARAM)
		return;

	switch (id) {
	case AICWF_SCANU_RESULT_IND:
		aicwf_scan_result(sc, msg + AICWF_RX_MSG_PARAM, param_len);
		return;
	case AICWF_SCANU_START_CFM:
		aicwf_scan_done(sc, msg + AICWF_RX_MSG_PARAM, param_len);
		return;
	case AICWF_SM_EXTERNAL_AUTH_IND:
		aicwf_sae_event(sc, true, msg + AICWF_RX_MSG_PARAM, param_len);
		return;
	case AICWF_SM_CONNECT_IND:
		aicwf_connect_ind(sc, msg + AICWF_RX_MSG_PARAM, param_len);
		return;
	case AICWF_SM_DISCONNECT_IND:
		aicwf_disconnect_ind(sc, msg + AICWF_RX_MSG_PARAM, param_len);
		return;
	case AICWF_MM_CHANNEL_SWITCH_IND:
	case AICWF_MM_CHANNEL_PRE_SWITCH_IND:
	case AICWF_MM_CHANNEL_SURVEY_IND:
		/* The radio moving between channels: nothing to do. */
		return;
	case AICWF_ME_TRAFFIC_IND_REQ:
		/* AP-mode UAPSD housekeeping; a station has nothing to add. */
		return;
	case AICWF_MM_CONNECTION_LOSS_IND:
		device_printf(sc->sc_dev, "firmware reported connection loss\n");
		aicwf_alive_lost(sc);
		return;
	}

	if (param_len > sizeof(sc->sc_cfm))
		return;

	mutex_enter(&sc->sc_lock);
	if (id == sc->sc_cfm_id && !sc->sc_cfm_done) {
		memcpy(sc->sc_cfm, msg + AICWF_RX_MSG_PARAM, param_len);
		sc->sc_cfm_len = param_len;
		sc->sc_cfm_done = true;
		cv_broadcast(&sc->sc_cv);
	} else {
		device_printf(sc->sc_dev, "unexpected message 0x%04x\n", id);
	}
	mutex_exit(&sc->sc_lock);
}

/* Split what one read returned into frames. */
static void
aicwf_rx(struct aicwf_softc *sc, const uint8_t *buf, size_t len)
{
	while (len >= AICWF_HDR_LEN) {
		const size_t frame_len = le16dec(buf);
		const uint8_t type = buf[2];

		if (frame_len == 0)
			break;
		if ((type & AICWF_TYPE_CFG) == 0) {
			const size_t total = frame_len + AICWF_RXHDR_LEN;

			if (total > len)
				break;
			aicwf_rx_data(sc, buf, frame_len);
			if (roundup(total, 4) >= len)
				break;
			buf += roundup(total, 4);
			len -= roundup(total, 4);
			continue;
		}
		if (frame_len > len - AICWF_RX_MSG_OFFSET)
			break;
		if ((type & 0x7f) == AICWF_TYPE_TX_CFM)
			aicwf_sta_tx_cfm(sc, buf + AICWF_RX_MSG_OFFSET, frame_len);
		if ((type & 0x7f) == AICWF_TYPE_MSG)
			aicwf_rx_msg(sc, buf + AICWF_RX_MSG_OFFSET,
			    frame_len);

		const size_t step = AICWF_RX_MSG_OFFSET +
		    roundup(frame_len, 4);
		if (step >= len)
			break;
		buf += step;
		len -= step;
	}
}

static int
aicwf_intr(void *arg)
{
	struct aicwf_softc * const sc = arg;
	struct sdmmc_function * const sf = sc->sc_sf;
	uint8_t status, pending;
	size_t len;
	int error;

	mutex_enter(&sc->sc_bus_lock);
	status = sdmmc_io_read_1(sf, AICWF_INTR_STATUS);
	if (status == 0) {
		mutex_exit(&sc->sc_bus_lock);
		return 0;
	}

	if ((status & AICWF_INTR_STATUS_OTHER) != 0) {
		pending = sdmmc_io_read_1(sf, AICWF_INTR_PENDING);
		sdmmc_io_write_1(sf, AICWF_INTR_PENDING,
		    pending & ~AICWF_INTR_PENDING_SOFT);
	}

	status = __SHIFTOUT(status, AICWF_INTR_STATUS_BLOCKS);
	if (status == AICWF_INTR_STATUS_BYTEMODE)
		len = sdmmc_io_read_1(sf, AICWF_BYTEMODE_LEN) * 4;
	else
		len = status * AICWF_BLOCK_SIZE;
	if (len == 0 || len > AICWF_RX_MAX) {
		mutex_exit(&sc->sc_bus_lock);
		return 1;
	}
	error = aicwf_fifo(sc, false, sc->sc_rxbuf, len);
	mutex_exit(&sc->sc_bus_lock);

	if (error != 0) {
		device_printf(sc->sc_dev, "read of %zu bytes failed\n", len);
		return 1;
	}
	aicwf_rx(sc, sc->sc_rxbuf, len);

	return 1;
}

static int
aicwf_mem_read(struct aicwf_softc *sc, uint32_t addr, uint32_t *data)
{
	uint8_t req[4], cfm[8];
	int error;

	le32enc(req, addr);
	error = aicwf_cmd(sc, AICWF_DBG_MEM_READ_REQ, req, sizeof(req),
	    AICWF_DBG_MEM_READ_CFM, cfm, sizeof(cfm));
	if (error == 0)
		*data = le32dec(cfm + 4);

	return error;
}

static int
aicwf_mem_write(struct aicwf_softc *sc, uint32_t addr, uint32_t data)
{
	uint8_t req[8], cfm[8];

	le32enc(req, addr);
	le32enc(req + 4, data);
	return aicwf_cmd(sc, AICWF_DBG_MEM_WRITE_REQ, req, sizeof(req),
	    AICWF_DBG_MEM_WRITE_CFM, cfm, sizeof(cfm));
}

static int
aicwf_fw_read(struct aicwf_softc *sc, const char *name, uint8_t **datap,
    size_t *sizep)
{
	firmware_handle_t fh;
	uint8_t *data;
	size_t size;
	int error;

	error = firmware_open("if_aicwf", name, &fh);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev, "couldn't open %s: %d\n",
		    name, error);
		return error;
	}
	size = firmware_get_size(fh);
	data = firmware_malloc(size);
	error = firmware_read(fh, 0, data, size);
	firmware_close(fh);
	if (error != 0) {
		firmware_free(data, size);
		return error;
	}
	*datap = data;
	*sizep = size;

	return 0;
}

/* Copy a firmware file into the chip's memory. */
static int
aicwf_upload(struct aicwf_softc *sc, const char *name, uint32_t addr)
{
	uint8_t req[8 + AICWF_BLOCK_WRITE_MAX], cfm[4];
	uint8_t *data;
	size_t size, off, n;
	int error;

	if ((error = aicwf_fw_read(sc, name, &data, &size)) != 0)
		return error;

	for (off = 0; off < size && error == 0; off += n) {
		n = MIN(size - off, AICWF_BLOCK_WRITE_MAX);
		memset(req, 0, sizeof(req));
		le32enc(req, addr + off);
		le32enc(req + 4, n);
		memcpy(req + 8, data + off, n);
		error = aicwf_cmd(sc, AICWF_DBG_MEM_BLOCK_WRITE_REQ, req,
		    sizeof(req), AICWF_DBG_MEM_BLOCK_WRITE_CFM, cfm,
		    sizeof(cfm));
	}
	firmware_free(data, size);
	if (error == 0)
		aprint_verbose_dev(sc->sc_dev, "%s: %zu bytes\n", name, size);

	return error;
}

/* The addresses of the Bluetooth patches, then the patches themselves. */
static int
aicwf_bt_patches(struct aicwf_softc *sc, const uint8_t *pair, u_int npairs)
{
	uint32_t adid = AICWF_BT_ADID_ADDR, patch = AICWF_BT_PATCH_ADDR;
	char name[sizeof(AICWF_BT_EXT_NAME) + 10];
	u_int ext = 0, i;
	int error;

	if (npairs == 0)
		return EINVAL;
	adid = le32dec(pair + AICWF_BT_INF_ADID * 8 + 4);
	if (npairs > AICWF_BT_INF_PATCH)
		patch = le32dec(pair + AICWF_BT_INF_PATCH * 8 + 4);
	if (npairs > AICWF_BT_INF_EXT_COUNT)
		ext = le32dec(pair + AICWF_BT_INF_EXT_COUNT * 8 + 4);
	if (ext > npairs - MIN(npairs, AICWF_BT_INF_EXT))
		return EINVAL;

	if ((error = aicwf_upload(sc, AICWF_BT_ADID_NAME, adid)) != 0 ||
	    (error = aicwf_upload(sc, AICWF_BT_PATCH_NAME, patch)) != 0)
		return error;
	for (i = 0; i < ext; i++) {
		const uint8_t *p = pair + (AICWF_BT_INF_EXT + i) * 8;

		snprintf(name, sizeof(name), AICWF_BT_EXT_NAME, le32dec(p));
		if ((error = aicwf_upload(sc, name, le32dec(p + 4))) != 0)
			return error;
	}

	return 0;
}

/*
 * Prepare the Bluetooth controller: upload its patches and write the
 * patch table, with the mode entry describing how the host reaches it.
 */
static int
aicwf_bt_load(struct aicwf_softc *sc)
{
	static const uint32_t mode[] = {
		1,			/* no hardware information */
		0xffffffff,
		0,
		AICWF_BT_MODE_NO_SWITCH,
		AICWF_BT_PORT_UART,
		AICWF_BT_BAUD,
		1,			/* RTS/CTS */
		0,			/* no low power mode */
		AICWF_BT_TXPWR,
	};
	uint8_t *tbl;
	size_t size, off;
	uint32_t type, value;
	u_int npairs, pass, i;
	bool found = false;
	int error;

	if ((error = aicwf_fw_read(sc, AICWF_BT_TABLE_NAME, &tbl, &size)) != 0)
		return error;
	if (size < AICWF_BT_TABLE_TAG_LEN ||
	    memcmp(tbl, AICWF_BT_TABLE_TAG, sizeof(AICWF_BT_TABLE_TAG)) != 0) {
		error = EINVAL;
		goto out;
	}

	/* The first pass uploads the patches, the second writes the pairs. */
	for (pass = 0; pass < 2; pass++) {
		off = AICWF_BT_TABLE_TAG_LEN;
		while (size - off >= AICWF_BT_ENTRY_HDR) {
			const uint8_t *pair;

			type = le32dec(tbl + off + AICWF_BT_ENTRY_TYPE_OFF);
			npairs = le32dec(tbl + off + AICWF_BT_ENTRY_LEN_OFF);
			off += AICWF_BT_ENTRY_HDR;
			if (type >= AICWF_BT_PT_NODATA)
				continue;
			if (npairs > (size - off) / 8) {
				error = EINVAL;
				goto out;
			}
			pair = tbl + off;
			off += npairs * 8;

			if (pass == 0) {
				if (type != AICWF_BT_PT_INF)
					continue;
				error = aicwf_bt_patches(sc, pair, npairs);
				if (error != 0)
					goto out;
				found = true;
				continue;
			}
			if (type == AICWF_BT_PT_VER)
				continue;
			if (type == AICWF_BT_PT_INF)
				npairs = MIN(npairs, AICWF_BT_INF_WRITES);
			for (i = 0; i < npairs; i++) {
				value = le32dec(pair + i * 8 + 4);
				if (type == AICWF_BT_PT_BTMODE &&
				    i < __arraycount(mode))
					value = mode[i];
				error = aicwf_mem_write(sc,
				    le32dec(pair + i * 8), value);
				if (error != 0)
					goto out;
			}
			if (type == AICWF_BT_PT_PWRON)
				kpause("aicwfbt", false, hz / 10, NULL);
		}
		if (!found) {
			error = ENOENT;
			goto out;
		}
	}
out:
	firmware_free(tbl, size);

	return error;
}

/* Hand the firmware its options through the patch table. */
static int
aicwf_fw_options_set(struct aicwf_softc *sc)
{
	uint32_t config_base, desc, fw_version, pairs;
	u_int n;
	int error;

	if ((error = aicwf_mem_read(sc, AICWF_FW_CONFIG_BASE,
	    &config_base)) != 0 ||
	    (error = aicwf_mem_read(sc, AICWF_FW_PATCH_DESC, &desc)) != 0 ||
	    (error = aicwf_mem_read(sc, AICWF_FW_VERSION, &fw_version)) != 0)
		return error;
	pairs = AICWF_FW_PATCH_BUF_OLD;
	if (fw_version > AICWF_FW_VERSION_PATCH_BUF &&
	    (error = aicwf_mem_read(sc, AICWF_FW_PATCH_BUF, &pairs)) != 0)
		return error;
	aprint_verbose_dev(sc->sc_dev, "firmware version 0x%08x\n",
	    fw_version);

	if ((error = aicwf_mem_write(sc, desc + AICWF_PATCH_MAGIC_OFF,
	    AICWF_PATCH_MAGIC)) != 0 ||
	    (error = aicwf_mem_write(sc, desc + AICWF_PATCH_MAGIC_2_OFF,
	    AICWF_PATCH_MAGIC_2)) != 0 ||
	    (error = aicwf_mem_write(sc, desc + AICWF_PATCH_PAIRS_OFF,
	    pairs)) != 0 ||
	    (error = aicwf_mem_write(sc, desc + AICWF_PATCH_COUNT_OFF,
	    __arraycount(aicwf_fw_options))) != 0)
		return error;
	for (n = 0; n < __arraycount(aicwf_fw_options); n++) {
		if ((error = aicwf_mem_write(sc, pairs + 8 * n,
		    config_base + aicwf_fw_options[n][0])) != 0 ||
		    (error = aicwf_mem_write(sc, pairs + 8 * n + 4,
		    aicwf_fw_options[n][1])) != 0)
			return error;
	}
	for (n = 0; n < 4; n++) {
		if ((error = aicwf_mem_write(sc,
		    desc + AICWF_PATCH_BLOCK_SIZE_OFF + 4 * n, 0)) != 0)
			return error;
	}

	return 0;
}

static int
aicwf_fw_start(struct aicwf_softc *sc)
{
	uint8_t req[8], cfm[4];
	int error;

	le32enc(req, AICWF_FW_ADDR);
	le32enc(req + 4, AICWF_START_APP_AUTO);
	error = aicwf_cmd(sc, AICWF_DBG_START_APP_REQ, req, sizeof(req),
	    AICWF_DBG_START_APP_CFM, cfm, sizeof(cfm));
	if (error == 0)
		aprint_verbose_dev(sc->sc_dev,
		    "firmware started, status 0x%x\n", le32dec(cfm));

	return error;
}

static void
aicwf_chan_set(uint8_t *chan, u_int band, u_int freq)
{
	le16enc(chan, freq);
	chan[2] = band;
	chan[3] = 0;			/* flags */
	chan[4] = AICWF_TX_POWER_DBM;
}

/* Fill a channel list with everything the radio covers. */
static u_int
aicwf_chan_list(struct aicwf_softc *sc, uint8_t *chan2g, uint8_t *chan5g,
    u_int *n5g)
{
	static const uint8_t chan5[] = {
		36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120,
		124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165
	};
	u_int n;

	for (n = 0; n < 13; n++)
		aicwf_chan_set(chan2g + n * AICWF_CHAN_LEN, AICWF_BAND_2G,
		    2412 + 5 * n);
	*n5g = 0;
	if (sc->sc_5ghz) {
		for (*n5g = 0; *n5g < __arraycount(chan5); (*n5g)++)
			aicwf_chan_set(chan5g + *n5g * AICWF_CHAN_LEN,
			    AICWF_BAND_5G, 5000 + 5 * chan5[*n5g]);
	}

	return n;
}

/* Take the firmware from its start to a station interface. */
static int
aicwf_fw_init(struct aicwf_softc *sc)
{
	uint8_t req[256], cfm[64];
	u_int n2g, n5g;
	int error;

	/* Start the stack: on, no efuse override, vendor option bit 5. */
	memset(req, 0, sizeof(req));
	req[0] = 1;
	req[2] = __BIT(5);
	error = aicwf_cmd(sc, AICWF_MM_SET_STACK_START_REQ, req, 4,
	    AICWF_MM_SET_STACK_START_CFM, cfm, 2);
	if (error != 0)
		return error;
	sc->sc_5ghz = cfm[0] != 0;

	error = aicwf_cmd(sc, AICWF_MM_GET_FW_VERSION_REQ, NULL, 0,
	    AICWF_MM_GET_FW_VERSION_CFM, cfm, 64);
	if (error != 0)
		return error;
	cfm[MIN(cfm[0] + 1, 63)] = '\0';
	aprint_normal_dev(sc->sc_dev, "firmware %s%s\n", cfm + 1,
	    sc->sc_5ghz ? ", 5 GHz" : "");

	memset(req, 0, sizeof(req));
	le32enc(req + 0, AICWF_CAL_CFG_2G);
	le32enc(req + 4, AICWF_CAL_CFG_5G);
	le32enc(req + 8, AICWF_CAL_PARAM_ALPHA);
	le32enc(req + 16, AICWF_CAL_BT_PARAM);
	error = aicwf_cmd(sc, AICWF_MM_SET_RF_CALIB_REQ, req, 24,
	    AICWF_MM_SET_RF_CALIB_CFM, cfm, 16);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev, "radio calibration failed\n");
		return error;
	}

	memset(req, 0, sizeof(req));
	le32enc(req, 1);
	error = aicwf_cmd(sc, AICWF_MM_GET_MAC_ADDR_REQ, req, 4,
	    AICWF_MM_GET_MAC_ADDR_CFM, cfm, ETHER_ADDR_LEN);
	if (error != 0)
		return error;
	memcpy(sc->sc_enaddr, cfm, ETHER_ADDR_LEN);
	aprint_normal_dev(sc->sc_dev, "Ethernet address %s\n",
	    ether_sprintf(sc->sc_enaddr));

	error = aicwf_cmd(sc, AICWF_MM_RESET_REQ, NULL, 0,
	    AICWF_MM_RESET_CFM, NULL, 0);
	if (error != 0)
		return error;

	error = aicwf_cmd(sc, AICWF_MM_VERSION_REQ, NULL, 0,
	    AICWF_MM_VERSION_CFM, cfm, 28);
	if (error != 0)
		return error;
	aprint_verbose_dev(sc->sc_dev, "MAC 0x%08x, hardware 0x%08x 0x%08x, "
	    "PHY 0x%08x 0x%08x, features 0x%08x, %u stations, %u interfaces\n",
	    le32dec(cfm), le32dec(cfm + 4), le32dec(cfm + 8),
	    le32dec(cfm + 12), le32dec(cfm + 16), le32dec(cfm + 20),
	    le16dec(cfm + 24), cfm[26]);

	sc->sc_fw_version = le32dec(cfm);
	if (le32dec(cfm) == 0x06090101 &&
	    (le32dec(cfm + 20) & AICWF_FEATURE_MFP) != 0) {
		sc->sc_sae_caps = IEEE80211_SAE_CAP_EXTERNAL |
		    IEEE80211_SAE_CAP_PMF | IEEE80211_SAE_CAP_SA_QUERY;
		aprint_normal_dev(sc->sc_dev, "external SAE and firmware PMF available\n");
	}

	/* Firmware builds association capabilities from this bounded request. */
	if (aicwf_me_config_build(req, le32dec(cfm), le32dec(cfm + 12),
	    le32dec(cfm + 20), sc->sc_5ghz))
		aprint_normal_dev(sc->sc_dev,
		    "VHT enabled: 1 stream, MCS 0-9, maximum width 80 MHz\n");
	error = aicwf_cmd(sc, AICWF_ME_CONFIG_REQ, req, AICWF_ME_CONFIG_LEN,
	    AICWF_ME_CONFIG_CFM, NULL, 0);
	if (error != 0)
		return error;

	memset(req, 0, sizeof(req));
	n2g = aicwf_chan_list(sc, req, req + AICWF_CHAN_2G_MAX *
	    AICWF_CHAN_LEN, &n5g);
	req[AICWF_CHAN_MAX * AICWF_CHAN_LEN] = n2g;
	req[AICWF_CHAN_MAX * AICWF_CHAN_LEN + 1] = n5g;
	error = aicwf_cmd(sc, AICWF_ME_CHAN_CONFIG_REQ, req,
	    AICWF_CHAN_MAX * AICWF_CHAN_LEN + 2,
	    AICWF_ME_CHAN_CONFIG_CFM, NULL, 0);
	if (error != 0)
		return error;

	memset(req, 0, sizeof(req));
	le32enc(req + 64, AICWF_UAPSD_TIMEOUT_MS);
	le16enc(req + 68, AICWF_LP_CLK_PPM);
	error = aicwf_cmd(sc, AICWF_MM_START_REQ, req, 72,
	    AICWF_MM_START_CFM, NULL, 0);
	if (error != 0)
		return error;

	memset(req, 0, sizeof(req));
	req[0] = AICWF_IF_STA;
	memcpy(req + 2, sc->sc_enaddr, ETHER_ADDR_LEN);
	error = aicwf_cmd(sc, AICWF_MM_ADD_IF_REQ, req, 10,
	    AICWF_MM_ADD_IF_CFM, cfm, 2);
	if (error != 0)
		return error;
	if (cfm[0] != 0) {
		aprint_error_dev(sc->sc_dev, "no interface: status %u\n",
		    cfm[0]);
		return EIO;
	}
	sc->sc_vif = cfm[1];

	return 0;
}

/* Scan every channel; the networks arrive as indications. */
static void
aicwf_scan(struct aicwf_softc *sc)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	const size_t chans = AICWF_CHAN_MAX * AICWF_CHAN_LEN;
	uint8_t req[376];
	u_int n2g, n5g, nssid;

	memset(req, 0, sizeof(req));
	n2g = aicwf_chan_list(sc, req, req + 13 * AICWF_CHAN_LEN, &n5g);
	/* The wildcard, and the wanted network in case it is hidden. */
	nssid = 1;
	if (ic->ic_des_esslen != 0 && ic->ic_des_esslen <= 32) {
		req[chans + 33] = ic->ic_des_esslen;
		memcpy(req + chans + 34, ic->ic_des_essid, ic->ic_des_esslen);
		nssid++;
	}
	memset(req + chans + 100, 0xff, ETHER_ADDR_LEN);  /* any BSSID */
	req[chans + 114] = sc->sc_vif;
	req[chans + 115] = n2g + n5g;
	req[chans + 116] = nssid;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_scanning || sc->sc_sta_scan_pending ||
	    sc->sc_sta_cancel_queued) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	sc->sc_scanning = true;
	sc->sc_scan_published = false;
	sc->sc_scan_ticks = getticks();
	mutex_exit(&sc->sc_lock);
	if (aicwf_cmd(sc, AICWF_SCANU_START_REQ, req, sizeof(req),
	    AICWF_SCANU_START_ACK, NULL, 0) != 0) {
		mutex_enter(&sc->sc_lock);
		if (!sc->sc_scan_published)
			sc->sc_scanning = false;
		device_printf(sc->sc_dev,
		    "scan request failed; FIFO attempted %u, pending %u\n",
		    sc->sc_scan_published, sc->sc_scanning);
		mutex_exit(&sc->sc_lock);
	}
}

/* One network found by a scan: a beacon or a probe response. */
static void
aicwf_scan_result(struct aicwf_softc *sc, const uint8_t *ind, size_t len)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	const size_t hdr = 12, fixed = 12;
	struct ieee80211_scanparams scan;
	struct ieee80211_frame wh;
	uint8_t tstamp[8];
	const uint8_t *frm, *efrm;
	u_int ds_chan = 0, ht_chan = 0, primary;
	int rssi, s;

	if (!sc->sc_if_attached || !sc->sc_scanning ||
	    !aicwf_sta_scan_result_valid(ind, len, sc->sc_vif))
		return;
	mutex_enter(&sc->sc_lock);
	const bool admitted = !sc->sc_sta_scan_started ||
	    (sc->sc_sta_scan_pending && !sc->sc_sta_scan_draining &&
	    sc->sc_sta_scan.generation == (uint32_t)sc->sc_sae_epoch);
	const uint64_t epoch = sc->sc_sae_epoch;
	const uint32_t serial = sc->sc_sta_scan_serial;
	mutex_exit(&sc->sc_lock);
	if (!admitted)
		return;
	const uint8_t * const frame = ind + hdr;
	const size_t frame_len = le16dec(ind);
	if (frame_len < sizeof(wh) + fixed)
		return;

	memcpy(&wh, frame, sizeof(wh));
	const int subtype = wh.i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
	if ((wh.i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
	    IEEE80211_FC0_TYPE_MGT ||
	    (subtype != IEEE80211_FC0_SUBTYPE_BEACON &&
	    subtype != IEEE80211_FC0_SUBTYPE_PROBE_RESP))
		return;

	memset(&scan, 0, sizeof(scan));
	frm = frame + sizeof(wh);
	efrm = frame + frame_len;
	memcpy(tstamp, frm, sizeof(tstamp));
	scan.sp_tstamp = tstamp;
	scan.sp_bintval = le16dec(frm + 8);
	scan.sp_capinfo = le16dec(frm + 10);
	const u_int chan = ieee80211_mhz2ieee(le16dec(ind + 4), 0);
	if (chan > IEEE80211_CHAN_MAX ||
	    ic->ic_channels[chan].ic_freq != le16dec(ind + 4) ||
	    !isset(ic->ic_chan_active, chan))
		return;
	scan.sp_chan = scan.sp_bchan = chan;

	const uint8_t *mdie = NULL;

	for (frm += fixed; efrm - frm >= 2 && efrm - frm >= 2 + frm[1];
	    frm += 2 + frm[1]) {
		uint8_t * const ie = __UNCONST(frm);

		switch (frm[0]) {
		case IEEE80211_ELEMID_SSID:
			if (frm[1] <= IEEE80211_NWID_LEN)
				scan.sp_ssid = ie;
			break;
		case IEEE80211_ELEMID_RATES:
			if (frm[1] <= IEEE80211_RATE_MAXSIZE)
				scan.sp_rates = ie;
			break;
		case IEEE80211_ELEMID_DSPARMS:
			if (frm[1] == 1)
				ds_chan = frm[2];
			break;
		case IEEE80211_ELEMID_HTINFO:
			if (frm[1] >= 22)
				ht_chan = frm[2];
			break;
		case 54:	/* Mobility Domain, 802.11r */
			if (frm[1] >= 3)
				mdie = frm;
			break;
		case IEEE80211_ELEMID_XRATES:
			scan.sp_xrates = ie;
			break;
		case IEEE80211_ELEMID_COUNTRY:
			scan.sp_country = ie;
			break;
		case IEEE80211_ELEMID_TIM:
			scan.sp_tim = ie;
			scan.sp_timoff = frm - (frame + sizeof(wh));
			break;
		case IEEE80211_ELEMID_ERP:
			if (frm[1] == 1)
				scan.sp_erp = frm[2];
			break;
		case IEEE80211_ELEMID_EXTCAP:
			scan.sp_extcap = ie;
			break;
		case IEEE80211_ELEMID_RRM:
			if (frm[1] == 5)
				scan.sp_rrm = ie;
			break;
		case IEEE80211_ELEMID_RSN:
			scan.sp_wpa = ie;
			break;
		case IEEE80211_ELEMID_RSNX:
			if (frm[1] >= 1 && frm[1] <= 16 &&
			    (frm[2] & 0x0f) + 1 == frm[1])
				scan.sp_rsnx = ie;
			break;
		}
	}
	if (frm != efrm || scan.sp_ssid == NULL || scan.sp_rates == NULL)
		return;

	/* A 2.4 GHz beacon can be received on an adjacent scan channel. */
	primary = le16dec(ind + 4) < 3000 && ds_chan != 0 ? ds_chan :
	    ht_chan != 0 ? ht_chan : chan;
	if (primary > IEEE80211_CHAN_MAX ||
	    ic->ic_channels[primary].ic_freq == 0 ||
	    !isset(ic->ic_chan_active, primary) ||
	    (ic->ic_channels[primary].ic_freq < 3000) !=
	    (ic->ic_channels[chan].ic_freq < 3000))
		return;
	if (primary != chan)
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_STATE,
		    "scan primary channel %u received on %u\n", primary, chan);
	scan.sp_chan = primary;

	/* The firmware reports dBm; net80211 compares a positive scale. */
	scan.sp_rssi_dbm = (int8_t)ind[9];
	scan.sp_rssi_dbm_valid = true;
	rssi = (int8_t)ind[9] + 100;
	rssi = MAX(1, MIN(rssi, 100));

	mutex_enter(&sc->sc_lock);
	s = splnet();
	if (sc->sc_scanning && epoch == sc->sc_sae_epoch &&
	    serial == sc->sc_sta_scan_serial &&
	    (!sc->sc_sta_scan_started || (sc->sc_sta_scan_pending &&
	    !sc->sc_sta_scan_draining)) && (ic->ic_flags & IEEE80211_F_SCAN) != 0) {
		if (mdie != NULL) {
			struct aicwf_ft_bss *ft = NULL;
			for (u_int k = 0; k < __arraycount(sc->sc_ft_bss); k++) {
				if (IEEE80211_ADDR_EQ(sc->sc_ft_bss[k].bssid, wh.i_addr3)) {
					ft = &sc->sc_ft_bss[k];
					break;
				}
				if (ft == NULL && sc->sc_ft_bss[k].mdid == 0)
					ft = &sc->sc_ft_bss[k];
			}
			if (ft != NULL) {
				memcpy(ft->bssid, wh.i_addr3, ETHER_ADDR_LEN);
				ft->mdid = le16dec(mdie + 2);
				ft->r0kh_len = MIN(mdie[1] - 3, sizeof(ft->r0kh));
				memcpy(ft->r0kh, mdie + 5, ft->r0kh_len);
			}
		}
		ieee80211_add_scan(ic, &scan, &wh, subtype, rssi, 0);
	}
	splx(s);
	mutex_exit(&sc->sc_lock);
}

static void
aicwf_scan_done(struct aicwf_softc *sc, const uint8_t *p, size_t len)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	uint8_t event[AICWF_STA_NOTICE_LEN];
	bool background, notify = false;
	int s;

	if (!sc->sc_if_attached || !aicwf_sta_scan_complete_valid(p, len, sc->sc_vif))
		return;
	mutex_enter(&sc->sc_lock);
	if (!sc->sc_scanning) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	background = sc->sc_sta_scan_started;
	if (background) {
		device_printf(sc->sc_dev,
		    "station scan complete: generation %u cookie %u serial %u status %u results %u draining %u\n",
		    sc->sc_sta_scan.generation, sc->sc_sta_scan.cookie, sc->sc_sta_scan_serial,
		    p[1], p[2], sc->sc_sta_scan_draining);
		notify = aicwf_sta_scan_finish_locked(sc, p[1] != 0 ||
		    sc->sc_sta_scan.generation != (uint32_t)sc->sc_sae_epoch, event);
		sc->sc_sta_scan_started = false;
		sc->sc_scan_published = false;
		sc->sc_sta_scan_draining = false;
	}
	/* Clear the cache admission flag before releasing radio ownership. */
	s = splnet();
	if (background && ic->ic_state == IEEE80211_S_RUN)
		ieee80211_cancel_scan(ic);
	splx(s);
	sc->sc_scanning = false;
	mutex_exit(&sc->sc_lock);
	s = splnet();
	if (ic->ic_state == IEEE80211_S_SCAN) {
		if (background)
			(void)aicwf_newstate(ic, IEEE80211_S_SCAN, -1);
		else
			ieee80211_end_scan(ic);
	}
	splx(s);
	if (notify)
		rt_ieee80211msg(&sc->sc_if, RTM_IEEE80211_STA_SCAN, event, sizeof(event));
}

/* Serialize retirement with the final check and write of a bound command. */
static void
aicwf_sae_advance(struct aicwf_softc *sc)
{
	uint8_t tx[AICWF_STA_NOTICE_LEN], scan[AICWF_STA_NOTICE_LEN];
	bool tx_event, scan_event;

	mutex_enter(&sc->sc_lock);
	aicwf_sta_retire_locked(sc, tx, &tx_event, scan, &scan_event);
	sc->sc_ptk_valid = false;
	sc->sc_pmf_rx_pn = 0;
	explicit_memset(sc->sc_ptk, 0, sizeof(sc->sc_ptk));
	mutex_spin_enter(&sc->sc_epoch_lock);
	if (++sc->sc_sae_epoch == 0)
		sc->sc_sae_epoch++;
	mutex_spin_exit(&sc->sc_epoch_lock);
	mutex_exit(&sc->sc_lock);
	if (tx_event)
		rt_ieee80211msg(&sc->sc_if, RTM_IEEE80211_STA_TX, tx, sizeof(tx));
	if (scan_event)
		rt_ieee80211msg(&sc->sc_if, RTM_IEEE80211_STA_SCAN, scan, sizeof(scan));
}

/*
 * Firmware protocol references: Radxa's AIC8800 SDIO driver, revision
 * d13d07963cd15d731e2895e8288a04cca6152ac9, lmac_msg.h and rwnx_tx.c.
 * This is an independent BSD implementation of that wire interface.
 */
static void
aicwf_sae_event(struct aicwf_softc *sc, bool start, const uint8_t *data,
    size_t len)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	struct ieee80211req_sae *req;

	sc->sc_ev_sae_drop.ev_count++;
	if (!sc->sc_if_attached || !sc->sc_sae_enabled ||
	    !sc->sc_connecting || ic->ic_state != IEEE80211_S_AUTH ||
	    ic->ic_bss == NULL ||
	    !IEEE80211_ADDR_EQ(ic->ic_bss->ni_bssid, sc->sc_sae_bssid))
		return;
	if (start) {
		if (len >= 44)
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_STATE,
			    "external SAE request: len %zu vif %u AKM %02x%02x%02x%02x\n",
			    len, data[0], data[40], data[41], data[42], data[43]);
		if (!aicwf_sae_request_valid(data, len, sc->sc_vif,
		    sc->sc_sae_bssid, ic->ic_bss->ni_essid,
		    ic->ic_bss->ni_esslen))
			return;
		len = data[1];
		data += 2;
	} else {
		if (!sc->sc_sae_pending ||
		    !aicwf_sae_frame_valid(data, len, ic->ic_myaddr,
		    sc->sc_sae_bssid, sc->sc_sae_bssid))
			return;
	}
	/* Do not relabel a checked event after sleeping through a new join. */
	req = kmem_zalloc(sizeof(*req), KM_NOSLEEP);
	if (req == NULL)
		return;
	if (start) {
		sc->sc_sae_pending = true;
		sc->sc_ev_sae_start.ev_count++;
	} else {
		sc->sc_ev_sae_rx.ev_count++;
	}
	req->version = IEEE80211_SAE_VERSION;
	req->generation = sc->sc_sae_generation;
	req->op = start ? IEEE80211_SAE_START : IEEE80211_SAE_RX_FRAME;
	req->len = len;
	memcpy(req->bssid, sc->sc_sae_bssid, ETHER_ADDR_LEN);
	memcpy(req->data, data, len);
	rt_ieee80211msg(ic->ic_ifp, RTM_IEEE80211_SAE, req,
	    offsetof(struct ieee80211req_sae, data) + len);
	kmem_free(req, sizeof(*req));
	sc->sc_ev_sae_drop.ev_count--;
}

static int
aicwf_sae_tx(struct aicwf_softc *sc, const uint8_t *frame, size_t len,
    uint64_t epoch, bool query)
{
	uint8_t * const buf = sc->sc_txbuf;
	uint8_t * const desc = buf + AICWF_HDR_LEN;
	int error;

	mutex_enter(&sc->sc_lock);
	if (epoch != sc->sc_sae_epoch ||
	    (query && (!sc->sc_ptk_valid || !sc->sc_connected ||
	    !sc->sc_sae_authenticated || sc->sc_ic.ic_state != IEEE80211_S_RUN))) {
		mutex_exit(&sc->sc_lock);
		return ESTALE;
	}
	le16enc(buf, AICWF_TXDESC_LEN + len);
	buf[2] = AICWF_TYPE_DATA;
	buf[3] = aicwf_crc8(buf, 3);
	memset(desc, 0, AICWF_TXDESC_LEN);
	le16enc(desc, len);
	memcpy(desc + 8, frame + 4, ETHER_ADDR_LEN);
	memcpy(desc + 14, frame + 10, ETHER_ADDR_LEN);
	desc[22] = AICWF_HWQ_VO;
	desc[23] = AICWF_TID_NONE;
	desc[24] = sc->sc_vif;
	desc[25] = query ? sc->sc_ap : AICWF_STA_NONE;
	/* Bit 7 asks firmware to protect robust management with the PTK. */
	le16enc(desc + 26, AICWF_TX_MGMT | (query ? __BIT(7) : 0));
	memcpy(desc + AICWF_TXDESC_LEN, frame, len);
	error = aicwf_write(sc, AICWF_HDR_LEN + AICWF_TXDESC_LEN + len,
	    AICWF_TX_RESERVE);
	mutex_exit(&sc->sc_lock);
	if (error == 0)
		sc->sc_ev_sae_tx.ev_count++;
	return error;
}

/* A command can sleep while a disconnect retires the current join. */
static bool
aicwf_sae_current(struct aicwf_softc *sc, const struct ieee80211req_sae *req,
    uint64_t epoch)
{
	return sc->sc_sae_enabled && sc->sc_sae_epoch == epoch &&
	    req->generation == sc->sc_sae_generation &&
	    IEEE80211_ADDR_EQ(req->bssid, sc->sc_sae_bssid) &&
	    IEEE80211_ADDR_EQ(req->bssid, sc->sc_ic.ic_bss->ni_bssid);
}

static int
aicwf_sae_ioctl(struct aicwf_softc *sc, u_long cmd, struct ieee80211req *ireq)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	struct ieee80211req_sae *req;
	uint8_t wire[44], cfm[2] = { 0xff };
	uint64_t epoch;
	uint16_t index;
	int error;

	if (cmd == SIOCG80211) {
		if (ireq->i_len != 0)
			return EINVAL;
		ireq->i_val = sc->sc_sae_caps;
		return 0;
	}
	error = kauth_authorize_network(kauth_cred_get(),
	    KAUTH_NETWORK_INTERFACE, KAUTH_REQ_NETWORK_INTERFACE_SETPRIV,
	    ic->ic_ifp, (void *)cmd, NULL);
	if (error != 0)
		return error;
	if (ireq->i_len != sizeof(*req))
		return EINVAL;
	req = kmem_zalloc(sizeof(*req), KM_SLEEP);
	error = copyin(ireq->i_data, req, sizeof(*req));
	if (error != 0)
		goto out;
	error = EINVAL;
	if (req->version != IEEE80211_SAE_VERSION ||
	    req->reserved[0] != 0 || req->reserved[1] != 0 ||
	    req->len > sizeof(req->data))
		goto out;
	if (req->op == IEEE80211_SAE_CONFIGURE) {
		if (req->len != 1 || req->data[0] > 1 ||
		    (req->data[0] &&
		    !aicwf_sae_peer_valid(req->bssid, req->generation)))
			goto out;
		if (req->data[0] && (sc->sc_sae_caps == 0 ||
		    ic->ic_opmode != IEEE80211_M_STA)) {
			error = EOPNOTSUPP;
			goto out;
		}
		aicwf_sae_advance(sc);
		sc->sc_sae_enabled = req->data[0] != 0;
		sc->sc_sae_pending = false;
		sc->sc_sae_authenticated = false;
		sc->sc_sae_generation = req->generation;
		memcpy(sc->sc_sae_bssid, req->bssid, ETHER_ADDR_LEN);
		error = 0;
		goto out;
	}
	if (!sc->sc_sae_enabled || req->generation != sc->sc_sae_generation ||
	    !IEEE80211_ADDR_EQ(req->bssid, sc->sc_sae_bssid) ||
	    !IEEE80211_ADDR_EQ(req->bssid, ic->ic_bss->ni_bssid)) {
		error = ESTALE;
		goto out;
	}
	epoch = sc->sc_sae_epoch;
	memset(wire, 0, sizeof(wire));
	switch (req->op) {
	case IEEE80211_SAE_AUTH_STATUS:
		if (req->len != 2 || !sc->sc_sae_pending ||
		    !sc->sc_connecting || ic->ic_state != IEEE80211_S_AUTH)
			break;
		wire[0] = sc->sc_vif;
		le16enc(wire + 2, le16dec(req->data));
		/* A successful association can arrive before the confirmation. */
		sc->sc_sae_pending = false;
		sc->sc_sae_authenticated = le16dec(req->data) == 0;
		error = aicwf_cmd_reply(sc, AICWF_SM_EXTERNAL_AUTH_RSP, wire, 4,
		    AICWF_SM_EXTERNAL_AUTH_CFM, NULL, 0, true, epoch);
		if (!aicwf_sae_current(sc, req, epoch))
			error = ESTALE;
		else if (error != 0)
			sc->sc_sae_authenticated = false;
		break;
	case IEEE80211_SAE_TX_FRAME:
		if (!sc->sc_sae_pending || !sc->sc_connecting ||
		    ic->ic_state != IEEE80211_S_AUTH ||
		    !aicwf_sae_frame_valid(req->data, req->len, req->bssid,
		    ic->ic_myaddr, req->bssid))
			break;
		error = aicwf_sae_tx(sc, req->data, req->len, epoch, false);
		break;
	case IEEE80211_SAE_TX_SA_QUERY:
		if (!sc->sc_connected || !sc->sc_sae_authenticated ||
		    ic->ic_state != IEEE80211_S_RUN ||
		    !aicwf_pmf_query_valid(req->data, req->len, req->bssid,
		    ic->ic_myaddr, req->bssid))
			break;
		error = aicwf_sae_tx(sc, req->data, req->len, epoch, true);
		break;
	case IEEE80211_SAE_SET_IGTK:
	case IEEE80211_SAE_DELETE_IGTK:
		if (req->len != (req->op == IEEE80211_SAE_SET_IGTK ? 24 : 2))
			break;
		index = le16dec(req->data);
		if (index != 4 && index != 5)
			break;
		if (req->op == IEEE80211_SAE_DELETE_IGTK) {
			if (sc->sc_igtk[index - 4] == AICWF_HWKEY_NONE) {
				error = 0;
				break;
			}
			wire[0] = sc->sc_igtk[index - 4];
			error = aicwf_cmd_reply(sc, AICWF_MM_KEY_DEL_REQ, wire, 1,
			    AICWF_MM_KEY_DEL_CFM, NULL, 0, true, epoch);
			if (!aicwf_sae_current(sc, req, epoch) ||
			    sc->sc_igtk[index - 4] != wire[0])
				error = ESTALE;
			else if (error == 0)
				sc->sc_igtk[index - 4] = AICWF_HWKEY_NONE;
			break;
		}
		if (!sc->sc_connected || !sc->sc_sae_authenticated ||
		    ic->ic_state != IEEE80211_S_RUN)
			break;
		if (!aicwf_sae_igtk_valid(req->data, req->len)) {
			device_printf(sc->sc_dev, "nonzero initial IGTK IPN is unsupported\n");
			error = EOPNOTSUPP;
			break;
		}
		wire[0] = index;
		wire[1] = AICWF_STA_NONE;
		wire[4] = 16;
		memcpy(wire + 8, req->data + 8, 16);
		wire[40] = AICWF_CIPHER_BIP;
		wire[41] = sc->sc_vif;
		error = aicwf_cmd_reply(sc, AICWF_MM_KEY_ADD_REQ, wire, sizeof(wire),
		    AICWF_MM_KEY_ADD_CFM, cfm, sizeof(cfm), true, epoch);
		if (!aicwf_sae_current(sc, req, epoch) ||
		    !sc->sc_connected || !sc->sc_sae_authenticated ||
		    ic->ic_state != IEEE80211_S_RUN)
			error = ESTALE;
		if (error == 0 && cfm[0] != 0)
			error = EIO;
		if (error == 0)
			sc->sc_igtk[index - 4] = cfm[1];
		break;
	default:
		break;
	}
	explicit_memset(wire, 0, sizeof(wire));
out:
	explicit_memset(req, 0, sizeof(*req));
	kmem_free(req, sizeof(*req));
	return error;
}

/* Ask the firmware to join the network net80211 has chosen. */
static void
aicwf_connect(struct aicwf_softc *sc)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	struct ieee80211_node * const ni = ic->ic_bss;
	uint8_t req[320], cfm[1] = { 0xff };
	uint32_t flags = 0;
	int error;

	if (ni->ni_esslen == 0 || ni->ni_esslen > 32)
		return;

	memset(req, 0, sizeof(req));
	req[0] = ni->ni_esslen;
	memcpy(req + 1, ni->ni_essid, ni->ni_esslen);
	memcpy(req + 34, ni->ni_bssid, ETHER_ADDR_LEN);
	aicwf_chan_set(req + 40, IEEE80211_IS_CHAN_5GHZ(ni->ni_chan) ?
	    AICWF_BAND_5G : AICWF_BAND_2G, ni->ni_chan->ic_freq);
	if ((ic->ic_flags & IEEE80211_F_WPA) != 0) {
		/* The supplicant does the handshake and then opens the port. */
		flags |= AICWF_CONNECT_PORT_HOST | AICWF_CONNECT_WPA;
		be16enc(req + 52, ETHERTYPE_PAE);
		if (ic->ic_opt_ie != NULL && ic->ic_opt_ie_len <= 256) {
			le16enc(req + 54, ic->ic_opt_ie_len);
			memcpy(req + 64, ic->ic_opt_ie, ic->ic_opt_ie_len);
		}
	}
	if (sc->sc_sae_enabled) {
		if (!fullmac_sae_rsn_valid(ic->ic_opt_ie, ic->ic_opt_ie_len) ||
		    !IEEE80211_ADDR_EQ(sc->sc_sae_bssid, ni->ni_bssid)) {
			device_printf(sc->sc_dev, "SAE requires CCMP, required PMF and the selected peer\n");
			return;
		}
		flags |= AICWF_CONNECT_MFP;
	}
	le32enc(req + 48, flags);
	req[59] = sc->sc_sae_enabled ? AICWF_AUTH_SAE : IEEE80211_AUTH_ALG_OPEN;
	req[61] = sc->sc_vif;

	aicwf_sae_advance(sc);
	sc->sc_sae_pending = false;
	sc->sc_sae_authenticated = false;
	sc->sc_connecting = true;
	const uint64_t epoch = sc->sc_sae_epoch;
	error = aicwf_cmd_reply(sc, AICWF_SM_CONNECT_REQ, req, sizeof(req),
	    AICWF_SM_CONNECT_CFM, cfm, sizeof(cfm), true, epoch);
	if (error != 0 || cfm[0] != 0) {
		/* net80211 times the join out and scans again. */
		device_printf(sc->sc_dev, "connect request failed: %d, %u\n",
		    error, cfm[0]);
		sc->sc_connecting = false;
	}
}

/* Print only protocol metadata from the successful current-peer indication. */
static void
aicwf_assoc_report(struct aicwf_softc *sc, const uint8_t *ind, size_t len)
{
	struct aicwf_assoc_info info;

	if (sc->sc_fw_version != 0x06090101 || len < 13 ||
	    ind[9] != sc->sc_vif ||
	    !IEEE80211_ADDR_EQ(ind + 2, sc->sc_ic.ic_bss->ni_bssid))
		return;
	if (!aicwf_assoc_parse(ind, len, &info)) {
		device_printf(sc->sc_dev, "association metadata unavailable: len %zu\n",
		    len);
		return;
	}
	device_printf(sc->sc_dev,
	    "association VHT IEs: request %u cap 0x%08x RX/TX 0x%04x/0x%04x; "
	    "response %u cap 0x%08x RX/TX 0x%04x/0x%04x\n",
	    info.req.vht, info.req.cap, info.req.rx_map, info.req.tx_map,
	    info.rsp.vht, info.rsp.cap, info.rsp.rx_map, info.rsp.tx_map);
	if (info.rsp.operation)
		device_printf(sc->sc_dev,
		    "association VHT operation: width %u, center segments %u/%u\n",
		    info.rsp.op_width, info.rsp.seg0, info.rsp.seg1);
	if (info.channel)
		device_printf(sc->sc_dev,
		    "association firmware channel: band %u, primary %u MHz, "
		    "width code %u, centers %u/%u MHz\n",
		    info.band, info.primary, info.width, info.center1, info.center2);
	else
		device_printf(sc->sc_dev, "association firmware channel unavailable\n");
}

static void
aicwf_sta_assoc(struct aicwf_softc *sc, const uint8_t *ind, size_t len)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	struct aicwf_assoc_info info;
	size_t request, response;
	u_int channel;

	mutex_enter(&sc->sc_lock);
	sc->sc_sta_assoc_len = 0;
	if (len < 20 || ind[9] != sc->sc_vif || le16dec(ind) != 0 ||
	    ic->ic_bss == NULL || !IEEE80211_ADDR_EQ(ind + 2, ic->ic_bss->ni_bssid) ||
	    !aicwf_assoc_parse(ind, len, &info))
		goto out;
	request = le16dec(ind + 14);
	response = le16dec(ind + 16);
	if (request == 0 || response == 0)
		goto out;
	le16enc(sc->sc_sta_assoc, request);
	le16enc(sc->sc_sta_assoc + 2, response);
	memcpy(sc->sc_sta_assoc + 4, ind + 20, request + response);
	sc->sc_sta_assoc_len = 4 + request + response;
	if (info.channel && info.band <= 1 &&
	    (info.band == 0 ? info.primary < 3000 : info.primary >= 4000)) {
		channel = ieee80211_mhz2ieee(info.primary, 0);
		if (channel > 0 && channel <= IEEE80211_CHAN_MAX &&
		    ic->ic_channels[channel].ic_freq == info.primary &&
		    isset(ic->ic_chan_active, channel))
			ic->ic_bss->ni_chan = &ic->ic_channels[channel];
	}
	device_printf(sc->sc_dev, "station association IEs: request %zu response %zu generation %u\n",
	    request, response, (uint32_t)sc->sc_sae_epoch);
out:
	mutex_exit(&sc->sc_lock);
}

static void
aicwf_connect_ind(struct aicwf_softc *sc, const uint8_t *ind, size_t len)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	int s;

	if (!sc->sc_if_attached || len < 13 || ind[9] != sc->sc_vif)
		return;
	if ((!sc->sc_connecting || ic->ic_bss == NULL ||
	    !IEEE80211_ADDR_EQ(ind + 2, ic->ic_bss->ni_bssid)) && le16dec(ind) == 0) {
		if (sc->sc_connected) {
			device_printf(sc->sc_dev, "unsolicited firmware association: roamed %u; reconnecting through host\n", ind[8]);
			aicwf_alive_lost(sc);
		}
		return;
	}
	if (!sc->sc_connecting || ic->ic_bss == NULL ||
	    !IEEE80211_ADDR_EQ(ind + 2, ic->ic_bss->ni_bssid) ||
	    (sc->sc_sae_enabled && !IEEE80211_ADDR_EQ(ind + 2, sc->sc_sae_bssid)))
		return;
	sc->sc_connecting = false;

	const u_int status = le16dec(ind);
	s = splnet();
	if (status == 0 && sc->sc_sae_enabled && !sc->sc_sae_authenticated) {
		device_printf(sc->sc_dev, "association before SAE completion rejected\n");
		sc->sc_connecting = true;
		ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
		splx(s);
		return;
	}
	if (status == 0) {
		aicwf_assoc_report(sc, ind, len);
		aicwf_sta_assoc(sc, ind, len);
		sc->sc_ap = ind[10];
		sc->sc_qos = ind[12] != 0;
		sc->sc_connected = true;
		sc->sc_alive_rx = ticks;
		sc->sc_alive_misses = 0;
		for (u_int k = 0; k < __arraycount(sc->sc_ft_bss); k++) {
			if (sc->sc_ft_bss[k].mdid != 0 && IEEE80211_ADDR_EQ(
			    sc->sc_ft_bss[k].bssid, ic->ic_bss->ni_bssid)) {
				device_printf(sc->sc_dev,
				    "802.11r mobility domain 0x%04x, R0KH len %u\n",
				    sc->sc_ft_bss[k].mdid,
				    sc->sc_ft_bss[k].r0kh_len);
				break;
			}
		}
		/*
		 * net80211 tells the supplicant about the association
		 * only on the way from ASSOC to RUN.
		 */
		if (ic->ic_state == IEEE80211_S_AUTH)
			ieee80211_new_state(ic, IEEE80211_S_ASSOC, -1);
		if (ic->ic_state == IEEE80211_S_AUTH ||
		    ic->ic_state == IEEE80211_S_ASSOC)
			ieee80211_new_state(ic, IEEE80211_S_RUN,
			    IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	} else {
		device_printf(sc->sc_dev, "connection refused: status %u\n",
		    status);
		if (ic->ic_state == IEEE80211_S_AUTH ||
		    ic->ic_state == IEEE80211_S_ASSOC)
			ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	}
	splx(s);
}

/* Action codes and minimum bodies from IEEE 802.11 RRM and WNM. */
static bool
aicwf_sta_action_valid(const uint8_t *body, size_t len, bool to_ap)
{

	if (len < 3 || len > IEEE80211_STA_MAX_BODY)
		return false;
	if (body[0] == 10) {		/* WNM */
		if (to_ap)
			return (body[1] == 6 && len >= 4) || /* BTM query */
			    (body[1] == 8 && len >= 5 && /* BTM response */
			    (body[3] != 0 || len >= 11));
		return body[1] == 7 && len >= 7; /* BTM request */
	}
	if (body[0] == 5) {		/* RRM */
		if (to_ap)
			return body[1] == 4 || body[1] == 1; /* NR request, RM report */
		return body[1] == 5 || /* Neighbor Report response */
		    (body[1] == 0 && len >= 5); /* measurement request */
	}
	return false;
}

/* The checked SDIO firmware ABI is separate from the current security mode. */
static bool
aicwf_sta_supported(struct aicwf_softc *sc)
{
	return sc->sc_if_attached && sc->sc_fw_version == 0x06090101 &&
	    sc->sc_ic.ic_opmode == IEEE80211_M_STA;
}

static bool
aicwf_sta_security(struct aicwf_softc *sc)
{
	struct ieee80211com * const ic = &sc->sc_ic;

	if (sc->sc_sae_enabled)
		return ic->ic_opt_ie != NULL &&
		    (sc->sc_sae_caps & IEEE80211_SAE_CAP_PMF) != 0 &&
		    fullmac_sae_rsn_valid(ic->ic_opt_ie, ic->ic_opt_ie_len);
	return (ic->ic_flags & IEEE80211_F_WPA) == IEEE80211_F_WPA2 &&
	    aicwf_sta_psk_valid(ic->ic_opt_ie, ic->ic_opt_ie_len);
}

static bool
aicwf_sta_ready(struct aicwf_softc *sc)
{
	struct ieee80211com * const ic = &sc->sc_ic;

	KASSERT(mutex_owned(&sc->sc_lock));
	return aicwf_sta_supported(sc) && sc->sc_sta_enabled &&
	    sc->sc_connected && !sc->sc_connecting &&
	    ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL &&
	    ic->ic_bss->ni_chan != IEEE80211_CHAN_ANYC &&
	    (sc->sc_if.if_flags & IFF_RUNNING) != 0 &&
	    sc->sc_sta_assoc_len != 0 && aicwf_sta_security(sc) &&
	    sc->sc_ptk_valid && (!sc->sc_sae_enabled || sc->sc_sae_authenticated);
}

/* Capture only the fixed ABI header. Deliver it after dropping sc_lock. */
static bool
aicwf_sta_tx_finish_locked(struct aicwf_softc *sc, bool ack, void *event)
{
	KASSERT(mutex_owned(&sc->sc_lock));
	if (!sc->sc_sta_tx_pending)
		return false;
	sc->sc_sta_tx_pending = false;
	sc->sc_sta_tx_sent = false;
	sc->sc_sta_tx.flags = ack ? IEEE80211_STA_TX_ACK : 0;
	sc->sc_sta_tx.len = 0;
	memcpy(event, &sc->sc_sta_tx, AICWF_STA_NOTICE_LEN);
	return true;
}

static bool
aicwf_sta_scan_finish_locked(struct aicwf_softc *sc, bool aborted, void *event)
{
	KASSERT(mutex_owned(&sc->sc_lock));
	if (!sc->sc_sta_scan_pending)
		return false;
	sc->sc_sta_scan_pending = false;
	/* Keep a published scan owned until its real firmware completion. */
	if (sc->sc_sta_scan_started)
		sc->sc_sta_scan_draining = true;
	sc->sc_sta_scan.flags = aborted ? IEEE80211_STA_SCAN_ABORTED : 0;
	sc->sc_sta_scan.len = 0;
	memcpy(event, &sc->sc_sta_scan, AICWF_STA_NOTICE_LEN);
	return true;
}

/* Cancel is best effort. A missing reply never releases radio ownership. */
static void
aicwf_sta_scan_cancel_locked(struct aicwf_softc *sc)
{
	struct aicwf_task *t;

	KASSERT(mutex_owned(&sc->sc_lock));
	if (!sc->sc_sta_scan_started || sc->sc_sta_cancel_queued)
		return;
	t = pool_cache_get(sc->sc_taskpool, PR_NOWAIT);
	if (t == NULL) {
		device_printf(sc->sc_dev, "station scan cancellation allocation failed\n");
		return;
	}
	sc->sc_sta_cancel_queued = true;
	t->t_cmd = AICWF_TASK_STA_CANCEL_SCAN;
	t->t_sta.flags = sc->sc_sta_scan_serial;
	workqueue_enqueue(sc->sc_taskq, &t->t_work, NULL);
}

/* Called with sc_lock held before the association epoch changes. */
static void
aicwf_sta_retire_locked(struct aicwf_softc *sc, uint8_t *tx, bool *tx_event,
    uint8_t *scan, bool *scan_event)
{
	KASSERT(mutex_owned(&sc->sc_lock));
	*tx_event = aicwf_sta_tx_finish_locked(sc, false, tx);
	*scan_event = aicwf_sta_scan_finish_locked(sc, true, scan);
	if (sc->sc_sta_scan_draining)
		aicwf_sta_scan_cancel_locked(sc);
	sc->sc_sta_assoc_len = 0;
}

static void
aicwf_sta_tx_cfm(struct aicwf_softc *sc, const uint8_t *p, size_t len)
{
	uint8_t event[AICWF_STA_NOTICE_LEN];
	uint32_t status, token;
	bool notify = false;

	if (!aicwf_sta_cfm_decode(p, len, &status, &token))
		return;
	mutex_enter(&sc->sc_lock);
	if (sc->sc_sta_tx_pending && sc->sc_sta_tx_sent &&
	    token != sc->sc_sta_token && !sc->sc_sta_tx_mismatch_reported) {
		sc->sc_sta_tx_mismatch_reported = true;
		device_printf(sc->sc_dev,
		    "station TX confirmation mismatch: cookie %u expected %u "
		    "raw 0x%08x status 0x%08x\n", sc->sc_sta_tx.cookie,
		    sc->sc_sta_token, le32dec(p + 4), status);
	}
	if (sc->sc_sta_tx_pending && sc->sc_sta_tx_sent &&
	    token == sc->sc_sta_token &&
	    sc->sc_sta_tx.generation == (uint32_t)sc->sc_sae_epoch &&
	    aicwf_sta_ready(sc) &&
	    IEEE80211_ADDR_EQ(sc->sc_sta_tx.bssid, sc->sc_ic.ic_bss->ni_bssid)) {
		device_printf(sc->sc_dev,
		    "station TX confirmation: cookie %u token %u raw 0x%08x "
		    "status 0x%08x ACK %u\n", sc->sc_sta_tx.cookie, token,
		    le32dec(p + 4), status, (status & AICWF_STA_CFM_ACK) != 0);
		notify = aicwf_sta_tx_finish_locked(sc,
		    (status & AICWF_STA_CFM_ACK) != 0, event);
	}
	mutex_exit(&sc->sc_lock);
	if (notify)
		rt_ieee80211msg(&sc->sc_if, RTM_IEEE80211_STA_TX, event, sizeof(event));
}

/* Check each requested frequency before queuing a command that can sleep. */
static int
aicwf_sta_scan_request(struct aicwf_softc *sc,
    const struct ieee80211req_sta_mgmt *req, uint8_t *wire)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	uint16_t freqs[AICWF_STA_SCAN_CHANNELS], f;
	size_t count = 0, i, j;
	u_int channel;

	if ((req->len & 1) != 0 || req->len > IEEE80211_STA_MAX_CHANNELS * 2)
		return EINVAL;
	if (req->len != 0) {
		for (i = 0; i < req->len / 2; i++) {
			memcpy(&f, req->data + i * 2, sizeof(f));
			channel = ieee80211_mhz2ieee(f, 0);
			if (channel == 0 || channel > IEEE80211_CHAN_MAX ||
			    ic->ic_channels[channel].ic_freq != f ||
			    !isset(ic->ic_chan_active, channel))
				return EINVAL;
			for (j = 0; j < count; j++)
				if (freqs[j] == f)
					return EINVAL;
			freqs[count++] = f;
		}
	} else {
		for (channel = 1; channel <= IEEE80211_CHAN_MAX; channel++) {
			if (!isset(ic->ic_chan_active, channel) ||
			    ic->ic_channels[channel].ic_freq == 0)
				continue;
			if (count == __arraycount(freqs))
				return E2BIG;
			freqs[count++] = ic->ic_channels[channel].ic_freq;
		}
	}
	return aicwf_sta_scan_build(wire, freqs, count, sc->sc_vif,
	    AICWF_TX_POWER_DBM) ? 0 : EINVAL;
}

static int
aicwf_sta_mgmt(struct ieee80211com *ic, struct ieee80211req_sta_mgmt *req)
{
	struct aicwf_softc * const sc = ic->ic_ifp->if_softc;
	struct aicwf_task *t;
	uint8_t event[AICWF_STA_NOTICE_LEN], scan_event[AICWF_STA_NOTICE_LEN];
	uint8_t scan_wire[AICWF_STA_SCAN_LEN];
	bool tx_notify = false, scan_notify = false;
	int error = 0;

	if (req->version != IEEE80211_STA_VERSION || req->reserved != 0 ||
	    req->len > sizeof(req->data) ||
	    (req->op != IEEE80211_STA_GET && req->flags != 0))
		return EINVAL;
	mutex_enter(&sc->sc_lock);
	if (!aicwf_sta_supported(sc)) {
		error = EOPNOTSUPP;
		goto out;
	}
	if (req->op >= IEEE80211_STA_TX &&
	    (req->generation != (uint32_t)sc->sc_sae_epoch || ic->ic_bss == NULL ||
	    !IEEE80211_ADDR_EQ(req->bssid, ic->ic_bss->ni_bssid))) {
		error = ESTALE;
		goto out;
	}
	switch (req->op) {
	case IEEE80211_STA_GET:
		req->flags = IEEE80211_STA_CAP_BTM | IEEE80211_STA_CAP_RRM;
		req->generation = (uint32_t)sc->sc_sae_epoch;
		memset(req->bssid, 0, sizeof(req->bssid));
		IEEE80211_ADDR_COPY(req->data, ic->ic_myaddr);
		req->len = IEEE80211_ADDR_LEN;
		req->freq = 0;
		if (sc->sc_connected && ic->ic_state == IEEE80211_S_RUN &&
		    ic->ic_bss != NULL && ic->ic_bss->ni_chan != IEEE80211_CHAN_ANYC) {
			if (!aicwf_sta_security(sc))
				req->flags = 0;
			req->flags |= IEEE80211_STA_ASSOCIATED;
			req->freq = ic->ic_bss->ni_chan->ic_freq;
			IEEE80211_ADDR_COPY(req->bssid, ic->ic_bss->ni_bssid);
			if (sc->sc_sta_assoc_len != 0) {
				req->flags |= IEEE80211_STA_ASSOC_IES;
				memcpy(req->data + 6, sc->sc_sta_assoc, sc->sc_sta_assoc_len);
				req->len += sc->sc_sta_assoc_len;
			}
		}
		break;
	case IEEE80211_STA_ENABLE:
	case IEEE80211_STA_DISABLE:
		if (req->len != 0) {
			error = EINVAL;
			break;
		}
		sc->sc_sta_enabled = req->op == IEEE80211_STA_ENABLE;
		if (!sc->sc_sta_enabled) {
			tx_notify = aicwf_sta_tx_finish_locked(sc, false, event);
			scan_notify = aicwf_sta_scan_finish_locked(sc, true, scan_event);
			if (sc->sc_sta_scan_draining)
				aicwf_sta_scan_cancel_locked(sc);
		}
		break;
	case IEEE80211_STA_CANCEL_TX:
		if (req->len != 0 || req->cookie == 0) {
			error = EINVAL;
			break;
		}
		if (sc->sc_sta_tx_pending && req->cookie == sc->sc_sta_tx.cookie)
			tx_notify = aicwf_sta_tx_finish_locked(sc, false, event);
		else
			error = ESTALE;
		break;
	case IEEE80211_STA_CANCEL_SCAN:
		if (req->len != 0 || req->cookie == 0) {
			error = EINVAL;
			break;
		}
		if (!sc->sc_sta_scan_pending || req->cookie != sc->sc_sta_scan.cookie) {
			error = ESTALE;
			break;
		}
		scan_notify = aicwf_sta_scan_finish_locked(sc, true, scan_event);
		aicwf_sta_scan_cancel_locked(sc);
		break;
	case IEEE80211_STA_TX:
	case IEEE80211_STA_SCAN:
		if (!aicwf_sta_ready(sc) || req->freq != ic->ic_bss->ni_chan->ic_freq) {
			error = ENOTCONN;
			break;
		}
		if (req->cookie == 0 || (req->op == IEEE80211_STA_TX &&
		    !aicwf_sta_action_valid(req->data, req->len, true))) {
			error = EINVAL;
			break;
		}
		if (req->op == IEEE80211_STA_TX) {
			if (sc->sc_sta_tx_pending || sc->sc_scanning || sc->sc_sta_scan_pending) {
				error = EBUSY;
				break;
			}
			if (sc->sc_sta_token == AICWF_STA_TOKEN_MAX) {
				error = EOVERFLOW;
				break;
			}
		} else {
			if (sc->sc_scanning || sc->sc_sta_scan_pending || sc->sc_sta_cancel_queued ||
			    sc->sc_sta_tx_pending || (ic->ic_flags & IEEE80211_F_SCAN) != 0) {
				error = EBUSY;
				break;
			}
			if (sc->sc_sta_scan_serial == UINT32_MAX) {
				error = EOVERFLOW;
				break;
			}
			error = aicwf_sta_scan_request(sc, req, scan_wire);
			if (error != 0)
				break;
		}
		t = pool_cache_get(sc->sc_taskpool, PR_NOWAIT);
		if (t == NULL) {
			error = ENOBUFS;
			break;
		}
		t->t_cmd = AICWF_TASK_STA_MGMT;
		t->t_epoch = sc->sc_sae_epoch;
		t->t_sta = *req;
		if (req->op == IEEE80211_STA_TX) {
			sc->sc_sta_tx_pending = true;
			sc->sc_sta_tx_sent = false;
			sc->sc_sta_tx_mismatch_reported = false;
			sc->sc_sta_tx = *req;
			t->t_sta.flags = ++sc->sc_sta_token;
		} else {
			sc->sc_sta_scan_pending = true;
			sc->sc_sta_scan = *req;
			t->t_sta.flags = ++sc->sc_sta_scan_serial;
		}
		workqueue_enqueue(sc->sc_taskq, &t->t_work, NULL);
		break;
	default:
		error = EINVAL;
		break;
	}
out:
	mutex_exit(&sc->sc_lock);
	if (tx_notify)
		rt_ieee80211msg(&sc->sc_if, RTM_IEEE80211_STA_TX, event, sizeof(event));
	if (scan_notify)
		rt_ieee80211msg(&sc->sc_if, RTM_IEEE80211_STA_SCAN, scan_event, sizeof(scan_event));
	return error;
}

static void
aicwf_sta_mgmt_cb(struct aicwf_softc *sc, const struct aicwf_task *t)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	const struct ieee80211req_sta_mgmt * const req = &t->t_sta;
	uint8_t event[AICWF_STA_NOTICE_LEN], wire[AICWF_STA_SCAN_LEN];
	uint8_t *buf, *desc, *frame;
	bool notify = false;
	int error = 0, s;

	mutex_enter(&sc->sc_lock);
	if (req->op == IEEE80211_STA_TX) {
		if (!sc->sc_sta_tx_pending || req->flags != sc->sc_sta_token)
			goto out;
	} else if (!sc->sc_sta_scan_pending || req->flags != sc->sc_sta_scan_serial)
		goto out;
	if (!aicwf_sta_ready(sc) || t->t_epoch != sc->sc_sae_epoch ||
	    !IEEE80211_ADDR_EQ(req->bssid, ic->ic_bss->ni_bssid)) {
		error = ESTALE;
		goto failed;
	}
	if (req->op == IEEE80211_STA_TX) {
		buf = sc->sc_txbuf;
		desc = buf + AICWF_HDR_LEN;
		frame = desc + AICWF_TXDESC_LEN;
		le16enc(buf, AICWF_TXDESC_LEN + 24 + req->len);
		buf[2] = AICWF_TYPE_DATA;
		buf[3] = aicwf_crc8(buf, 3);
		memset(desc, 0, AICWF_TXDESC_LEN + 24);
		le16enc(desc, 24 + req->len);
		le32enc(desc + 4, AICWF_STA_CFM_REQUEST | req->flags);
		memcpy(desc + 8, req->bssid, ETHER_ADDR_LEN);
		memcpy(desc + 14, ic->ic_myaddr, ETHER_ADDR_LEN);
		desc[22] = AICWF_HWQ_VO;
		desc[23] = AICWF_TID_NONE;
		desc[24] = sc->sc_vif;
		desc[25] = sc->sc_ap;
		le16enc(desc + 26, AICWF_TX_MGMT | (sc->sc_sae_enabled ? __BIT(7) : 0));
		frame[0] = 0xd0;
		memcpy(frame + 4, req->bssid, ETHER_ADDR_LEN);
		memcpy(frame + 10, ic->ic_myaddr, ETHER_ADDR_LEN);
		memcpy(frame + 16, req->bssid, ETHER_ADDR_LEN);
		memcpy(frame + 24, req->data, req->len);
		sc->sc_sta_tx_sent = true;
		error = aicwf_write(sc, AICWF_HDR_LEN + AICWF_TXDESC_LEN + 24 + req->len,
		    AICWF_TX_RESERVE);
		device_printf(sc->sc_dev,
		    "station TX: generation %u cookie %u token %u category %u action %u error %d\n",
		    req->generation, req->cookie, req->flags, req->data[0], req->data[1], error);
		if (error != 0)
			goto failed;
		goto out;
	}
	error = aicwf_sta_scan_request(sc, req, wire);
	if (error != 0)
		goto failed;
	sc->sc_scanning = true;
	sc->sc_sta_scan_started = true;
	sc->sc_scan_published = false;
	sc->sc_sta_scan_draining = false;
	sc->sc_scan_ticks = getticks();
	s = splnet();
	ic->ic_scan.nt_scan_epoch++;
	ic->ic_flags |= IEEE80211_F_SCAN;
	splx(s);
	mutex_exit(&sc->sc_lock);
	error = aicwf_cmd_reply(sc, AICWF_SCANU_START_REQ, wire, sizeof(wire),
	    AICWF_SCANU_START_ACK, NULL, 0, true, t->t_epoch);
	mutex_enter(&sc->sc_lock);
	device_printf(sc->sc_dev,
	    "station scan start: generation %u cookie %u serial %u channels %u error %d\n",
	    req->generation, req->cookie, req->flags, wire[AICWF_STA_SCAN_COUNT], error);
	if (req->flags != sc->sc_sta_scan_serial || !sc->sc_sta_scan_started)
		goto out;
	if (error != 0) {
		if (!sc->sc_scan_published) {
			sc->sc_sta_scan_started = false;
			sc->sc_scanning = false;
			sc->sc_sta_scan_draining = false;
		}
		goto failed;
	}
	goto out;
failed:
	device_printf(sc->sc_dev,
	    "station request aborted: generation %u cookie %u op %u error %d\n",
	    req->generation, req->cookie, req->op, error);
	if (req->op == IEEE80211_STA_TX)
		notify = aicwf_sta_tx_finish_locked(sc, false, event);
	else {
		notify = aicwf_sta_scan_finish_locked(sc, true, event);
		if (sc->sc_sta_scan_draining)
			aicwf_sta_scan_cancel_locked(sc);
		if (!sc->sc_scanning) {
			s = splnet();
			ieee80211_cancel_scan(ic);
			splx(s);
		}
	}
out:
	mutex_exit(&sc->sc_lock);
	if (notify)
		rt_ieee80211msg(&sc->sc_if, req->op == IEEE80211_STA_TX ?
		    RTM_IEEE80211_STA_TX : RTM_IEEE80211_STA_SCAN, event, sizeof(event));
}

static void
aicwf_sta_cancel_scan_cb(struct aicwf_softc *sc, uint32_t serial)
{
	uint8_t status = 0xff;
	int error;

	mutex_enter(&sc->sc_lock);
	if (serial != sc->sc_sta_scan_serial || !sc->sc_sta_scan_started ||
	    !sc->sc_sta_scan_draining) {
		sc->sc_sta_cancel_queued = false;
		mutex_exit(&sc->sc_lock);
		return;
	}
	mutex_exit(&sc->sc_lock);
	error = aicwf_cmd_reply(sc, AICWF_SCANU_CANCEL_REQ, NULL, 0,
	    AICWF_SCANU_CANCEL_CFM, &status, sizeof(status), true, 0);
	mutex_enter(&sc->sc_lock);
	sc->sc_sta_cancel_queued = false;
	mutex_exit(&sc->sc_lock);
	device_printf(sc->sc_dev, "station scan cancel: serial %u error %d status %u; completion required\n",
	    serial, error, status);
}

/* Validate one unicast management body while the association lock is held. */
static const uint8_t *
aicwf_mgmt_body(struct aicwf_softc *sc, const uint8_t *frame, size_t len,
    uint32_t status, uint32_t flags, size_t *body_len, uint64_t *pn)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	const uint8_t *peer = ic->ic_bss->ni_bssid;
	const bool pmf = sc->sc_sae_enabled;
	size_t hdr = 24;

	KASSERT(mutex_owned(&sc->sc_lock));
	if (!sc->sc_if_attached || !sc->sc_connected ||
	    ic->ic_state != IEEE80211_S_RUN ||
	    ((flags >> 8) & 0xff) != sc->sc_vif ||
	    !aicwf_pmf_header_valid(frame, len, ic->ic_myaddr, peer, peer,
	    pmf ? 0x40 : 0) || (status & 0x7e0) != 0 ||
	    (status & 0x6000) != 0x6000)
		return NULL;
	*pn = sc->sc_pmf_rx_pn;
	if (pmf) {
		if (!sc->sc_ptk_valid || !sc->sc_sae_authenticated ||
		    !IEEE80211_ADDR_EQ(peer, sc->sc_sae_bssid) || len < 32 ||
		    (status & 0x02000000) == 0 ||
		    ((status >> 2) & 7) != 3 ||
		    sc->sc_hwkey[AICWF_KEY_SLOT_PAIRWISE] == AICWF_HWKEY_NONE ||
		    ((status >> 15) & 0x3ff) !=
		    sc->sc_hwkey[AICWF_KEY_SLOT_PAIRWISE] ||
		    frame[26] != 0 || frame[27] != 0x20)
			return NULL;
		*pn = (uint64_t)frame[24] | (uint64_t)frame[25] << 8 |
		    (uint64_t)frame[28] << 16 | (uint64_t)frame[29] << 24 |
		    (uint64_t)frame[30] << 32 | (uint64_t)frame[31] << 40;
		if (*pn <= sc->sc_pmf_rx_pn)
			return NULL;
		/* The firmware strips the MIC but retains the CCMP header. */
		hdr += 8;
	} else if (((status >> 2) & 7) != 0) {
		return NULL;
	}
	*body_len = len - hdr;
	return frame + hdr;
}

static void
aicwf_sta_rx_event(struct aicwf_softc *sc, const uint8_t *frame, size_t len,
    uint32_t status, uint32_t flags)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	struct ieee80211req_sta_mgmt *ev;
	const uint64_t epoch = sc->sc_sae_epoch;
	const uint8_t *body;
	size_t body_len;
	uint64_t pn;

	if (len < 27 || len > 32 + IEEE80211_STA_MAX_BODY || frame[0] != 0xd0)
		return;
	ev = kmem_zalloc(sizeof(*ev), KM_NOSLEEP);
	if (ev == NULL)
		return;
	mutex_enter(&sc->sc_lock);
	if (!aicwf_sta_ready(sc) || epoch != sc->sc_sae_epoch)
		goto drop;
	body = aicwf_mgmt_body(sc, frame, len, status, flags, &body_len, &pn);
	if (body == NULL || !aicwf_sta_action_valid(body, body_len, false))
		goto drop;
	sc->sc_pmf_rx_pn = pn;
	device_printf(sc->sc_dev, "station RX: generation %u category %u action %u length %zu PMF %u\n",
	    (uint32_t)epoch, body[0], body[1], body_len, sc->sc_sae_enabled);
	ev->version = IEEE80211_STA_VERSION;
	ev->op = IEEE80211_STA_GET;
	ev->generation = (uint32_t)epoch;
	ev->freq = ic->ic_bss->ni_chan->ic_freq;
	ev->len = body_len;
	IEEE80211_ADDR_COPY(ev->bssid, ic->ic_bss->ni_bssid);
	memcpy(ev->data, body, body_len);
	mutex_exit(&sc->sc_lock);
	rt_ieee80211msg(&sc->sc_if, RTM_IEEE80211_STA_RX, ev,
	    offsetof(struct ieee80211req_sta_mgmt, data) + body_len);
	kmem_free(ev, sizeof(*ev));
	return;
drop:
	mutex_exit(&sc->sc_lock);
	kmem_free(ev, sizeof(*ev));
}

/* Pure liveness decision: the link counts as dead only while the driver
 * believes it is associated and no frame (beacons included) arrived for
 * AICWF_ALIVE_SILENT_MS. */
static bool
aicwf_alive_silent(struct aicwf_softc *sc, u_int now)
{
	const int delta = (int)(now - sc->sc_alive_rx);

	if (!sc->sc_if_attached || !sc->sc_connected)
		return false;
	return delta > (int)mstohz(AICWF_ALIVE_SILENT_MS);
}

/* The teardown sleeps in the firmware roundtrip: defer it to the taskq. */
static void
aicwf_alive_lost(struct aicwf_softc *sc)
{

	mutex_enter(&sc->sc_lock);
	if (!sc->sc_alive_queued) {
		sc->sc_alive_queued = true;
		sc->sc_alive_epoch = sc->sc_sae_epoch;
		workqueue_enqueue(sc->sc_taskq, &sc->sc_alive_work, NULL);
	}
	mutex_exit(&sc->sc_lock);
}

static void
aicwf_alive_work(struct aicwf_softc *sc)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	int s;

	mutex_enter(&sc->sc_lock);
	sc->sc_alive_queued = false;
	const bool connected = sc->sc_connected && sc->sc_if_attached &&
	    sc->sc_alive_epoch == sc->sc_sae_epoch;
	mutex_exit(&sc->sc_lock);
	if (!connected)
		return;

	device_printf(sc->sc_dev, "no frames from the AP; reconnecting\n");
	aicwf_disconnect(sc);
	mutex_enter(&sc->sc_lock);
	sc->sc_connected = false;
	sc->sc_alive_misses = 0;
	mutex_exit(&sc->sc_lock);

	s = splnet();
	if (ic->ic_state == IEEE80211_S_RUN)
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	splx(s);
}

static void
aicwf_alive_tick(void *arg)
{
	struct aicwf_softc * const sc = arg;

	mutex_enter(&sc->sc_lock);
	if (aicwf_alive_silent(sc, ticks)) {
		if (++sc->sc_alive_misses >= 2) {
			sc->sc_alive_misses = 0;
			if (!sc->sc_alive_queued) {
				sc->sc_alive_queued = true;
				sc->sc_alive_epoch = sc->sc_sae_epoch;
				workqueue_enqueue(sc->sc_taskq,
				    &sc->sc_alive_work, NULL);
			}
		}
	} else
		sc->sc_alive_misses = 0;
	mutex_exit(&sc->sc_lock);

	callout_schedule(&sc->sc_alive_ch, mstohz(AICWF_ALIVE_PERIOD_MS));
}

static void
aicwf_disconnect_ind(struct aicwf_softc *sc, const uint8_t *ind, size_t len)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	int s;

	/* reason (u16), VIF, FT-over-DS, reassociation, alignment byte. */
	if (len < 6 || ind[2] != sc->sc_vif ||
	    !sc->sc_if_attached || !sc->sc_connected)
		return;
	aicwf_sae_advance(sc);
	sc->sc_connected = false;
	sc->sc_sae_pending = false;
	sc->sc_sae_authenticated = false;
	memset(sc->sc_igtk, AICWF_HWKEY_NONE, sizeof(sc->sc_igtk));
	aicwf_reorder_flush(sc, -1, false);

	s = splnet();
	if (ic->ic_state == IEEE80211_S_RUN)
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	splx(s);
}

/* Delete management keys before the firmware can reuse their hardware slots. */
static void
aicwf_sae_clear_keys(struct aicwf_softc *sc)
{
	const uint64_t epoch = sc->sc_sae_epoch;
	uint8_t key;
	unsigned i;

	for (i = 0; i < __arraycount(sc->sc_igtk); i++) {
		if (sc->sc_igtk[i] == AICWF_HWKEY_NONE)
			continue;
		key = sc->sc_igtk[i];
		(void)aicwf_cmd_reply(sc, AICWF_MM_KEY_DEL_REQ,
		    &key, 1, AICWF_MM_KEY_DEL_CFM, NULL, 0, true, epoch);
		if (epoch != sc->sc_sae_epoch)
			return;
		if (sc->sc_igtk[i] == key)
			sc->sc_igtk[i] = AICWF_HWKEY_NONE;
	}
}

static void
aicwf_disconnect(struct aicwf_softc *sc)
{
	uint8_t req[4];

	aicwf_sae_advance(sc);

	sc->sc_sae_pending = false;
	sc->sc_sae_authenticated = false;
	aicwf_sae_clear_keys(sc);
	/* CONNECT shares this workqueue; a new CONFIGURE cannot cancel leave. */
	memset(sc->sc_igtk, AICWF_HWKEY_NONE, sizeof(sc->sc_igtk));
	if (!sc->sc_connected && !sc->sc_connecting)
		return;
	sc->sc_connected = false;
	sc->sc_connecting = false;
	aicwf_reorder_flush(sc, -1, false);

	memset(req, 0, sizeof(req));
	le16enc(req, IEEE80211_REASON_AUTH_LEAVE);
	req[2] = sc->sc_vif;
	(void)aicwf_cmd(sc, AICWF_SM_DISCONNECT_REQ, req, sizeof(req),
	    AICWF_SM_DISCONNECT_CFM, NULL, 0);
}

static void
aicwf_newstate_cb(struct aicwf_softc *sc, enum ieee80211_state nstate,
    int arg)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	const enum ieee80211_state ostate = ic->ic_state;
	int s;

	switch (nstate) {
	case IEEE80211_S_INIT:
		aicwf_disconnect(sc);
		break;

	case IEEE80211_S_SCAN:
		/*
		 * A user scan forces ic_state to INIT in setupscan(),
		 * bypassing RUN's normal station departure.  The radio
		 * still has that association: notify the supplicant and
		 * clear the old keys before disconnecting it.  Without
		 * this event the client can remain COMPLETED after the
		 * scan, with no firmware connection to carry traffic.
		 */
		if (ostate == IEEE80211_S_INIT && sc->sc_connected) {
			s = splnet();
			ieee80211_sta_leave(ic, ic->ic_bss);
			splx(s);
		}
		aicwf_disconnect(sc);
		if (ostate != IEEE80211_S_SCAN) {
			if (!sc->sc_scanning)
				aicwf_scan(sc);
			break;
		}
		/*
		 * net80211 found nothing and goes round again: one scan
		 * covers every channel, so start the next after a pause.
		 */
		if (sc->sc_scanning ||
		    (ic->ic_flags & IEEE80211_F_SCAN) == 0)
			break;
		const int wait = sc->sc_scan_ticks +
		    mstohz(AICWF_SCAN_INTERVAL_MS) - getticks();
		if (wait > 0)
			kpause("aicwfscn", false, wait, NULL);
		if (ic->ic_state == IEEE80211_S_SCAN && !sc->sc_scanning &&
		    (ic->ic_flags & IEEE80211_F_SCAN) != 0)
			aicwf_scan(sc);
		break;

	case IEEE80211_S_AUTH:
		/* Firmware may request SAE before CONNECT_CFM arrives. */
		s = splnet();
		sc->sc_newstate(ic, nstate, arg);
		splx(s);
		aicwf_connect(sc);
		return;

	default:
		break;
	}

	s = splnet();
	sc->sc_newstate(ic, nstate, arg);
	splx(s);
}

static void
aicwf_key_set_cb(struct aicwf_softc *sc, const struct aicwf_task *t)
{
	const u_int slot = t->t_group ? t->t_keyix : AICWF_KEY_SLOT_PAIRWISE;
	uint8_t req[44], cfm[2] = { 0xff, 0xff };
	bool duplicate;
	int error;

	if (t->t_cipher != IEEE80211_CIPHER_AES_CCM ||
	    slot >= AICWF_KEY_SLOTS || t->t_keylen != 16) {
		device_printf(sc->sc_dev, "cipher %u is not supported\n",
		    t->t_cipher);
		return;
	}
	mutex_enter(&sc->sc_lock);
	if (t->t_epoch != sc->sc_sae_epoch || !sc->sc_connected) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	duplicate = !t->t_group && sc->sc_ptk_valid &&
	    memcmp(sc->sc_ptk, t->t_key, sizeof(sc->sc_ptk)) == 0;
	/* Never reset firmware TX PN by programming the same live PTK twice. */
	if (!t->t_group && !duplicate)
		sc->sc_ptk_valid = false;
	memset(req, 0, sizeof(req));
	req[0] = t->t_group ? t->t_keyix : 0;
	req[1] = t->t_group ? AICWF_STA_NONE : sc->sc_ap;
	req[4] = t->t_keylen;
	memcpy(req + 8, t->t_key, t->t_keylen);
	req[40] = AICWF_CIPHER_CCMP;
	req[41] = sc->sc_vif;
	req[43] = !t->t_group;
	mutex_exit(&sc->sc_lock);

	if (!duplicate) {
		error = aicwf_cmd_reply(sc, AICWF_MM_KEY_ADD_REQ, req,
		    sizeof(req), AICWF_MM_KEY_ADD_CFM, cfm, sizeof(cfm),
		    true, t->t_epoch);
		explicit_memset(req, 0, sizeof(req));
		if (error != 0 || cfm[0] != 0 || cfm[1] == AICWF_HWKEY_NONE) {
			device_printf(sc->sc_dev, "key %u not set: %d, %u\n",
			    slot, error, cfm[0]);
			return;
		}
	}
	mutex_enter(&sc->sc_lock);
	if (t->t_epoch != sc->sc_sae_epoch || !sc->sc_connected) {
		mutex_exit(&sc->sc_lock);
		explicit_memset(req, 0, sizeof(req));
		return;
	}
	if (!duplicate) {
		sc->sc_hwkey[slot] = cfm[1];
		if (!t->t_group) {
			memcpy(sc->sc_ptk, t->t_key, sizeof(sc->sc_ptk));
			sc->sc_pmf_rx_pn = 0;
			sc->sc_ptk_valid = true;
		}
	}
	explicit_memset(req, 0, sizeof(req));
	req[0] = sc->sc_ap;
	req[1] = 1;
	mutex_exit(&sc->sc_lock);
	if (!t->t_group) {
		/* The pairwise key is in: let all traffic through. */
		(void)aicwf_cmd_reply(sc, AICWF_ME_SET_CONTROL_PORT_REQ, req, 2,
		    AICWF_ME_SET_CONTROL_PORT_CFM, NULL, 0, true, t->t_epoch);
	}
}

static void
aicwf_key_delete_cb(struct aicwf_softc *sc, const struct aicwf_task *t)
{
	const u_int slot = t->t_group ? t->t_keyix : AICWF_KEY_SLOT_PAIRWISE;
	uint8_t req[1];

	if (slot >= AICWF_KEY_SLOTS)
		return;
	mutex_enter(&sc->sc_lock);
	if (t->t_epoch != sc->sc_sae_epoch ||
	    sc->sc_hwkey[slot] == AICWF_HWKEY_NONE ||
	    (!t->t_group && sc->sc_ptk_valid &&
	    memcmp(sc->sc_ptk, t->t_key, sizeof(sc->sc_ptk)) != 0)) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	req[0] = sc->sc_hwkey[slot];
	sc->sc_hwkey[slot] = AICWF_HWKEY_NONE;
	if (!t->t_group) {
		sc->sc_ptk_valid = false;
		explicit_memset(sc->sc_ptk, 0, sizeof(sc->sc_ptk));
	}
	mutex_exit(&sc->sc_lock);
	(void)aicwf_cmd_reply(sc, AICWF_MM_KEY_DEL_REQ, req, sizeof(req),
	    AICWF_MM_KEY_DEL_CFM, NULL, 0, true, t->t_epoch);
}

static void
aicwf_task(struct work *wk, void *arg)
{
	struct aicwf_softc * const sc = arg;
	struct aicwf_task * const t = (struct aicwf_task *)wk;

	if (wk == &sc->sc_alive_work) {
		aicwf_alive_work(sc);
		return;
	}
	switch (t->t_cmd) {
	case AICWF_TASK_NEWSTATE:
		aicwf_newstate_cb(sc, t->t_state, t->t_arg);
		break;
	case AICWF_TASK_KEY_SET:
		aicwf_key_set_cb(sc, t);
		break;
	case AICWF_TASK_KEY_DELETE:
		aicwf_key_delete_cb(sc, t);
		break;
	case AICWF_TASK_STA_MGMT:
		aicwf_sta_mgmt_cb(sc, t);
		break;
	case AICWF_TASK_STA_CANCEL_SCAN:
		aicwf_sta_cancel_scan_cb(sc, t->t_sta.flags);
		break;
	}

	explicit_memset(t->t_key, 0, sizeof(t->t_key));
	pool_cache_put(sc->sc_taskpool, t);
}

/* Requests to the firmware sleep, so net80211's calls are deferred. */
static int
aicwf_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct aicwf_softc * const sc = ic->ic_ifp->if_softc;
	struct aicwf_task *t;

	t = pool_cache_get(sc->sc_taskpool, PR_NOWAIT);
	if (t == NULL) {
		device_printf(sc->sc_dev, "no free tasks\n");
		return EIO;
	}
	t->t_cmd = AICWF_TASK_NEWSTATE;
	t->t_state = nstate;
	t->t_arg = arg;
	workqueue_enqueue(sc->sc_taskq, &t->t_work, NULL);

	return 0;
}

static int
aicwf_key_task(struct ieee80211com *ic, const struct ieee80211_key *wk,
    enum aicwf_task_cmd cmd)
{
	struct aicwf_softc * const sc = ic->ic_ifp->if_softc;
	struct aicwf_task *t;

	t = pool_cache_get(sc->sc_taskpool, PR_NOWAIT);
	if (t == NULL) {
		device_printf(sc->sc_dev, "no free tasks\n");
		return 0;
	}
	/* net80211 may call DELETE while holding its IPL_NET node lock. */
	mutex_spin_enter(&sc->sc_epoch_lock);
	t->t_epoch = sc->sc_sae_epoch;
	mutex_spin_exit(&sc->sc_epoch_lock);
	t->t_cmd = cmd;
	t->t_cipher = wk->wk_cipher->ic_cipher;
	t->t_keyix = wk->wk_keyix;
	t->t_group = (wk->wk_flags & IEEE80211_KEY_GROUP) != 0;
	t->t_keylen = wk->wk_keylen;
	memcpy(t->t_key, wk->wk_key, sizeof(t->t_key));
	workqueue_enqueue(sc->sc_taskq, &t->t_work, NULL);

	return 1;
}

static int
aicwf_key_set(struct ieee80211com *ic, const struct ieee80211_key *wk,
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	return aicwf_key_task(ic, wk, AICWF_TASK_KEY_SET);
}

static int
aicwf_key_delete(struct ieee80211com *ic, const struct ieee80211_key *wk)
{
	return aicwf_key_task(ic, wk, AICWF_TASK_KEY_DELETE);
}

/* The firmware does the management frames and picks the rates. */
static int
aicwf_send_mgmt(struct ieee80211com *ic, struct ieee80211_node *ni,
    int type, int arg)
{
	return 0;
}

static void
aicwf_recv_mgmt(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, int subtype, int rssi, uint32_t rstamp)
{
}

static void
aicwf_newassoc(struct ieee80211_node *ni, int isnew)
{
	ni->ni_txrate = 0;
}

static int
aicwf_media_change(struct ifnet *ifp)
{
	return 0;
}

/* Hand an Ethernet frame to the firmware. */
static int
aicwf_tx_data(struct aicwf_softc *sc, struct mbuf *m)
{
	uint8_t * const buf = sc->sc_txbuf;
	uint8_t * const desc = buf + AICWF_HDR_LEN;
	const size_t len = m->m_pkthdr.len;
	int error;

	if (len < ETHER_HDR_LEN || len > ETHER_HDR_LEN + ETHERMTU)
		return EINVAL;
	const size_t payload = len - ETHER_HDR_LEN;

	mutex_enter(&sc->sc_lock);
	le16enc(buf, AICWF_TXDESC_LEN + payload);
	buf[2] = AICWF_TYPE_DATA;
	buf[3] = aicwf_crc8(buf, 3);
	memset(desc, 0, AICWF_TXDESC_LEN);
	le16enc(desc, payload);
	m_copydata(m, 0, 2 * ETHER_ADDR_LEN + 2, desc + 8);
	desc[22] = AICWF_HWQ_BE;
	desc[23] = sc->sc_qos ? 0 : AICWF_TID_NONE;
	desc[24] = sc->sc_vif;
	desc[25] = sc->sc_ap;
	m_copydata(m, ETHER_HDR_LEN, payload, desc + AICWF_TXDESC_LEN);
	error = aicwf_write(sc, AICWF_HDR_LEN + AICWF_TXDESC_LEN + payload,
	    AICWF_TX_RESERVE);
	mutex_exit(&sc->sc_lock);

	return error;
}

static void
aicwf_tx_task(struct work *wk, void *arg)
{
	struct aicwf_softc * const sc = arg;
	struct ifnet * const ifp = &sc->sc_if;
	struct mbuf *m;
	int error;

	atomic_swap_uint(&sc->sc_txqueued, 0);

	for (;;) {
		IFQ_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;
		if (!sc->sc_connected) {
			if_statinc(ifp, if_oerrors);
			m_freem(m);
			continue;
		}
		bpf_mtap(ifp, m, BPF_D_OUT);
		error = aicwf_tx_data(sc, m);
		if_statinc(ifp, error != 0 ? if_oerrors : if_opackets);
		m_freem(m);
	}
}

/* Writes to the chip sleep, so a thread sends the queue. */
static void
aicwf_start(struct ifnet *ifp)
{
	struct aicwf_softc * const sc = ifp->if_softc;

	if ((ifp->if_flags & (IFF_RUNNING | IFF_OACTIVE)) != IFF_RUNNING)
		return;
	if (atomic_swap_uint(&sc->sc_txqueued, 1) == 0)
		workqueue_enqueue(sc->sc_txq, &sc->sc_txwork, NULL);
}

/*
 * A received 802.11 data frame, already decrypted: turn it into an
 * Ethernet frame.
 */
/* Pass a chain of received packets (linked by m_nextpkt) to the stack. */
static void
aicwf_rx_input(struct aicwf_softc *sc, struct mbuf *m)
{
	struct ifnet * const ifp = &sc->sc_if;
	struct mbuf *next;
	int s;

	s = splnet();
	for (; m != NULL; m = next) {
		next = m->m_nextpkt;
		m->m_nextpkt = NULL;
		if_percpuq_enqueue(ifp->if_percpuq, m);
	}
	splx(s);
}

static void
aicwf_rx_free(struct mbuf *m)
{
	struct mbuf *next;

	for (; m != NULL; m = next) {
		next = m->m_nextpkt;
		m_freem(m);
	}
}

/*
 * Move what is ready from the window of one traffic class to the end of
 * the chain at *tailp: everything before "upto", then the unbroken run
 * that follows.
 */
static struct mbuf **
aicwf_reorder_release(struct aicwf_reorder *ro, uint16_t upto,
    struct mbuf **tailp)
{
	struct mbuf *m;

	for (;;) {
		m = ro->slot[ro->head % AICWF_REORDER_WINDOW];
		if (m == NULL && (ro->head == upto || ro->held == 0))
			break;
		if (m != NULL) {
			ro->slot[ro->head % AICWF_REORDER_WINDOW] = NULL;
			ro->held--;
			*tailp = m;
			while (m->m_nextpkt != NULL)
				m = m->m_nextpkt;
			tailp = &m->m_nextpkt;
		}
		if (ro->head == upto)
			upto = (upto + 1) & AICWF_SEQ_MASK;
		ro->head = (ro->head + 1) & AICWF_SEQ_MASK;
	}
	if (ro->held == 0 && AICWF_SEQ_BEFORE(ro->head, upto))
		ro->head = upto;
	return tailp;
}

/* A frame of an A-MPDU: hold it until the frames before it have come. */
static void
aicwf_reorder(struct aicwf_softc *sc, u_int tid, uint16_t seq,
    struct mbuf *m)
{
	struct aicwf_reorder * const ro = &sc->sc_reorder[tid];
	struct mbuf *ready = NULL, **tailp = &ready;
	u_int slot;

	mutex_enter(&sc->sc_reorder_lock);
	if (!ro->started) {
		ro->started = true;
		ro->head = seq;
	}
	if (AICWF_SEQ_BEFORE(seq, ro->head)) {
		/* Late; the firmware has dropped the duplicates. */
		mutex_exit(&sc->sc_reorder_lock);
		aicwf_rx_input(sc, m);
		return;
	}
	if (AICWF_SEQ_SUB(seq, ro->head) >= AICWF_REORDER_WINDOW) {
		/* The window moves on; what falls out of it goes up. */
		tailp = aicwf_reorder_release(ro, (seq -
		    (AICWF_REORDER_WINDOW - 1)) & AICWF_SEQ_MASK, tailp);
	}
	slot = seq % AICWF_REORDER_WINDOW;
	if (ro->slot[slot] != NULL) {
		aicwf_rx_free(ro->slot[slot]);
		ro->held--;
	}
	ro->slot[slot] = m;
	ro->held++;
	tailp = aicwf_reorder_release(ro, ro->head, tailp);
	if (ro->held != 0 && !callout_pending(&sc->sc_reorder_ch))
		callout_schedule(&sc->sc_reorder_ch,
		    mstohz(AICWF_REORDER_WAIT_MS));
	mutex_exit(&sc->sc_reorder_lock);

	aicwf_rx_input(sc, ready);
}

/* Empty the windows: of one traffic class, or of all (tid < 0). */
static void
aicwf_reorder_flush(struct aicwf_softc *sc, int tid, bool deliver)
{
	struct mbuf *ready = NULL, **tailp = &ready;

	mutex_enter(&sc->sc_reorder_lock);
	for (u_int i = 0; i < AICWF_NTID; i++) {
		struct aicwf_reorder * const ro = &sc->sc_reorder[i];

		if (tid >= 0 && (u_int)tid != i)
			continue;
		if (ro->held != 0)
			tailp = aicwf_reorder_release(ro, (ro->head +
			    AICWF_REORDER_WINDOW) & AICWF_SEQ_MASK, tailp);
		ro->started = false;
	}
	mutex_exit(&sc->sc_reorder_lock);

	if (deliver)
		aicwf_rx_input(sc, ready);
	else
		aicwf_rx_free(ready);
}

/* A frame did not come in time: go on without it. */
static void
aicwf_reorder_timeout(void *arg)
{
	struct aicwf_softc * const sc = arg;
	struct mbuf *ready = NULL, **tailp = &ready;
	bool again = false;

	mutex_enter(&sc->sc_reorder_lock);
	for (u_int i = 0; i < AICWF_NTID; i++) {
		struct aicwf_reorder * const ro = &sc->sc_reorder[i];
		uint16_t seq;

		if (ro->held == 0)
			continue;
		for (seq = ro->head;
		    ro->slot[seq % AICWF_REORDER_WINDOW] == NULL;
		    seq = (seq + 1) & AICWF_SEQ_MASK)
			continue;
		sc->sc_ev_gap.ev_count++;
		tailp = aicwf_reorder_release(ro, seq, tailp);
		if (ro->held != 0)
			again = true;
	}
	if (again)
		callout_schedule(&sc->sc_reorder_ch,
		    mstohz(AICWF_REORDER_WAIT_MS));
	mutex_exit(&sc->sc_reorder_lock);

	aicwf_rx_input(sc, ready);
}

/* One Ethernet packet from addresses and an LLC/SNAP payload. */
static struct mbuf *
aicwf_rx_ether(struct aicwf_softc *sc, const uint8_t *dst,
    const uint8_t *src, const uint8_t *llc, size_t llc_len)
{
	static const uint8_t rfc1042[6] = { 0xaa, 0xaa, 0x03, 0, 0, 0 };
	struct ether_header *eh;
	struct mbuf *m;

	if (llc_len < 8 || memcmp(llc, rfc1042, 6) != 0)
		return NULL;
	const size_t len = ETHER_HDR_LEN + llc_len - 8;
	if (len + ETHER_ALIGN > MCLBYTES)
		return NULL;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return NULL;
	if (len + ETHER_ALIGN > MHLEN) {
		MCLGET(m, M_DONTWAIT);
		if ((m->m_flags & M_EXT) == 0) {
			m_freem(m);
			return NULL;
		}
	}
	m->m_data += ETHER_ALIGN;
	eh = mtod(m, struct ether_header *);
	memcpy(eh->ether_dhost, dst, ETHER_ADDR_LEN);
	memcpy(eh->ether_shost, src, ETHER_ADDR_LEN);
	memcpy(&eh->ether_type, llc + 6, 2);
	memcpy(eh + 1, llc + 8, llc_len - 8);
	m->m_len = m->m_pkthdr.len = len;
	m->m_nextpkt = NULL;
	m_set_rcvif(m, &sc->sc_if);
	return m;
}

/* Keep peer/key/replay admission atomic with retirement and rekey. */
static void
aicwf_pmf_rx(struct aicwf_softc *sc, const uint8_t *frame, size_t len,
    uint32_t status, uint32_t flags)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	struct ieee80211req_sae *req;
	uint8_t query[28];
	uint64_t pn;
	uint16_t op;

	req = kmem_zalloc(sizeof(*req), KM_NOSLEEP);
	if (req == NULL)
		return;
	mutex_enter(&sc->sc_lock);
	if (!sc->sc_sae_enabled || !sc->sc_connected || !sc->sc_ptk_valid ||
	    !sc->sc_sae_authenticated || ic->ic_state != IEEE80211_S_RUN ||
	    ((flags >> 8) & 0xff) != sc->sc_vif ||
	    !IEEE80211_ADDR_EQ(ic->ic_bss->ni_bssid, sc->sc_sae_bssid))
		goto drop;
	if (aicwf_pmf_unprot_valid(frame, len, ic->ic_myaddr,
	    sc->sc_sae_bssid)) {
		op = IEEE80211_SAE_UNPROT_DISCONNECT;
	} else if (aicwf_pmf_query_rx(frame, len, ic->ic_myaddr,
	    sc->sc_sae_bssid, status, sc->sc_hwkey[AICWF_KEY_SLOT_PAIRWISE],
	    sc->sc_pmf_rx_pn, query, &pn)) {
		op = IEEE80211_SAE_RX_SA_QUERY;
		sc->sc_pmf_rx_pn = pn;
		frame = query;
		len = sizeof(query);
	} else {
		goto drop;
	}
	IEEE80211_DPRINTF(ic, IEEE80211_MSG_STATE,
	    "PMF event 0x%x action/reason %u\n", op,
	    frame[op == IEEE80211_SAE_RX_SA_QUERY ? 25 : 24]);
	req->version = IEEE80211_SAE_VERSION;
	req->generation = sc->sc_sae_generation;
	req->op = op;
	req->len = len;
	memcpy(req->bssid, sc->sc_sae_bssid, ETHER_ADDR_LEN);
	memcpy(req->data, frame, len);
	mutex_exit(&sc->sc_lock);
	rt_ieee80211msg(ic->ic_ifp, RTM_IEEE80211_SAE, req,
	    offsetof(struct ieee80211req_sae, data) + len);
	kmem_free(req, sizeof(*req));
	return;
drop:
	mutex_exit(&sc->sc_lock);
	kmem_free(req, sizeof(*req));
}

/* Keep BTM policy in the supplicant; only valid disconnects retire the link. */
static void
aicwf_mgmt_rx(struct aicwf_softc *sc, const uint8_t *frame, size_t len,
    uint32_t status, uint32_t flags)
{
	const uint8_t *body;
	size_t body_len;
	uint64_t pn;
	bool disconnected = false;

	if (len < 24)
		return;
	if (frame[0] == 0xd0) {
		aicwf_sta_rx_event(sc, frame, len, status, flags);
		aicwf_pmf_rx(sc, frame, len, status, flags);
		return;
	}
	if (frame[0] != 0xc0 && frame[0] != 0xa0)
		return;
	/* Unprotected reasons 6/7 still start the validated SA Query path. */
	if ((frame[1] & 0x40) == 0 && sc->sc_sae_enabled) {
		aicwf_pmf_rx(sc, frame, len, status, flags);
		return;
	}
	mutex_enter(&sc->sc_lock);
	body = aicwf_mgmt_body(sc, frame, len, status, flags, &body_len, &pn);
	if (body != NULL && body_len >= 2) {
		sc->sc_pmf_rx_pn = pn;
		disconnected = true;
	}
	mutex_exit(&sc->sc_lock);
	if (disconnected)
		aicwf_alive_lost(sc);
}

static void
aicwf_rx_data(
struct aicwf_softc *sc, const uint8_t *pkt, size_t mpdu_len)
{
	struct ifnet * const ifp = &sc->sc_if;
	const uint8_t * const wh = pkt + AICWF_RXHDR_LEN;
	const uint32_t status = le32dec(pkt + AICWF_RXHDR_STATUS);
	const uint32_t flags = le32dec(pkt + AICWF_RXHDR_FLAGS);
	uint16_t seq;
	struct mbuf *m = NULL, **tailp = &m;
	bool qos = false, amsdu = false;
	u_int tid = 0;
	size_t hdr;

	if (!sc->sc_if_attached || (flags & AICWF_RX_FLAG_UPLOAD) == 0)
		return;
	/* Any frame from the air, beacons included, proves the link lives. */
	sc->sc_alive_rx = ticks;
	/* Metadata only: protected management bodies may contain key material. */
	if (mpdu_len >= sizeof(struct ieee80211_frame) &&
	    (wh[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_MGT &&
	    (wh[0] == IEEE80211_FC0_SUBTYPE_DEAUTH ||
	    wh[0] == IEEE80211_FC0_SUBTYPE_DISASSOC ||
	    wh[0] == 0xd0))	/* Action; absent from legacy net80211. */
		IEEE80211_DPRINTF(&sc->sc_ic, IEEE80211_MSG_STATE,
		    "management RX: subtype 0x%02x flags 0x%02x len %zu "
		    "status 0x%08x descriptor 0x%08x\n",
		    wh[0], wh[1], mpdu_len, status, flags);
	if (mpdu_len >= 2 && wh[0] == IEEE80211_FC0_SUBTYPE_AUTH) {
		/* Unassociated management frames can have no firmware VIF. */
		const uint8_t vif = (flags >> 8) & 0xff;
		if (vif == sc->sc_vif || vif == 0xff)
			aicwf_sae_event(sc, false, wh, mpdu_len);
		return;
	}
	if (mpdu_len >= 24 &&
	    (wh[0] == 0xc0 || wh[0] == 0xa0 || wh[0] == 0xd0)) {
		aicwf_mgmt_rx(sc, wh, mpdu_len, status, flags);
		return;
	}
	/* Beacons and data frames without a body come up too. */
	if (mpdu_len < sizeof(struct ieee80211_frame) + 8 ||
	    (wh[0] & (IEEE80211_FC0_TYPE_MASK |
	    IEEE80211_FC0_SUBTYPE_NODATA)) != IEEE80211_FC0_TYPE_DATA)
		return;
	seq = le16dec(wh + 22) >> 4;
	/* Whole frames from the access point. */
	if ((wh[1] & IEEE80211_FC1_DIR_MASK) != IEEE80211_FC1_DIR_FROMDS ||
	    (wh[1] & IEEE80211_FC1_MORE_FRAG) != 0 || (wh[22] & 0x0f) != 0)
		goto drop;

	hdr = sizeof(struct ieee80211_frame);
	if ((wh[0] & IEEE80211_FC0_SUBTYPE_QOS) != 0) {
		qos = true;
		tid = wh[hdr] & 0x0f;
		amsdu = (wh[hdr] & 0x80) != 0;
		hdr += 2;
		if ((wh[1] & IEEE80211_FC1_ORDER) != 0)
			hdr += 4;
	}
	switch (__SHIFTOUT(status, AICWF_RX_DECR)) {
	case 0:
		break;
	case AICWF_RX_DECR_WEP:
		hdr += 4;
		break;
	case AICWF_RX_DECR_WAPI:
		goto drop;
	default:
		hdr += 8;
		break;
	}
	if (mpdu_len < hdr + 8)
		goto drop;

	if (!amsdu) {
		m = aicwf_rx_ether(sc, wh + 4, wh + 16, wh + hdr,
		    mpdu_len - hdr);
		if (m == NULL)
			goto drop;
	} else {
		/* Several packets in one frame, each with its addresses. */
		sc->sc_ev_amsdu.ev_count++;
		const uint8_t *sub = wh + hdr;
		size_t left = mpdu_len - hdr;

		while (left >= ETHER_HDR_LEN + 8) {
			const size_t sub_len = be16dec(sub + 12);
			struct mbuf *m0;

			if (sub_len > left - ETHER_HDR_LEN)
				break;
			m0 = aicwf_rx_ether(sc, sub, sub + 6,
			    sub + ETHER_HDR_LEN, sub_len);
			if (m0 != NULL) {
				*tailp = m0;
				tailp = &m0->m_nextpkt;
			}
			const size_t step = roundup(ETHER_HDR_LEN + sub_len, 4);
			if (step >= left)
				break;
			sub += step;
			left -= step;
		}
		if (m == NULL)
			goto drop;
	}

	if (qos && !ETHER_IS_MULTICAST(wh + 4)) {
		if ((flags & AICWF_RX_FLAG_REORDER) != 0) {
			sc->sc_ev_ampdu.ev_count++;
			aicwf_reorder(sc, tid, seq, m);
			return;
		}
		/* The aggregation on this traffic class has ended. */
		aicwf_reorder_flush(sc, tid, true);
	}
	aicwf_rx_input(sc, m);
	return;

drop:
	if_statinc(ifp, if_ierrors);
}

static int
aicwf_init(struct ifnet *ifp)
{
	struct aicwf_softc * const sc = ifp->if_softc;
	struct ieee80211com * const ic = &sc->sc_ic;

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	if (ic->ic_roaming != IEEE80211_ROAMING_MANUAL)
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);

	return 0;
}

static void
aicwf_stop(struct ifnet *ifp, int disable)
{
	struct aicwf_softc * const sc = ifp->if_softc;

	ifp->if_timer = 0;
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	ieee80211_new_state(&sc->sc_ic, IEEE80211_S_INIT, -1);
}

static void
aicwf_watchdog(struct ifnet *ifp)
{
	struct aicwf_softc * const sc = ifp->if_softc;

	ifp->if_timer = 0;
	ieee80211_watchdog(&sc->sc_ic);
}

static int
aicwf_ioctl(struct ifnet *ifp, u_long cmd, void *data)
{
	struct aicwf_softc * const sc = ifp->if_softc;
	struct ieee80211com * const ic = &sc->sc_ic;
	int s, error = 0, oflags;

	if ((cmd == SIOCG80211 || cmd == SIOCS80211) &&
	    ((struct ieee80211req *)data)->i_type == IEEE80211_IOC_SAE)
		return aicwf_sae_ioctl(sc, cmd, data);
	s = splnet();

	switch (cmd) {
	case SIOCSIFFLAGS:
		oflags = ifp->if_flags;
		if ((error = ifioctl_common(ifp, cmd, data)) != 0)
			break;
		switch (ifp->if_flags & (IFF_UP | IFF_RUNNING)) {
		case IFF_UP:
			if ((oflags & IFF_UP) == 0)
				aicwf_init(ifp);
			break;
		case IFF_RUNNING:
			if ((oflags & IFF_UP) != 0)
				aicwf_stop(ifp, 1);
			break;
		default:
			break;
		}
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/* The firmware passes every multicast frame of the network. */
		if ((error = ether_ioctl(ifp, cmd, data)) == ENETRESET)
			error = 0;
		break;

	default:
		error = ieee80211_ioctl(ic, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			aicwf_init(ifp);
		error = 0;
	}

	splx(s);

	return error;
}

/* Give the system a wireless network interface. */
static void
aicwf_ifattach(struct aicwf_softc *sc)
{
	static const uint8_t chan5[] = {
		36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120,
		124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165
	};
	struct ieee80211com * const ic = &sc->sc_ic;
	struct ifnet * const ifp = &sc->sc_if;
	u_int n;

	if (workqueue_create(&sc->sc_taskq, device_xname(sc->sc_dev),
	    aicwf_task, sc, PRI_NONE, IPL_NET, 0) != 0 ||
	    workqueue_create(&sc->sc_txq, "aicwftx", aicwf_tx_task, sc,
	    PRI_NONE, IPL_NET, 0) != 0) {
		aprint_error_dev(sc->sc_dev, "couldn't create workqueues\n");
		return;
	}
	sc->sc_taskpool = pool_cache_init(sizeof(struct aicwf_task), 0, 0, 0,
	    "aicwftask", NULL, IPL_NET, NULL, NULL, NULL);
	pool_cache_prime(sc->sc_taskpool, AICWF_TASK_COUNT);
	memset(sc->sc_hwkey, AICWF_HWKEY_NONE, sizeof(sc->sc_hwkey));

	ic->ic_ifp = ifp;
	ic->ic_phytype = IEEE80211_T_OFDM;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_state = IEEE80211_S_INIT;
	ic->ic_caps = IEEE80211_C_AES_CCM | IEEE80211_C_SHSLOT |
	    IEEE80211_C_SHPREAMBLE | IEEE80211_C_WPA;
	memcpy(ic->ic_myaddr, sc->sc_enaddr, ETHER_ADDR_LEN);
	ic->ic_ibss_chan = &ic->ic_channels[0];

	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;
	for (n = 1; n <= 13; n++) {
		ic->ic_channels[n].ic_freq =
		    ieee80211_ieee2mhz(n, IEEE80211_CHAN_2GHZ);
		ic->ic_channels[n].ic_flags = IEEE80211_CHAN_CCK |
		    IEEE80211_CHAN_OFDM | IEEE80211_CHAN_DYN |
		    IEEE80211_CHAN_2GHZ;
	}
	if (sc->sc_5ghz) {
		ic->ic_sup_rates[IEEE80211_MODE_11A] =
		    ieee80211_std_rateset_11a;
		for (n = 0; n < __arraycount(chan5); n++) {
			ic->ic_channels[chan5[n]].ic_freq =
			    ieee80211_ieee2mhz(chan5[n], IEEE80211_CHAN_5GHZ);
			ic->ic_channels[chan5[n]].ic_flags = IEEE80211_CHAN_A;
		}
	}

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = aicwf_init;
	ifp->if_ioctl = aicwf_ioctl;
	ifp->if_start = aicwf_start;
	ifp->if_stop = aicwf_stop;
	ifp->if_watchdog = aicwf_watchdog;
	IFQ_SET_READY(&ifp->if_snd);
	memcpy(ifp->if_xname, device_xname(sc->sc_dev), IFNAMSIZ);

	if_initialize(ifp);
	ieee80211_ifattach(ic);
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = aicwf_newstate;
	ic->ic_sta_mgmt = aicwf_sta_mgmt;
	ic->ic_newassoc = aicwf_newassoc;
	ic->ic_send_mgmt = aicwf_send_mgmt;
	ic->ic_recv_mgmt = aicwf_recv_mgmt;
	ic->ic_crypto.cs_key_set = aicwf_key_set;
	ic->ic_crypto.cs_key_delete = aicwf_key_delete;

	ifp->if_percpuq = if_percpuq_create(ifp);
	if_deferred_start_init(ifp, NULL);
	if_register(ifp);
	ieee80211_media_init(ic, aicwf_media_change, ieee80211_media_status);

	ieee80211_announce(ic);

	memset(sc->sc_igtk, AICWF_HWKEY_NONE, sizeof(sc->sc_igtk));
	evcnt_attach_dynamic(&sc->sc_ev_sae_start, EVCNT_TYPE_MISC, NULL,
	    device_xname(sc->sc_dev), "SAE starts");
	evcnt_attach_dynamic(&sc->sc_ev_sae_rx, EVCNT_TYPE_MISC, NULL,
	    device_xname(sc->sc_dev), "SAE received");
	evcnt_attach_dynamic(&sc->sc_ev_sae_tx, EVCNT_TYPE_MISC, NULL,
	    device_xname(sc->sc_dev), "SAE sent");
	evcnt_attach_dynamic(&sc->sc_ev_sae_drop, EVCNT_TYPE_MISC, NULL,
	    device_xname(sc->sc_dev), "SAE rejected");
	sc->sc_if_attached = true;
}

static int
aicwf_match(device_t parent, cfdata_t match, void *aux)
{
	struct sdmmc_attach_args * const saa = aux;
	struct sdmmc_function * const sf = saa->sf;

	/* Not SDIO. */
	if (sf == NULL)
		return 0;

	const struct sdmmc_cis * const cis = &sf->sc->sc_fn0->cis;
	if (cis->manufacturer != AICWF_SDIO_VENDOR ||
	    cis->product != AICWF_SDIO_PRODUCT_D80)
		return 0;

	/* Everything goes through function 1. */
	return sf->number == 1;
}

static void
aicwf_attach(device_t parent, device_t self, void *aux)
{
	struct aicwf_softc * const sc = device_private(self);
	struct sdmmc_attach_args * const saa = aux;
	struct sdmmc_function * const sf = saa->sf;

	sc->sc_dev = self;
	sc->sc_sf = sf;
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&sc->sc_epoch_lock, MUTEX_DEFAULT, IPL_NET);
	mutex_init(&sc->sc_bus_lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&sc->sc_reorder_lock, MUTEX_DEFAULT, IPL_SOFTNET);
	callout_init(&sc->sc_reorder_ch, CALLOUT_MPSAFE);
	callout_setfunc(&sc->sc_reorder_ch, aicwf_reorder_timeout, sc);
	callout_init(&sc->sc_alive_ch, CALLOUT_MPSAFE);
	callout_setfunc(&sc->sc_alive_ch, aicwf_alive_tick, sc);
	sc->sc_alive_rx = ticks;
	callout_schedule(&sc->sc_alive_ch, mstohz(AICWF_ALIVE_PERIOD_MS));
	evcnt_attach_dynamic(&sc->sc_ev_ampdu, EVCNT_TYPE_MISC, NULL,
	    device_xname(self), "A-MPDU frames");
	evcnt_attach_dynamic(&sc->sc_ev_amsdu, EVCNT_TYPE_MISC, NULL,
	    device_xname(self), "A-MSDU frames");
	evcnt_attach_dynamic(&sc->sc_ev_gap, EVCNT_TYPE_MISC, NULL,
	    device_xname(self), "A-MPDU gaps");
	cv_init(&sc->sc_cv, "aicwfcmd");
	sc->sc_txbuf = kmem_alloc(AICWF_RX_MAX, KM_SLEEP);
	sc->sc_rxbuf = kmem_alloc(AICWF_RX_MAX, KM_SLEEP);

	aprint_naive("\n");
	aprint_normal(": AIC8800D80\n");

	sc->sc_io_max = MAX(AICWF_BLOCK_SIZE, rounddown(
	    sdmmc_chip_host_maxblklen(sf->sc->sc_sct, sf->sc->sc_sch),
	    AICWF_BLOCK_SIZE));
	sdmmc_io_set_blocklen(sf, AICWF_BLOCK_SIZE);
	if (sdmmc_io_function_enable(sf) != 0) {
		aprint_error_dev(self, "couldn't enable function 1\n");
		return;
	}
	sdmmc_io_write_1(sf->sc->sc_fn0, AICWF_FN0_PAD,
	    AICWF_FN0_PAD_DEFAULT);
	sdmmc_io_write_1(sf, AICWF_BYTEMODE_ENABLE, AICWF_BYTEMODE_DISABLED);

	sc->sc_ih = sdmmc_intr_establish(parent, aicwf_intr, sc,
	    device_xname(self));
	if (sc->sc_ih == NULL) {
		aprint_error_dev(self, "couldn't establish interrupt\n");
		return;
	}
	sdmmc_intr_enable(sf);
	sdmmc_io_write_1(sf, AICWF_INTR_ENABLE, AICWF_INTR_ENABLE_ALL);

	/*
	 * The rest waits for answers that arrive through the interrupt
	 * task, which shares its thread with this attachment.
	 */
	config_mountroot(self, aicwf_attachhook);
}

static void
aicwf_attachhook(device_t self)
{
	struct aicwf_softc * const sc = device_private(self);
	uint32_t id;
	int error;

	error = aicwf_mem_read(sc, AICWF_CHIP_ID_ADDR, &id);
	if (error != 0) {
		aprint_error_dev(self, "the boot ROM does not answer: %d\n",
		    error);
		return;
	}
	const bool h = __SHIFTOUT(id, AICWF_CHIP_ID_H) == 3;
	aprint_normal_dev(self, "chip ID 0x%08x, revision %u%s\n", id,
	    (u_int)__SHIFTOUT(id, AICWF_CHIP_ID_REV), h ? ", H variant" : "");

	/* The wireless network works without the Bluetooth half. */
	if ((error = aicwf_bt_load(sc)) != 0)
		aprint_error_dev(self, "Bluetooth is not set up: %d\n",
		    error);

	if ((error = aicwf_upload(sc, h ? AICWF_FW_NAME_H : AICWF_FW_NAME,
	    AICWF_FW_ADDR)) != 0 ||
	    (error = aicwf_fw_options_set(sc)) != 0 ||
	    (error = aicwf_fw_start(sc)) != 0) {
		aprint_error_dev(self, "firmware start failed: %d\n", error);
		return;
	}

	/* The firmware takes a moment to come up. */
	kpause("aicwfup", false, hz / 5, NULL);
	if ((error = aicwf_fw_init(sc)) != 0) {
		aprint_error_dev(self, "firmware setup failed: %d\n", error);
		return;
	}

	aicwf_ifattach(sc);
}
