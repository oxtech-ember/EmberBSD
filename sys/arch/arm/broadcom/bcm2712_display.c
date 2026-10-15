/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Resource facts: raspberrypi/linux a1a601d4857978d810175365550c4536701df4ed,
 * bcm2712.dtsi and bcm2712-rpi-cm5.dtsi; firmware legacy order: eotics-com/
 * edk2-platforms c4b5d05de1f2ef633bdb4c175b5c118fcb2666ed, RPi5 Dsdt.asl.
 * Linux GPL implementation code is not copied here.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <arm/broadcom/bcm2712_display.h>
#include <arch/evbarm/rpi/vcprop.h>

#define MEM(n, o, a, l) { n, BCM2712_OWNER_##o, { UINT64_C(a), l } }
const struct bcm2712_display_resource
bcm2712_display_resources[BCM2712_DISPLAY_NMEM] = {
	MEM("v3d-hub", RENDER, 0x1002000000, 0x4000),
	MEM("v3d-core", RENDER, 0x1002008000, 0x6000),
	MEM("v3d-sms", RENDER, 0x1002030800, 0x700),
	MEM("hvs", SCANOUT, 0x107c580000, 0x1a000),
	MEM("hvs-iommu", SCANOUT, 0x1000005200, 0x80),
	MEM("pixelvalve0", SCANOUT, 0x107c410000, 0x100),
	MEM("pixelvalve1", SCANOUT, 0x107c411000, 0x100),
	MEM("mop", SCANOUT, 0x107c500000, 0x28),
	MEM("moplet", SCANOUT, 0x107c501000, 0x20),
	MEM("display-irq", SCANOUT, 0x107c502000, 0x30),
	MEM("dvp-clock-reset", DVP_CLOCK, 0x107c700000, 0x10),
	MEM("hd-shared", HD_SHARED, 0x107c720000, 0x100),
	MEM("aon-irq", AON_IRQ, 0x107d510600, 0x30),
	MEM("bsc-irq", BSC_IRQ, 0x107d508380, 0x10),
	MEM("ddc0", DDC0, 0x107d508200, 0x58),
	MEM("ddc1", DDC1, 0x107d508280, 0x58),
	MEM("hdmi0-core", HDMI0, 0x107c701400, 0x300),
	MEM("hdmi0-dvp", HDMI0, 0x107c701000, 0x200),
	MEM("hdmi0-phy", HDMI0, 0x107c701d00, 0x300),
	MEM("hdmi0-rm", HDMI0, 0x107c702000, 0x80),
	MEM("hdmi0-packet", HDMI0, 0x107c703800, 0x200),
	MEM("hdmi0-metadata", HDMI0, 0x107c704000, 0x800),
	MEM("hdmi0-csc", HDMI0, 0x107c700100, 0x80),
	MEM("hdmi0-cec", HDMI0, 0x107d510800, 0x100),
	MEM("hdmi1-core", HDMI1, 0x107c706400, 0x300),
	MEM("hdmi1-dvp", HDMI1, 0x107c706000, 0x200),
	MEM("hdmi1-phy", HDMI1, 0x107c706d00, 0x300),
	MEM("hdmi1-rm", HDMI1, 0x107c707000, 0x80),
	MEM("hdmi1-packet", HDMI1, 0x107c708800, 0x200),
	MEM("hdmi1-metadata", HDMI1, 0x107c709000, 0x800),
	MEM("hdmi1-csc", HDMI1, 0x107c700180, 0x80),
	MEM("hdmi1-cec", HDMI1, 0x107d511000, 0x100)
};
#undef MEM

const struct bcm2712_display_irq bcm2712_display_irqs[BCM2712_DISPLAY_NIRQ] = {
	/* Preserve firmware order; native TFU completion uses GSI 282. */
	{ "v3d-hub", 282 }, { "v3d-core", 281 },
	{ "pixelvalve0", 133 }, { "pixelvalve1", 142 },
	{ "display", 129 }, { "aon", 271 }, { "bsc", 274 }
};

const struct bcm2712_display_domain
bcm2712_display_domains[BCM2712_DISPLAY_NDOMAIN] = {
	{ "display", 9, 4 }, { "aon", 12, 5 }, { "bsc", 13, 6 }
};

const struct bcm2712_display_leaf
bcm2712_display_leaves[BCM2712_DISPLAY_NLEAF] = {
	{ "moplet", 0, 0 }, { "mop", 0, 1 },
	{ "hvs-eof0", 0, 2 }, { "hvs-eof1", 0, 9 }, { "hvs-eof2", 0, 16 },
	{ "hdmi0-cec-tx", 1, 1 }, { "hdmi0-cec-rx", 1, 2 },
	{ "hdmi0-cec-low", 1, 3 },
	{ "hdmi0-connected", 1, 7 }, { "hdmi0-removed", 1, 8 },
	{ "hdmi1-cec-tx", 1, 11 }, { "hdmi1-cec-rx", 1, 12 },
	{ "hdmi1-cec-low", 1, 13 },
	{ "hdmi1-connected", 1, 14 }, { "hdmi1-removed", 1, 15 },
	{ "ddc0", 2, 1 }, { "ddc1", 2, 2 }
};

int
bcm2712_display_decode(uint64_t contract_version,
    const struct bcm2712_display_memory *memory, size_t nmem,
    const uint32_t *irq, size_t nirq, struct bcm2712_display_contract *out)
{
	struct bcm2712_display_contract result;
	size_t i, j;

	if (out == NULL)
		return EINVAL;
	if (contract_version != BCM2712_DISPLAY_VERSION)
		return EOPNOTSUPP;
	if (memory == NULL || irq == NULL || nmem != BCM2712_DISPLAY_NMEM ||
	    nirq != BCM2712_DISPLAY_NIRQ)
		return EINVAL;
	for (i = 0; i < nmem; i++) {
		if (memory[i].length == 0 ||
		    memory[i].address > UINT64_MAX - (memory[i].length - 1))
			return EINVAL;
		for (j = 0; j < i; j++)
			if (memory[i].address <= memory[j].address + memory[j].length - 1 &&
			    memory[j].address <= memory[i].address + memory[i].length - 1)
				return EINVAL;
		if (memory[i].address != bcm2712_display_resources[i].memory.address ||
		    memory[i].length != bcm2712_display_resources[i].memory.length)
			return EINVAL;
	}
	for (i = 0; i < nirq; i++)
		if (irq[i] != bcm2712_display_irqs[i].gsi)
			return EINVAL;
	memset(&result, 0, sizeof(result));
	result.version = BCM2712_DISPLAY_VERSION;
	memcpy(result.memory, memory, sizeof(result.memory));
	memcpy(result.irq, irq, sizeof(result.irq));
	*out = result;
	return 0;
}

int
bcm2712_display_read_clocks(bcm2712_display_exchange_fn exchange, void *arg,
    struct bcm2712_display_clocks *out)
{
	static const uint32_t ids[BCM2712_DISPLAY_NCLOCK] = {
		VCPROP_CLK_M2MC, VCPROP_CLK_PIXEL_BVB, VCPROP_CLK_CORE,
		VCPROP_CLK_DISP
	};
	static const uint32_t tags[2] = {
		VCPROPTAG_GET_CLOCKSTATE, VCPROPTAG_GET_CLOCKRATE
	};
	struct bcm2712_display_clocks result;
	uint32_t request[8] __aligned(16), values[2];
	unsigned int i, j;

	if (exchange == NULL || out == NULL)
		return EINVAL;
	memset(&result, 0, sizeof(result));
	for (i = 0; i < BCM2712_DISPLAY_NCLOCK; i++) {
		for (j = 0; j < 2; j++) {
			int error;

			memset(request, 0, sizeof(request));
			request[0] = htole32(sizeof(request));
			request[2] = htole32(tags[j]);
			request[3] = htole32(8);
			request[5] = htole32(ids[i]);
			error = exchange(arg, request);
			if (error != 0)
				return error;
			if (le32toh(request[0]) != sizeof(request) ||
			    le32toh(request[1]) != VCPROP_REQ_SUCCESS ||
			    le32toh(request[2]) != tags[j] || le32toh(request[3]) != 8 ||
			    le32toh(request[4]) != (VCPROPTAG_RESPONSE | 8) ||
			    le32toh(request[5]) != ids[i] || request[7] != 0)
				return EIO;
			values[j] = le32toh(request[6]);
		}
		if (values[0] != 1 || values[1] == 0 ||
		    (ids[i] == VCPROP_CLK_M2MC &&
		    values[1] < BCM2712_DISPLAY_HSM_MIN_RATE))
			return EBUSY;
		result.clock[i].id = ids[i];
		result.clock[i].state = values[0];
		result.clock[i].rate = values[1];
	}
	*out = result;
	return 0;
}
