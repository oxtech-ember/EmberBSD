/* $NetBSD$ */

/*-
 * Copyright (c) 2026 EmberBSD contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Origin: EmberBSD; AI-assisted native A733 power-domain controller.
 * Register facts: Arm DEN0051E and Allwinner BSP 2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f.
 * See ember/boot/a733-power-domains.md for exact provenance and limits.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <arm/sunxi/sun60i_a733_pck600.h>
#include <sys/systm.h>

#include <dev/clk/clk_backend.h>
#include <dev/fdt/fdtvar.h>

#define PCK600_DOMAIN_SIZE	0x1000
#define PCK600_NDOMAINS		11
#define PCK600_GPU_TOP		5
#define PCK600_GPU_CORE		6
#define PCK600_PWPR		0x000
#define PCK600_PMER		0x004
#define PCK600_PWSR		0x008
#define PCK600_DCDR0		0x170
#define PCK600_DCDR1		0x174
#define PCK600_SWITCH0		0xc00
#define PCK600_SWITCH1		0xc04
#define PCK600_OFF2ON		0xc10
#define PCK600_MODE		__BITS(3, 0)
#define PCK600_ON		8
#define PCK600_OFF		0
#define PCK600_DYNAMIC		(__BIT(8) | __BIT(24))
#define PCK600_LOCK		__BIT(12)
#define PCK600_EMULATION	__BIT(0)
#define PCK600_TIMEOUT_US	10000
#define PCK600_POLL_US		10

struct sun60i_pck600_softc {
	device_t sc_dev;
	bus_space_tag_t sc_bst;
	bus_space_handle_t sc_bsh;
	kmutex_t sc_lock;
	bool sc_failed[PCK600_NDOMAINS];
	int sc_phandle;
	const void *sc_gpu_owner;
	bool sc_gpu_retained;
	LIST_ENTRY(sun60i_pck600_softc) sc_next;
};

static LIST_HEAD(, sun60i_pck600_softc) sun60i_pck600_providers =
    LIST_HEAD_INITIALIZER(sun60i_pck600_providers);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun60i-a733-pck-600" },
	DEVICE_COMPAT_EOL
};

static uint32_t
sun60i_pck600_read(struct sun60i_pck600_softc *sc, bus_size_t reg)
{
	return bus_space_read_4(sc->sc_bst, sc->sc_bsh, reg);
}

static int
sun60i_pck600_write(struct sun60i_pck600_softc *sc, bus_size_t reg,
    uint32_t value)
{
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, reg, value);
	bus_space_barrier(sc->sc_bst, sc->sc_bsh, reg, sizeof(value),
	    BUS_SPACE_BARRIER_WRITE | BUS_SPACE_BARRIER_READ);
	/* Read back also drains posted writes before status polling. */
	return sun60i_pck600_read(sc, reg) == value ? 0 : EIO;
}

static int
sun60i_pck600_program_delays(struct sun60i_pck600_softc *sc, bus_size_t base)
{
	static const struct {
		bus_size_t reg;
		uint32_t value;
	} delays[] = {
		{ PCK600_DCDR0, 0x1f1f1f },
		{ PCK600_DCDR1, 0x1f1f },
		{ PCK600_SWITCH0, 0x08080808 },
		{ PCK600_SWITCH1, 0x0808 },
		{ PCK600_OFF2ON, 0x08 },
	};
	int error;

	KASSERT(mutex_owned(&sc->sc_lock));
	for (u_int n = 0; n < __arraycount(delays); n++) {
		error = sun60i_pck600_write(sc, base + delays[n].reg,
		    delays[n].value);
		if (error != 0)
			return error;
	}
	return 0;
}

static int
sun60i_pck600_transition(struct sun60i_pck600_softc *sc, u_int id,
    bool enable)
{
	const bus_size_t base = id * PCK600_DOMAIN_SIZE;
	const uint32_t mode = enable ? PCK600_ON : PCK600_OFF;
	uint32_t policy, status;
	int error;

	KASSERT(mutex_owned(&sc->sc_lock));
	if (id >= PCK600_NDOMAINS)
		return EINVAL;
	/* Upstream marks GPU_CORE always-on; never request its power-off. */
	if (id == PCK600_GPU_CORE && !enable)
		return EOPNOTSUPP;
	if (sc->sc_failed[id])
		return EIO;

	policy = sun60i_pck600_read(sc, base + PCK600_PWPR);
	status = sun60i_pck600_read(sc, base + PCK600_PWSR);
	/* Do not take over firmware's dynamic, locked or emulated policy. */
	if ((policy | status) & (PCK600_DYNAMIC | PCK600_LOCK))
		return EOPNOTSUPP;
	if (sun60i_pck600_read(sc, base + PCK600_PMER) & PCK600_EMULATION)
		return EOPNOTSUPP;
	if ((policy & PCK600_MODE) != (status & PCK600_MODE))
		return EBUSY;
	if ((status & PCK600_MODE) == mode)
		return 0;

	error = sun60i_pck600_program_delays(sc, base);
	if (error != 0)
		goto failed;
	policy = (policy & ~PCK600_MODE) | mode;
	error = sun60i_pck600_write(sc, base + PCK600_PWPR, policy);
	if (error != 0)
		goto failed;

	for (u_int elapsed = 0; ; elapsed += PCK600_POLL_US) {
		/* A denied static request can revert PWPR (Arm DEN0051E). */
		if (sun60i_pck600_read(sc, base + PCK600_PWPR) != policy) {
			error = EIO;
			goto failed;
		}
		status = sun60i_pck600_read(sc, base + PCK600_PWSR);
		if ((status & (PCK600_MODE | PCK600_DYNAMIC | PCK600_LOCK))
		    == mode)
			return 0;
		if (elapsed == PCK600_TIMEOUT_US)
			break;
		delay(PCK600_POLL_US);
	}
	error = ETIMEDOUT;
failed:
	/* An in-flight transition cannot safely be undone by another write. */
	sc->sc_failed[id] = true;
	return error;
}

static int
sun60i_pck600_set(device_t dev, const uint32_t *data, bool enable)
{
	struct sun60i_pck600_softc * const sc = device_private(dev);
	const u_int id = be32toh(data[1]);
	int error;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_gpu_owner != NULL &&
	    (id == PCK600_GPU_TOP || id == PCK600_GPU_CORE))
		error = EBUSY;
	else
		error = sun60i_pck600_transition(sc, id, enable);
	mutex_exit(&sc->sc_lock);
	if (error != 0)
		aprint_error_dev(dev, "domain %u power %s failed: %d\n",
		    id, enable ? "on" : "off", error);
	return error;
}

struct sun60i_pck600_observation {
	uint32_t sample[2][3];
	bool enabled;
	int state_error;
};

static int
sun60i_pck600_observe(struct sun60i_pck600_softc *sc, u_int id,
    struct sun60i_pck600_observation *state)
{
	const bus_size_t base = id * PCK600_DOMAIN_SIZE;
	struct sun60i_pck600_observation observed = { 0 };
	uint32_t policy, status, emulation;

	KASSERT(mutex_owned(&sc->sc_lock));
	if (id >= PCK600_NDOMAINS)
		return EINVAL;
	if (sc->sc_failed[id])
		return EIO;

	for (u_int i = 0; i < 2; i++) {
		observed.sample[i][0] = sun60i_pck600_read(sc, base + PCK600_PWPR);
		observed.sample[i][1] = sun60i_pck600_read(sc, base + PCK600_PMER);
		observed.sample[i][2] = sun60i_pck600_read(sc, base + PCK600_PWSR);
	}
	policy = observed.sample[0][0];
	emulation = observed.sample[0][1];
	status = observed.sample[0][2];
	observed.state_error = EBUSY;
	if (memcmp(observed.sample[0], observed.sample[1],
	    sizeof(observed.sample[0])) != 0)
		goto out;
	observed.state_error = EOPNOTSUPP;
	if (((policy | status) & PCK600_DYNAMIC) != 0 ||
	    (emulation & PCK600_EMULATION) != 0)
		goto out;
	observed.state_error = EBUSY;
	if ((policy & PCK600_MODE) != (status & PCK600_MODE))
		goto out;

	/* A locked static policy can be observed without changing it. */
	switch (status & PCK600_MODE) {
	case PCK600_OFF:
		observed.enabled = false;
		observed.state_error = 0;
		break;
	case PCK600_ON:
		observed.enabled = true;
		observed.state_error = 0;
		break;
	default:
		observed.state_error = EOPNOTSUPP;
		break;
	}
out:
	*state = observed;
	return 0;
}

static int
sun60i_pck600_state(struct sun60i_pck600_softc *sc, u_int id, bool *enabled)
{
	struct sun60i_pck600_observation state;
	int error;

	error = sun60i_pck600_observe(sc, id, &state);
	if (error != 0)
		return error;
	if (state.state_error != 0)
		return state.state_error;
	*enabled = state.enabled;
	return 0;
}

/*
 * Separate diagnostic allowlist, from Arm DEN0051E, Table 5-1. Reads of
 * ISR/AISR do not clear their W1C events. Never access UNLK or reserved
 * implementation-specific space. This is not the power-state query path.
 */
static const struct {
	bus_size_t reg;
	const char *name;
} sun60i_pck600_diag_regs[] = {
	{ PCK600_PWPR, "PWPR" },
	{ PCK600_PMER, "PMER" },
	{ PCK600_PWSR, "PWSR" },
	{ 0x010, "DISR" },
	{ 0x014, "MISR" },
	{ 0x018, "STSR" },
	{ 0x020, "PWCR" },
	{ 0x024, "PTCR" },
	{ 0x030, "IMR" },
	{ 0x034, "AIMR" },
	{ 0x038, "ISR" },
	{ 0x03c, "AISR" },
	{ 0x160, "EDTR0" },
	{ 0x164, "EDTR1" },
	{ PCK600_DCDR0, "DCDR0" },
	{ PCK600_DCDR1, "DCDR1" },
	{ 0xfb0, "IDR0" },
	{ 0xfb4, "IDR1" },
	{ 0xfc8, "IIDR" },
	{ 0xfcc, "AIDR" },
};
#define PCK600_DIAG_IDR0		16
#define PCK600_DIAG_IIDR		18
#define PCK600_DIAG_AIDR		19

static const struct {
	u_int id;
	const char *name;
} sun60i_pck600_diag_domains[] = {
	{ PCK600_GPU_TOP, "GPU_TOP" },
	{ PCK600_GPU_CORE, "GPU_CORE" },
};

struct sun60i_pck600_diagnostic {
	uint32_t sample[__arraycount(sun60i_pck600_diag_domains)][2]
	    [__arraycount(sun60i_pck600_diag_regs)];
	uint32_t changed[__arraycount(sun60i_pck600_diag_domains)];
};

static int
sun60i_pck600_inspect(struct sun60i_pck600_softc *sc,
    struct sun60i_pck600_diagnostic *state)
{
	struct sun60i_pck600_diagnostic observed = { 0 };
	bus_size_t base, reg;

	KASSERT(mutex_owned(&sc->sc_lock));
	for (u_int d = 0; d < __arraycount(sun60i_pck600_diag_domains); d++)
		if (sc->sc_failed[sun60i_pck600_diag_domains[d].id])
			return EIO;

	for (u_int d = 0; d < __arraycount(sun60i_pck600_diag_domains); d++) {
		base = sun60i_pck600_diag_domains[d].id * PCK600_DOMAIN_SIZE;
		for (u_int s = 0; s < 2; s++) {
			for (u_int r = 0;
			    r < __arraycount(sun60i_pck600_diag_regs); r++) {
				reg = base + sun60i_pck600_diag_regs[r].reg;
				if (bus_space_peek_4(sc->sc_bst, sc->sc_bsh,
				    reg, &observed.sample[d][s][r]) != 0)
					return EFAULT;
			}
		}
		for (u_int r = 0; r < __arraycount(sun60i_pck600_diag_regs); r++)
			if (observed.sample[d][0][r] != observed.sample[d][1][r])
				observed.changed[d] |= __BIT(r);
	}
	/* Publish only complete acquisition, including unsupported raw identities. */
	*state = observed;
	return 0;
}

static int
sun60i_pck600_channels(const struct sun60i_pck600_diagnostic *state, u_int d)
{
	const uint32_t *sample = state->sample[d][0];
	const uint32_t idr0 = sample[PCK600_DIAG_IDR0];
	const uint32_t channels = idr0 & __BITS(3, 0);

	/* Decode only stable Arm PCK-600/PPU v1.1 identification metadata. */
	if ((state->changed[d] & __BITS(19, 16)) != 0 ||
	    sample[PCK600_DIAG_AIDR] != 0x11 ||
	    (sample[PCK600_DIAG_IIDR] & 0xfff00fff) != 0x0b60043b ||
	    (idr0 & 0x00030100) != 0x00030100 || channels > 8 ||
	    (channels != 0 && (idr0 & __BITS(7, 4)) != 0))
		return -1;
	return channels;
}

/* A separate read-only waiter; pdc_get/set keep their strict transition rules. */
static struct sun60i_pck600_softc *
sun60i_pck600_gpu_provider(int phandle)
{
	struct sun60i_pck600_softc *sc;

	LIST_FOREACH(sc, &sun60i_pck600_providers, sc_next)
		if (sc->sc_phandle == phandle)
			return sc;
	return NULL;
}

static int
sun60i_pck600_gpu_check(struct sun60i_pck600_softc *sc, bool initial,
    uint32_t *last)
{
	struct sun60i_pck600_diagnostic state;
	int error;

	error = sun60i_pck600_inspect(sc, &state);
	if (error != 0)
		return error;
	if (last != NULL) {
		last[0] = state.sample[1][1][0];
		last[1] = state.sample[1][1][2];
		last[2] = state.sample[1][1][4];
	}
	for (u_int d = 0; d < 2; d++) {
		const uint32_t *v = state.sample[d][0];

		if (state.changed[d] != 0)
			return EBUSY;
		if (sun60i_pck600_channels(&state, d) != 1 ||
		    v[0] != PCK600_ON || v[1] != 0 || v[6] != 0x101 ||
		    v[7] != 0)
			return EOPNOTSUPP;
		if (v[12] != 0 || v[13] != 0 || v[14] != 0x1f1f1f ||
		    v[15] != 0x1f1f || v[16] != 0x10130101 || v[17] != 2 ||
		    v[18] != 0x0b61143b || v[19] != 0x11)
			return EOPNOTSUPP;
		if ((v[10] & __BIT(2)) != 0 || (v[11] & __BIT(0)) != 0 ||
		    (v[4] & __BIT(16)) != 0)
			return EIO;
		if (d == 0 && (v[2] != PCK600_ON || v[4] != __BIT(8)))
			return EBUSY;
		if (d == 1 && initial && (v[2] != 0 || v[4] != 0))
			return EBUSY;
		if (d == 1 && !initial && (v[4] != __BIT(8) ||
		    v[2] != PCK600_ON))
			return EBUSY;
	}
	return 0;
}

int
sun60i_a733_pck_gpu_reserve(int phandle, const void *owner)
{
	struct sun60i_pck600_softc *sc = sun60i_pck600_gpu_provider(phandle);
	int error;

	if (owner == NULL)
		return EINVAL;
	if (sc == NULL)
		return ENXIO;
	mutex_enter(&sc->sc_lock);
	if (sc->sc_gpu_owner != NULL)
		error = EBUSY;
	else if ((error = sun60i_pck600_gpu_check(sc, true, NULL)) == 0)
		sc->sc_gpu_owner = owner;
	mutex_exit(&sc->sc_lock);
	return error;
}

int
sun60i_a733_pck_gpu_release(int phandle, const void *owner)
{
	struct sun60i_pck600_softc *sc = sun60i_pck600_gpu_provider(phandle);
	int error = 0;

	if (sc == NULL)
		return ENXIO;
	mutex_enter(&sc->sc_lock);
	if (owner == NULL || sc->sc_gpu_owner != owner)
		error = EINVAL;
	else if (sc->sc_gpu_retained)
		error = EBUSY;
	else
		sc->sc_gpu_owner = NULL;
	mutex_exit(&sc->sc_lock);
	return error;
}

int
sun60i_a733_pck_gpu_retain(int phandle, const void *owner)
{
	struct sun60i_pck600_softc *sc = sun60i_pck600_gpu_provider(phandle);
	int error;

	if (sc == NULL)
		return ENXIO;
	mutex_enter(&sc->sc_lock);
	if (owner == NULL || sc->sc_gpu_owner != owner || sc->sc_gpu_retained)
		error = EBUSY;
	else if ((error = sun60i_pck600_gpu_check(sc, true, NULL)) == 0)
		sc->sc_gpu_retained = true;
	mutex_exit(&sc->sc_lock);
	return error;
}

int
sun60i_a733_pck_gpu_wait(int phandle, const void *owner, bool quiet)
{
	struct sun60i_pck600_softc *sc = sun60i_pck600_gpu_provider(phandle);
	uint32_t last[3] = { UINT32_MAX, UINT32_MAX, UINT32_MAX };
	int error;

	if (sc == NULL)
		return ENXIO;
	mutex_enter(&sc->sc_lock);
	if (owner == NULL || sc->sc_gpu_owner != owner || !sc->sc_gpu_retained) {
		error = EINVAL;
		goto out;
	}
	for (u_int elapsed = 0; ; elapsed += PCK600_POLL_US) {
		error = sun60i_pck600_gpu_check(sc, false, last);
		if (error != EBUSY)
			break;
		if (elapsed == PCK600_TIMEOUT_US) {
			error = ETIMEDOUT;
			break;
		}
		delay(PCK600_POLL_US);
	}
	if (!quiet)
		aprint_normal_dev(sc->sc_dev, "experimental CORE wait: %d; last "
		    "PWPR/PWSR/MISR 0x%08x/0x%08x/0x%08x\n", error,
		    last[0], last[1], last[2]);
out:
	mutex_exit(&sc->sc_lock);
	return error;
}

int
sun60i_a733_pck_gpu_request_on(int phandle, const void *owner, bool quiet)
{
	struct sun60i_pck600_softc *sc = sun60i_pck600_gpu_provider(phandle);
	const bus_size_t base = PCK600_GPU_CORE * PCK600_DOMAIN_SIZE;
	uint32_t policy = 0, status = 0;
	int error;

	if (sc == NULL)
		return ENXIO;
	mutex_enter(&sc->sc_lock);
	if (owner == NULL || sc->sc_gpu_owner != owner || !sc->sc_gpu_retained) {
		error = EINVAL;
		goto out;
	}
	policy = sun60i_pck600_read(sc, base + PCK600_PWPR);
	status = sun60i_pck600_read(sc, base + PCK600_PWSR);
	if ((policy & PCK600_MODE) == PCK600_ON &&
	    (status & PCK600_MODE) == PCK600_ON) {
		error = 0;
		goto out;
	}
	/* The observed stalled policy-ON/status-OFF state is the only base. */
	error = sun60i_pck600_gpu_check(sc, true, NULL);
	if (error != 0)
		goto out;
	/*
	 * Allwinner's own writers re-issue the static ON request without
	 * comparing policy and status first: the SCP resume loop masks OR 8
	 * into every PWPR, and Linux's always-on genpd path writes COMMAND_ON
	 * and polls PWSR.  A write that never lands or a policy that reverts
	 * (a denial) quarantines the domain like an ordinary transition; a
	 * status timeout does not, so the read-only waiter stays usable.
	 */
	error = sun60i_pck600_program_delays(sc, base);
	if (error != 0)
		goto failed;
	policy = (policy & ~PCK600_MODE) | PCK600_ON;
	error = sun60i_pck600_write(sc, base + PCK600_PWPR, policy);
	if (error != 0)
		goto failed;

	for (u_int elapsed = 0; ; elapsed += PCK600_POLL_US) {
		/* A denied static request can revert PWPR (Arm DEN0051E). */
		if (sun60i_pck600_read(sc, base + PCK600_PWPR) != policy) {
			error = EIO;
			goto failed;
		}
		status = sun60i_pck600_read(sc, base + PCK600_PWSR);
		if ((status & PCK600_MODE) == PCK600_ON) {
			error = 0;
			goto out;
		}
		if (elapsed == PCK600_TIMEOUT_US) {
			error = ETIMEDOUT;
			goto out;
		}
		delay(PCK600_POLL_US);
	}
failed:
	sc->sc_failed[PCK600_GPU_CORE] = true;
out:
	if (!quiet)
		aprint_normal_dev(sc->sc_dev, "experimental CORE request: %d; "
		    "last PWPR/PWSR 0x%08x/0x%08x\n", error, policy, status);
	mutex_exit(&sc->sc_lock);
	return error;
}

static void
sun60i_pck600_report(struct sun60i_pck600_softc *sc)
{
	struct sun60i_pck600_diagnostic state;
	const char *name;
	int channels, error;

	mutex_enter(&sc->sc_lock);
	error = sun60i_pck600_inspect(sc, &state);
	mutex_exit(&sc->sc_lock);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev, "GPU PPU diagnostic unavailable: "
		    "%d\n", error);
		return;
	}
	for (u_int d = 0; d < __arraycount(sun60i_pck600_diag_domains); d++) {
		name = sun60i_pck600_diag_domains[d].name;
		aprint_normal_dev(sc->sc_dev, "%s diagnostic: changed 0x%05x; "
		    "not a power-state or GPU readiness guarantee\n", name,
		    state.changed[d]);
		channels = sun60i_pck600_channels(&state, d);
		if (channels < 0)
			aprint_normal_dev(sc->sc_dev, "%s PPU metadata: "
			    "unrecognized or unstable, raw values only\n", name);
		else
			aprint_normal_dev(sc->sc_dev, "%s PPU metadata: %s, "
			    "%u device channel(s)\n", name,
			    channels == 0 ? "P-Channel" : "Q-Channel",
			    channels == 0 ? 1 : (u_int)channels);
		for (u_int r = 0; r < __arraycount(sun60i_pck600_diag_regs); r++)
			aprint_normal_dev(sc->sc_dev, "%s %s [0x%03x]: "
			    "0x%08x 0x%08x\n", name,
			    sun60i_pck600_diag_regs[r].name,
			    (u_int)sun60i_pck600_diag_regs[r].reg,
			    state.sample[d][0][r], state.sample[d][1][r]);
	}
}

static int
sun60i_pck600_get(device_t dev, const uint32_t *data, bool *enabled)
{
	struct sun60i_pck600_softc * const sc = device_private(dev);
	const u_int id = be32toh(data[1]);
	int error;

	mutex_enter(&sc->sc_lock);
	error = sun60i_pck600_state(sc, id, enabled);
	mutex_exit(&sc->sc_lock);

	return error;
}

static const struct fdtbus_powerdomain_controller_func sun60i_pck600_funcs = {
	.pdc_set = sun60i_pck600_set,
	.pdc_get = sun60i_pck600_get,
};

static int
sun60i_pck600_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
sun60i_pck600_attach(device_t parent, device_t self, void *aux)
{
	struct sun60i_pck600_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
	bus_addr_t addr;
	bus_size_t size;
	struct clk *clk;
	uint32_t cells;
	int error;

	sc->sc_dev = self;
	sc->sc_phandle = phandle;
	if (sun60i_pck600_gpu_provider(phandle) != NULL) {
		aprint_error(": duplicate power-domain provider\n");
		return;
	}
	sc->sc_bst = faa->faa_bst;
	if (of_getprop_uint32(phandle, "#power-domain-cells", &cells) != 0 ||
	    cells != 1 || fdtbus_get_reg(phandle, 0, &addr, &size) != 0 ||
	    size < PCK600_NDOMAINS * PCK600_DOMAIN_SIZE) {
		aprint_error(": invalid power-domain binding\n");
		return;
	}
	clk = fdtbus_clock_get_index(phandle, 0);
	if (clk == NULL) {
		aprint_error(": missing PPU clock\n");
		return;
	}
	error = bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_bsh);
	if (error != 0) {
		aprint_error(": cannot map registers: %d\n", error);
		return;
	}
	error = clk_enable(clk);
	if (error != 0) {
		aprint_error(": cannot enable PPU clock: %d\n", error);
		goto unmap;
	}
	/* A733 has no PPU reset. Do not reset domains left on by firmware. */
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	error = fdtbus_register_powerdomain_controller(self, phandle,
	    &sun60i_pck600_funcs);
	if (error != 0) {
		aprint_error(": cannot register power domains: %d\n", error);
		mutex_destroy(&sc->sc_lock);
		clk_disable(clk);
		goto unmap;
	}
	LIST_INSERT_HEAD(&sun60i_pck600_providers, sc, sc_next);
	aprint_naive("\n");
	aprint_normal(": A733 PCK-600 power domains\n");
	sun60i_pck600_report(sc);
	return;
unmap:
	bus_space_unmap(sc->sc_bst, sc->sc_bsh, size);
}

CFATTACH_DECL_NEW(sun60i_a733_pck, sizeof(struct sun60i_pck600_softc),
    sun60i_pck600_match, sun60i_pck600_attach, NULL, NULL);
