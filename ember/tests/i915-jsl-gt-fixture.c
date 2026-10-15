/* SPDX-License-Identifier: BSD-2-Clause */
/* Host device and register hooks. Extracted code retains its original notices. */
@LICENSES@
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
@ENUMS@
typedef uint32_t u32;
#define _MMIO(x) (x)
#define REG_BIT(x) (1u << (x))
#define BIT(x) REG_BIT(x)
#define GENMASK(h, l) ((~0u << (l)) & (~0u >> (31 - (h))))
@REGS@
struct drm_i915_private { enum intel_platform platform; int gen, rev; };
struct intel_uncore { int unused; };
struct intel_gt { struct drm_i915_private *i915; struct intel_uncore *uncore; };
struct intel_engine_cs { struct drm_i915_private *i915; };
#define INTEL_REVID(p) ((p)->rev)
#define PLATFORM(p, x) ((p)->platform == INTEL_##x)
#define IS_ICELAKE(p) PLATFORM(p, ICELAKE)
#define IS_ELKHARTLAKE(p) PLATFORM(p, ELKHARTLAKE)
#define IS_JASPERLAKE(p) PLATFORM(p, JASPERLAKE)
#define IS_JSL_EHL(p) (IS_JASPERLAKE(p) || IS_ELKHARTLAKE(p))
#define IS_TIGERLAKE(p) PLATFORM(p, TIGERLAKE)
#define IS_SKYLAKE(p) PLATFORM(p, SKYLAKE)
#define IS_KABYLAKE(p) PLATFORM(p, KABYLAKE)
#define IS_COFFEELAKE(p) PLATFORM(p, COFFEELAKE)
#define IS_BROXTON(p) PLATFORM(p, BROXTON)
#define IS_GEN(p, n) ((p)->gen == (n))
#define IS_GEN_RANGE(p, a, b) ((p)->gen >= (a) && (p)->gen <= (b))
#define IS_GEN9_LP(p) (IS_BROXTON(p) || PLATFORM(p, GEMINILAKE))
@REVISIONS@
struct i915_wa_list { unsigned n; struct { u32 reg, value; } list[128]; };
static void write_wa(struct i915_wa_list *w, u32 reg, u32 value) {
 assert(w->n < 128); w->list[w->n].reg = reg; w->list[w->n++].value = value;
}
#define wa_init_mcr(p, w) ((void)(p))
#define wa_write_or(w, r, v) write_wa(w, r, v)
#define wa_masked_en(w, r, v) write_wa(w, r, v)
#define wa_write_masked_or(w, r, m, v) do { (void)(m); write_wa(w, r, v); } while (0)
/* Ordered reset hooks validate real register, domain mask and timeout inputs. */
static unsigned events[16], count, calls, fail_on;
static u32 expected_mask;
static unsigned expected_timeout;
static void event(unsigned e) { assert(count < 16); events[count++] = e; }
#define intel_uncore_write_fw(u, r, m) do { \
 (void)(u); assert((r) == 0x941c); assert((m) == expected_mask); event(1); } while (0)
static int wait_reset(struct intel_uncore *u, u32 r, u32 m, u32 value, unsigned t, unsigned slow, void *result) {
 (void)u; assert(r == 0x941c && m == expected_mask && value == 0);
 assert(t == expected_timeout && slow == 0 && result == NULL);
 event(2); calls++; return calls == fail_on ? -5 : 0;
}
#define __intel_wait_for_register_fw wait_reset
#define DRM_DEBUG_DRIVER(...) event(4)
#define udelay(t) do { assert((t) == 50); event(3); } while (0)
@FUNCTIONS@
static bool has_wa(const struct i915_wa_list *w, u32 reg, u32 bits) {
 for (unsigned i = 0; i < w->n; i++) if (w->list[i].reg == reg && (w->list[i].value & bits) == bits) return true;
 return false;
}
static void check_wa(enum intel_platform platform, int gen, int rev, bool l3, bool dop) {
 struct drm_i915_private p = {platform, gen, rev}; struct i915_wa_list w = {0};
 struct intel_engine_cs engine = {&p};
 if (gen == 11) {
  icl_gt_workarounds_init(&p, &w);
  /* Independent hardware address and mask literals from Intel's register definitions. */
  assert(has_wa(&w, 0x94d4, 0x30000) == l3);
 }
 w.n = 0; rcs_engine_wa_init(&engine, &w);
 assert(has_wa(&w, 0x20ec, 2) == dop);
}
static void check_reset(enum intel_platform platform, unsigned failure) {
 struct drm_i915_private p = { .platform = platform };
 struct intel_uncore u = {0}; struct intel_gt gt = {&p, &u};
 bool jsl = platform == INTEL_JASPERLAKE;
 count = calls = 0; fail_on = failure; expected_mask = 0x81234567;
 expected_timeout = jsl ? 2000 : 500;
 int err = gen6_hw_domain_reset(&gt, expected_mask);
 assert(err == ((failure && (jsl || failure == 1)) ? -5 : 0));
 unsigned loops = jsl && failure != 1 ? 2 : 1;
 assert(calls == loops);
 unsigned n = 0;
 for (unsigned i = 0; i < loops; i++) { assert(events[n++] == 1); assert(events[n++] == 2); }
 if (err) assert(events[n++] == 4);
 if (jsl) assert(events[n++] == 3);
 assert(count == n);
}
int main(void) {
 for (int rev = 0; rev <= 3; rev++) {
  check_wa(INTEL_JASPERLAKE, 11, rev, rev == 0, true);
  check_wa(INTEL_ELKHARTLAKE, 11, rev, rev == 0, true);
  check_wa(INTEL_ICELAKE, 11, rev, true, false);
 }
 check_wa(INTEL_TIGERLAKE, 12, 0, false, true);
 check_wa(INTEL_TIGERLAKE, 12, 1, false, false);
 check_wa(INTEL_SKYLAKE, 9, 0, false, false);
 for (unsigned failure = 0; failure <= 2; failure++) {
  check_reset(INTEL_JASPERLAKE, failure);
  check_reset(INTEL_ELKHARTLAKE, failure);
  check_reset(INTEL_ICELAKE, failure);
  check_reset(INTEL_SKYLAKE, failure);
 }
 puts("JSL/EHL stepping, RCS clock gating and ordered JSL-only reset: PASS"); return 0;
}
