/* SPDX-License-Identifier: MIT */
/* Origin: EmberBSD; MIPS IRQ register facts from Linux PowerVR MIT option. */
/*
 * Copyright (c) 2023 Imagination Technologies Ltd.
 * Copyright (c) 2026 EmberBSD contributors.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * KCCB publication and completion reference: Linux PowerVR driver,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * MIPS interrupt observation and acknowledgement: EmberBSD, AI-assisted.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/errno.h>
#include <dev/powervr/pvr_mips_irq.h>
#define PVR_MIPS_IRQ_STATUS 0x08a8
#define PVR_MIPS_IRQ_CLEAR  0x08b0

int
pvr_mips_irq_pending(bus_space_tag_t tag, bus_space_handle_t handle, bool *pending)
{
	uint32_t value;

	if (tag == NULL || pending == NULL)
		return EINVAL;
	value = bus_space_read_4(tag, handle, PVR_MIPS_IRQ_STATUS);
	*pending = (value & 1) != 0;
	return 0;
}

int
pvr_mips_irq_clear(bus_space_tag_t tag, bus_space_handle_t handle)
{
	if (tag == NULL)
		return EINVAL;
	bus_space_write_4(tag, handle, PVR_MIPS_IRQ_CLEAR, 1);
	bus_space_barrier(tag, handle, PVR_MIPS_IRQ_CLEAR, 4, BUS_SPACE_BARRIER_WRITE);
	return 0;
}
