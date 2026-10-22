/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Internal actual-worker RFK ownership; no userspace or HT readiness ABI. */
#ifndef _DEV_IC_RTW8822C_RFK_H_
#define _DEV_IC_RTW8822C_RFK_H_

struct rtw88_softc;
struct rtw8822c_rfk;
struct rtw88_rfk_diag;

/* enter returns with PCI held on success; wait is always entered unlocked.
 * cleanup validates the old exclusive owner, not the new requested epoch.
 * Each successful wait recaptures the complete current stage-appropriate
 * peer/profile before allowing more new work. No borrowed node survives it. */
struct rtw8822c_rfk_services {
	void *cookie;
	int (*enter)(void *, int cleanup);
	void (*leave)(void *);
	int (*wait)(void *, unsigned min_us, int cleanup);
	unsigned (*clock_ticks)(void *);
	unsigned ticks_per_second;
};

int rtw8822c_rfk_blocked(void);
int rtw8822c_rfk_active(const struct rtw88_softc *);
int rtw8822c_rfk_begin(struct rtw88_softc *,
    const struct rtw8822c_rfk_services *);
int rtw8822c_rfk_run(struct rtw88_softc *);
int rtw8822c_rfk_retire(struct rtw88_softc *);
int rtw8822c_rfk_free(struct rtw88_softc *);
/* Same process owner, without PCI. Copy to a PCI-protected mirror before yield. */
void rtw8822c_rfk_progress(struct rtw88_softc *, struct rtw88_rfk_diag *);

#endif /* _DEV_IC_RTW8822C_RFK_H_ */
