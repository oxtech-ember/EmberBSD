/*	EmberBSD: OpenBSD kernel API shims for the iwx(4) port.	*/

/*
 * Copyright (c) 2026 EmberBSD contributors.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _IF_IWX_COMPAT_H_
#define _IF_IWX_COMPAT_H_

#include <sys/atomic.h>
#include <sys/bitops.h>
#include <sys/callout.h>
#include <sys/kmem.h>
#include <sys/rwlock.h>
#include <sys/time.h>
#include <sys/workqueue.h>
#include <dev/firmload.h>

#define nitems(_a)		__arraycount(_a)
#define letoh16(_x)		le16toh(_x)
#define letoh32(_x)		le32toh(_x)
#define letoh64(_x)		le64toh(_x)

/* libkern: OpenBSD fls(3)/flsl(3) on NetBSD bitops(9). */
#define fls(_x)			fls32(_x)
#define flsl(_x)		fls64(_x)

/* malloc(9)/bus_dma flags absent on NetBSD. */
#define M_CANFAIL		0
#define BUS_DMA_64BIT		0
/* NetBSD bus_dmamem_alloc has no zero-fill flag: iwx_dma_contig_alloc memsets. */
#define BUS_DMA_ZERO		0

/* OpenBSD splassert(9); NetBSD asserts locks differently. */
#define splassert(_ipl)		do { } while (0)

/* caddr_t is a userland-only type on NetBSD. */
typedef char *caddr_t;

/* OpenBSD rw_enter(9) flag names. */
#define RW_WRITE		RW_WRITER
#define RW_INTR			0

/* OpenBSD net80211 names for what NetBSD spells differently. */
#define IEEE80211_CIPHER_CCMP	IEEE80211_CIPHER_AES_CCM
#define IEEE80211_F_RSNON	IEEE80211_F_WPA
#define IEEE80211_C_RSN		IEEE80211_C_WPA

/*
 * HT/VHT constants from OpenBSD net80211 (values checked out 2026-10-19).
 * Stage 1 compiles the HT paths out (IWX_HT undefined); the negotiation
 * flags below are zero so no legacy path can ever see them set.
 */
#define IEEE80211_NODE_HT		0	/* never negotiated in stage 1 */
#define IEEE80211_NODE_VHT		0
#define IEEE80211_NODE_UAPSD		0
#define IEEE80211_NODE_MFP		0
#define IEEE80211_NODE_RXPROT		0
#define IEEE80211_NODE_TXPROT		0
#define IEEE80211_NODE_RXMGMTPROT	0
#define IEEE80211_NODE_TXMGMTPROT	0
#define IEEE80211_NUM_TID		16
#define IEEE80211_HTOP0_SCO_SCN		0
#define IEEE80211_HTOP0_SCO_SCA		1
#define IEEE80211_HTOP0_SCO_SCB		3
#define IEEE80211_VHTOP0_CHAN_WIDTH_HT	0
#define IEEE80211_VHTOP0_CHAN_WIDTH_80	1
#define IEEE80211_VHTOP0_CHAN_WIDTH_160	2
#define IEEE80211_BA_INIT		0
#define IEEE80211_BA_REQUESTED		1
#define IEEE80211_BA_AGREED		2
#define IEEE80211_BA_MAX_WINSZ		64
#define IEEE80211_CHAN_40MHZ		0
#define IEEE80211_CHAN_40MHZ_ALLOWED(_c)	0
#define IEEE80211_CHAN_80MHZ_ALLOWED(_c)	0
#define IEEE80211_CHAN_160MHZ_ALLOWED(_c)	0
#define IEEE80211_F_HTON		0
#define IEEE80211_F_VHTON		0
#define IEEE80211_FC0_SUBTYPE_BAR	0x80
#define IEEE80211_CCMP_HDRLEN		8
#define IEEE80211_HT_RATESET_SISO	0
#define IEEE80211_HT_RATESET_MIMO2	2
#define IEEE80211_HTCAP_RXSTBC_MASK	0x00000300
#define IEEE80211_HTCAP_TXSTBC		0x00000080
#define IEEE80211_HTOP1_PROT_MASK	0x0003
#define IEEE80211_VHT_MCS_0_7		0
#define IEEE80211_VHT_MCS_0_8		1
#define IEEE80211_VHT_MCS_0_9		2
#define IEEE80211_VHT_MCS_SS_NOT_SUPP	3
#define IEEE80211_VHTCAP_MAX_MPDU_LENGTH_3895	0
#define IEEE80211_VHTCAP_CHAN_WIDTH_160		1
#define IEEE80211_VHTCAP_CHAN_WIDTH_SHIFT	2
#define IEEE80211_VHTCAP_SGI80			0x00000020
#define IEEE80211_VHTCAP_SGI160			0x00000040
#define IEEE80211_VHTCAP_TX_STBC		0x00000080
#define IEEE80211_VHTCAP_RX_STBC_SS_MASK	0x00000700
#define IEEE80211_VHTCAP_RX_STBC_SS_SHIFT	8
#define IEEE80211_VHTCAP_MAX_AMPDU_LEN_MASK	0x03800000
#define IEEE80211_VHTCAP_MAX_AMPDU_LEN_SHIFT	23
#define IEEE80211_VHTCAP_MAX_AMPDU_LEN_1024K	7
#define IEEE80211_VHTCAP_RX_ANT_PATTERN		0x10000000
#define IEEE80211_VHTCAP_TX_ANT_PATTERN		0x20000000
#define IEEE80211_AMPDU_PARAM_SS_2		(4 << 2)
#define IEEE80211_AMPDU_PARAM_SS_4		(5 << 2)
#define IEEE80211_AMPDU_PARAM_SS_8		(6 << 2)
#define IEEE80211_AMPDU_PARAM_SS_16		(7 << 2)
#define IEEE80211_AMPDU_PARAM_SS		0x1c
#define IEEE80211_AMPDU_PARAM_LE		0x03

/* OpenBSD EDCA category enum matches the NetBSD WME order. */
#define EDCA_AC_BE		WME_AC_BE
#define EDCA_AC_BK		WME_AC_BK
#define EDCA_AC_VI		WME_AC_VI
#define EDCA_AC_VO		WME_AC_VO
#define EDCA_NUM_AC		WME_NUM_AC

/* OpenBSD extended channel flags; nothing sets them without IWX_HT. */
#define IEEE80211_CHANX_80MHZ		0
#define IEEE80211_CHANX_160MHZ		0

/* OpenBSD RX info and its flags, kept for the A-MSDU/duplicate code paths. */
struct ieee80211_rxinfo {
	uint32_t	rxi_flags;
	uint16_t	rxi_chan;
	int8_t		rxi_rssi;
	uint32_t	rxi_tstamp;
};
#define IEEE80211_RXI_HWDEC		0x00000001
#define IEEE80211_RXI_HWDEC_SAME_PN	0x00000002
#define IEEE80211_RXI_SAME_SEQ		0x00000004
#define IEEE80211_RXI_AMPDU_DONE		0x00000008

/* OpenBSD bgscan roaming argument; unused without IWX_HT. */
struct ieee80211_node_switch_bss_arg {
	int unused;
};
enum ieee80211_htprot {
	IEEE80211_HTPROT_NONE = 0,
	IEEE80211_HTPROT_NONMEMBER,
	IEEE80211_HTPROT_20MHZ,
	IEEE80211_HTPROT_NONHT_MIXED
};

/*
 * OpenBSD uses a zero ethernet address constant; NetBSD has none.
 * Zero storage keeps the "unspecified address" meaning.
 */
extern const uint8_t iwx_etheranyaddr[ETHER_ADDR_LEN];
#define etheranyaddr		iwx_etheranyaddr

/* OpenBSD systq(9) is a per-device queue: sc->sc_systq (see iwx_attach). */

/* OpenBSD pcireg.h names of registers NetBSD spells without PCI_. */
#define PCI_PCIE_LCSR		PCIE_LCSR
#define PCI_PCIE_DCSR2		PCIE_DCSR2
#define PCI_PCIE_LCSR_ASPM_L0S	PCIE_LCSR_ASPM_L0S
#define PCI_PCIE_LCSR_ASPM_L1	PCIE_LCSR_ASPM_L1
#define PCI_PCIE_DCSR2_LTREN	0x0400	/* OpenBSD pcireg.h: LTR enable */

/*
 * Intel 22500 Wi-Fi product IDs from OpenBSD pcidevs (checked out
 * 2026-10-19); NetBSD pcidevs only carries WIFI7 (0x272b) from this set.
 */
#define PCI_PRODUCT_INTEL_WL_22500_1	0x2723	/* Wi-Fi 6 AX200 */
#define PCI_PRODUCT_INTEL_WL_22500_2	0x02f0	/* Wi-Fi 6 AX201 */
#define PCI_PRODUCT_INTEL_WL_22500_3	0xa0f0	/* Wi-Fi 6 AX201 */
#define PCI_PRODUCT_INTEL_WL_22500_4	0x34f0	/* Wi-Fi 6 AX201 */
#define PCI_PRODUCT_INTEL_WL_22500_5	0x06f0	/* Wi-Fi 6 AX201 */
#define PCI_PRODUCT_INTEL_WL_22500_6	0x43f0	/* Wi-Fi 6 AX201 */
#define PCI_PRODUCT_INTEL_WL_22500_7	0x3df0	/* Wi-Fi 6 AX201 */
#define PCI_PRODUCT_INTEL_WL_22500_8	0x4df0	/* Wi-Fi 6 AX201 */
#define PCI_PRODUCT_INTEL_WL_22500_9	0x2725	/* Wi-Fi 6 AX210 */
#define PCI_PRODUCT_INTEL_WL_22500_10	0x2726	/* Wi-Fi 6 AX211 */
#define PCI_PRODUCT_INTEL_WL_22500_11	0x51f0	/* Wi-Fi 6 AX211 */
#define PCI_PRODUCT_INTEL_WL_22500_12	0x7a70	/* Wi-Fi 6 AX211 */
#define PCI_PRODUCT_INTEL_WL_22500_13	0x7af0	/* Wi-Fi 6 AX211 */
#define PCI_PRODUCT_INTEL_WL_22500_14	0x7e40	/* Wi-Fi 6 AX210 */
#define PCI_PRODUCT_INTEL_WL_22500_15	0x7f70	/* Wi-Fi 6 AX211 */
#define PCI_PRODUCT_INTEL_WL_22500_16	0x54f0	/* Wi-Fi 6 AX211 */
#define PCI_PRODUCT_INTEL_WL_22500_17	0x51f1	/* Wi-Fi 6 AX211 */
#define PCI_PRODUCT_INTEL_WL_22500_18	0x7740	/* Wi-Fi AX211 */

#define USEC_TO_NSEC(_u)	((uint64_t)(_u) * 1000ULL)
#define MSEC_TO_NSEC(_m)	((uint64_t)(_m) * 1000000ULL)
#define SEC_TO_NSEC(_s)		((uint64_t)(_s) * 1000000000ULL)
#define INFSLP			((uint64_t)-1)

static inline int
iwx_nsec2ticks(uint64_t nsec)
{
	uint64_t t;

	if (nsec == INFSLP)
		return 0;
	t = nsec / (1000000000ULL / hz);
	return t == 0 ? 1 : (t > INT_MAX ? INT_MAX : (int)t);
}

#define tsleep_nsec(_id, _pri, _msg, _ns) \
	tsleep((_id), (_pri), (_msg), iwx_nsec2ticks(_ns))

/* malloc(9)/free(9) with the OpenBSD size argument dropped. */
/* NetBSD kern_free() dereferences its argument: free(NULL) is not a no-op. */
#define iwx_free(_p, _t, _s)						\
	do {								\
		void *iwx_free_p_ = (void *)(_p);			\
		if (iwx_free_p_ != NULL)				\
			free(iwx_free_p_, (_t));			\
	} while (0)
#define mallocarray(_n, _s, _t, _f)	malloc((size_t)(_n) * (_s), (_t), (_f))

/* OpenBSD ieee80211_release_node(9) drops the com argument on NetBSD. */
#define ieee80211_release_node(_ic, _ni)	ieee80211_free_node(_ni)

/* timeval helper from OpenBSD systm.h. */
static inline struct timeval
iwx_usec_to_timeval(uint64_t usec)
{
	struct timeval tv;

	tv.tv_sec = usec / 1000000ULL;
	tv.tv_usec = (suseconds_t)(usec % 1000000ULL);
	return tv;
}
#define USEC_TO_TIMEVAL(_u)	iwx_usec_to_timeval(_u)

/* OpenBSD task(9) on top of NetBSD workqueue(9). */
struct task {
	struct work	t_work;
	void		(*t_func)(void *);
	void		*t_arg;
	volatile unsigned int t_pending;
};
struct taskq {
	struct workqueue *tq_wq;
};

static void
iwx_task_worker(struct work *wk, void *arg __unused)
{
	struct task *t = container_of(wk, struct task, t_work);

	atomic_swap_uint(&t->t_pending, 0);
	t->t_func(t->t_arg);
}

static inline struct taskq *
taskq_create(const char *name, unsigned int nthreads __unused, int ipl,
    unsigned int flags __unused)
{
	struct taskq *tq = kmem_zalloc(sizeof(*tq), KM_SLEEP);

	if (workqueue_create(&tq->tq_wq, name, iwx_task_worker, NULL,
	    PRI_NONE, ipl, 0) != 0) {
		kmem_free(tq, sizeof(*tq));
		return NULL;
	}
	return tq;
}

static inline void
taskq_destroy(struct taskq *tq)
{
	workqueue_destroy(tq->tq_wq);
	kmem_free(tq, sizeof(*tq));
}

static inline void
task_set(struct task *t, void (*fn)(void *), void *arg)
{
	t->t_func = fn;
	t->t_arg = arg;
	t->t_pending = 0;
}

static inline int
task_add(struct taskq *tq, struct task *t)
{
	if (atomic_swap_uint(&t->t_pending, 1) != 0)
		return 0;
	workqueue_enqueue(tq->tq_wq, &t->t_work, NULL);
	return 1;
}

/*
 * A queued work item cannot be removed from a NetBSD workqueue.  Callers
 * of task_del() in iwx hold a reference which the task drops itself when
 * it runs, so report "not removed" and let the task run to completion.
 */
static inline int
task_del(struct taskq *tq __unused, struct task *t __unused)
{
	return 0;
}

#define task_pending(_t)	((_t)->t_pending != 0)

/* OpenBSD timeout(9) on top of NetBSD callout(9). */
struct timeout {
	callout_t	to_ch;
};

static inline void
timeout_set(struct timeout *to, void (*fn)(void *), void *arg)
{
	callout_init(&to->to_ch, 0);
	callout_setfunc(&to->to_ch, fn, arg);
}

static inline void
timeout_add_usec(struct timeout *to, uint64_t usec)
{
	int t = (int)((usec * hz) / 1000000ULL);

	callout_schedule(&to->to_ch, t > 0 ? t : 1);
}

#define timeout_add_msec(_to, _ms)	timeout_add_usec((_to), (uint64_t)(_ms) * 1000)
#define timeout_add_sec(_to, _s)	timeout_add_usec((_to), (uint64_t)(_s) * 1000000)
#define timeout_del(_to)		callout_stop(&(_to)->to_ch)
#define timeout_pending(_to)		callout_pending(&(_to)->to_ch)

/* OpenBSD rwlock(9) names. */
#define rw_enter_write(_l)	rw_enter((_l), RW_WRITER)
#define rw_assert_wrlock(_l)	KASSERT(rw_write_held(_l))
#define iwx_rw_init(_l, _n)	rw_init(_l)

/* OpenBSD refcnt(9), used to wait for tasks on stop. */
struct refcnt {
	volatile unsigned int r_refs;
};

static inline void
refcnt_init(struct refcnt *r)
{
	r->r_refs = 1;
}

static inline void
refcnt_take(struct refcnt *r)
{
	atomic_inc_uint(&r->r_refs);
}

static inline int
refcnt_rele(struct refcnt *r)
{
	return atomic_dec_uint_nv(&r->r_refs) == 0;
}

static inline void
refcnt_rele_wake(struct refcnt *r)
{
	if (refcnt_rele(r))
		wakeup(r);
}

static inline void
refcnt_finalize(struct refcnt *r, const char *wmesg)
{
	if (!refcnt_rele(r)) {
		while (r->r_refs != 0)
			tsleep(r, 0, wmesg, hz / 10);
	}
}

/* OpenBSD loadfirmware(9) on top of NetBSD firmload(9). */
static inline int
loadfirmware(const char *name, u_char **bufp, size_t *buflen)
{
	firmware_handle_t fwh;
	size_t size;
	u_char *buf;
	int err;

	err = firmware_open("if_iwx", name, &fwh);
	if (err)
		return err;
	size = firmware_get_size(fwh);
	if (size == 0) {
		firmware_close(fwh);
		return EINVAL;
	}
	buf = firmware_malloc(size);
	if (buf == NULL) {
		firmware_close(fwh);
		return ENOMEM;
	}
	err = firmware_read(fwh, 0, buf, size);
	firmware_close(fwh);
	if (err) {
		firmware_free(buf, size);
		return err;
	}
	*bufp = buf;
	*buflen = size;
	return 0;
}

/* Buffers from loadfirmware() come from firmware_malloc(), not malloc(9). */
static inline void
iwx_firmware_free(void *buf, size_t len)
{
	if (buf != NULL)
		firmware_free(buf, len);
}

#endif /* _IF_IWX_COMPAT_H_ */
