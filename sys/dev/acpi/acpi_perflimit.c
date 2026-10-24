/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original ACPI processor performance limit broker, AI-assisted. */

/*
 * Passive cooling (ACPI 6.5, section 11.1.5) lowers the performance of
 * the processors listed in a thermal zone's _PSL. Several zones can list
 * one processor, and the user keeps its own frequency target. The broker
 * keeps one request per zone and CPU and hands the strictest limit to the
 * processor's performance provider.
 */

#include <sys/cdefs.h>

#include <sys/param.h>
#include <sys/cpu.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/systm.h>

#include <dev/acpi/acpi_perflimit.h>

struct acpi_perflimit {
	int		(*apl_apply)(void *, u_int);
	void		*apl_arg;
	const void	*apl_owner[ACPI_PERFLIMIT_OWNERS];
	u_int		 apl_limit[ACPI_PERFLIMIT_OWNERS];
	u_int		 apl_applied;
};

static kmutex_t			 acpi_perflimit_lock;
static struct acpi_perflimit	*acpi_perflimit_cpu[MAXCPUS];
static ONCE_DECL(acpi_perflimit_once);

static int
acpi_perflimit_init(void)
{

	mutex_init(&acpi_perflimit_lock, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

static struct acpi_perflimit **
acpi_perflimit_slot(struct cpu_info *ci)
{
	u_int index = cpu_index(ci);

	return index < MAXCPUS ? &acpi_perflimit_cpu[index] : NULL;
}

static u_int
acpi_perflimit_strictest(const struct acpi_perflimit *apl)
{
	u_int i, limit = ACPI_PERFLIMIT_NONE;

	for (i = 0; i < ACPI_PERFLIMIT_OWNERS; i++) {
		if (apl->apl_owner[i] != NULL && apl->apl_limit[i] < limit)
			limit = apl->apl_limit[i];
	}
	return limit;
}

int
acpi_perflimit_attach(struct cpu_info *ci, int (*apply)(void *, u_int),
    void *arg)
{
	struct acpi_perflimit **slot, *apl;
	int error = 0;

	if (ci == NULL || apply == NULL)
		return EINVAL;
	(void)RUN_ONCE(&acpi_perflimit_once, acpi_perflimit_init);
	apl = kmem_zalloc(sizeof(*apl), KM_SLEEP);
	apl->apl_apply = apply;
	apl->apl_arg = arg;
	apl->apl_applied = ACPI_PERFLIMIT_NONE;

	mutex_enter(&acpi_perflimit_lock);
	slot = acpi_perflimit_slot(ci);
	if (slot == NULL)
		error = EINVAL;
	else if (*slot != NULL)
		error = EEXIST;
	else
		*slot = apl;
	mutex_exit(&acpi_perflimit_lock);

	if (error != 0)
		kmem_free(apl, sizeof(*apl));
	return error;
}

void
acpi_perflimit_detach(struct cpu_info *ci)
{
	struct acpi_perflimit **slot, *apl = NULL;

	(void)RUN_ONCE(&acpi_perflimit_once, acpi_perflimit_init);
	mutex_enter(&acpi_perflimit_lock);
	slot = acpi_perflimit_slot(ci);
	if (slot != NULL) {
		apl = *slot;
		*slot = NULL;
	}
	mutex_exit(&acpi_perflimit_lock);

	if (apl != NULL)
		kmem_free(apl, sizeof(*apl));
}

int
acpi_perflimit_set(struct cpu_info *ci, const void *owner, u_int limit)
{
	struct acpi_perflimit **slot, *apl;
	u_int i, vacant = ACPI_PERFLIMIT_OWNERS, mine = ACPI_PERFLIMIT_OWNERS;
	u_int strictest;
	int error = 0;

	if (ci == NULL || owner == NULL)
		return EINVAL;
	if (limit > ACPI_PERFLIMIT_NONE)
		limit = ACPI_PERFLIMIT_NONE;

	(void)RUN_ONCE(&acpi_perflimit_once, acpi_perflimit_init);
	mutex_enter(&acpi_perflimit_lock);
	slot = acpi_perflimit_slot(ci);
	apl = slot != NULL ? *slot : NULL;
	if (apl == NULL) {
		error = ENODEV;
		goto out;
	}

	for (i = 0; i < ACPI_PERFLIMIT_OWNERS; i++) {
		if (apl->apl_owner[i] == owner)
			mine = i;
		else if (apl->apl_owner[i] == NULL &&
		    vacant == ACPI_PERFLIMIT_OWNERS)
			vacant = i;
	}
	if (limit == ACPI_PERFLIMIT_NONE) {
		if (mine != ACPI_PERFLIMIT_OWNERS)
			apl->apl_owner[mine] = NULL;
	} else {
		if (mine == ACPI_PERFLIMIT_OWNERS) {
			if (vacant == ACPI_PERFLIMIT_OWNERS) {
				error = ENOSPC;
				goto out;
			}
			mine = vacant;
			apl->apl_owner[mine] = owner;
		}
		apl->apl_limit[mine] = limit;
	}

	strictest = acpi_perflimit_strictest(apl);
	if (strictest != apl->apl_applied) {
		error = (*apl->apl_apply)(apl->apl_arg, strictest);
		if (error == 0)
			apl->apl_applied = strictest;
	}
out:
	mutex_exit(&acpi_perflimit_lock);
	return error;
}

u_int
acpi_perflimit_applied(struct cpu_info *ci)
{
	struct acpi_perflimit **slot;
	u_int limit = ACPI_PERFLIMIT_NONE;

	(void)RUN_ONCE(&acpi_perflimit_once, acpi_perflimit_init);
	mutex_enter(&acpi_perflimit_lock);
	slot = acpi_perflimit_slot(ci);
	if (slot != NULL && *slot != NULL)
		limit = (*slot)->apl_applied;
	mutex_exit(&acpi_perflimit_lock);
	return limit;
}
