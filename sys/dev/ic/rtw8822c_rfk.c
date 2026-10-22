/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD d2145e9b96448da34d3cfede24c509af6d830c92 rtw8822c RFK. */
#ifdef _KERNEL
#include "opt_rtw88_rfk.h"
#endif
#ifdef RTW88_RFK_EXPERIMENT
#ifdef _MODULE
#error Experimental RFK quarantine requires a built-in, kernel-lifetime owner
#endif
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/atomic.h>
#ifdef _KERNEL
#include <sys/stdbool.h>
#else
#include <stdbool.h>
#endif
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8822c.h>
#include <dev/ic/rtw8822c_rfk.h>
#include <dev/ic/rtw8822c_table.h>

#define BIT(n) (UINT32_C(1) << (n))
#define GENMASK(h, l) ((UINT32_MAX << (l)) & (UINT32_MAX >> (31U - (h))))
#define FIELD_GET(m, v) (((v) & (m)) >> __builtin_ctz(m))
#define ARRAY_SIZE(a) __arraycount(a)
#define MASKDWORD UINT32_MAX
#define MASKHWORD UINT32_C(0xffff0000)
#define MASKLWORD UINT32_C(0x0000ffff)
#define MASKBYTE0 UINT32_C(0xff)
#define MASKBYTE1 UINT32_C(0xff00)
#define MASKBYTE2 UINT32_C(0xff0000)
#define MASKBYTE3 UINT32_C(0xff000000)
#define MASK20BITS UINT32_C(0xfffff)
#define DIV_ROUND_CLOSEST(v, d) (((v) + (d) / 2) / (d))
#define RFK_BAND_2G 1U /* RF18 bit16 clear: source DPK band = 1 << 0. */
#define RF_PATH_A 0
#define RF_PATH_B 1
#define RTW_RF_PATH_MAX 4
#include <dev/ic/rtw8822c_rfk_regs.h>

#define RFK_BUSY 1U
#define RFK_QUARANTINE 2U
#define RFK_JOURNAL 48U

/* Deliberately outside every device/interface allocation. There is NO clear
 * operation. Replacing the kernel is not evidence of a hardware reset; an
 * uncertain experiment requires the operator's physical cold-reset protocol. */
static unsigned rfk_global;
static uint64_t rfk_serial;
_Static_assert(__atomic_always_lock_free(sizeof(rfk_global), 0),
    "experimental RFK owner needs native lock-free integer atomics");
_Static_assert(__atomic_always_lock_free(sizeof(rfk_serial), 0),
    "experimental RFK serial needs native lock-free 64-bit atomics");
enum { RFK_START6D = 1, RFK_END6D, RFK_GAPK, RFK_IQK,
    RFK_DPK, RFK_CAL_PWR, RFK_COEF };

struct rfk_saved {
	uint32_t addr, value;
	uint8_t path, rf, valid;
};

/* Source rtw_gapk_info/rtw_dpk_info: chip owns two paths even though the
 * generic source storage allows four. No thermal-tracking worker is added. */
enum { RF_BAND_2G_CCK, RF_BAND_2G_OFDM, RF_BAND_5G_L,
    RF_BAND_5G_M, RF_BAND_5G_H, RF_BAND_MAX };
enum { RTW_DPK_GAIN_CHECK, RTW_DPK_GAIN_LARGE, RTW_DPK_GAIN_LESS,
    RTW_DPK_GL_LARGE, RTW_DPK_GL_LESS, RTW_DPK_LOSS_CHECK, RTW_DPK_AGC_OUT };
enum { RTW_DPK_CAL_PWR, RTW_DPK_GAIN_LOSS, RTW_DPK_DO_DPK,
    RTW_DPK_DPK_ON, RTW_DPK_DAGC, RTW_DPK_ACTION_MAX };
typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef uint32_t u32;
struct rtw_gapk_info {
	u32 rf3f_bp[RF_BAND_MAX][RF_GAIN_NUM][RTW_RF_PATH_MAX];
	u32 rf3f_fs[RTW_RF_PATH_MAX][RF_GAIN_NUM];
	bool txgapk_bp_done;
	s8 offset[RF_GAIN_NUM][RTW_RF_PATH_MAX];
	s8 fianl_offset[RF_GAIN_NUM][RTW_RF_PATH_MAX];
	u8 read_txgain, channel;
};
struct rtw_dpk_info {
	bool is_dpk_pwr_on, is_reload;
	uint32_t dpk_path_ok[1];
	u8 thermal_dpk[DPK_RF_PATH_NUM];
	u32 gnt_control, gnt_value;
	u8 result[RTW_RF_PATH_MAX], dpk_txagc[RTW_RF_PATH_MAX];
	u32 coef[RTW_RF_PATH_MAX][20];
	u16 dpk_gs[RTW_RF_PATH_MAX];
	u8 thermal_dpk_delta[RTW_RF_PATH_MAX], pre_pwsf[RTW_RF_PATH_MAX];
	u8 dpk_band, dpk_ch, dpk_bw;
};
struct rtw_backup_info { u8 len; u32 reg, val; };

struct rtw8822c_rfk {
	struct rtw88_softc *sc;
	struct rtw8822c_rfk_services svc;
	struct rtw8822c_efuse efuse;
	struct { unsigned rf_path_num; } hal;
	struct { struct rtw_gapk_info gapk; struct rtw_dpk_info dpk_info; } dm_info;
	uint32_t baseline_generation;
	uint8_t baseline_rfe, baseline_cut;
	uint64_t token;
	uint32_t generation;
	unsigned active, cleanup_mode, mutated, canceled;
	unsigned pending, non_target, report_deadline, report_not_before;
	unsigned report_size;
	unsigned report_kind, engine_pending;
	unsigned handshake_started, handshake_end_sent, handshake_finished;
	uint32_t report_addr, report_mask, report_target;
	uint32_t last_raw, raw_valid, arms[7], done[7];
	unsigned operations, delay_us, sleeps;
	int error, cleanup_error;
	struct rfk_saved saved[RFK_JOURNAL];
	unsigned nsaved;
	uint32_t outer_tx[2];
	unsigned outer_valid, outer_restoring;
	unsigned started_at, new_deadline, cleanup_deadline;
	unsigned packet_deadline, packet_started, completed, gnt_valid;
};

int
rtw8822c_rfk_blocked(void)
{

	return atomic_load_acquire(&rfk_global) != 0;
}

int
rtw8822c_rfk_active(const struct rtw88_softc *sc)
{

	return sc->sc_rfk != NULL && sc->sc_rfk->active;
}

static void
rfk_quarantine(struct rtw8822c_rfk *k)
{

	(void)k;
	/* Native atomic_ops do not order other memory. These rare RMWs pair
	 * full barriers with owner acquisition/release and published state. */
	membar_sync();
	atomic_or_uint(&rfk_global, RFK_QUARANTINE);
	membar_sync();
}

static void
rfk_drop_owner(void)
{

	membar_sync();
	atomic_and_uint(&rfk_global, ~RFK_BUSY);
	membar_sync();
}

static int
rfk_error(struct rtw8822c_rfk *k, int error)
{

	if (error != 0) {
		if (k->cleanup_mode) {
			if (k->cleanup_error == 0)
				k->cleanup_error = error;
		} else if (k->error == 0) {
			k->error = error;
		}
		if (error == ECANCELED)
			k->canceled = 1;
	}
	return error;
}

/* The service validates full current inputs after every yield. Cleanup keeps
 * only the old exclusive numeric owner, same firmware and resource lifetime. */
static int
rfk_enter(struct rtw8822c_rfk *k)
{
	unsigned global;
	int error;

	if ((!k->cleanup_mode && k->error != 0) ||
	    (k->cleanup_mode && k->cleanup_error != 0))
		return k->cleanup_mode ? k->cleanup_error : k->error;
	global = atomic_load_acquire(&rfk_global);
	/* Another retired context may lose populated originals while we yield.
	 * Quarantine withdraws new work, even from the current owner. Only its
	 * same-token bounded cleanup may continue under BUSY|QUARANTINE. */
	if (!k->active || (global != RFK_BUSY &&
	    !(k->cleanup_mode && global == (RFK_BUSY | RFK_QUARANTINE))))
		return rfk_error(k, EBUSY);
	if (k->token != atomic_load_acquire(&rfk_serial))
		return rfk_error(k, ESTALE);
	if (k->generation != k->sc->sc_generation ||
	    k->sc->sc_stop_requested || !k->sc->sc_resources_ready)
		return rfk_error(k, ESTALE);
	if ((int)(k->svc.clock_ticks(k->svc.cookie) -
	    (k->cleanup_mode ? k->cleanup_deadline : k->new_deadline)) >= 0)
		return rfk_error(k, ETIMEDOUT);
	error = k->svc.enter(k->svc.cookie, (int)k->cleanup_mode);
	return rfk_error(k, error);
}

static uint32_t
rfk_read(struct rtw8822c_rfk *k, uint32_t addr, unsigned size)
{
	uint32_t value = 0;
	uint16_t v16 = 0;
	uint8_t v8 = 0;
	int error;

	if (rfk_enter(k) != 0)
		return 0;
	k->operations++;
	if (size == 1) {
		error = k->sc->sc_ops->read_1(k->sc->sc_bus, addr, &v8);
		value = v8;
	} else if (size == 2) {
		error = k->sc->sc_ops->read_2(k->sc->sc_bus, addr, &v16);
		value = v16;
	} else {
		error = k->sc->sc_ops->read_4(k->sc->sc_bus, addr, &value);
	}
	k->svc.leave(k->svc.cookie);
	(void)rfk_error(k, error);
	return error == 0 ? value : 0;
}

static void
rfk_write(struct rtw8822c_rfk *k, uint32_t addr, unsigned size, uint32_t value)
{
	int error;

	if (rfk_enter(k) != 0)
		return;
	k->operations++;
	k->mutated = 1;
	if (size == 1)
		error = k->sc->sc_ops->write_1(k->sc->sc_bus, addr, (uint8_t)value);
	else if (size == 2)
		error = k->sc->sc_ops->write_2(k->sc->sc_bus, addr, (uint16_t)value);
	else
		error = k->sc->sc_ops->write_4(k->sc->sc_bus, addr, value);
	k->svc.leave(k->svc.cookie);
	(void)rfk_error(k, error);
}

static void
rfk_delay(struct rtw8822c_rfk *k, unsigned us)
{
	int error;

	if (us >= 1000) {
		if ((!k->cleanup_mode && k->error != 0) ||
		    (k->cleanup_mode && k->cleanup_error != 0))
			return;
		k->sleeps++;
		error = k->svc.wait(k->svc.cookie, us, (int)k->cleanup_mode);
		(void)rfk_error(k, error);
	} else {
		if (rfk_enter(k) != 0)
			return;
		k->sc->sc_ops->delay_us(k->sc->sc_bus, us);
		k->svc.leave(k->svc.cookie);
	}
	k->delay_us += us;
}

/* New-word and field operations retain the primary read/modify/write order. */
static uint32_t
rfk_mask_read(struct rtw8822c_rfk *k, uint32_t addr, uint32_t mask)
{

	if (mask == 0) {
		(void)rfk_error(k, EINVAL);
		return 0;
	}
	return (rfk_read(k, addr, 4) & mask) >> __builtin_ctz(mask);
}

static void
rfk_mask_write(struct rtw8822c_rfk *k, uint32_t addr, uint32_t mask,
    uint32_t value)
{
	uint32_t old;

	if (mask == 0) {
		(void)rfk_error(k, EINVAL);
		return;
	}
	if ((value & ~(mask >> __builtin_ctz(mask))) != 0) {
		(void)rfk_error(k, ERANGE);
		return;
	}
	old = rfk_read(k, addr, 4);
	rfk_write(k, addr, 4, (old & ~mask) |
	    ((value << __builtin_ctz(mask)) & mask));
}

/* Existing native PHY RF transport: direct map except RF0 SIPI. */
static uint32_t
rfk_read_rf(struct rtw8822c_rfk *k, unsigned path, uint32_t addr,
    uint32_t mask)
{
	static const uint32_t base[2] = { 0x3c00, 0x4c00 };

	if (path >= 2 || addr > 0xff || mask == 0 || (mask & ~RFREG_MASK)) {
		(void)rfk_error(k, EINVAL);
		return 0;
	}
	return rfk_mask_read(k, base[path] + addr * 4, mask);
}

static void
rfk_write_rf(struct rtw8822c_rfk *k, unsigned path, uint32_t addr,
    uint32_t mask, uint32_t value)
{
	static const uint32_t base[2] = { 0x3c00, 0x4c00 };
	static const uint32_t sipi[2] = { 0x1808, 0x4108 };
	uint32_t old = 0, reg;
	unsigned delay;
	int error = 0;

	if (path >= 2 || addr > 0xff || mask == 0 || (mask & ~RFREG_MASK)) {
		(void)rfk_error(k, EINVAL);
		return;
	}
	if ((value & ~(mask >> __builtin_ctz(mask))) != 0) {
		(void)rfk_error(k, ERANGE);
		return;
	}
	if (rfk_enter(k) != 0)
		return;
	if (addr != 0 || mask != RFREG_MASK) {
		k->operations++;
		error = k->sc->sc_ops->read_4(k->sc->sc_bus,
		    base[path] + addr * 4, &old);
	}
	if (error == 0) {
		reg = addr == 0 ? sipi[path] : base[path] + addr * 4;
		delay = addr == 0 ? 13 : 1;
		k->mutated = 1;
		k->operations++;
		value = (old & ~mask) | (value << __builtin_ctz(mask));
		if (addr == 0)
			value &= RFREG_MASK;
		error = k->sc->sc_ops->write_4(k->sc->sc_bus, reg, value);
		/* A failed write may have landed. Keep its mandatory minimum in
		 * this same bounded group, even if the absolute clock just expired. */
		k->sc->sc_ops->delay_us(k->sc->sc_bus, delay);
		k->delay_us += delay;
	}
	k->svc.leave(k->svc.cookie);
	(void)rfk_error(k, error);
}

static void
rfk_restore_regs(struct rtw8822c_rfk *k, const struct rtw_backup_info *b,
    unsigned n)
{
	unsigned i;

	if (n > RFK_JOURNAL) {
		(void)rfk_error(k, EINVAL);
		return;
	}
	for (i = 0; i < n; i++) {
		if (b[i].len != 1 && b[i].len != 2 && b[i].len != 4) {
			(void)rfk_error(k, EINVAL);
			return;
		}
		/* Internal phase snapshots can see the exclusive TX pause.
		 * Only the outer finalizer may restore these two original words. */
		if (b[i].reg != REG_TX_PTCL_CTRL && b[i].reg != REG_TX_FIFO)
			rfk_write(k, b[i].reg, b[i].len, b[i].val);
	}
}

/* Only explicitly sourced backup groups enter the restoration journal. A
 * failed read never creates a valid value. Nothing snapshots volatile reports. */
static void
rfk_save(struct rtw8822c_rfk *k, uint32_t addr, uint32_t value,
    unsigned path, int rf)
{
	unsigned i;

	if (k->error != 0 || k->cleanup_error != 0)
		return;
	for (i = 0; i < k->nsaved; i++)
		if (k->saved[i].addr == addr && k->saved[i].path == path &&
		    k->saved[i].rf == rf)
			return;
	if (k->nsaved == RFK_JOURNAL) {
		(void)rfk_error(k, EOVERFLOW);
		return;
	}
	k->saved[k->nsaved++] = (struct rfk_saved){
	    .addr = addr, .value = value, .path = (uint8_t)path,
	    .rf = (uint8_t)rf, .valid = 1 };
}

static unsigned
rfk_ticks(struct rtw8822c_rfk *k, unsigned us)
{
	uint64_t ticks = ((uint64_t)us * k->svc.ticks_per_second + 999999) / 1000000;

	return ticks == 0 ? 1 : (unsigned)ticks;
}

static int
rfk_wait(struct rtw8822c_rfk *k, unsigned us)
{
	int error;

	if ((!k->cleanup_mode && k->error != 0) ||
	    (k->cleanup_mode && k->cleanup_error != 0))
		return k->cleanup_mode ? k->cleanup_error : k->error;
	k->sleeps++;
	error = k->svc.wait(k->svc.cookie, us, (int)k->cleanup_mode);
	return rfk_error(k, error);
}

/* Called BEFORE a possibly-landed trigger. Initial target never proves fresh
 * completion; no report register is cleared to manufacture an edge. */
static void
rfk_arm(struct rtw8822c_rfk *k, uint32_t addr, unsigned size, uint32_t mask,
    uint32_t target, unsigned budget_us, unsigned settle_us, unsigned kind)
{
	unsigned now;

	if ((size != 1 && size != 4) || mask == 0 ||
	    (size == 1 && (mask & ~UINT32_C(0xff))) || budget_us == 0 ||
	    budget_us > 1300000 || settle_us >= budget_us ||
	    (target & ~(mask >> __builtin_ctz(mask))) != 0) {
		(void)rfk_error(k, EINVAL);
		return;
	}
	if ((!k->cleanup_mode && k->error != 0) ||
	    (k->cleanup_mode && k->cleanup_error != 0) || k->pending) {
		if ((k->cleanup_mode ? k->cleanup_error : k->error) == 0)
			(void)rfk_error(k, EPROTO);
		return;
	}
	if (kind < RFK_START6D || kind > RFK_COEF ||
	    (k->cleanup_mode && (kind != RFK_END6D || !k->handshake_started ||
	    k->handshake_end_sent || k->engine_pending || addr != REG_ARFR4 ||
	    size != 1 || mask != BIT_WL_RFK || target != 1))) {
		(void)rfk_error(k, EPROTO);
		return;
	}
	now = k->svc.clock_ticks(k->svc.cookie);
	k->report_addr = addr;
	k->report_size = size;
	k->report_mask = mask;
	k->report_target = target;
	k->report_kind = kind;
	k->engine_pending = kind >= RFK_GAPK;
	k->report_deadline = now + rfk_ticks(k, budget_us);
	k->report_not_before = now + (settle_us ? rfk_ticks(k, settle_us) : 0);
	k->non_target = k->raw_valid = k->last_raw = 0;
	k->arms[kind - 1]++;
	k->pending = 1;
}

/* An extra POST-trigger read is only freshness evidence. It cannot accept the
 * shot and does not replace its mandatory settle or later ordered sample. */
static void
rfk_capture(struct rtw8822c_rfk *k)
{
	uint32_t value;

	if (!k->pending)
		return;
	value = rfk_read(k, k->report_addr, k->report_size);
	if ((k->cleanup_mode ? k->cleanup_error : k->error) == 0) {
		k->last_raw = value;
		k->raw_valid = 1;
		value = (value & k->report_mask) >> __builtin_ctz(k->report_mask);
		if (value != k->report_target)
			k->non_target = 1;
	}
}

static int
rfk_report(struct rtw8822c_rfk *k, unsigned interval_us)
{
	uint32_t value;
	unsigned now;

	if (interval_us == 0) {
		(void)rfk_error(k, EINVAL);
		return 0;
	}
	for (;;) {
		if ((!k->cleanup_mode && k->error != 0) ||
		    (k->cleanup_mode && k->cleanup_error != 0))
			return 0;
		if (!k->pending) {
			(void)rfk_error(k, EPROTO);
			return 0;
		}
		now = k->svc.clock_ticks(k->svc.cookie);
		if ((int)(now - k->report_deadline) >= 0) {
			(void)rfk_error(k, ETIMEDOUT);
			return 0;
		}
		if ((int)(now - k->report_not_before) < 0) {
			if (rfk_wait(k, interval_us) != 0)
				return 0;
			continue;
		}
		value = rfk_read(k, k->report_addr, k->report_size);
		if ((!k->cleanup_mode && k->error != 0) ||
		    (k->cleanup_mode && k->cleanup_error != 0))
			return 0;
		k->last_raw = value;
		k->raw_valid = 1;
		value = (value & k->report_mask) >> __builtin_ctz(k->report_mask);
		if (value != k->report_target)
			k->non_target = 1;
		else if (k->non_target) {
			k->pending = 0;
			k->done[k->report_kind - 1]++;
			k->engine_pending = 0;
			if (k->report_kind == RFK_END6D)
				k->handshake_finished = 1;
			return 1;
		}
		if (rfk_wait(k, interval_us) != 0)
			return 0;
	}
}

/* Ordinary transport/RF-idle readiness, not a semantic RFK completion oracle. */
static int
rfk_value(struct rtw8822c_rfk *k, uint32_t addr, uint32_t mask,
    uint32_t target, unsigned interval_us, unsigned budget_us)
{
	unsigned deadline = k->svc.clock_ticks(k->svc.cookie) + rfk_ticks(k, budget_us);

	for (;;) {
		if ((int)(k->svc.clock_ticks(k->svc.cookie) - deadline) >= 0)
			return rfk_error(k, ETIMEDOUT);
		if (rfk_mask_read(k, addr, mask) == target &&
		    (k->cleanup_mode ? k->cleanup_error : k->error) == 0)
			return 0;
		if (rfk_wait(k, interval_us) != 0)
			return k->cleanup_mode ? k->cleanup_error : k->error;
	}
}

static int
rfk_rf_idle(struct rtw8822c_rfk *k, int delay_after_read)
{
	unsigned deadline = k->svc.clock_ticks(k->svc.cookie) + rfk_ticks(k, 5000);
	uint32_t a, b;

	for (;;) {
		if ((int)(k->svc.clock_ticks(k->svc.cookie) - deadline) >= 0)
			return rfk_error(k, ETIMEDOUT);
		a = rfk_read_rf(k, 0, 0, BIT_RF_MODE);
		b = rfk_read_rf(k, 1, 0, BIT_RF_MODE);
		if (delay_after_read)
			rfk_delay(k, 2);
		if (k->error != 0)
			return k->error;
		if (a != 2 && b != 2)
			return 0;
		if (rfk_wait(k, 2) != 0)
			return k->error;
	}
}

static int
rfk_check_report(struct rtw8822c_rfk *k, uint32_t addr, uint32_t mask,
    uint32_t target)
{

	if (!k->pending || k->report_addr != addr || k->report_mask != mask ||
	    k->report_target != target || k->report_size != 4) {
		(void)rfk_error(k, EPROTO);
		return 0;
	}
	return rfk_report(k, 10);
}

/* LTE is a register transport, never an RFK semantic-completion decoder.
 * Keep the primary readiness wait and ordered read/modify/write even when the
 * mask is full. A failed read cannot supply a fabricated zero to a write. */
static uint32_t
rfk_lte_read(struct rtw8822c_rfk *k, uint16_t addr)
{

	if (rfk_value(k, 0x1700, BIT(29), 1, 10, 10000) != 0)
		return 0;
	rfk_write(k, 0x1700, 4, UINT32_C(0x800f0000) | addr);
	return rfk_read(k, 0x1708, 4);
}

static void
rfk_lte_write(struct rtw8822c_rfk *k, uint16_t addr, uint32_t mask,
    uint32_t value)
{
	uint32_t old;

	if (mask == 0 || (value & ~(mask >> __builtin_ctz(mask))) != 0) {
		(void)rfk_error(k, EINVAL);
		return;
	}
	old = rfk_lte_read(k, addr);
	if (rfk_value(k, 0x1700, BIT(29), 1, 10, 10000) != 0)
		return;
	rfk_write(k, 0x1704, 4,
	    (old & ~mask) | ((value << __builtin_ctz(mask)) & mask));
	rfk_write(k, 0x1700, 4, UINT32_C(0xc00f0000) | addr);
}

static void
rfk_load_dpk(struct rtw8822c_rfk *k, const struct rtw8822c_table *table)
{
	const uint32_t *p = table->data;
	unsigned i;

	if (table->kind != RTW8822C_TBL_DPK || table->count % 3 != 0) {
		(void)rfk_error(k, EINVAL);
		return;
	}
	for (i = 0; i < table->count; i += 3)
		rfk_mask_write(k, p[i], p[i + 1], p[i + 2]);
}

static unsigned
rfk_fls(uint32_t value)
{

	KASSERT(value != 0);
	return 31U - (unsigned)__builtin_clz(value);
}

#define rtw_read8(k, a) ((uint8_t)rfk_read((k), (a), 1))
#define rtw_read32(k, a) rfk_read((k), (a), 4)
#define rtw_write8(k, a, v) rfk_write((k), (a), 1, (v))
#define rtw_write32(k, a, v) rfk_write((k), (a), 4, (v))
#define rtw_read32_mask rfk_mask_read
#define rtw_write32_mask rfk_mask_write
#define rtw_read_rf rfk_read_rf
#define rtw_write_rf rfk_write_rf
#define rtw_coex_read_indirect_reg rfk_lte_read
#define rtw_coex_write_indirect_reg rfk_lte_write
#define rtw_load_table rfk_load_dpk
#define rtw_restore_reg rfk_restore_regs
#define check_hw_ready rfk_check_report
#define __fls rfk_fls
#define udelay(us) rfk_delay(rtwdev, (us))
#define fsleep(us) rfk_delay(rtwdev, (us))
#define msleep(ms) rfk_delay(rtwdev, (ms) * 1000U)
#define usleep_range(lo, hi) rfk_delay(rtwdev, (hi))
#define test_bit(n, a) (((a)[0] & BIT(n)) != 0)
#define set_bit(n, a) ((a)[0] |= BIT(n))
#define clear_bit(n, a) ((a)[0] &= ~BIT(n))
#include <dev/ic/rtw8822c_rfk_phases.inc>

int
rtw8822c_rfk_begin(struct rtw88_softc *sc,
    const struct rtw8822c_rfk_services *svc)
{
	struct rtw8822c_rfk *k;
	unsigned previous;
	int error;

	if (svc == NULL || svc->enter == NULL || svc->leave == NULL ||
	    svc->wait == NULL || svc->clock_ticks == NULL || svc->ticks_per_second == 0 ||
	    svc->ticks_per_second > 1000000)
		return EINVAL;
	if (!sc->sc_started || !sc->sc_running || sc->sc_stop_requested ||
	    !sc->sc_rf_enabled || sc->sc_rfe > 6 || sc->sc_cut > 6)
		return ENXIO;
	if (rtw8822c_rfk_active(sc)) {
		/* The ordinary post-completion mailbox may have yielded Giant.
		 * Revalidate the original lease without new IO, token rebinding
		 * or extending RFK's deadline to ordinary mailbox latency. */
		if (sc->sc_rfk->generation != sc->sc_generation ||
		    sc->sc_rfk->token != atomic_load_acquire(&rfk_serial) ||
		    !sc->sc_resources_ready)
			return ESTALE;
		return atomic_load_acquire(&rfk_global) == RFK_BUSY ? 0 : EBUSY;
	}
	/* NetBSD atomic_cas_uint is atomic but supplies no implicit ordering
	 * for the context published by a previous owner. Pair full barriers
	 * here with drop/quarantine and serial publication below. */
	membar_sync();
	previous = atomic_cas_uint(&rfk_global, 0, RFK_BUSY);
	membar_sync();
	if (previous != 0)
		return EBUSY;
	/* Serial wrap cannot identify a fresh owner. Only this exclusive owner
	 * writes the counter; the global CAS is also its lifetime barrier. */
	if (atomic_load_acquire(&rfk_serial) == UINT64_MAX) {
		atomic_store_release(&rfk_global, RFK_QUARANTINE);
		return EOVERFLOW;
	}
	k = sc->sc_rfk;
	if (k == NULL) {
		k = kmem_zalloc(sizeof(*k), KM_SLEEP);
		k->sc = sc;
		sc->sc_rfk = k;
	}
	k->svc = *svc;
	membar_sync();
	k->token = atomic_inc_64_nv(&rfk_serial);
	membar_sync();
	k->generation = sc->sc_generation;
	k->started_at = svc->clock_ticks(svc->cookie);
	k->new_deadline = k->started_at + 6U * svc->ticks_per_second;
	k->cleanup_deadline = k->started_at + 8U * svc->ticks_per_second;
	k->packet_started = k->completed = k->gnt_valid = 0;
	k->active = 1;
	k->cleanup_mode = k->mutated = k->canceled = 0;
	k->pending = k->non_target = k->nsaved = 0;
	k->outer_valid = k->outer_restoring = 0;
	k->report_kind = k->engine_pending = 0;
	k->handshake_started = k->handshake_end_sent = k->handshake_finished = 0;
	k->operations = k->delay_us = k->sleeps = 0;
	k->raw_valid = k->last_raw = 0;
	memset(k->arms, 0, sizeof(k->arms));
	memset(k->done, 0, sizeof(k->done));
	k->error = k->cleanup_error = 0;
	k->hal.rf_path_num = DPK_RF_PATH_NUM;
	/* Native successful bootstrap already loaded the pinned cal-init table.
	 * No runtime table-reload shortcut or thermal tracking worker is enabled. */
	k->dm_info.dpk_info.is_dpk_pwr_on = true;
	/* A new firmware generation is not proof that all 110 RF gain LUT
	 * originals were reloaded. Never recapture our committed GAPK gains
	 * under the name "original". This first experiment rejects that retry
	 * before any IO; only a new controlled cold-reset kernel trial can
	 * establish another first baseline. Same-generation reuse is exact. */
	if (k->dm_info.gapk.read_txgain == 0 &&
	    k->baseline_generation != sc->sc_generation) {
		memset(&k->dm_info.gapk, 0, sizeof(k->dm_info.gapk));
		k->baseline_generation = sc->sc_generation;
		k->baseline_rfe = sc->sc_rfe;
		k->baseline_cut = sc->sc_cut;
	}
	error = k->baseline_generation != sc->sc_generation ||
	    k->baseline_rfe != sc->sc_rfe ||
	    k->baseline_cut != sc->sc_cut ? ESTALE : 0;
	if (error == 0)
		error = rtw8822c_efuse_decode(sc->sc_efuse_phys,
	    sizeof(sc->sc_efuse_phys), &k->efuse);
	if (error == 0 && k->efuse.power_track_type >= 4 &&
	    k->efuse.power_track_type <= 7)
		error = EOPNOTSUPP;
	if (error != 0) {
		k->error = error;
		k->active = 0;
		rfk_drop_owner();
	}
	return error;
}

/* One old exclusive owner may finish an already-started 6d handshake. A
 * changed requested epoch cannot turn this into a command for its successor. */
static int
rfk_handshake(struct rtw8822c_rfk *k, int start)
{
	unsigned deadline = k->svc.clock_ticks(k->svc.cookie) + rfk_ticks(k, 1000000);
	uint8_t payload = start ? 1 : 0;
	int ready, error;

	if (start && rfk_value(k, REG_PMC_DBG_CTRL1,
	    BITS_PMC_BT_IQK_STS, 0, 20, 600000) != 0)
		return k->error;
	for (;;) {
		if ((int)(k->svc.clock_ticks(k->svc.cookie) - deadline) >= 0)
			return rfk_error(k, ETIMEDOUT);
		if (rfk_enter(k) != 0)
			return k->cleanup_mode ? k->cleanup_error : k->error;
		error = rtw88_fw_mailbox_begin(k->sc, 1, &ready);
		if (error == 0 && ready) {
			rfk_arm(k, REG_ARFR4, 1, BIT_WL_RFK, 1,
			    100000, 0, start ? RFK_START6D : RFK_END6D);
			/* Set before the doorbell can land, including a write error. */
			if (start)
				k->handshake_started = 1;
			else
				k->handshake_end_sent = 1;
			error = rtw88_fw_command(k->sc, 0x6d, &payload, 1);
			error = rtw88_fw_mailbox_end(k->sc, error);
		}
		k->svc.leave(k->svc.cookie);
		if (error != 0)
			return rfk_error(k, error);
		if (ready)
			break;
		if (rfk_wait(k, 50) != 0)
			return k->cleanup_mode ? k->cleanup_error : k->error;
	}
	rfk_capture(k);
	return rfk_report(k, 20) ? 0 :
	    (k->cleanup_mode ? k->cleanup_error : k->error);
}

static int
rfk_packet_poll(struct rtw8822c_rfk *k)
{
	int error;

	for (;;) {
		if ((int)(k->svc.clock_ticks(k->svc.cookie) - k->packet_deadline) >= 0)
			return rfk_error(k, ETIMEDOUT);
		if (rfk_enter(k) != 0)
			return k->cleanup_mode ? k->cleanup_error : k->error;
		error = rtw88_fw_iqk_poll(k->sc);
		k->svc.leave(k->svc.cookie);
		if (error == 0) {
			k->packet_started = 0;
			return 0;
		}
		if (error != EAGAIN)
			return rfk_error(k, error);
		if (rfk_wait(k, 50) != 0)
			return k->cleanup_mode ? k->cleanup_error : k->error;
	}
}

static void
rfk_iqk(struct rtw8822c_rfk *k)
{
	int error;

	rfk_arm(k, REG_RPT_CIP, 1, MASKBYTE0, 0xaa,
	    1300000, 0, RFK_IQK);
	if (rfk_enter(k) != 0)
		return;
	/* Original one-second transport and 300ms semantic budgets are both
	 * bounded by the transaction deadline; neither restarts after cancel. */
	k->packet_deadline = k->svc.clock_ticks(k->svc.cookie) + rfk_ticks(k, 1000000);
	k->packet_started = 1;
	error = rtw88_fw_iqk_begin(k->sc);
	k->svc.leave(k->svc.cookie);
	if (rfk_error(k, error) != 0)
		return;
	rfk_capture(k);
	if (rfk_packet_poll(k) != 0)
		return;
	{
		unsigned semantic = k->svc.clock_ticks(k->svc.cookie) + rfk_ticks(k, 300000);
		if ((int)(semantic - k->report_deadline) < 0)
			k->report_deadline = semantic;
	}
	if (rfk_report(k, 20000))
		rfk_write(k, REG_IQKSTAT, 1, 0);
}

static void
rfk_dpk(struct rtw8822c_rfk *k)
{
	struct rtw_backup_info bb[18];
	uint32_t rf_backup[7][2];
	uint32_t bb_reg[18] = {0x520, 0x820, 0x824, 0x1c3c, 0x1d58, 0x1864,
	    0x4164, 0x180c, 0x410c, 0x186c, 0x416c, 0x1a14, 0x1e70,
	    0x80c, 0x1d70, 0x1e7c, 0x18a4, 0x41a4};
	uint32_t rf_reg[7] = {0x0, 0x1a, 0x55, 0x63, 0x87, 0x8f, 0xde};
	unsigned path;

	if (k->error != 0)
		return;
	rtw8822c_dpk_information(k);
	rtw8822c_dpk_backup_registers(k, bb_reg, __arraycount(bb_reg), bb);
	rtw8822c_dpk_backup_rf_registers(k, rf_reg, rf_backup);
	rtw8822c_dpk_mac_bb_setting(k);
	rtw8822c_dpk_afe_setting(k, true);
	rtw8822c_dpk_pre_setting(k);
	rtw8822c_dpk_result_reset(k);
	rtw8822c_dpk_path_select(k);
	rtw8822c_dpk_afe_setting(k, false);
	rtw8822c_dpk_enable_disable(k);
	rtw8822c_dpk_reload_rf_registers(k, rf_reg, rf_backup);
	/* This is a new RXBB calibration trigger, not cancellation cleanup. */
	for (path = 0; path < DPK_RF_PATH_NUM && k->error == 0; path++)
		rtw8822c_dpk_rxbb_dc_cal(k, path);
	rtw8822c_dpk_restore_registers(k, __arraycount(bb_reg), bb);
	if (k->error == 0 && k->dm_info.dpk_info.dpk_path_ok[0] != 3)
		(void)rfk_error(k, EIO);
}

/* Pinned rtw8822c_set_channel_rf and native channel_rf program exactly
 * these RF18 fields for 20 MHz. Unrelated/reserved bits are not compared.
 * This read-only precondition precedes power-save, pause and calibration
 * writes; a failed channel selection cannot become an RFK transaction. */
static int
rfk_channel_current(struct rtw8822c_rfk *k)
{
	const uint32_t mask = UINT32_C(0x10300) | UINT32_C(0xff) |
	    UINT32_C(0x60000) | UINT32_C(0x3000);
	unsigned ch = k->sc->sc_channel;
	uint32_t expected = (ch & 0xff) | UINT32_C(0x3000);
	uint32_t a, b;

	if (ch > 14)
		expected |= UINT32_C(0x10100);
	if (ch >= 149 && ch <= 177)
		expected |= UINT32_C(0x40000);
	else if (ch >= 100 && ch <= 144)
		expected |= UINT32_C(0x20000);
	a = rfk_read_rf(k, 0, RF_CFGCH, RFREG_MASK);
	b = rfk_read_rf(k, 1, RF_CFGCH, RFREG_MASK);
	if (k->error != 0)
		return k->error;
	if ((a & mask) != expected || (b & mask) != expected)
		return rfk_error(k, EPROTO);
	return 0;
}

int
rtw8822c_rfk_run(struct rtw88_softc *sc)
{
	struct rtw8822c_rfk *k = sc->sc_rfk;
	uint32_t value;

	if (k == NULL || !k->active)
		return EINVAL;
	if (k->completed)
		return 0;
	if (rfk_channel_current(k) != 0)
		return k->error;
	rtw8822c_rfk_power_save(k, false);
	value = rfk_read(k, REG_TX_PTCL_CTRL, 4);
	if (k->error == 0) {
		k->outer_tx[0] = value;
		k->outer_valid |= 1;
		rfk_save(k, REG_TX_PTCL_CTRL, value, 0, 0);
	}
	value = rfk_read(k, REG_TX_FIFO, 4);
	if (k->error == 0) {
		k->outer_tx[1] = value;
		k->outer_valid |= 2;
		rfk_save(k, REG_TX_FIFO, value, 0, 0);
	}
	if (rfk_handshake(k, 1) == 0)
		rtw8822c_txgapk(k);
	if (k->error == 0)
		(void)rfk_handshake(k, 0);
	if (k->error == 0)
		rfk_iqk(k);
	if (k->error == 0)
		rfk_dpk(k);
	if (k->error == 0)
		rtw8822c_rfk_power_save(k, true);
	if (k->error == 0 && k->outer_valid == 3 && !k->pending &&
	    !k->engine_pending && !k->packet_started && k->handshake_finished) {
		/* Keep the primary full-word restore order and exact originals. */
		k->outer_restoring = 1;
		rfk_write(k, REG_TX_PTCL_CTRL, 4, k->outer_tx[0]);
		rfk_write(k, REG_TX_FIFO, 4, k->outer_tx[1]);
	}
	if (k->error != 0)
		return k->error;
	if (!k->outer_restoring)
		return rfk_error(k, EPROTO);
	/* Account for time and cancellation during the last possibly-landed
	 * restore, before publishing the completed descriptor to the worker. */
	if (rfk_enter(k) != 0)
		return k->error;
	k->svc.leave(k->svc.cookie);
	k->completed = 1;
	return 0;
}

int
rtw8822c_rfk_retire(struct rtw88_softc *sc)
{
	struct rtw8822c_rfk *k = sc->sc_rfk;
	unsigned i;
	int error;

	if (k == NULL || !k->active)
		return 0;
	if (!k->completed && k->error == 0)
		(void)rfk_error(k, ECANCELED);
	if (k->error != 0 && k->mutated) {
		/* Only old-token completion/polls and valid source restores follow
		 * cancellation. Never start another IQK/DPK/RXBB shot here. */
		k->cleanup_mode = 1;
		if (k->packet_started)
			(void)rfk_packet_poll(k);
		if (k->pending && k->cleanup_error == 0)
			(void)rfk_report(k, 20);
		if (!k->pending && !k->engine_pending && !k->packet_started &&
		    k->cleanup_error == 0) {
			if (k->gnt_valid) {
				rfk_lte_write(k, 0x38, MASKDWORD, k->dm_info.dpk_info.gnt_value);
				rfk_write(k, 0x70, 4, k->dm_info.dpk_info.gnt_control);
				if (k->cleanup_error == 0)
					k->gnt_valid = 0;
			}
			for (i = 0; i < k->nsaved; i++) {
				const struct rfk_saved *s = &k->saved[i];
				if (!s->valid || (!s->rf &&
				    (s->addr == REG_TX_PTCL_CTRL || s->addr == REG_TX_FIFO)))
					continue;
				if (s->rf)
					rfk_write_rf(k, s->path, s->addr, MASK20BITS, s->value);
				else
					rfk_write(k, s->addr, 4, s->value);
			}
			if (k->handshake_started && !k->handshake_end_sent &&
			    k->cleanup_error == 0)
				(void)rfk_handshake(k, 0);
		}
		/* Even idle status does not prove full operational recovery without
		 * the primary RXBB trigger. Never restore outer TX on this path.
		 * A prior failed final restore can already have landed; require cold
		 * reset rather than claim that TX is still physically paused. */
		rfk_quarantine(k);
	}
	error = k->error != 0 ? k->error : k->cleanup_error;
	k->active = 0;
	rfk_drop_owner();
	return error;
}

void
rtw8822c_rfk_progress(struct rtw88_softc *sc, struct rtw88_rfk_diag *out)
{
	const struct rtw8822c_rfk *k = sc->sc_rfk;

	memset(out, 0, sizeof(*out));
	out->global = atomic_load_acquire(&rfk_global);
	if (k == NULL)
		return;
	out->token = k->token;
	out->generation = k->generation;
	out->baseline_generation = k->baseline_generation;
	out->flags = k->active | (k->completed << 1) | (k->pending << 2) |
	    (k->engine_pending << 3) | (k->cleanup_mode << 4) | (k->canceled << 5) |
	    (k->handshake_started << 6) | (k->handshake_end_sent << 7) |
	    (k->handshake_finished << 8);
	out->kind = k->report_kind;
	out->addr = k->report_addr;
	out->size = k->report_size;
	out->raw = k->last_raw;
	out->raw_valid = k->raw_valid;
	out->non_target = k->non_target;
	memcpy(out->arms, k->arms, sizeof(out->arms));
	memcpy(out->done, k->done, sizeof(out->done));
	out->operations = k->operations;
	out->requested_delay_us = k->delay_us;
	out->sleeps = k->sleeps;
	out->error = k->error;
	out->cleanup_error = k->cleanup_error;
}

int
rtw8822c_rfk_free(struct rtw88_softc *sc)
{

	if (rtw8822c_rfk_active(sc))
		return EBUSY;
	if (sc->sc_rfk != NULL) {
		/* Clean DMA teardown is not a reload of the persistent gain LUT.
		 * Discarding populated originals must not let a fresh attach in
		 * this kernel recapture our own GAPK gains as another baseline. */
		if (sc->sc_rfk->dm_info.gapk.read_txgain != 0)
			rfk_quarantine(sc->sc_rfk);
		kmem_free(sc->sc_rfk, sizeof(*sc->sc_rfk));
		sc->sc_rfk = NULL;
	}
	/* Neither a new heap nor verified DMA teardown clears quarantine. */
	return 0;
}

/* The full-chain implementation below owns every real phase and restoration.
 * No helper or partial phase may publish a successful transaction receipt. */

#endif /* RTW88_RFK_EXPERIMENT */
