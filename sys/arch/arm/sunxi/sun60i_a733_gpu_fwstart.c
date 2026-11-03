/* SPDX-License-Identifier: MIT */
/* Origin: EmberBSD PowerVR adapter; MIT-option Linux reference, AI-assisted. */
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
 * Register definitions and MIPS startup sequence: Linux PowerVR,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * Native fault-aware startup: EmberBSD, AI-assisted.
 */

#include "sun60i_a733_gpu_fwstart.h"

int
sun60i_a733_gpu_fwstart(bus_space_tag_t bst, bus_space_handle_t bsh,
    const struct sun60i_a733_gpu_fwinfo *info, uint64_t code,
    uint64_t data, uint64_t exception,
    struct sun60i_a733_gpu_fwstart_result *result)
{
	return pvr_fwstart(PVR_FW_PROFILE_A733, bst, bsh, info, code, data,
	    exception, result);
}
