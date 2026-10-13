/* Origin: EmberBSD bounded native BCM2712 V3D reset takeover, 2026-10-09. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

/*
 * Register/lifecycle facts: raspberrypi/linux
 * 43c132e8863c3bff3647033b6a7d2bf87b15501c, v3d_{drv,gem,regs} and
 * bcm2835-power. This native implementation does not import Linux code.
 * Only the explicit BCM2712_V3D_TAKEOVER configuration includes this file.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <arm/broadcom/bcm2835_pmwdogvar.h>
#include <arm/broadcom/bcm2712_v3d_takeover.h>

#define TV3D_HUB	0
#define TV3D_CORE	1
#define TV3D_SMS	2
#define TV3D_NREG	3
#define TV3D_SMS_TEE	0x400
#define TV3D_SMS_CLEAR_POWER_OFF	__BIT(29)
#define TV3D_SMS_RESET	4
#define TV3D_SMS_MODES	0xf0
#define TV3D_SMS_STATE	0x0f
#define TV3D_SMS_PROGRESS	0x1ff00
#define TV3D_MASK_STS	0x5c
#define TV3D_MASK_SET	0x60
#define TV3D_HUB_IRQS	0x0000007f
#define TV3D_CORE_IRQS	0x0fff007f
#define TV3D_GMP_ACTIVITY	(__BIT(4) | __BIT(5))
#define TV3D_ERR_VCD_IDLE	__BIT(12)
#define TV3D_POLL_COUNT	1000
#define TV3D_POLL_US	100

static const bus_addr_t tv3d_addresses[TV3D_NREG] = {
	UINT64_C(0x1002000000), UINT64_C(0x1002008000),
	UINT64_C(0x1002030800)
};
static const bus_size_t tv3d_lengths[TV3D_NREG] = { 0x4000, 0x6000, 0x700 };

enum tv3d_register {
	TV3D_HID1, TV3D_HID2, TV3D_HID3, TV3D_CID0, TV3D_CID1, TV3D_CID2,
	TV3D_MMU_DEBUG, TV3D_MMU_CTL, TV3D_MMUC_CTL, TV3D_PT_BASE,
	TV3D_BYPASS_START, TV3D_BYPASS_END, TV3D_TFU_CS, TV3D_TFU_SU,
	TV3D_CSD_STATUS, TV3D_GMP_STATUS, TV3D_CT0CS, TV3D_CT1CS, TV3D_PCS,
	TV3D_ERROR, TV3D_HUB_STATUS, TV3D_CORE_STATUS, TV3D_REGISTER_COUNT
};
static const struct {
	unsigned int block;
	bus_size_t offset;
	const char *name;
} tv3d_registers[TV3D_REGISTER_COUNT] = {
	{ TV3D_HUB, 0x00c, "HUB_IDENT1" },
	{ TV3D_HUB, 0x010, "HUB_IDENT2" },
	{ TV3D_HUB, 0x014, "HUB_IDENT3" },
	{ TV3D_CORE, 0x000, "CORE_IDENT0" },
	{ TV3D_CORE, 0x004, "CORE_IDENT1" },
	{ TV3D_CORE, 0x008, "CORE_IDENT2" },
	{ TV3D_HUB, 0x1238, "MMU_DEBUG" },
	{ TV3D_HUB, 0x1200, "MMU_CTL" },
	{ TV3D_HUB, 0x1000, "MMUC_CTL" },
	{ TV3D_HUB, 0x1204, "MMU_PT_BASE" },
	{ TV3D_HUB, 0x121c, "MMU_BYPASS_START" },
	{ TV3D_HUB, 0x1220, "MMU_BYPASS_END" },
	{ TV3D_HUB, 0x700, "TFU_CS" },
	{ TV3D_HUB, 0x704, "TFU_SU" },
	{ TV3D_CORE, 0x900, "CSD_STATUS" },
	{ TV3D_HUB, 0x600, "GMP_STATUS" },
	{ TV3D_CORE, 0x100, "CT0CS" },
	{ TV3D_CORE, 0x104, "CT1CS" },
	{ TV3D_CORE, 0x130, "PCS" },
	{ TV3D_CORE, 0xf20, "ERR_STAT" },
	{ TV3D_HUB, 0x050, "HUB_INT_STS" },
	{ TV3D_CORE, 0x050, "CORE_INT_STS" },
};

struct tv3d_snapshot {
	uint32_t reg[TV3D_REGISTER_COUNT], sms[2], pm;
};

/* One physical V3D, one attempt. Claimed mappings survive until reboot. */
static struct {
	device_t dev;
	bus_space_tag_t bst;
	bus_space_handle_t handle[TV3D_NREG];
	bool attempted, claimed, sealed, mapped[TV3D_NREG], complete;
	uint32_t last_sms[2];
	const char *stage;
	struct tv3d_snapshot before, after;
} tv3d;

int bcmv3d_takeover_probe(device_t, bus_space_tag_t);

static int
tv3d_read(unsigned int block, bus_size_t offset, uint32_t *value)
{
	return bus_space_peek_4(tv3d.bst, tv3d.handle[block], offset, value);
}

static int
tv3d_write(unsigned int block, bus_size_t offset, uint32_t value)
{
	int error;

	KASSERT(tv3d.claimed && tv3d.sealed);
	error = bus_space_poke_4(tv3d.bst, tv3d.handle[block], offset, value);
	if (error == 0)
		bus_space_barrier(tv3d.bst, tv3d.handle[block], offset, 4,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return error;
}

static int
tv3d_snapshot(struct tv3d_snapshot *snapshot, const char *name)
{
	uint32_t second[2];
	unsigned int i;
	int error;

	error = bcmpmwdog_v3d_status(&snapshot->pm);
	if (error != 0)
		return error;
	for (i = 0; i < 2; i++) {
		error = tv3d_read(TV3D_SMS, i * TV3D_SMS_TEE, &snapshot->sms[i]);
		if (error != 0)
			return error;
	}
	/* Do not access HUB/CORE through an unsupported SMS power state. */
	if (snapshot->sms[0] != 0 || snapshot->sms[1] != 0x50)
		return EOPNOTSUPP;
	for (i = 0; i < TV3D_REGISTER_COUNT; i++) {
		error = tv3d_read(tv3d_registers[i].block,
		    tv3d_registers[i].offset, &snapshot->reg[i]);
		if (error != 0)
			return error;
	}
	for (i = 0; i < 2; i++) {
		error = tv3d_read(TV3D_SMS, i * TV3D_SMS_TEE, &second[i]);
		if (error != 0)
			return error;
		if (second[i] != snapshot->sms[i])
			return EBUSY;
	}
	aprint_normal_dev(tv3d.dev, "%s PM=%#x SMS REE=%#x TEE=%#x\n",
	    name, snapshot->pm, snapshot->sms[0], snapshot->sms[1]);
	for (i = 0; i < TV3D_REGISTER_COUNT; i++)
		aprint_normal_dev(tv3d.dev, "%s %s=%#x\n", name,
		    tv3d_registers[i].name, snapshot->reg[i]);
	return 0;
}

static int
tv3d_eligible(const struct tv3d_snapshot *snapshot)
{
	uint32_t id = snapshot->reg[TV3D_HID1];

	if ((id & 0xfff) != 0x117 ||
	    snapshot->reg[TV3D_CID0] != 0x07443356)
		return ENODEV;
	/* These exact modes are the first observed CM5 handoff, not a lease. */
	if (snapshot->sms[0] != 0 || snapshot->sms[1] != 0x50)
		return EOPNOTSUPP;
	/*
	 * CT/PCS fields are inventory only. This destructive initialization
	 * reset can cancel unknown firmware CL work; it cannot resume it or
	 * prove that firmware will never submit again.
	 * GMP RD/WR_ACTIVE are not outstanding counts. ERR VCDI is an idle
	 * indication (VCIV table 87; same bit in the V7.1 Linux register map).
	 * Retain the conservative rejection of every other register bit.
	 */
	if ((snapshot->reg[TV3D_MMU_CTL] & (__BIT(0) | __BIT(7) |
	    __BIT(12) | __BIT(20) | __BIT(27))) != 0 ||
	    (snapshot->reg[TV3D_MMUC_CTL] & __BIT(2)) != 0 ||
	    (snapshot->reg[TV3D_TFU_CS] & __BIT(0)) != 0 ||
	    (snapshot->reg[TV3D_CSD_STATUS] & 0xf) != 0 ||
	    (snapshot->reg[TV3D_GMP_STATUS] & ~TV3D_GMP_ACTIVITY) != 0 ||
	    (snapshot->reg[TV3D_ERROR] & ~TV3D_ERR_VCD_IDLE) != 0 ||
	    snapshot->reg[TV3D_HUB_STATUS] != 0 ||
	    snapshot->reg[TV3D_CORE_STATUS] != 0)
		return EBUSY;
	return 0;
}

static int
tv3d_mask_interrupts(void)
{
	static const uint32_t fields[2] = { TV3D_HUB_IRQS, TV3D_CORE_IRQS };
	uint32_t before, after;
	unsigned int i;
	int error;

	for (i = 0; i < 2; i++) {
		error = tv3d_read(i, TV3D_MASK_STS, &before);
		if (error != 0)
			return error;
		/* W1S: touch only known fields, never restore masks with W1C. */
		error = tv3d_write(i, TV3D_MASK_SET, fields[i]);
		if (error != 0)
			return error;
		error = tv3d_read(i, TV3D_MASK_STS, &after);
		if (error != 0)
			return error;
		if (after != (before | fields[i]))
			return EIO;
	}
	return 0;
}

static int
tv3d_wait_sms(bool reset)
{
	uint32_t ree, tee, state;
	unsigned int i;
	int error;

	for (i = 0; i < TV3D_POLL_COUNT; i++) {
		error = tv3d_read(TV3D_SMS, 0, &ree);
		if (error != 0)
			return error;
		error = tv3d_read(TV3D_SMS, TV3D_SMS_TEE, &tee);
		if (error != 0)
			return error;
		tv3d.last_sms[0] = ree;
		tv3d.last_sms[1] = tee;
		/* SEQ_PC/HUBCORE_STATUS describe progress, not error flags. */
		if ((ree & ~(TV3D_SMS_STATE | TV3D_SMS_PROGRESS)) != 0 ||
		    (tee & ~(TV3D_SMS_STATE | TV3D_SMS_PROGRESS)) != 0x50)
			return EIO;
		state = ree & TV3D_SMS_STATE;
		if (state != 0 && (!reset ||
		    (state != TV3D_SMS_RESET && state != 0xa && state != 0xb)))
			return EIO;
		state = tee & TV3D_SMS_STATE;
		if (state != 0 && (reset || (state != 0xc && state != 0xd)))
			return EIO;
		if (ree == 0 && tee == 0x50)
			return 0;
		delay(TV3D_POLL_US);
	}
	return ETIMEDOUT;
}

int
bcmv3d_takeover_probe(device_t dev, bus_space_tag_t bst)
{
	unsigned int i;
	int error;

	if (tv3d.attempted)
		return EBUSY;
	tv3d.attempted = true;
	tv3d.dev = dev;
	tv3d.bst = bst;
	tv3d.stage = "exclusive PM claim";
	error = bcmpmwdog_v3d_claim(dev);
	if (error != 0)
		goto out;
	tv3d.claimed = true;
	tv3d.stage = "persistent GPU mappings";
	for (i = 0; i < TV3D_NREG; i++) {
		error = bus_space_map(bst, tv3d_addresses[i], tv3d_lengths[i],
		    0, &tv3d.handle[i]);
		if (error != 0)
			goto out;
		tv3d.mapped[i] = true;
	}
	tv3d.stage = "takeover inventory and eligibility";
	error = tv3d_snapshot(&tv3d.before, "before takeover");
	if (error != 0)
		goto out;
	error = tv3d_eligible(&tv3d.before);
	if (error != 0)
		goto out;
	tv3d.stage = "seal ownership before writes";
	error = bcmpmwdog_v3d_seal(dev);
	if (error != 0)
		goto out;
	tv3d.sealed = true;
	tv3d.stage = "initial interrupt masks";
	error = tv3d_mask_interrupts();
	if (error != 0)
		goto out;
	tv3d.stage = "SMS clear power-off";
	error = tv3d_write(TV3D_SMS, TV3D_SMS_TEE, TV3D_SMS_CLEAR_POWER_OFF);
	if (error != 0)
		goto out;
	error = tv3d_wait_sms(false);
	if (error != 0)
		goto out;
	tv3d.stage = "SMS reset";
	error = tv3d_write(TV3D_SMS, 0, TV3D_SMS_RESET);
	if (error != 0)
		goto out;
	error = tv3d_wait_sms(true);
	if (error != 0)
		goto out;
	tv3d.stage = "PM reset cycle";
	error = bcmpmwdog_v3d_reset(dev);
	if (error != 0)
		goto out;
	tv3d.stage = "post-reset interrupt masks";
	error = tv3d_mask_interrupts();
	if (error != 0)
		goto out;
	tv3d.stage = "post-reset inventory";
	error = tv3d_snapshot(&tv3d.after, "after takeover");
	if (error != 0)
		goto out;
	error = tv3d_eligible(&tv3d.after);
	if (error != 0)
		goto out;
	for (i = TV3D_HID1; i <= TV3D_MMU_DEBUG; i++) {
		if (tv3d.before.reg[i] != tv3d.after.reg[i]) {
			error = EIO;
			goto out;
		}
	}
	if (tv3d.before.pm != tv3d.after.pm) {
		error = EIO;
		goto out;
	}
	tv3d.complete = true;
	aprint_normal_dev(dev, "reset takeover complete; ownership and GPU "
	    "mappings retained until reboot; no DMA or command submission\n");
out:
	if (!tv3d.sealed) {
		for (i = 0; i < TV3D_NREG; i++) {
			if (!tv3d.mapped[i])
				continue;
			bus_space_unmap(bst, tv3d.handle[i], tv3d_lengths[i]);
			tv3d.mapped[i] = false;
		}
		if (tv3d.claimed) {
			(void)bcmpmwdog_v3d_release(dev);
			tv3d.claimed = false;
		}
	}
	if (error != 0)
		aprint_normal_dev(dev, "takeover stopped at %s: error %d; "
		    "last polled SMS REE=%#x TEE=%#x; %s\n",
		    tv3d.stage, error, tv3d.last_sms[0], tv3d.last_sms[1],
		    tv3d.sealed ?
		    "ownership/maps retained until reboot; no retry" :
		    "no writes attempted; claim/maps released");
	return error;
}

bool
bcmv3d_takeover_complete(void)
{

	return tv3d.complete;
}

/*
 * HUB access for the opt-in DMA probe. The probe may only observe and
 * program the GPU through the takeover's sealed, persistent mappings.
 */
int
bcmv3d_takeover_hub_peek(bus_size_t offset, uint32_t *value)
{

	if (!tv3d.complete)
		return EPERM;
	return tv3d_read(TV3D_HUB, offset, value);
}

int
bcmv3d_takeover_hub_poke(bus_size_t offset, uint32_t value)
{

	if (!tv3d.complete)
		return EPERM;
	return tv3d_write(TV3D_HUB, offset, value);
}

int
bcmv3d_takeover_core_peek(bus_size_t offset, uint32_t *value)
{

	if (!tv3d.complete)
		return EPERM;
	return tv3d_read(TV3D_CORE, offset, value);
}

int
bcmv3d_takeover_core_poke(bus_size_t offset, uint32_t value)
{

	if (!tv3d.complete)
		return EPERM;
	return tv3d_write(TV3D_CORE, offset, value);
}
