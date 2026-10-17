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

static int
rtwce_sysctl_stats(SYSCTLFN_ARGS)
{
	struct sysctlnode node;
	char buf[2048];
	int error;

	error = rtwce_stats(buf, sizeof(buf));
	if (error != 0)
		return error;
	node = *rnode;
	node.sysctl_data = buf;
	node.sysctl_size = strlen(buf) + 1;
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static int
rtwce_sysctl_h2c(SYSCTLFN_ARGS)
{
	struct sysctlnode node;
	int val = 0, error;

	node = *rnode;
	node.sysctl_data = &val;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	return rtwce_h2c((uint32_t)val);
}

static int
rtwce_sysctl_loopback(SYSCTLFN_ARGS)
{
	struct sysctlnode node;
	int val = 0, error;

	node = *rnode;
	node.sysctl_data = &val;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	return rtwce_loopback(val);
}

static int
rtwce_sysctl_lbmode(SYSCTLFN_ARGS)
{
	struct sysctlnode node;
	int val = 0, error;

	node = *rnode;
	node.sysctl_data = &val;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	return rtwce_lbmode(val);
}

static int
rtwce_sysctl_channel(SYSCTLFN_ARGS)
{
	struct sysctlnode node;
	int val = 0, error;

	(void)rtwce_channel(&val, 0);
	node = *rnode;
	node.sysctl_data = &val;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	return rtwce_channel(&val, 1);
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
	err = sysctl_createv(clog, 0, NULL, NULL,
	    CTLFLAG_PERMANENT | CTLFLAG_READONLY, CTLTYPE_STRING,
	    "rtwce_stats", SYSCTL_DESCR("RTL8822CE interrupt and RX counters"),
	    rtwce_sysctl_stats, 0, NULL, 0, CTL_HW, CTL_CREATE, CTL_EOL);
	if (err != 0)
		aprint_error("rtwce: sysctl_createv failed: %d\n", err);
	err = sysctl_createv(clog, 0, NULL, NULL,
	    CTLFLAG_PERMANENT | CTLFLAG_READWRITE, CTLTYPE_INT,
	    "rtwce_h2c", SYSCTL_DESCR("Send one diagnostic H2C: id | payload << 8"),
	    rtwce_sysctl_h2c, 0, NULL, 0, CTL_HW, CTL_CREATE, CTL_EOL);
	if (err != 0)
		aprint_error("rtwce: sysctl_createv failed: %d\n", err);
	err = sysctl_createv(clog, 0, NULL, NULL,
	    CTLFLAG_PERMANENT | CTLFLAG_READWRITE, CTLTYPE_INT,
	    "rtwce_loopback",
	    SYSCTL_DESCR("Post n broadcast frames on the management queue"),
	    rtwce_sysctl_loopback, 0, NULL, 0, CTL_HW, CTL_CREATE, CTL_EOL);
	if (err != 0)
		aprint_error("rtwce: sysctl_createv failed: %d\n", err);
	err = sysctl_createv(clog, 0, NULL, NULL,
	    CTLFLAG_PERMANENT | CTLFLAG_READWRITE, CTLTYPE_INT,
	    "rtwce_lbmode",
	    SYSCTL_DESCR("Loopback mode: 0, 1 PHY, 3 MAC delay, 7 DMA, 11"),
	    rtwce_sysctl_lbmode, 0, NULL, 0, CTL_HW, CTL_CREATE, CTL_EOL);
	if (err != 0)
		aprint_error("rtwce: sysctl_createv failed: %d\n", err);
	err = sysctl_createv(clog, 0, NULL, NULL,
	    CTLFLAG_PERMANENT | CTLFLAG_READWRITE, CTLTYPE_INT,
	    "rtwce_channel", SYSCTL_DESCR("RTL8822CE 20 MHz receive channel"),
	    rtwce_sysctl_channel, 0, NULL, 0, CTL_HW, CTL_CREATE, CTL_EOL);
	if (err != 0)
		aprint_error("rtwce: sysctl_createv failed: %d\n", err);
}
