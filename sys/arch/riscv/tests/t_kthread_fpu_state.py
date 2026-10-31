#!/usr/bin/env python3
"""Exercise actual RISC-V FPU state callbacks with simulated register I/O."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
a = p.parse_args()
source = (Path(__file__).resolve().parents[1] / 'riscv/fpu.c').read_text()
start = source.index('void\nfpu_state_save(')
functions = source[start:]
functions = re.sub(r'__asm volatile\(RISCV_FPU(?:32|64)_STORE.*?: "memory"\);',
                   'mock_store(fp->r_fpreg);', functions, flags=re.S)
functions = re.sub(r'__asm volatile\(RISCV_FPU(?:32|64)_LOAD\s*::\s*"r"\(fp->r_fpreg\).*?: "memory"\);',
                   'mock_load(fp->r_fpreg);', functions, flags=re.S)
functions = re.sub(r'__asm volatile\(RISCV_FPU(?:32|64)_LOAD\s*::\s*"r"\(zero.r_fpreg\).*?: "memory"\);',
                   'mock_load(zero.r_fpreg);', functions, flags=re.S)
assert '__asm volatile' not in functions
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef unsigned int u_int;
#define LW_SYSTEM 1
#define LW_SYSTEM_FPU 2
#define PCU_VALID 1
#define PCU_REENABLE 2
#define PCU_FPU 0
#define SR_FS 0x6000u
#define SR_FS_CLEAN 2
#define __SHIFTIN(v,m) (((v) << 13) & (m))
#define __predict_false(x) (x)
#define KASSERT(x) assert(x)
#define RISCV_FPU_SINGLE 1
union reg { uint64_t u_u64; };
struct fpreg { union reg r_fpreg[33]; };
#define r_fcsr r_fpreg[32].u_u64
struct pcb { struct fpreg pcb_fpregs; };
struct trapframe { unsigned tf_sr; };
struct cpu_info { struct lwp *ci_pcu_curlwp[1]; struct { int ev_count; } ci_ev_fpu_saves, ci_ev_fpu_loads, ci_ev_fpu_reenables; } cpu;
struct lwp { unsigned l_flag; void *l_pcu_cpu[1]; struct { struct trapframe *md_utf; } l_md; struct pcb pcb; };
typedef struct lwp lwp_t;
static struct lwp thread;
#define curlwp (&thread)
#define curcpu() (&cpu)
#define lwp_getpcb(l) (&(l)->pcb)
static uint64_t regs[32];
static uint32_t csr;
static bool enabled;
static int loads, discards;
static int pcu_fpu_ops;
static int splhigh(void) { return 0; }
static void splx(int s) { (void)s; }
static void fpu_enable(void) { enabled = true; }
static void fpu_disable(void) { enabled = false; }
static uint32_t fpu_csr_read(void) { return csr; }
static void fpu_csr_write(uint32_t v) { csr = v; }
static void mock_store(union reg *r) { for (int i=0;i<32;i++) r[i].u_u64 |= (uint32_t)regs[i]; }
static void mock_load(const union reg *r) { for (int i=0;i<32;i++) regs[i] = (uint32_t)r[i].u_u64; }
static void pcu_load(const int *p) { assert(p==&pcu_fpu_ops); loads++; }
static void pcu_discard(const int *p, lwp_t *l, bool valid) {
    assert(p==&pcu_fpu_ops && l==curlwp && !valid); discards++; if (l->l_pcu_cpu[0] != NULL) fpu_disable();
}
'''
suffix = r'''
int main(void) {
    struct trapframe tf = {0};
    thread.l_flag = LW_SYSTEM | LW_SYSTEM_FPU;
    thread.l_pcu_cpu[0] = &cpu; cpu.ci_pcu_curlwp[0] = &thread;
    thread.l_md.md_utf = NULL;
    for (int i=0;i<32;i++) regs[i] = 0x12340000u+i;
    csr = 0x73;
    fpu_state_save(&thread);
    for (int i=0;i<32;i++) assert(thread.pcb.pcb_fpregs.r_fpreg[i].u_u64 == (UINT64_C(0xffffffff00000000) | (0x12340000u+i)));
    memset(regs, 0, sizeof(regs)); csr = 0;
    fpu_state_load(&thread, PCU_VALID);
    for (int i=0;i<32;i++) assert(regs[i] == 0x12340000u+i);
    assert(enabled && csr == 0x73);
    enabled = false; fpu_state_load(&thread, PCU_VALID | PCU_REENABLE);
    assert(enabled);
    fpu_state_release(&thread); assert(!enabled);
    kthread_fpu_enter_md(); assert(loads == 1);
    kthread_fpu_exit_md(); assert(discards == 1 && !enabled && csr == 0);
    for (int i=0;i<32;i++) assert(regs[i] == 0);
    /* LW_SYSTEM_FPU was cleared, then another LWP became PCU owner. */
    struct lwp foreign = {0};
    thread.l_flag = LW_SYSTEM; thread.l_pcu_cpu[0] = NULL;
    cpu.ci_pcu_curlwp[0] = &foreign; regs[0] = 0x4567; csr = 0x55;
    fpu_enable();
    kthread_fpu_exit_md();
    assert(regs[0] == 0x4567 && csr == 0x55 && enabled);
    thread.l_flag = 0; thread.l_md.md_utf = &tf; tf.tf_sr = 0;
    fpu_state_load(&thread, PCU_VALID);
    assert((tf.tf_sr & SR_FS) == __SHIFTIN(SR_FS_CLEAN, SR_FS));
    assert(!enabled);
    fpu_state_release(&thread); assert((tf.tf_sr & SR_FS) == 0);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c = Path(td)/'test.c'; exe = Path(td)/'test'
    c.write_text(prefix + functions + suffix)
    subprocess.run([a.cc, '-std=c11', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('F-only PCU state callbacks passed with simulated registers')
