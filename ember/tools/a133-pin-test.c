/* Origin: EmberBSD - keep the verified touch pins outside EMAC ownership. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef int bus_space_tag_t;
typedef uint32_t *bus_space_handle_t;
static uint32_t ccu_regs[1024], pio_regs[1024];
static int
bus_space_map(bus_space_tag_t tag, uint32_t addr, size_t len, int flags,
    bus_space_handle_t *handle)
{
	(void)tag; (void)len; (void)flags;
	if (addr == 0x03001000)
		*handle = ccu_regs;
	else if (addr == 0x0300b000)
		*handle = pio_regs;
	else
		return -1;
	return 0;
}
static uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle, size_t off)
{
	(void)tag;
	return handle[off / 4];
}
static void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle, size_t off,
    uint32_t value)
{
	(void)tag;
	handle[off / 4] = value;
}
static void
bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t handle, size_t len)
{
	(void)tag; (void)handle; (void)len;
}
#include "a133-emac-enable.inc"
int
main(void)
{
	pio_regs[0x100 / 4] = 0x33333333;
	sunxi_emac_a133_enable(0);
	assert(((pio_regs[0x100 / 4] >> 20) & 7) == 3);
	assert(((pio_regs[0x100 / 4] >> 16) & 7) == 3);
	assert(((pio_regs[0x100 / 4] >> 4) & 7) == 5);
	assert(((pio_regs[0x100 / 4] >> 8) & 7) == 5);
	assert(((pio_regs[0x100 / 4] >> 12) & 7) == 1);
	assert((ccu_regs[0x97c / 4] & 0x10001) == 0x10001);
	puts("A133 EMAC pin ownership: touch SDA/SCL preserved");
	return 0;
}
