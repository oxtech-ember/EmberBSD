/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 Anton and EmberBSD contributors. */
#ifndef _DEV_SDMMC_RTW88_SDIO_IO_H_
#define _DEV_SDMMC_RTW88_SDIO_IO_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

#define RTW88_SDIO_MAX_TRANSFER 4096U
#define RTW88_SDIO_MAX_REQUEST 512U

/* Success supplies a response. Callers serialize the bus and its scratch. */
struct rtw88_sdio_ops {
	int (*command52)(void *, uint32_t, uint32_t *);
	int (*command53)(void *, uint32_t, void *, size_t, uint32_t *);
	void (*wait_ms)(void *, unsigned int);
	void *cookie;
};

enum rtw88_sdio_domain { RTW88_SDIO_LOCAL, RTW88_SDIO_MAC };
enum rtw88_sdio_restore_policy {
	RTW88_SDIO_RESTORE_OWNED_BIT,
	RTW88_SDIO_RESTORE_DIAGNOSTIC_SNAPSHOT
};

struct rtw88_sdio_bus {
	const struct rtw88_sdio_ops *ops;
	unsigned int block_size;
	size_t max_request;
	uint8_t *read_scratch;
	size_t read_scratch_len;
	bool function_claimed, function_ready, cmd53_enabled;
};

/* Keep this state until release succeeds, including after an uncertain write. */
struct rtw88_sdio_function_state {
	uint8_t original, ready;
	bool changed, owns_enable;
};

int rtw88_sdio_cmd52(const struct rtw88_sdio_ops *, unsigned int, uint32_t,
    bool, uint8_t *);
int rtw88_sdio_transfer(struct rtw88_sdio_bus *, enum rtw88_sdio_domain,
    uint32_t, void *, size_t, bool);
int rtw88_sdio_set_cmd53(struct rtw88_sdio_bus *, bool);
int rtw88_sdio_function_acquire(struct rtw88_sdio_bus *,
    struct rtw88_sdio_function_state *, bool);
int rtw88_sdio_function_release(struct rtw88_sdio_bus *,
    struct rtw88_sdio_function_state *, enum rtw88_sdio_restore_policy);

#endif /* _DEV_SDMMC_RTW88_SDIO_IO_H_ */
