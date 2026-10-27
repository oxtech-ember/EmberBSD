/* SPDX-License-Identifier: BSD-2-Clause */
/* Execute the actual quirk and helper with a small memory-backed device. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

typedef uint16_t u16;
struct drm_i915_private {
	struct { u16 pri_latency[5], spr_latency[5], cur_latency[5]; } wm;
	int drm;
};
static unsigned printed;
static int
ilk_wm_max_level(struct drm_i915_private *device)
{

	(void)device;
	return 4;
}
static void
intel_print_wm_latency(struct drm_i915_private *device, const char *name,
    const u16 *latency)
{

	(void)device; (void)name; (void)latency;
	printed++;
}
#define max(a, b) ((a) > (b) ? (a) : (b))
#define max_t(type, a, b) ((type)max((a), (b)))
#define DIV_ROUND_UP(a, b) (((a) + (b) - 1) / (b))
#define drm_dbg_kms(device, ...) ((void)(device))
#include "i915-latency.inc"

int
main(void)
{
	for (unsigned mask = 0; mask < 8; mask++) {
		struct drm_i915_private device = {0};
		u16 *planes[] = { device.wm.pri_latency, device.wm.spr_latency,
		    device.wm.cur_latency };
		for (unsigned plane = 0; plane < 3; plane++)
			for (unsigned level = 0; level < 5; level++)
				planes[plane][level] = (mask & (1U << plane)) ? 1 : 20;
		printed = 0;
		snb_wm_latency_quirk(&device);
		for (unsigned plane = 0; plane < 3; plane++)
			for (unsigned level = 0; level < 5; level++)
				assert(planes[plane][level] ==
				    ((mask & (1U << plane)) ? (level ? 3 : 12) : 20));
		assert(printed == (mask ? 3 : 0));
	}
	return 0;
}
