/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright(c) 2018-2019 Realtek Corporation
 * Copyright (c) 2026 EmberBSD contributors.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
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
/*
 * Adapted under the BSD option from Linux rtw88 rtw8723d.c, mac.c, main.c,
 * reg.h, fw.h and util.c at adc218676eef25575469234709c2d87185ca223a.
 * Only the SDIO-selected power rows and 8051 legacy download path are used.
 * Firmware blobs have a separate license. This code does not implement RF,
 * calibration, networking, or board-level power control.
 */
#ifdef _KERNEL
#include <sys/types.h>
#include <sys/null.h>
#include <sys/systm.h>
#else
#include <string.h>
#endif
#include <dev/ic/rtw88_io.h>
#include <dev/ic/rtw8723d_boot.h>

#define SYS_CFG1		0x00f0
#define MAC_CR		0x0100
#define MCUFW_CTRL	0x0080
#define MCUFW_EN		0x01
#define MCUFW_RDY	0x02
#define FWDL_REPORT	0x04
#define WINTINI_RDY	0x40
#define FW_READY		0xc6
#define ROM_DLEN		0x00080000U
#define ROM_PAGE		0x00070000U
#define SDIO_HIMR	(RTW8723D_SDIO_LOCAL_BASE + 0x14)
#define POWER_POLLS	20000U
#define FW_HEADER_SIZE	32U
#define FW_MAX_SIZE	32768U
#define FW_PAGE_SIZE	4096U

enum power_command { POWER_WRITE, POWER_POLL, POWER_DELAY_MS, POWER_DELAY_US };
struct power_step {
	uint32_t address;
	uint8_t mask, value, command, cuts;
};

#define W(a, m, v) { (a), (m), (v), POWER_WRITE, 0xff }
#define P(a, m, v) { (a), (m), (v), POWER_POLL, 0xff }
#define L(a) (RTW8723D_SDIO_LOCAL_BASE + (a))

/* carddis -> cardemu -> active; interface-only rows are filtered for SDIO. */
static const struct power_step power_on_steps[] = {
	W(0x0005, 0x88, 0x00), W(L(0x0086), 0x01, 0x00),
	P(L(0x0086), 0x02, 0x02), W(0x0005, 0x18, 0x00),
	W(0x0023, 0x10, 0x00),
	W(0x0020, 0x01, 0x01), { 1, 0, 0, POWER_DELAY_MS, 0xff },
	W(0x0000, 0x20, 0x00), W(0x0005, 0x1c, 0x00),
	P(0x0006, 0x02, 0x02), W(0x0006, 0x01, 0x01),
	P(0x0005, 0x03, 0x00), W(0x0005, 0x80, 0x00),
	W(0x0005, 0x18, 0x00), W(0x0005, 0x01, 0x01),
	P(0x0005, 0x01, 0x00), W(0x0010, 0x40, 0x40),
	W(0x0049, 0x02, 0x02), W(0x0063, 0x02, 0x02),
	W(0x0062, 0x02, 0x00), W(0x0058, 0x01, 0x01),
	W(0x005a, 0x02, 0x02), { 0x0068, 0x08, 0x08, POWER_WRITE, 0x01 },
	W(0x0069, 0x40, 0x40), W(0x001f, 0xff, 0x00),
	W(0x0077, 0xff, 0x00), W(0x001f, 0xff, 0x07),
	W(0x0077, 0xff, 0x07)
};

/* active -> LPS -> pre-carddis -> cardemu -> carddis -> post-carddis. */
static const struct power_step power_off_steps[] = {
	W(0x0522, 0xff, 0xff), P(0x05f8, 0xff, 0x00),
	P(0x05f9, 0xff, 0x00), P(0x05fa, 0xff, 0x00),
	P(0x05fb, 0xff, 0x00), W(0x0002, 0x01, 0x00),
	{ 0, 0, 0, POWER_DELAY_US, 0xff }, W(0x0002, 0x02, 0x00),
	W(0x0100, 0xff, 0x03), W(0x0101, 0x02, 0x00),
	W(0x0093, 0xff, 0x00), W(0x0553, 0x20, 0x20),
	W(0x0003, 0x04, 0x00), W(0x0080, 0xff, 0x00),
	W(0x0002, 0x01, 0x00), W(0x0049, 0x02, 0x00),
	W(0x0006, 0x01, 0x01), W(0x0005, 0x02, 0x02),
	P(0x0005, 0x02, 0x00), W(0x0010, 0x40, 0x00),
	W(0x0000, 0x20, 0x20), W(0x0020, 0x01, 0x00),
	W(0x0007, 0xff, 0x20), W(0x0005, 0x18, 0x08),
	W(0x0023, 0x10, 0x10), W(L(0x0086), 0x01, 0x01),
	P(L(0x0086), 0x02, 0x00), W(0x001d, 0x01, 0x00),
	W(0x001d, 0x01, 0x01), W(0x001c, 0xff, 0x0e)
};
#undef W
#undef P
#undef L

static int
fail(struct rtw88_io *io, int error)
{

	if (io->error == 0)
		io->error = error;
	return io->error;
}

static bool
check_cancel(struct rtw88_io *io, const struct rtw8723d_boot_ops *ops)
{

	if (io->error == 0 && ops->cancelled(ops->cookie))
		fail(io, ECANCELED);
	return io->error != 0;
}

static bool
valid_ops(struct rtw88_io *io, const struct rtw8723d_boot_ops *ops, bool window)
{

	if (io == NULL)
		return false;
	if (io->error != 0)
		return false;
	if (ops == NULL || ops->wait_ms == NULL || ops->cancelled == NULL ||
	    (window && ops->write_window == NULL)) {
		fail(io, EINVAL);
		return false;
	}
	return true;
}

static void
update(struct rtw88_io *io, uint32_t address, unsigned width,
    uint32_t mask, uint32_t value)
{
	uint32_t old = rtw88_io_rd(io, address, width);

	rtw88_io_wr(io, address, width, (old & ~mask) | (value & mask));
}

int
rtw8723d_identify(struct rtw88_io *io, struct rtw8723d_identity *identity)
{
	struct rtw8723d_identity result;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	if (identity == NULL)
		return fail(io, EINVAL);
	result.sys_cfg1 = rtw88_io_rd(io, SYS_CFG1, 4);
	if (io->error != 0)
		return io->error;
	result.cut = (result.sys_cfg1 >> 12) & 0xf;
	result.rf_paths = (result.sys_cfg1 & 0x08000000U) != 0 ? 2 : 1;
	*identity = result;
	return 0;
}

static void
power_steps(struct rtw88_io *io, const struct rtw8723d_boot_ops *ops,
    const struct power_step *steps, size_t count, unsigned cut, bool cancel)
{
	const struct power_step *step;
	uint32_t value;
	unsigned tries;
	size_t i;

	for (i = 0; i < count && io->error == 0; i++) {
		step = &steps[i];
		if ((step->cuts & (1U << (cut + 1))) == 0)
			continue;
		if (cancel && check_cancel(io, ops))
			return;
		switch (step->command) {
		case POWER_WRITE:
			update(io, step->address, 1, step->mask, step->value);
			break;
		case POWER_POLL:
			for (tries = 0; tries < POWER_POLLS; tries++) {
				if (cancel && check_cancel(io, ops))
					return;
				value = rtw88_io_rd(io, step->address, 1);
				if (io->error != 0 || (value & step->mask) == step->value)
					break;
				rtw88_io_delay(io, 50);
			}
			if (tries == POWER_POLLS)
				fail(io, ETIMEDOUT);
			break;
		case POWER_DELAY_MS:
			ops->wait_ms(ops->cookie, step->address);
			break;
		case POWER_DELAY_US:
			rtw88_io_delay(io, step->address);
			break;
		}
	}
	if (cancel)
		(void)check_cancel(io, ops);
}

static void
cleanup_power(struct rtw88_io *io, const struct rtw8723d_boot_ops *ops,
    unsigned cut)
{
	struct rtw88_io cleanup;

	rtw88_io_init(&cleanup, io->ops, io->cookie);
	/* An earlier restore can have completed even when its write failed. */
	rtw88_io_wr(&cleanup, SDIO_HIMR, 4, 0);
	power_steps(&cleanup, ops, power_off_steps,
	    sizeof(power_off_steps) / sizeof(power_off_steps[0]), cut, false);
}

static void
pre_system_config(struct rtw88_io *io)
{
	uint32_t configuration;

	rtw88_io_wr(io, 0x001c, 1, 0); /* RSV_CTRL: unlock system registers. */
	configuration = rtw88_io_rd(io, SYS_CFG1, 4);
	/* Select the chip's strapped LDO/SPS mode, not a board regulator. */
	rtw88_io_wr(io, 0x007c, 1,
	    (configuration & 0x01000000U) != 0 ? 0xc3 : 0x83);
}

static void
post_system_config(struct rtw88_io *io, const struct rtw8723d_boot_ops *ops)
{

	rtw88_io_wr(io, MAC_CR, 1, 0xff);
	if (check_cancel(io, ops))
		return;
	ops->wait_ms(ops->cookie, 2);
	if (check_cancel(io, ops))
		return;
	rtw88_io_wr(io, 0x0423, 1, 0x7f); /* HWSEQ_CTRL */
	if (check_cancel(io, ops))
		return;
	ops->wait_ms(ops->cookie, 2);
	if (check_cancel(io, ops))
		return;
	rtw88_io_set(io, 0x0008, 1, 0x08); /* SYS_CLKR: WAKEPAD_EN */
	rtw88_io_clr(io, 0x0040, 2, 0x1000); /* GPIO_MUXCFG: disable SIC */
	rtw88_io_wr(io, MAC_CR, 2, 0x02ff);
}

static int
power_switch(struct rtw88_io *io, const struct rtw8723d_boot_ops *ops, bool on)
{
	struct rtw8723d_identity identity;
	struct rtw88_io restore;
	uint32_t interrupts;
	int error;
	bool started = false;

	if (!valid_ops(io, ops, false))
		return io != NULL ? io->error : EINVAL;
	if (on && check_cancel(io, ops))
		return io->error;
	if (rtw8723d_identify(io, &identity) != 0)
		return io->error;
	/* ALL is an eight-bit table mask; unknown cuts must not skip all rows. */
	if (identity.cut > 6)
		return fail(io, ENOTSUP);
	if (!on) {
		uint32_t control = rtw88_io_rd(io, MAC_CR, 1);

		if (io->error != 0 || control == 0xea)
			return io->error;
	}
	interrupts = rtw88_io_rd(io, SDIO_HIMR, 4);
	if (io->error != 0)
		return io->error;
	rtw88_io_wr(io, SDIO_HIMR, 4, 0);
	if (io->error == 0) {
		started = true;
		if (on) {
			uint32_t control;

			pre_system_config(io);
			control = rtw88_io_rd(io, MAC_CR, 1);
			if (io->error == 0 && control != 0xea) {
				/* Donor EALREADY recovery: one bounded off/pre/on cycle. */
				power_steps(io, ops, power_off_steps,
				    sizeof(power_off_steps) / sizeof(power_off_steps[0]),
				    identity.cut, false);
				if (!check_cancel(io, ops)) {
					pre_system_config(io);
					control = rtw88_io_rd(io, MAC_CR, 1);
					if (io->error == 0 && control != 0xea)
						fail(io, EBUSY);
				}
			}
			power_steps(io, ops, power_on_steps,
			    sizeof(power_on_steps) / sizeof(power_on_steps[0]), identity.cut, true);
			if (io->error == 0)
				post_system_config(io, ops);
		} else {
			power_steps(io, ops, power_off_steps,
			    sizeof(power_off_steps) / sizeof(power_off_steps[0]), identity.cut, false);
		}
	}
	error = io->error;
	if (error != 0 && on && started)
		cleanup_power(io, ops, identity.cut);
	/* Even a failed mask write may have reached the chip. */
	rtw88_io_init(&restore, io->ops, io->cookie);
	rtw88_io_wr(&restore, SDIO_HIMR, 4, interrupts);
	if (error == 0 && restore.error != 0) {
		error = restore.error;
		if (on && started) {
			cleanup_power(io, ops, identity.cut);
			rtw88_io_init(&restore, io->ops, io->cookie);
			rtw88_io_wr(&restore, SDIO_HIMR, 4, interrupts);
		}
	}
	if (error == 0 && on && check_cancel(io, ops)) {
		error = io->error;
		cleanup_power(io, ops, identity.cut);
		rtw88_io_init(&restore, io->ops, io->cookie);
		rtw88_io_wr(&restore, SDIO_HIMR, 4, interrupts);
	}
	io->error = error;
	return error;
}

int
rtw8723d_power_on(struct rtw88_io *io, const struct rtw8723d_boot_ops *ops)
{

	return power_switch(io, ops, true);
}

int
rtw8723d_power_off(struct rtw88_io *io, const struct rtw8723d_boot_ops *ops)
{

	return power_switch(io, ops, false);
}

static uint16_t
get_le16(const uint8_t *p)
{

	return p[0] | (uint16_t)p[1] << 8;
}

int
rtw8723d_fw_parse(const uint8_t *data, size_t length, struct rtw8723d_fw *firmware)
{
	struct rtw8723d_fw result;
	size_t payload;

	if (data == NULL || firmware == NULL || length < FW_HEADER_SIZE)
		return EINVAL;
	payload = length - FW_HEADER_SIZE;
	if (get_le16(data) != 0x23d1 || payload == 0 || payload > FW_MAX_SIZE ||
	    get_le16(data + 12) != payload)
		return EINVAL;
	result.payload = data + FW_HEADER_SIZE;
	result.payload_size = payload;
	result.version = get_le16(data + 4);
	*firmware = result;
	return 0;
}

static void
cpu_enable(struct rtw88_io *io, bool enable)
{

	if (enable) {
		rtw88_io_set(io, 0x001d, 1, 1);
		rtw88_io_set(io, 0x0003, 1, 4);
	} else {
		rtw88_io_clr(io, 0x0003, 1, 4);
		rtw88_io_clr(io, 0x001d, 1, 1);
	}
}

static void
cleanup_firmware(struct rtw88_io *io)
{
	struct rtw88_io cleanup;

	/* Each owned release gets a fresh context even if another release fails. */
	rtw88_io_init(&cleanup, io->ops, io->cookie);
	rtw88_io_clr(&cleanup, MCUFW_CTRL, 1, MCUFW_EN | MCUFW_RDY);
	rtw88_io_init(&cleanup, io->ops, io->cookie);
	rtw88_io_clr(&cleanup, 0x0003, 1, 4);
	rtw88_io_init(&cleanup, io->ops, io->cookie);
	rtw88_io_clr(&cleanup, 0x001d, 1, 1);
}

static void
poll_firmware(struct rtw88_io *io, const struct rtw8723d_boot_ops *ops,
    uint32_t mask, uint32_t expected, unsigned attempts, unsigned wait_ms)
{
	uint32_t value;
	unsigned i;

	for (i = 0; i < attempts; i++) {
		if (check_cancel(io, ops))
			return;
		value = rtw88_io_rd(io, MCUFW_CTRL, 4);
		if (io->error != 0 || (value & mask) == expected)
			return;
		if (wait_ms != 0)
			ops->wait_ms(ops->cookie, wait_ms);
		else
			rtw88_io_delay(io, 10);
	}
	fail(io, ETIMEDOUT);
}

int
rtw8723d_fw_boot(struct rtw88_io *io, const struct rtw8723d_fw *firmware,
    const struct rtw8723d_boot_ops *ops)
{
	struct rtw88_io disable;
	uint8_t word[4];
	uint32_t value;
	size_t page, offset, count, remain, i;
	unsigned attempt;
	int error;
	bool started = false;

	if (!valid_ops(io, ops, true))
		return io != NULL ? io->error : EINVAL;
	if (firmware == NULL || firmware->payload == NULL ||
	    firmware->payload_size == 0 || firmware->payload_size > FW_MAX_SIZE)
		return fail(io, EINVAL);
	if (check_cancel(io, ops))
		return io->error;
	/* Exclusive chip ownership is required before the first CPU write. */
	started = true;
	cpu_enable(io, false);
	cpu_enable(io, true);
	rtw88_io_set(io, MCUFW_CTRL, 1, MCUFW_EN);
	for (attempt = 0; attempt < 10 && io->error == 0; attempt++) {
		if (check_cancel(io, ops))
			break;
		value = rtw88_io_rd(io, MCUFW_CTRL, 1);
		if (io->error != 0 || (value & MCUFW_EN) != 0)
			break;
		rtw88_io_set(io, MCUFW_CTRL, 1, MCUFW_EN);
		if (io->error == 0)
			ops->wait_ms(ops->cookie, 20);
	}
	if (attempt == 10)
		fail(io, ETIMEDOUT);
	rtw88_io_clr(io, MCUFW_CTRL, 4, ROM_DLEN);
	rtw88_io_set(io, MCUFW_CTRL, 1, FWDL_REPORT);
	for (page = 0, offset = 0; offset < firmware->payload_size && io->error == 0;
	    page++, offset += count) {
		if (check_cancel(io, ops))
			break;
		remain = firmware->payload_size - offset;
		count = remain > FW_PAGE_SIZE ? FW_PAGE_SIZE : remain;
		update(io, MCUFW_CTRL, 4, ROM_PAGE, (uint32_t)page << 16);
		for (i = 0; i < count && io->error == 0; i += sizeof(word)) {
			if (check_cancel(io, ops))
				break;
			remain = count - i;
			memset(word, 0, sizeof(word));
			memcpy(word, firmware->payload + offset + i,
			    remain < sizeof(word) ? remain : sizeof(word));
			if (io->error == 0) {
				error = ops->write_window(ops->cookie, 0x1000 + (uint32_t)i,
				    word, sizeof(word));
				if (error != 0)
					fail(io, error);
			}
		}
	}
	poll_firmware(io, ops, FWDL_REPORT, FWDL_REPORT, 1000, 0);
	error = io->error;
	/* Disable download mode after every payload attempt, including errors. */
	rtw88_io_init(&disable, io->ops, io->cookie);
	rtw88_io_clr(&disable, MCUFW_CTRL, 1, MCUFW_EN);
	if (error == 0)
		error = disable.error;
	io->error = error;
	if (error == 0) {
		update(io, MCUFW_CTRL, 4, MCUFW_RDY | WINTINI_RDY, MCUFW_RDY);
		cpu_enable(io, false);
		cpu_enable(io, true);
		poll_firmware(io, ops, FW_READY, FW_READY, 10, 20);
		(void)check_cancel(io, ops);
	}
	if (io->error != 0 && started)
		cleanup_firmware(io);
	return io->error;
}
