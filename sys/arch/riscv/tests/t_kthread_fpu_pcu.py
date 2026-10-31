#!/usr/bin/env python3
"""Exercise actual pcu_switchpoint dispatch using a small PCU model."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
a = p.parse_args()
source = (Path(__file__).resolve().parents[3] / 'kern/subr_pcu.c').read_text()
start = source.index('void\npcu_switchpoint(')
end = source.index('\n/*\n * pcu_discard_all:', start)
function = source[start:end]
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef unsigned int u_int;
#define PCU_UNIT_COUNT 1
#define PCU_FPU 0
#define LW_SYSTEM 1
#define LW_SYSTEM_FPU 2
#define __predict_true(x) (x)
#define __predict_false(x) (x)
#define KASSERTMSG(x, ...) assert(x)
#define KASSERT(x) assert(x)
struct cpu_info { struct lwp *ci_pcu_curlwp[1]; } cpu;
struct lwp { uint32_t l_pcu_valid; struct cpu_info *l_pcu_cpu[1], *l_cpu;
    unsigned int l_flag; };
typedef struct lwp lwp_t;
typedef struct { void (*pcu_state_release)(lwp_t *); } pcu_ops_t;
static lwp_t current;
#define curlwp (&current)
static int releases, loads;
static void release(lwp_t *l) { (void)l; releases++; }
static const pcu_ops_t ops = { release };
static const pcu_ops_t *pcu_ops_md_defs[] = { &ops };
static int splpcu(void) { return 0; }
static void splx(int s) { (void)s; }
static void pcu_load(const pcu_ops_t *p) { assert(p == &ops); loads++; }
'''
suffix = r'''
int main(void) {
    current.l_cpu = &cpu;
    current.l_pcu_valid = 1;
    current.l_flag = LW_SYSTEM | LW_SYSTEM_FPU;
    pcu_switchpoint(&current);
    assert(releases == 1 && loads == 1);
    current.l_flag = 0;
    pcu_switchpoint(&current);
    assert(releases == 2 && loads == 1);
    current.l_flag = LW_SYSTEM | LW_SYSTEM_FPU;
    current.l_pcu_valid = 0;
    pcu_switchpoint(&current);
    assert(loads == 1);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp)/'test.c'; out = Path(tmp)/'test'
    src.write_text(prefix + function + suffix)
    subprocess.run([a.cc, '-std=c11', '-D__HAVE_PCU_KTHREAD_EAGER', str(src), '-o', str(out)], check=True)
    subprocess.run([str(out)], check=True)
print('PCU kernel-thread switchpoint contract passed')
