/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _DEV_IC_RTW8723D_BOOT_H_
#define _DEV_IC_RTW8723D_BOOT_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

struct rtw88_io;

/* Logical chip address tag; SDIO adapters decode it to their LOCAL domain. */
#define RTW8723D_SDIO_LOCAL_BASE 0x10250000U

struct rtw8723d_identity {
	uint32_t sys_cfg1;
	uint8_t cut;
	uint8_t rf_paths;
};

struct rtw8723d_fw {
	const uint8_t *payload;
	size_t payload_size;
	uint16_t version;
};

struct rtw8723d_boot_ops {
	int (*write_window)(void *, uint32_t, const uint8_t *, size_t);
	void (*wait_ms)(void *, unsigned);
	bool (*cancelled)(void *);
	void *cookie;
};

/*
 * The caller owns the chip/function and serializes all operations. These
 * routines do not control board GPIOs or regulators. Use CMD52 during chip
 * power transitions; enable CMD53 only after power_on succeeds. Disable
 * CMD53 before power_off. Power-off is bounded and ignores cancellation.
 * Power-on includes legacy pre/post MAC system setup. Firmware boot requires
 * that setup to have completed; it does not initialize RF or network state.
 * An already-active MAC gets one bounded off/on recovery. Already-off release
 * succeeds without replaying a power sequence.
 * Errors stick in io. Failure cleanup uses a separate I/O context and keeps
 * the original errno; the caller must also unwind its function ownership.
 */
int rtw8723d_identify(struct rtw88_io *, struct rtw8723d_identity *);
int rtw8723d_power_on(struct rtw88_io *, const struct rtw8723d_boot_ops *);
int rtw8723d_power_off(struct rtw88_io *, const struct rtw8723d_boot_ops *);
int rtw8723d_fw_parse(const uint8_t *, size_t, struct rtw8723d_fw *);
int rtw8723d_fw_boot(struct rtw88_io *, const struct rtw8723d_fw *,
    const struct rtw8723d_boot_ops *);

#endif /* _DEV_IC_RTW8723D_BOOT_H_ */
