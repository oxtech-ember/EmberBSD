/* Origin: EmberBSD - preserve the YS-M33 vendor GPIO route for GT9271 reset. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

int gt9xx_ys_m33_reset(bus_space_tag_t);

int
gt9xx_ys_m33_reset(bus_space_tag_t bst)
{
	bus_space_handle_t pio, rpio;
	uint32_t value;
	int error;

	error = bus_space_map(bst, 0x0300b000, 0x400, 0, &pio);
	if (error != 0)
		return error;
	error = bus_space_map(bst, 0x07022000, 0x400, 0, &rpio);
	if (error != 0) {
		bus_space_unmap(bst, pio, 0x400);
		return error;
	}
	/* Only PH14 (reset) and PL10 (INT/address selection) are owned here. */
	value = bus_space_read_4(bst, pio, 0x10c) & ~(1U << 14);
	bus_space_write_4(bst, pio, 0x10c, value);
	value = bus_space_read_4(bst, pio, 0x100);
	value = (value & ~(7U << 24)) | (1U << 24);
	bus_space_write_4(bst, pio, 0x100, value);
	value = bus_space_read_4(bst, rpio, 0x10) & ~(1U << 10);
	bus_space_write_4(bst, rpio, 0x10, value);
	value = bus_space_read_4(bst, rpio, 0x04);
	value = (value & ~(7U << 8)) | (1U << 8);
	bus_space_write_4(bst, rpio, 0x04, value);
	bus_space_barrier(bst, pio, 0, 0x400, BUS_SPACE_BARRIER_WRITE);
	bus_space_barrier(bst, rpio, 0, 0x400, BUS_SPACE_BARRIER_WRITE);
	delay(20000);
	value = bus_space_read_4(bst, pio, 0x10c) | (1U << 14);
	bus_space_write_4(bst, pio, 0x10c, value);
	bus_space_barrier(bst, pio, 0, 0x400, BUS_SPACE_BARRIER_WRITE);
	delay(6000);
	/* INT stays low for the synchronization pulse before becoming input. */
	delay(50000);
	value = bus_space_read_4(bst, rpio, 0x04) & ~(7U << 8);
	bus_space_write_4(bst, rpio, 0x04, value);
	bus_space_barrier(bst, rpio, 0, 0x400, BUS_SPACE_BARRIER_WRITE);
	delay(10000);
	bus_space_unmap(bst, rpio, 0x400);
	bus_space_unmap(bst, pio, 0x400);
	return 0;
}
