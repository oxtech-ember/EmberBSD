#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
"""Compile actual DCN wrappers with mock FP bodies and PP callbacks."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc', required=True)
p.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
p.add_argument('--sanitize', action='store_true')
a=p.parse_args()
path=a.source_root/'sys/external/bsd/drm2/dist/drm/amd/display/dc/calcs/amdgpu_dcn_calcs.c'
source=path.read_text()
names=['dcn_bw_apply_registry_override','get_highest_allowed_voltage_level','dcn_validate_bandwidth','dcn_find_dcfclk_suits_all','dcn_bw_update_from_pplib','dcn_bw_notify_pplib_of_wm_ranges','dcn_bw_sync_calcs_and_dml']
def function(name):
 m=re.search(r'(?m)^(?:static )?(?:unsigned int|bool|void)\s+'+name+r'\s*\(', source)
 if not m: raise ValueError('missing wrapper: '+name)
 start=source.find('{',m.start()); depth=1; end=start+1
 while depth:
  if source[end]=='{': depth+=1
  elif source[end]=='}': depth-=1
  end+=1
 return source[m.start():end]
pre=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define WM_SET_COUNT 4
#define WM_A 0
#define WM_B 1
#define WM_C 2
#define WM_D 3
#define WM_REPORT_OVERRIDE 1
#define ASICREV_IS_POLLOCK(x) ((x) == 1)
#define ASICREV_IS_DALI(x) ((x) == 2)
static int depth, pos, event[80], changed, pass, voltage, callbacks, override;
static void e(int n) {
    assert(pos < 80);
    event[pos++] = n;
}
static void begin(void) {
    assert(depth == 0);
    depth = 1;
    e(1);
}
static void end(void) {
    assert(depth == 1);
    depth = 0;
    e(2);
}
#define DC_FP_START() begin()
#define DC_FP_END() end()
#define BW_VAL_TRACE_SETUP() unsigned long long voltage_level_tick = 0, watermark_tick = 0
#define PERFORMANCE_TRACE_START() e(3)
#define BW_VAL_TRACE_COUNT() e(4)
#define PERFORMANCE_TRACE_END() e(5)
#define BW_VAL_TRACE_FINISH()                                                            \
    do {                                                                                 \
        assert(voltage_level_tick == 12 && watermark_tick == 34);                        \
        e(6);                                                                            \
    } while (0)
struct asic {
    unsigned hw_internal_rev;
};
struct dc_context {
    struct asic asic_id;
};
struct set {
    int wm_inst, min_drain_clk_mhz, max_drain_clk_mhz, min_fill_clk_mhz, max_fill_clk_mhz;
};
struct pp_smu_wm_range_sets {
    int num_reader_wm_sets, num_writer_wm_sets;
    struct set reader_wm_sets[4], writer_wm_sets[4];
};
struct pp_smu_funcs_rv {
    int pp_smu;
    void (*set_wm_ranges)(int *, struct pp_smu_wm_range_sets *);
};
struct pp_smu {
    struct pp_smu_funcs_rv rv_funcs;
};
struct pool {
    struct pp_smu *pp_smu;
};
struct dc {
    struct dc_context *ctx;
    struct pool *res_pool;
    struct {
        int pplib_wm_report_mode;
    } debug;
};
struct dcn_bw_internal_vars {
    unsigned voltage_level;
    int other;
};
struct dc_state {
    struct dcn_bw_internal_vars dcn_bw_vars;
};
struct dc_clocks {
    int x;
};
struct dm_pp_clock_levels_with_voltage {
    int value[4];
};
#define DM_PP_CLOCK_TYPE_FCLK 10
#define DM_PP_CLOCK_TYPE_DCFCLK 20
static bool
dm_pp_get_clock_levels_by_type_with_voltage(struct dc_context *c, int type,
                                            struct dm_pp_clock_levels_with_voltage *x) {
    (void)c;
    assert(depth == 0);
    for (int i = 0; i < 4; i++)
        assert(x->value[i] == 0);
    e(type);
    x->value[0] = type;
    return type == 10;
}
static bool dcn_bw_apply_registry_override_fp(struct dc *dc) {
    (void)dc;
    assert(depth == 1);
    e(30);
    return changed;
}
static void dcn_bw_sync_calcs_and_dml_fp(struct dc *dc) {
    (void)dc;
    assert(depth == 1);
    e(31);
}
static bool dcn_validate_bandwidth_fp(struct dc *dc, struct dc_state *s, bool fast,
                                      unsigned long long *v, unsigned long long *w) {
    (void)dc;
    (void)fast;
    assert(depth == 1);
    assert(s->dcn_bw_vars.voltage_level == 0 && s->dcn_bw_vars.other == 0);
    e(32);
    s->dcn_bw_vars.voltage_level = voltage;
    *v = 12;
    *w = 34;
    return pass;
}
static unsigned dcn_find_dcfclk_suits_all_fp(const struct dc *dc, struct dc_clocks *x) {
    (void)dc;
    (void)x;
    assert(depth == 1);
    e(33);
    return 42;
}
static void dcn_bw_update_fclk_fp(struct dc *dc,
                                  struct dm_pp_clock_levels_with_voltage *x, bool ok) {
    (void)dc;
    assert(depth == 1 && ok && x->value[0] == 10);
    e(11);
}
static void dcn_bw_update_dcfclk_fp(struct dc *dc,
                                    struct dm_pp_clock_levels_with_voltage *x, bool ok) {
    (void)dc;
    assert(depth == 1 && !ok && x->value[0] == 20);
    e(21);
}
static void dcn_bw_get_wm_clocks_fp(struct dc *dc, volatile int *f, volatile int *d,
                                    volatile int *s) {
    (void)dc;
    assert(depth == 1);
    e(40);
    *f = 810000;
    *d = 320000;
    *s = 210000;
}
static void callback(int *p, struct pp_smu_wm_range_sets *r) {
    (void)p;
    assert(depth == 0);
    e(41);
    callbacks++;
    assert(r->num_reader_wm_sets == 4 && r->num_writer_wm_sets == 4);
    assert(r->reader_wm_sets[0].wm_inst == 0 &&
           r->reader_wm_sets[0].min_drain_clk_mhz == (override ? 300 : 320) &&
           r->reader_wm_sets[0].min_fill_clk_mhz == (override ? 800 : 810) &&
           r->reader_wm_sets[0].max_fill_clk_mhz == 5000);
    assert(r->writer_wm_sets[0].min_fill_clk_mhz == (override ? 200 : 210) &&
           r->writer_wm_sets[0].min_drain_clk_mhz == (override ? 800 : 810));
    for (int i = 1; i < 4; i++)
        assert(r->reader_wm_sets[i].wm_inst == i);
}
void dcn_bw_sync_calcs_and_dml(struct dc *);
'''
post=r'''
static void expect(const int *a, int n) {
    assert(pos == n);
    for (int i = 0; i < n; i++)
        assert(event[i] == a[i]);
    assert(depth == 0);
    pos = 0;
}
int main(void) {
    struct dc_context ctx = {0};
    struct pool pool = {0};
    struct dc dc = {.ctx = &ctx, .res_pool = &pool};
    struct dc_state st = {0};
    struct dc_clocks clocks = {0};
    struct pp_smu pp = {0};
    dcn_bw_update_from_pplib(&dc);
    int a[] = {10, 1, 11, 2, 20, 1, 21, 2};
    expect(a, 8);
    dcn_bw_notify_pplib_of_wm_ranges(&dc);
    assert(pos == 0 && callbacks == 0);
    pool.pp_smu = &pp;
    dcn_bw_notify_pplib_of_wm_ranges(&dc);
    assert(pos == 0 && callbacks == 0);
    pp.rv_funcs.set_wm_ranges = callback;
    dcn_bw_notify_pplib_of_wm_ranges(&dc);
    int b[] = {1, 40, 2, 41};
    expect(b, 4);
    dc.debug.pplib_wm_report_mode = 1;
    override = 1;
    dcn_bw_notify_pplib_of_wm_ranges(&dc);
    expect(b, 4);
    assert(callbacks == 2);
    changed = 1;
    pass = 1;
    voltage = 3;
    st.dcn_bw_vars.other = 99;
    assert(dcn_validate_bandwidth(&dc, &st, false));
    int c[] = {3, 4, 1, 30, 2, 1, 31, 2, 1, 32, 2, 5, 6};
    expect(c, 13);
    changed = 0;
    pass = 1;
    voltage = 1;
    ctx.asic_id.hw_internal_rev = 1;
    assert(!dcn_validate_bandwidth(&dc, &st, true));
    int d[] = {3, 4, 1, 30, 2, 1, 32, 2, 5, 6};
    expect(d, 10);
    pass = 0;
    voltage = 0;
    assert(!dcn_validate_bandwidth(&dc, &st, false));
    expect(d, 10);
    assert(dcn_find_dcfclk_suits_all(&dc, &clocks) == 42);
    int f[] = {1, 33, 2};
    expect(f, 3);
    dcn_bw_sync_calcs_and_dml(&dc);
    int g[] = {1, 31, 2};
    expect(g, 3);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='dcn-fp-guards-') as td:
 c=Path(td)/'test.c'; c.write_text(pre+'\n\n'.join(function(n) for n in names)+post)
 cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Werror','-O2']
 if a.sanitize: cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run(cmd+[str(c),'-o',str(Path(td)/'test')],check=True)
 subprocess.run([str(Path(td)/'test')],check=True)
print('PASS: actual DCN wrappers, FPU scopes, callbacks, validation and watermark ranges')
