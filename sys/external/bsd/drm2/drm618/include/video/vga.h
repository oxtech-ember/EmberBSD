/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: dist618/drm/i915/display/intel_vga.c (the names it uses); the
 * documented addresses of the IBM VGA registers (sequencer index and data 0x3c4 and 0x3c5, miscellaneous
 * output read 0x3cc and write 0x3c2, bit 5 of sequencer register 1 turns the screen off); the GPL original was not read.
 */
#ifndef _DRM618_VIDEO_VGA_H_
#define _DRM618_VIDEO_VGA_H_

#define	VGA_SEQ_I		0x3c4	/* sequencer index */
#define	VGA_SEQ_D		0x3c5	/* sequencer data */
#define	VGA_MIS_R		0x3cc	/* miscellaneous output, read */
#define	VGA_MIS_W		0x3c2	/* miscellaneous output, write */
#define	VGA_SR01_SCREEN_OFF	(1 << 5)

#endif
