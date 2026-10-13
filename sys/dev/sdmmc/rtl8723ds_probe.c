/* Origin: EmberBSD - guarded RTL8723DS diagnostics for the inspected YS-M33. */
/*-
 * Copyright (c) 2026 Anton and EmberBSD contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <libfdt.h>

#include <dev/fdt/fdtvar.h>
#include <dev/ofw/openfirm.h>
#include <dev/sdmmc/sdmmcvar.h>
#include <dev/sdmmc/sdmmc_ioreg.h>
#include <dev/sdmmc/rtl8723ds_read.h>
#include <dev/sdmmc/rtl8723ds_function.h>

struct rtl8723ds_probe_softc {
	struct sdmmc_function *sc_sf;
	bool sc_function_probe;
	uint32_t sc_cfg1, sc_cfg2;
	int sc_error;
};

static int rtl8723ds_probe_match(device_t, cfdata_t, void *);
static void rtl8723ds_probe_attach(device_t, device_t, void *);
static int rtl8723ds_probe_detach(device_t, int);
static int rtl8723ds_probe_command(void *, uint32_t, uint32_t *);

CFATTACH_DECL_NEW(rtl8723dsprobe, sizeof(struct rtl8723ds_probe_softc),
    rtl8723ds_probe_match, rtl8723ds_probe_attach, rtl8723ds_probe_detach, NULL);

static int
rtl8723ds_probe_match(device_t parent, cfdata_t cf, void *aux)
{
	const struct sdmmc_attach_args *saa = aux;
	struct sdmmc_function *sf;
	const struct sdmmc_cis *cis;
	device_t host;
	devhandle_t handle;

	if (saa == NULL || (sf = saa->sf) == NULL || sf->sc == NULL ||
	    sf->sc->sc_dev != parent || (sf->flags & SFF_ERROR) != 0 ||
	    sf->sc->sc_fn0 == NULL || sf->sc->sc_fn0->sc != sf->sc ||
	    sf->sc->sc_fn0->number != 0 ||
	    (sf->sc->sc_fn0->flags & SFF_ERROR) != 0 ||
	    (sf->sc->sc_flags & SMF_IO_MODE) == 0 ||
	    (sf->sc->sc_flags & SMF_MEM_MODE) != 0)
		return 0;
	host = device_parent(parent);
	if (host == NULL || !device_is_a(host, "sunximmc"))
		return 0;
	handle = device_handle(host);
	if (devhandle_type(handle) != DEVHANDLE_TYPE_OF)
		return 0;
	cis = &sf->sc->sc_fn0->cis;
	return rtl8723ds_read_match(fdtbus_get_data(),
	    fdtbus_phandle2offset(devhandle_to_of(handle)), cis->manufacturer,
	    cis->product, sf->number, saa->interface) ? 1 : 0;
}

static int
rtl8723ds_probe_command(void *cookie, uint32_t argument, uint32_t *response)
{
	struct rtl8723ds_probe_softc *sc = cookie;
	struct sdmmc_function *sf = sc->sc_sf;
	struct sdmmc_command cmd;
	int error;

	/* The attachment task serializes this one-shot probe; no async handler. */
	KASSERT(curlwp == sf->sc->sc_tskq_lwp);
	if ((argument & SD_ARG_CMD52_EXCHANGE) != 0)
		return EACCES;
	if ((argument & SD_ARG_CMD52_WRITE) != 0 &&
	    !rtl8723ds_function_write_allowed(sc->sc_function_probe, argument))
		return EACCES;
	memset(&cmd, 0, sizeof(cmd));
	cmd.c_opcode = SD_IO_RW_DIRECT;
	cmd.c_arg = argument;
	cmd.c_flags = SCF_CMD_AC | SCF_RSP_R5;
	error = sdmmc_mmc_command(sf->sc, &cmd);
	if (error == 0)
		*response = cmd.c_resp[0];
	return error;
}

static void
rtl8723ds_probe_wait(void *cookie, unsigned int ms)
{
	kpause("rtwready", false, MAX(1, mstohz(ms)), NULL);
}

static void
rtl8723ds_probe_attach(device_t parent, device_t self, void *aux)
{
	struct rtl8723ds_probe_softc *sc = device_private(self);
	const struct sdmmc_attach_args *saa = aux;
	struct sdmmc_function *sf = saa->sf;
	const struct rtl8723ds_read_ops ops = { rtl8723ds_probe_command, sc };
	const struct rtl8723ds_function_ops function_ops = {
	    rtl8723ds_probe_command, rtl8723ds_probe_wait, sc
	};
	struct rtl8723ds_function_state function_state;
	uint32_t repeat;
	uint8_t enable, ready, suspend;
	const char *stage = "select card";
	bool changed;
	int error, len, restore_error;

	sc->sc_sf = sf;
	sc->sc_function_probe = fdt_getprop(fdtbus_get_data(), 0,
	    "ember,ys-m33-rtl8723ds-function-probe", &len) != NULL && len == 0;

	aprint_naive("\n");
	aprint_normal(": RTL8723DS diagnostic (no network interface)\n");
	aprint_normal_dev(self, "CIS card=%04x:%04x function=%04x:%04x\n",
	    sf->sc->sc_fn0->cis.manufacturer, sf->sc->sc_fn0->cis.product,
	    saa->manufacturer, saa->product);
	error = sdmmc_select_card(sf->sc, sf);
	if (error != 0)
		goto fail;
	stage = "CCCR function enable";
	error = rtl8723ds_read8(&ops, 0, SD_IO_CCCR_FN_ENABLE, &enable);
	if (error != 0)
		goto fail;
	stage = "CCCR function ready";
	error = rtl8723ds_read8(&ops, 0, SD_IO_CCCR_FN_IOREADY, &ready);
	if (error != 0)
		goto fail;
	aprint_normal_dev(self, "CCCR enable=%02x ready=%02x (unchanged)\n",
	    enable, ready);
	stage = "SYS_CFG1";
	error = rtl8723ds_read32(&ops, 0x102600f0, &sc->sc_cfg1);
	if (error != 0)
		goto fail;
	stage = "SYS_CFG2";
	error = rtl8723ds_read32(&ops, 0x102600fc, &sc->sc_cfg2);
	if (error != 0)
		goto fail;
	stage = "HCI suspend control";
	error = rtl8723ds_read8(&ops, 1, 0x86, &suspend);
	if (error != 0)
		goto fail;
	stage = "repeat SYS_CFG1";
	error = rtl8723ds_read32(&ops, 0x102600f0, &repeat);
	if (error != 0)
		goto fail;
	aprint_normal_dev(self, "SYS_CFG1=%08x SYS_CFG2=%08x HSUS=%02x\n",
	    sc->sc_cfg1, sc->sc_cfg2, suspend);
	if (repeat != sc->sc_cfg1) {
		error = EAGAIN;
		aprint_error_dev(self, "SYS_CFG1 changed to %08x\n", repeat);
		goto fail;
	}
	aprint_normal_dev(self, "CMD52 reads completed; radio not initialized\n");
	if (!sc->sc_function_probe)
		return;
	stage = "function enable/ready";
	error = rtl8723ds_function_enable(&function_ops, &function_state);
	changed = function_state.changed;
	if (error == 0) {
		aprint_normal_dev(self, "function 1 ready=%02x; original IOEx=%02x\n",
		    function_state.ready, function_state.original);
		stage = "enabled SYS_CFG1";
		error = rtl8723ds_read32(&ops, 0x102600f0, &repeat);
		if (error == 0 && repeat != sc->sc_cfg1)
			error = EAGAIN;
	}
	restore_error = rtl8723ds_function_restore(&function_ops, &function_state);
	if (restore_error != 0) {
		aprint_error_dev(self, "function restore failed: error %d\n",
		    restore_error);
		if (error == 0) {
			error = restore_error;
			stage = "function restore";
		}
	} else if (changed) {
		aprint_normal_dev(self, "function enable restored to %02x\n",
		    function_state.original);
	}
	if (error != 0)
		goto fail;
	aprint_normal_dev(self, "function lifecycle completed; radio not initialized\n");
	return;
fail:
	sc->sc_error = error;
	aprint_error_dev(self, "%s diagnostic stopped: error %d\n", stage, error);
}

static int
rtl8723ds_probe_detach(device_t self, int flags)
{
	/* The one-shot function probe restores IOEx before returning. */
	return 0;
}
