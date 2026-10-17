/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original native implementation, AI-assisted. */
#ifndef _DEV_IC_CIXVPU_RESOURCES_H_
#define _DEV_IC_CIXVPU_RESOURCES_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdint.h>
#endif

struct cixvpu_mem_resource {
	uint64_t base;
	uint64_t translated;
	uint64_t length;
};

struct cixvpu_resources {
	struct cixvpu_mem_resource mem[2];
	unsigned int nmem;
	unsigned int nirq;
	unsigned int other;
	uint32_t irq;
	uint32_t irq_trigger;
	int cca_present;
	uint64_t cca;
};

enum cixvpu_resource_status {
	CIXVPU_RESOURCE_OK,
	CIXVPU_RESOURCE_COUNT,
	CIXVPU_RESOURCE_MEMORY,
	CIXVPU_RESOURCE_OVERLAP,
	CIXVPU_RESOURCE_IRQ,
	CIXVPU_RESOURCE_CCA
};

/* Validate descriptions only. This interface cannot access device hardware. */
enum cixvpu_resource_status
	cixvpu_resources_validate(const struct cixvpu_resources *res);

#endif
