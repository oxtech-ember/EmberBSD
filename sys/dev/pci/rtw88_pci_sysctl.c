/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD RTL8822CE operator bootstrap trigger, AI-assisted. */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <dev/ic/rtw88var.h>

static int
rtwce_sysctl_bootstrap(SYSCTLFN_ARGS)
{
	struct sysctlnode node;
	int val, error;

	val = rtwce_started();
	node = *rnode;
	node.sysctl_data = &val;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	if (val != 0 && val != 1)
		return EINVAL;
	return rtwce_bootstrap(val);
}

SYSCTL_SETUP(sysctl_hw_rtwce_setup, "rtwce firmware bootstrap")
{
	int err;

	err = sysctl_createv(clog, 0, NULL, NULL,
	    CTLFLAG_PERMANENT | CTLFLAG_READWRITE, CTLTYPE_INT,
	    "rtwce_bootstrap",
	    SYSCTL_DESCR("Write 1 to load RTL8822CE firmware, 0 to stop"),
	    rtwce_sysctl_bootstrap, 0, NULL, 0,
	    CTL_HW, CTL_CREATE, CTL_EOL);
	if (err != 0)
		aprint_error("rtwce: sysctl_createv failed: %d\n", err);
}
