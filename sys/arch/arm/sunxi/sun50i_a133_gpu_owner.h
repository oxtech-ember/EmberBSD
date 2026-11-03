/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD A133 GPU explicit boot ownership, AI-assisted. */
#ifndef _SUN50I_A133_GPU_OWNER_H
#define _SUN50I_A133_GPU_OWNER_H
#include <sys/types.h>
#ifndef _KERNEL
#include <stdbool.h>
#include <stdint.h>
#endif
int a133_gpu_effective_hz(uint64_t, uint32_t, uint64_t *);
bool a133_gpu_opp_valid(uint64_t, uint32_t);
struct a133_gpu_owner {
	bool attempted, retain_resources;
};
struct a133_gpu_owner_ops {
	int (*preflight)(void *);
	int (*activate)(void *);
	int (*prepare)(void *);
	int (*boot)(void *);
};
/* The caller serializes the full call. A failed activation is uncertain. */
int a133_gpu_owner_boot(struct a133_gpu_owner *,
    const struct a133_gpu_owner_ops *, void *);
#endif
