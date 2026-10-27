/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _DEV_IC_RTW88_CALIBRATION_H_
#define _DEV_IC_RTW88_CALIBRATION_H_

#include <sys/types.h>
#ifndef _KERNEL
#include <stdint.h>
#endif
struct rtw88_softc;
struct rtw88_rfk_diag;

/* enter succeeds with the bus interlock held; wait is entered unlocked.
 * Cleanup validates the old exclusive owner. Successful waits recapture the
 * current peer/profile. The monotonic clock retains all 64 bits. */
struct rtw88_calibration_services {
	void *cookie;
	int (*enter)(void *, int cleanup);
	void (*leave)(void *);
	int (*wait)(void *, unsigned min_us, int cleanup);
	uint64_t (*clock_ticks)(void *);
	unsigned ticks_per_second;
};

struct rtw88_calibration_ops {
	int (*blocked)(const struct rtw88_softc *);
	int (*active)(const struct rtw88_softc *);
	int (*begin)(struct rtw88_softc *, const struct rtw88_calibration_services *);
	int (*validate)(struct rtw88_softc *);
	int (*run)(struct rtw88_softc *);
	int (*retire)(struct rtw88_softc *);
	int (*seal)(struct rtw88_softc *);
	void (*progress)(struct rtw88_softc *, struct rtw88_rfk_diag *);
};

/* Ownership queries are false when the chip has no calibration owner.
 * Diagnostic operations return EOPNOTSUPP when the service is absent. */
int rtw88_calibration_blocked(const struct rtw88_softc *);
int rtw88_calibration_active(const struct rtw88_softc *);
int rtw88_calibration_begin(struct rtw88_softc *, const struct rtw88_calibration_services *);
int rtw88_calibration_validate(struct rtw88_softc *);
int rtw88_calibration_run(struct rtw88_softc *);
int rtw88_calibration_retire(struct rtw88_softc *);
int rtw88_calibration_seal(struct rtw88_softc *);
int rtw88_calibration_progress(struct rtw88_softc *, struct rtw88_rfk_diag *);
#endif
