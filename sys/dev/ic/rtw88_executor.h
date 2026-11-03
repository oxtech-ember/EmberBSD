/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW88_EXECUTOR_H_
#define _DEV_IC_RTW88_EXECUTOR_H_

#include <dev/ic/rtw88_async.h>

struct rtw88_softc;
struct rtw88_executor;

#define RTW88_EXEC_CONTROL 1U
#define RTW88_EXEC_TX 2U
#define RTW88_EXEC_RX 4U
#define RTW88_EXEC_STOP 8U
#define RTW88_EXEC_BOOTSTRAP 16U

typedef void (*rtw88_executor_service_fn)(struct rtw88_softc *, uint32_t);

/* The attach owner pins sc. Close producers before treating drain as a
 * lifetime fence or calling destroy.
 * A service runs without Giant and without the executor's short interlock.
 * A self drain/destroy returns EDEADLK and leaves the executor intact. */
int rtw88_executor_create(struct rtw88_softc *, rtw88_executor_service_fn);
void rtw88_executor_schedule(struct rtw88_softc *, uint32_t);
/* Process context; caller pins executor lifetime. True only on its active worker;
 * this query neither obtains bus ownership nor grants preparation access.
 * A stopping worker retains its identity through old-owner cleanup. */
int rtw88_executor_owned(struct rtw88_softc *);
int rtw88_executor_drain(struct rtw88_softc *);
int rtw88_executor_destroy(struct rtw88_softc *);

/* One copied diagnostic reservation. No caller pointer reaches the worker.
 * The frontend pins sc through this process-context call. Timeout cancels
 * an unsent request or detaches its waiter from an active request. */
int rtw88_executor_call(struct rtw88_softc *,
    const struct rtw88_control_snapshot *, struct rtw88_control_result *,
    unsigned);

/* Only the service owner takes or completes diagnostic work. The serial
 * distinguishes equal-epoch requests and rejects late duplicate results. */
int rtw88_executor_diag_take(struct rtw88_softc *,
    struct rtw88_control_snapshot *, uint64_t *);
int rtw88_executor_diag_done(struct rtw88_softc *, uint64_t,
    const struct rtw88_control_result *);

/* Native service entry; a bus wrapper supplies the control/IRQ barrier. */
void rtw88_async_service(struct rtw88_softc *, uint32_t);

#endif
