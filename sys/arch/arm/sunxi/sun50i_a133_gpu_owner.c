/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD A133 GPU explicit boot ownership, AI-assisted. */
#include "sun50i_a133_gpu_owner.h"
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <stddef.h>
#endif
/* Recognize only source-derived core/DFS pairs; never infer unknown bits. */
int
a133_gpu_effective_hz(uint64_t core, uint32_t dfs, uint64_t *out)
{
	uint64_t value;

	if (out == NULL)
		return EINVAL;
	if (core == UINT64_C(504000000) && dfs == 1)
		value = UINT64_C(504000000);
	else if (core == UINT64_C(504000000) && dfs == 2)
		value = UINT64_C(472500000);
	else if (core == UINT64_C(504000000) && dfs == 4)
		value = UINT64_C(441000000);
	else if (core == UINT64_C(252000000) && dfs == 1)
		value = UINT64_C(252000000);
	else
		return EOPNOTSUPP;
	*out = value;
	return 0;
}

/* Selected board firmware OPP table, after validating its DT binding. */
bool
a133_gpu_opp_valid(uint64_t hz, uint32_t microvolts)
{
	return microvolts == 950000 &&
	    (hz == UINT64_C(504000000) || hz == UINT64_C(472500000) ||
	    hz == UINT64_C(441000000) || hz == UINT64_C(252000000));
}

int
a133_gpu_owner_boot(struct a133_gpu_owner *owner,
    const struct a133_gpu_owner_ops *ops, void *cookie)
{
	int error;

	if (owner == NULL || ops == NULL || cookie == NULL ||
	    ops->preflight == NULL || ops->activate == NULL ||
	    ops->prepare == NULL || ops->boot == NULL)
		return EINVAL;
	if (owner->attempted || owner->retain_resources)
		return EBUSY;
	error = ops->preflight(cookie);
	if (error != 0)
		return error;
	/* Activation can touch hardware even when it reports an error. */
	owner->attempted = true;
	owner->retain_resources = true;
	error = ops->activate(cookie);
	if (error == 0)
		error = ops->prepare(cookie);
	if (error == 0)
		error = ops->boot(cookie);
	return error;
}
