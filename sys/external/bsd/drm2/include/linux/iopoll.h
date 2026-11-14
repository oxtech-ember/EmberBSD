/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native implementation of the interface of Linux 6.18
 * include/linux/iopoll.h (GPL-2.0; no source copied).
 *
 * poll_timeout_us(op, cond, sleep_us, timeout_us, sleep_before_op) runs the
 * statement op, then tests cond, until cond holds (result 0) or timeout_us
 * microseconds have passed (result -ETIMEDOUT; op and cond are evaluated one
 * more time after the deadline, so a late success is not lost).  timeout_us
 * of 0 waits forever.  sleep_us of 0 spins (cpu_relax); otherwise it waits
 * about sleep_us between tries (usleep_range here; udelay in the _atomic
 * form, which is safe in atomic context).  sleep_before_op waits once before
 * the first try.
 *
 * read_poll_timeout(op, val, cond, sleep_us, timeout_us, sleep_before_read,
 * args...) stores op(args...) in val.  readx_poll_timeout*() and the
 * readb/readw/readl/readq_poll_timeout*() forms read an address; the latter
 * need the platform's readb()...readq() at the point of use (the layer does
 * not define them in a header yet).
 */

#ifndef _LINUX_IOPOLL_H_
#define _LINUX_IOPOLL_H_

#include <sys/errno.h>

#include <asm/processor.h>

#include <linux/delay.h>
#include <linux/ktime.h>

#define	__poll_timeout(OP, COND, SLEEP_US, TIMEOUT_US, BEFORE, WAIT) ({	\
	uint64_t __pt_timeout_us = (TIMEOUT_US);			\
	unsigned long __pt_sleep_us = (SLEEP_US);			\
	ktime_t __pt_end = ktime_add_us(ktime_get(),			\
	    (int64_t)__pt_timeout_us);					\
	int __pt_ret;							\
									\
	if ((BEFORE) && __pt_sleep_us)					\
		WAIT(__pt_sleep_us);					\
	for (;;) {							\
		OP;							\
		if (COND) {						\
			__pt_ret = 0;					\
			break;						\
		}							\
		if (__pt_timeout_us &&					\
		    ktime_to_ns(ktime_get()) > ktime_to_ns(__pt_end)) {	\
			OP;						\
			__pt_ret = (COND) ? 0 : -ETIMEDOUT;		\
			break;						\
		}							\
		if (__pt_sleep_us)					\
			WAIT(__pt_sleep_us);				\
		else							\
			cpu_relax();					\
	}								\
	__pt_ret;							\
})

#define	__poll_usleep(US)	usleep_range(((US) >> 2) + 1, (US))
#define	__poll_udelay(US)	udelay(US)

#define	poll_timeout_us(OP, COND, SLEEP_US, TIMEOUT_US, BEFORE)		\
	__poll_timeout(OP, COND, SLEEP_US, TIMEOUT_US, BEFORE, __poll_usleep)

#define	poll_timeout_us_atomic(OP, COND, DELAY_US, TIMEOUT_US, BEFORE)	\
	__poll_timeout(OP, COND, DELAY_US, TIMEOUT_US, BEFORE, __poll_udelay)

#define	read_poll_timeout(OP, VAL, COND, SLEEP_US, TIMEOUT_US, BEFORE, ...) \
	poll_timeout_us((VAL) = OP(__VA_ARGS__), COND, SLEEP_US,	\
	    TIMEOUT_US, BEFORE)

#define	read_poll_timeout_atomic(OP, VAL, COND, DELAY_US, TIMEOUT_US,	\
    BEFORE, ...)							\
	poll_timeout_us_atomic((VAL) = OP(__VA_ARGS__), COND, DELAY_US,	\
	    TIMEOUT_US, BEFORE)

#define	readx_poll_timeout(OP, ADDR, VAL, COND, SLEEP_US, TIMEOUT_US)	\
	read_poll_timeout(OP, VAL, COND, SLEEP_US, TIMEOUT_US, false, ADDR)

#define	readx_poll_timeout_atomic(OP, ADDR, VAL, COND, DELAY_US, TIMEOUT_US) \
	read_poll_timeout_atomic(OP, VAL, COND, DELAY_US, TIMEOUT_US,	\
	    false, ADDR)

#define	readb_poll_timeout(ADDR, VAL, COND, DELAY_US, TIMEOUT_US)	\
	readx_poll_timeout(readb, ADDR, VAL, COND, DELAY_US, TIMEOUT_US)
#define	readb_poll_timeout_atomic(ADDR, VAL, COND, DELAY_US, TIMEOUT_US) \
	readx_poll_timeout_atomic(readb, ADDR, VAL, COND, DELAY_US, TIMEOUT_US)
#define	readw_poll_timeout(ADDR, VAL, COND, DELAY_US, TIMEOUT_US)	\
	readx_poll_timeout(readw, ADDR, VAL, COND, DELAY_US, TIMEOUT_US)
#define	readw_poll_timeout_atomic(ADDR, VAL, COND, DELAY_US, TIMEOUT_US) \
	readx_poll_timeout_atomic(readw, ADDR, VAL, COND, DELAY_US, TIMEOUT_US)
#define	readl_poll_timeout(ADDR, VAL, COND, DELAY_US, TIMEOUT_US)	\
	readx_poll_timeout(readl, ADDR, VAL, COND, DELAY_US, TIMEOUT_US)
#define	readl_poll_timeout_atomic(ADDR, VAL, COND, DELAY_US, TIMEOUT_US) \
	readx_poll_timeout_atomic(readl, ADDR, VAL, COND, DELAY_US, TIMEOUT_US)
#define	readq_poll_timeout(ADDR, VAL, COND, DELAY_US, TIMEOUT_US)	\
	readx_poll_timeout(readq, ADDR, VAL, COND, DELAY_US, TIMEOUT_US)
#define	readq_poll_timeout_atomic(ADDR, VAL, COND, DELAY_US, TIMEOUT_US) \
	readx_poll_timeout_atomic(readq, ADDR, VAL, COND, DELAY_US, TIMEOUT_US)

#endif	/* _LINUX_IOPOLL_H_ */
