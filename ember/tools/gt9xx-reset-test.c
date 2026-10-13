/* Origin: EmberBSD - verify YS-M33 reset ordering and preserve shared pins. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef int bus_space_tag_t;
typedef uint32_t *bus_space_handle_t;
#define BUS_SPACE_BARRIER_WRITE 1
static uint32_t main_regs[256], r_regs[256];
static unsigned int step;
static int
bus_space_map(bus_space_tag_t tag, uint32_t addr, size_t len, int flags,
    bus_space_handle_t *handle)
{
	(void)tag; (void)len; (void)flags;
	if (addr == 0x0300b000) *handle = main_regs;
	else if (addr == 0x07022000) *handle = r_regs;
	else return EIO;
	return 0;
}
static uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle, size_t off)
{
	(void)tag; return handle[off / 4];
}
static void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle, size_t off,
    uint32_t value)
{
	(void)tag; handle[off / 4] = value;
}
static void
bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t handle, size_t size)
{
	(void)tag; (void)handle; (void)size;
}
static void
bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle, size_t off,
    size_t size, int flags)
{
	(void)tag; (void)handle; (void)off; (void)size; (void)flags;
}
static void
delay(unsigned int usec)
{
	const unsigned int delays[] = {20000, 6000, 50000, 10000};
	assert(step < 4 && usec == delays[step]);
	assert((main_regs[0x10c / 4] & (1U << 14)) ==
	    (step == 0 ? 0 : (1U << 14)));
	assert((r_regs[0x10 / 4] & (1U << 10)) == 0);
	assert(((r_regs[0x04 / 4] >> 8) & 7) == (step == 3 ? 0 : 1));
	step++;
}
#include "gt9xx-reset.inc"
int
main(void)
{
	main_regs[0x100 / 4] = 0x17331557;
	main_regs[0x10c / 4] = 0x000d8800;
	r_regs[0x00 / 4] = 0x22222222;
	r_regs[0x04 / 4] = 0x77777777;
	r_regs[0x10 / 4] = 0xabcdefef;
	assert(gt9xx_ys_m33_reset(0) == 0 && step == 4);
	assert(main_regs[0x100 / 4] == 0x11331557);
	assert(main_regs[0x10c / 4] == 0x000dc800);
	assert(r_regs[0x00 / 4] == 0x22222222);
	assert(r_regs[0x04 / 4] == 0x77777077);
	assert(r_regs[0x10 / 4] == (0xabcdefefU & ~(1U << 10)));
	puts("Goodix reset: timing, address strap and MCU bus preservation passed");
	return 0;
}
