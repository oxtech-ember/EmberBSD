/*	$NetBSD$	*/

/*
 * Shadow-frame copy logic of efidrm, free of kernel dependencies so that a
 * host test can drive it (sys/external/bsd/drm2/efidrm/tests).
 *
 * The shadow is a copy of what is on the GOP.  It may only be used for
 * comparison while it is valid.  Every commit that does not itself keep the
 * shadow in step (shadow switched off, full redraw, nocopy, external
 * invalidation) first drops "valid"; while the shadow is invalid every copy
 * is mirrored into it, and it becomes valid again after a copy that covers
 * the whole screen.  Review Dev Bots #471.1.
 */

#ifndef EFIDRM_SHADOW_H
#define EFIDRM_SHADOW_H

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#endif

static inline uint64_t
efidrm_ld64(const void *p)
{
	uint64_t v;

	memcpy(&v, p, sizeof(v));
	return v;
}

#define EFIDRM_SEG	256	/* compare granularity, bytes */
#define EFIDRM_MASK64	UINT64_C(0x00ffffff00ffffff)	/* alpha/X byte is not visible */

/* Do the visible bits of two pixel spans differ? n is a multiple of 4. */
static bool
efidrm_seg_differs(const uint8_t *a, const uint8_t *b, size_t n)
{
	uint64_t acc = 0;
	size_t i = 0;

	for (; i + 8 <= n; i += 8)
		acc |= (efidrm_ld64(a + i) ^ efidrm_ld64(b + i)) & EFIDRM_MASK64;
	for (; i < n; i += 4) {
		uint32_t x, y;

		memcpy(&x, a + i, 4);
		memcpy(&y, b + i, 4);
		acc |= (x ^ y) & 0x00ffffffu;
	}
	return acc != 0;
}

/* Copy the differing runs of one row to the GOP and the shadow. */
static size_t
efidrm_row_diff(uint8_t *dst, uint8_t *sh, const uint8_t *src, size_t nbytes)
{
	size_t off = 0, start, n, written = 0;

	while (off < nbytes) {
		n = nbytes - off < (size_t)EFIDRM_SEG ? nbytes - off : (size_t)EFIDRM_SEG;
		if (!efidrm_seg_differs(src + off, sh + off, n)) {
			off += n;
			continue;
		}
		start = off;
		off += n;
		while (off < nbytes) {
			n = nbytes - off < (size_t)EFIDRM_SEG ?
			    nbytes - off : (size_t)EFIDRM_SEG;
			if (!efidrm_seg_differs(src + off, sh + off, n))
				break;
			off += n;
		}
		memcpy(dst + start, src + start, off - start);
		memcpy(sh + start, src + start, off - start);
		written += off - start;
	}
	return written;
}

/*
 * Decide how a commit treats the shadow.  *valid is updated before the copy;
 * returns whether to compare against the shadow (use_shadow).  have: the
 * shadow was allocated; tun: hw.efidrm.shadow; inval: external invalidation.
 */
static inline bool
efidrm_shadow_plan(bool have, bool tun, bool inval, bool full, bool nocopy,
    bool *valid)
{
	if (!have)
		return false;
	if (inval || !tun || full || nocopy)
		*valid = false;
	return tun && *valid;
}

/* After the copy: a whole-screen copy fills the shadow completely. */
static inline void
efidrm_shadow_done(bool have, bool nocopy, bool cover_all, bool *valid)
{
	if (have && !nocopy && !*valid && cover_all)
		*valid = true;
}

/*
 * Copy h rows of w4 bytes.  use_shadow: write only the differences; else
 * copy everything and, if mirror, into the shadow as well.  Returns bytes
 * written to the GOP.
 */
static inline size_t
efidrm_copy_rows(uint8_t *gop, size_t gstride, uint8_t *sh, const uint8_t *src,
    size_t spitch, size_t w4, int h, bool use_shadow, bool mirror,
    size_t *cmp_bytes)
{
	size_t total = 0;
	int row;

	for (row = 0; row < h; row++) {
		uint8_t *d = gop + (size_t)row * gstride;
		const uint8_t *s = src + (size_t)row * spitch;

		if (use_shadow) {
			total += efidrm_row_diff(d, sh + (size_t)row * gstride, s, w4);
			*cmp_bytes += w4;
		} else {
			memcpy(d, s, w4);
			if (mirror)
				memcpy(sh + (size_t)row * gstride, s, w4);
			total += w4;
		}
	}
	return total;
}

#endif /* EFIDRM_SHADOW_H */
