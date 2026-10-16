/* SPDX-License-Identifier: BSD-2-Clause */
/* The generator adds imported production code and its original MIT notices. */
@LICENSES@
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define min(a,b) ((a) < (b) ? (a) : (b))
#define DRM_DEBUG_KMS(...) ((void)0)
#define DRM_DEBUG_DRIVER(...) ((void)0)
#define BUG_ON(x) assert(!(x))
static bool
warn(bool condition)
{
	assert(!condition);
	return condition;
}
#define WARN_ON(x) warn(x)
@ENUMS@
struct drm_i915_private;
struct drm_device { struct drm_i915_private *owner; };
struct dpll_info { const char *name; int id; };
struct intel_dpll_mgr { const struct dpll_info *dpll_info; };
struct ddi_vbt_port_info { int dp_max_link_rate; };
struct drm_i915_private {
	enum intel_platform platform;
	int gen;
	struct drm_device drm;
	struct { struct { bool low_vswing; } edp;
		struct ddi_vbt_port_info ddi_port_info[16]; } vbt;
	struct { bool sink_psr2_support; int su_x_granularity; } psr;
	struct { struct { int ref, cdclk; } hw; } cdclk;
	int max_cdclk_freq, max_dotclk_freq, skl_preferred_vco_freq;
	const struct intel_dpll_mgr *dpll_mgr;
	struct { const struct dpll_info *info; } shared_dplls[8];
	int num_shared_dpll, dpll_lock;
};
struct intel_digital_port {
	struct { struct { struct drm_device *dev; } base; enum port port; } base;
};
struct intel_dp {
	struct intel_digital_port dig;
	bool edp;
	const int *source_rates;
	int num_source_rates;
};
struct intel_crtc_state {
	struct { struct { int crtc_hdisplay, crtc_vdisplay; } adjusted_mode; } hw;
	struct { bool compression_enable; } dsc;
	int cpu_transcoder, pipe_bpp;
	bool crc_enabled;
};
#define IS_PLATFORM(d,p) ((d)->platform == (p))
#define INTEL_GEN(d) ((d)->gen)
#define IS_GEN(d,n) (INTEL_GEN(d) == (n))
@PREDICATES@
/* These unchanged old-generation paths are outside the fixture scenarios. */
#define IS_GEN9_BC(d) false
#define IS_GEN9_LP(d) false
#define IS_BDW_ULX(d) false
#define IS_BDW_ULT(d) false
#define IS_HSW_ULX(d) false
#define IS_CNL_WITH_PORT_F(d) ((void)(d), false)
#define HAS_DDI(d) false
#define HAS_PCH_IBX(d) false
#define HAS_PCH_CPT(d) false
#define to_i915(d) ((d)->owner)
#define dp_to_dig_port(d) (&(d)->dig)
#define dp_to_i915(d) to_i915(dp_to_dig_port(d)->base.base.dev)
static bool
intel_dp_is_edp(const struct intel_dp *dp)
{
	return dp->edp;
}
#define INTEL_OUTPUT_HDMI 1
#define INTEL_OUTPUT_EDP 2
#define INTEL_OUTPUT_DP 3
@VOLTAGE@
#define ICL_PORT_COMP_DW3(p) (0x300U + (p))
#define CNL_PORT_COMP_DW3 ICL_PORT_COMP_DW3(PHY_A)
#define ICL_PORT_COMP_DW0(p) (0x200U + (p))
#define ICL_PHY_MISC(p) (0x100U + (p))
#define COMP_INIT (1U << 31)
#define ICL_PHY_MISC_DE_IO_COMP_PWR_DOWN (1U << 23)
#define SKL_DFSM 0x400
#define SKL_DFSM_CDCLK_LIMIT_MASK 3
#define SKL_DFSM_CDCLK_LIMIT_675 0
#define SKL_DFSM_CDCLK_LIMIT_540 1
#define SKL_DFSM_CDCLK_LIMIT_450 2
#define FUSE_STRAP 0x401
#define HSW_CDCLK_LIMIT 1
static u32 voltage[16];
static unsigned int forbidden_phy_misc_reads;
static u32
read_register(unsigned int reg)
{
	if (reg >= 0x300 && reg <= 0x30f)
		return voltage[reg - 0x300];
	if (reg >= 0x200 && reg <= 0x20f)
		return COMP_INIT;
	if (reg == ICL_PHY_MISC(PHY_C))
		forbidden_phy_misc_reads++;
	return 0;
}
#define I915_READ(r) ((void)(dev_priv), read_register(r))
static int
skl_calc_cdclk(int a, int b)
{
	(void)b;
	return a;
}
static bool
transcoder_has_psr2(struct drm_i915_private *d, int t)
{
	(void)d;
	(void)t;
	return true;
}
@PHY@
@DDI@
@RATES@
@PSR@
@CLOCK@
static const struct dpll_info pll_fixture[] = { {"pll0",0}, {NULL,0} };
static const struct intel_dpll_mgr tgl_pll_mgr = {pll_fixture}, ehl_pll_mgr = {pll_fixture};
static const struct intel_dpll_mgr icl_pll_mgr = {pll_fixture}, cnl_pll_mgr = {pll_fixture};
static const struct intel_dpll_mgr skl_pll_mgr = {pll_fixture}, bxt_pll_mgr = {pll_fixture};
static const struct intel_dpll_mgr hsw_pll_mgr = {pll_fixture}, pch_pll_mgr = {pll_fixture};
static void
mutex_init(int *lock)
{
	*lock = 1;
}
#define I915_NUM_PLLS 8
@PLL@
/* Values come independently from BSpec 21257 / upstream 1ba1014db258. */
static const u8 want_hbr[10][5] = {
	{8,127,63,0,0}, {8,127,56,0,7}, {1,127,51,0,12}, {10,53,54,0,9},
	{8,127,63,0,0}, {1,127,56,0,7}, {10,53,53,0,10}, {1,127,63,0,0},
	{10,53,56,0,7}, {10,53,63,0,0}
};
static const u8 want_hbr2[10][5] = {
	{8,127,63,0,0}, {8,127,63,0,0}, {1,127,61,0,2}, {10,53,56,0,7},
	{8,127,63,0,0}, {1,127,63,0,0}, {10,53,58,0,5}, {1,127,63,0,0},
	{10,53,56,0,7}, {10,53,63,0,0}
};
static void
check_swing(struct drm_i915_private *d, int rate, const u8 want[10][5])
{
	int count;
	const struct cnl_ddi_buf_trans *table = icl_get_combo_buf_trans(d, INTEL_OUTPUT_EDP, rate, &count);
	assert(count == 10);
	for (int i = 0; i < count; i++) {
		assert(table[i].dw2_swing_sel == want[i][0]);
		assert(table[i].dw7_n_scalar == want[i][1]);
		assert(table[i].dw4_cursor_coeff == want[i][2]);
		assert(table[i].dw4_post_cursor_2 == want[i][3]);
		assert(table[i].dw4_post_cursor_1 == want[i][4]);
	}
}
static int
max_rate_for(struct drm_i915_private *d, bool edp, u32 volts, int vbt_limit,
    enum port port, enum phy expected_phy)
{
	struct intel_dp dp = { .dig.base = { .base.dev = &d->drm, .port = port }, .edp = edp };
	d->vbt.ddi_port_info[port].dp_max_link_rate = vbt_limit;
	for (unsigned int i = 0; i < ARRAY_SIZE(voltage); i++)
		voltage[i] = VOLTAGE_INFO_1_05V;
	voltage[expected_phy] = volts;
	intel_dp_set_source_rates(&dp);
	assert(dp.num_source_rates > 0);
	return dp.source_rates[dp.num_source_rates - 1];
}
static int
max_rate(struct drm_i915_private *d, bool edp, u32 volts, int vbt_limit)
{
	return max_rate_for(d, edp, volts, vbt_limit, PORT_A, PHY_A);
}
int
main(void)
{
	struct drm_i915_private d = { .platform = INTEL_JASPERLAKE, .gen = 11 };
	d.drm.owner = &d;
	d.vbt.edp.low_vswing = true;
	check_swing(&d, 162000, want_hbr);
	check_swing(&d, 270000, want_hbr);
	check_swing(&d, 270001, want_hbr2);
	check_swing(&d, 540000, want_hbr2);
	assert(max_rate(&d, true, VOLTAGE_INFO_1_05V, 0) == 540000);
	assert(max_rate(&d, false, VOLTAGE_INFO_0_85V, 0) == 540000);
	assert(max_rate(&d, false, VOLTAGE_INFO_0_95V, 0) == 810000);
	assert(max_rate(&d, false, VOLTAGE_INFO_1_05V, 0) == 810000);
	assert(max_rate(&d, false, VOLTAGE_INFO_1_05V, 270000) == 270000);
	assert(max_rate_for(&d, false, VOLTAGE_INFO_0_85V, 0, PORT_C, PHY_C) == 540000);
	assert(max_rate_for(&d, false, VOLTAGE_INFO_0_85V, 0, PORT_D, PHY_A) == 540000);
	assert(intel_port_to_phy(&d, PORT_D) == PHY_A);
	assert(intel_phy_is_combo(&d, PHY_C) && !intel_phy_is_tc(&d, PHY_C));
	assert(icl_combo_phy_enabled(&d, PHY_C) && forbidden_phy_misc_reads == 0);
	d.cdclk.hw.ref = 24000;
	intel_update_max_cdclk(&d);
	assert(d.max_cdclk_freq == 552000 && d.max_dotclk_freq == 1104000);
	d.cdclk.hw.ref = 19200;
	intel_update_max_cdclk(&d);
	assert(d.max_cdclk_freq == 556800);
	intel_shared_dpll_init(&d.drm);
	assert(d.dpll_mgr == &ehl_pll_mgr && d.num_shared_dpll == 1 && d.dpll_lock == 1);
	struct intel_dp dp = { .dig.base.base.dev = &d.drm };
	struct intel_crtc_state crtc = { .hw.adjusted_mode = {1920,1080}, .pipe_bpp = 24 };
	d.psr.sink_psr2_support = true;
	d.psr.su_x_granularity = 4;
	assert(!intel_psr2_config_valid(&dp, &crtc));
	d.platform = INTEL_ELKHARTLAKE;
	assert(!intel_psr2_config_valid(&dp, &crtc));
	assert(max_rate(&d, true, VOLTAGE_INFO_1_05V, 0) == 540000);
	d.cdclk.hw.ref = 24000;
	intel_update_max_cdclk(&d);
	assert(d.max_cdclk_freq == 552000);
	d.platform = INTEL_ICELAKE;
	assert(intel_psr2_config_valid(&dp, &crtc));
	assert(!intel_phy_is_combo(&d, PHY_C) && intel_phy_is_tc(&d, PHY_C));
	assert(intel_port_to_phy(&d, PORT_D) == PHY_D);
	intel_update_max_cdclk(&d);
	assert(d.max_cdclk_freq == 648000);
	intel_shared_dpll_init(&d.drm);
	assert(d.dpll_mgr == &icl_pll_mgr);
	int count;
	const struct cnl_ddi_buf_trans *table = icl_get_combo_buf_trans(&d, INTEL_OUTPUT_EDP, 270000, &count);
	assert(table == icl_combo_phy_ddi_translations_edp_hbr2 && count == 10);
	d.platform = INTEL_JASPERLAKE;
	d.vbt.edp.low_vswing = false;
	table = icl_get_combo_buf_trans(&d, INTEL_OUTPUT_EDP, 540000, &count);
	assert(table == icl_combo_phy_ddi_translations_dp_hbr2);
	table = icl_get_combo_buf_trans(&d, INTEL_OUTPUT_HDMI, 0, &count);
	assert(table == icl_combo_phy_ddi_translations_hdmi && count == 7);
	puts("PASS: production JSL swing/rates/PHY/PLL/CDCLK/PSR2 selectors; EHL/ICL controls");
	return 0;
}
