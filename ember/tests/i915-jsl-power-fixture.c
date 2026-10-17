/* SPDX-License-Identifier: BSD-2-Clause */
/* Host state and MMIO hooks. Extracted driver code retains its notices. */
@LICENSES@
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
@ENUMS@
#define _MMIO(x) (x)
@REGS@
struct intel_fbc_state_cache {
 struct { int visible, rotation, pixel_blend_mode, src_h, adjusted_y; } plane;
 struct { int mode_flags, hsw_bdw_pixel_rate; } crtc;
 struct { int stride; const struct format { int format, has_alpha; } *format; } fb;
 int fence_id;
};
struct intel_fbc { struct intel_fbc_state_cache state_cache; bool underrun_detected; const char *no_fbc_reason; };
struct i915_power_domains { int lock, async_put_work, power_well_count; const int *selected; };
struct intel_uncore { int unused; };
struct drm_i915_private {
 enum intel_platform platform; enum intel_pch pch_type; int gen;
 struct { bool is_lp, is_g4x, is_mobile; } info;
 struct intel_fbc fbc;
 struct { struct { int cdclk; } hw; } cdclk;
 struct { int allowed_dc_mask, target_dc_state; } csr;
 struct i915_power_domains power_domains;
 struct intel_uncore uncore;
};
struct intel_crtc { struct { struct drm_i915_private *dev; } base; };
#define to_i915(x) (x)
#define INTEL_GEN(p) ((p)->gen)
#define INTEL_INFO(p) (&(p)->info)
#define INTEL_PCH_TYPE(p) ((p)->pch_type)
#define HAS_PCH_JSP(p) (INTEL_PCH_TYPE(p) == PCH_JSP)
#define HAS_PCH_MCC(p) (INTEL_PCH_TYPE(p) == PCH_MCC)
#define INTEL_SUBPLATFORM_PORTF 1
#define IS_SUBPLATFORM(p, x, s) false
#define IS_PLATFORM(p, x) ((p)->platform == (x))
#define IS_GEN(p, x) (INTEL_GEN(p) == (x))
#define IS_MOBILE(p) ((p)->info.is_mobile)
@PREDICATES@
#define DRM_MODE_FLAG_INTERLACE 1
#define DRM_MODE_ROTATE_0 0
#define DRM_MODE_BLEND_PIXEL_NONE 0
#define POWER_DOMAIN_NUM 1
#define DC_STATE_EN_UPTO_DC6 6
#define BUILD_BUG_ON(x) _Static_assert(!(x), "power-domain bound")
#define mutex_init(x) ((void)(x))
#define INIT_DELAYED_WORK(x, f) ((void)(x))
static struct { int disable_power_well, enable_dc; } i915_modparams;
#define sanitize_disable_power_well_option(p, x) (x)
#define get_allowed_dc_mask(p, x) (x)
#define sanitize_target_dc_state(p, x) (x)
#define intel_fbc_hw_tracking_covers_screen(x) true
#define stride_is_valid(p, x) true
#define pixel_format_is_valid(p, x) true
#define intel_fbc_cfb_size_changed(p) false
@TABLES@
static int set_power_wells(struct i915_power_domains *p, const int *s) { p->selected = s; return 0; }
/* Ordered hooks retain unrelated bits in the modeled MMIO register. */
static unsigned events[16], count, chicken;
static void event(unsigned e) { assert(count < 16); events[count++] = e; }
#define bxt_enable_dc9(p) event(1)
#define gen9_sanitize_dc_state(p) event(2)
#define bxt_disable_dc9(p) event(3)
#define hsw_enable_pc8(p) event(4)
#define hsw_disable_pc8(p) event(5)
#define intel_uncore_rmw(u, r, clear, set) do { \
 (void)(u); assert((r) == 0xc2000); assert((clear) == 128); \
 assert((set) == 0 || (set) == 128); chicken = (chicken & ~(clear)) | (set); \
 unsigned written = (set); event(written ? 6 : 7); } while (0)
@FUNCTIONS@
static void check_power(enum intel_platform platform, int gen, enum intel_pch pch, bool wa)
{
 struct drm_i915_private p = { .platform = platform, .gen = gen, .pch_type = pch };
 const unsigned expected[] = {1, 6, 2, 3, 7};
 const unsigned unchanged[] = {1, 2, 3};
 count = 0; chicken = 0x10000000;
 intel_display_power_suspend_late(&p);
 assert(chicken == (0x10000000u | (wa ? 128u : 0u)));
 intel_display_power_resume_early(&p);
 assert(chicken == 0x10000000u);
 assert(count == (wa ? 5u : 3u));
 assert(!memcmp(events, wa ? expected : unchanged, count * sizeof(*events)));
 assert(intel_power_domains_init(&p) == 0);
 assert(p.power_domains.selected == ((platform == INTEL_JASPERLAKE || platform == INTEL_ELKHARTLAKE) ? ehl_power_wells : icl_power_wells));
}
int main(void)
{
 struct drm_i915_private p = { .platform = INTEL_JASPERLAKE, .gen = 11 };
 static const struct format fmt = {0, 0};
 struct intel_crtc crtc = { .base.dev = &p };
 p.fbc.state_cache.plane.visible = 1; p.fbc.state_cache.fb.format = &fmt;
 /* Independent expected modulo rule, including the existing Y-offset check. */
 for (int gen = 8; gen <= 12; gen++) {
  p.gen = gen;
  for (int y = 0; y <= 8; y++) for (int h = 1080; h < 1088; h++) {
   p.fbc.state_cache.plane.adjusted_y = y; p.fbc.state_cache.plane.src_h = h;
   bool expected = (gen < 9 || y % 4 == 0) && (gen < 11 || (h + y) % 4 == 0);
   assert(intel_fbc_can_activate(&crtc) == expected);
   if (!expected) assert(p.fbc.no_fbc_reason != NULL);
  }
 }
 check_power(INTEL_JASPERLAKE, 11, PCH_JSP, true);
 check_power(INTEL_JASPERLAKE, 11, PCH_MCC, true);
 check_power(INTEL_JASPERLAKE, 11, PCH_NONE, false);
 check_power(INTEL_JASPERLAKE, 11, PCH_ICP, false);
 check_power(INTEL_JASPERLAKE, 11, PCH_TGP, false);
 check_power(INTEL_ELKHARTLAKE, 11, PCH_MCC, false);
 check_power(INTEL_ICELAKE, 11, PCH_ICP, false);
 p.platform = INTEL_HASWELL; p.gen = 7; count = 0;
 intel_display_power_suspend_late(&p); intel_display_power_resume_early(&p);
 assert(count == 2 && events[0] == 4 && events[1] == 5);
 puts("JSL FBC, power-well selection and late suspend/resume ordering: PASS");
 return 0;
}
