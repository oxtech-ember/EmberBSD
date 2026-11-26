#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run the actual ARM FDT resolver and A733 IRQ completion with host doubles."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = root / "sys/arch/arm"
def section(path, start, end=None):
    text = (source / path).read_text()
    lo = text.index(start)
    return text[lo:text.index(end, lo) if end else len(text)]

common = section("fdt/arm_fdt.c", "int\narm_fdt_intr_distribute(")
pmu = section("fdt/pmu_fdt.c", "static int\npmu_fdt_intr_distribute(")
eqos = section("sunxi/sun60i_a733_eqos.c", "struct sun60i_a733_eqos_softc {",
               "static int\nsun60i_a733_eqos_match(")
fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
typedef unsigned u_int;
typedef uint64_t bus_addr_t;
typedef unsigned kcpuset_t;
typedef unsigned CPU_INFO_ITERATOR;
typedef void *device_t;
#define SPCF_OFFLINE 4
#define SPCF_NOINTR 16
struct cpu_info { uint64_t ci_cpuid; unsigned idx; struct { unsigned spc_flags; } ci_schedstate; };
struct eqos_softc { int marker; };
struct device_compatible_entry { const char *compat; };
#define DEVICE_COMPAT_EOL {0}
#define CPU_INFO_FOREACH(i, c) (i)=0,(c)=&cpus[0];(i)<2;(c)=&cpus[++(i)]
static struct cpu_info cpus[3] = {{0, 0, {0}}, {0x600, 3, {0}}, {0}};
static unsigned running = 9, *kcpuset_running = &running;
static unsigned cpu_lock, alloc, calls, selected, ent, masks, diagnostics;
static unsigned irq_cookie;
static uint32_t prop[2]={0x21,0x20};
static int prop_len=4, provider=2, node_error, route_error, property_absent;
static uint64_t mpidr=0x600;
static const char *type="cpu";
static uint32_t be32toh(uint32_t x) { return x; }
static const uint32_t *fdtbus_get_prop(int node, const char *n, int *len)
{ assert(node==1 && strcmp(n,"interrupt-affinity")==0); *len=prop_len; return property_absent?NULL:prop; }
static int fdtbus_get_phandle_from_native(uint32_t p) { return p==0x21?5:p==0x20?6:-1; }
static const char *fdtbus_get_string(int node, const char *s)
{ assert((node==5 || node==6) && !strcmp(s,"device_type")); return type; }
static int fdtbus_get_reg(int node, int n, bus_addr_t *out, void *size)
{ assert((node==5 || node==6) && n==0 && size==NULL); *out=node==5?mpidr:0; return node_error; }
static int fdtbus_intr_parent(int node) { assert(node==1); return provider; }
static int OF_getproplen(int node, const char *n)
{ int len; return fdtbus_get_prop(node,n,&len)?len:-1; }
static bool of_hasprop(int node, const char *n) { return OF_getproplen(node,n)>=0; }
static bool of_compatible_match(int node, const struct device_compatible_entry *c)
{ assert(node>0 && !strcmp(c[0].compat,"arm,gic-v3")); return node==2; }
static void mutex_enter(unsigned *l) { assert(l==&cpu_lock && !*l); *l=1; }
static void mutex_exit(unsigned *l) { assert(l==&cpu_lock && *l); *l=0; }
static void kcpuset_create(kcpuset_t **set, bool zero)
{ assert(!cpu_lock && !alloc && zero); *set=&ent; ent=0; alloc++; }
static void kcpuset_destroy(kcpuset_t *set) { assert(set==&ent && !cpu_lock && alloc==1); alloc--; }
static bool kcpuset_isset(const kcpuset_t *s, unsigned n)
{ assert(cpu_lock && n<32); return (*s & (1u<<n))!=0; }
static void kcpuset_set(kcpuset_t *s, unsigned n) { assert(n<32); *s |= 1u<<n; }
static unsigned cpu_index(const struct cpu_info *c) { assert(cpu_lock); return c->idx; }
static int interrupt_distribute(void *ih, const kcpuset_t *set, kcpuset_t *old)
{ assert(ih==&irq_cookie && set==&ent && cpu_lock && old==NULL); calls++; selected=*set; return route_error; }
static void *device_private(device_t d) { return d; }
static void fdtbus_intr_unmask(int node, void *ih)
{ assert(node==1 && ih==&irq_cookie && cpu_lock==0); masks++; }
static void aprint_error_dev(device_t dev, const char *msg, ...)
{ assert(dev && strstr(msg,"default route")); diagnostics++; }
#define IPL_NET 3
#define FDT_INTR_MPSAFE 1
#define FDT_INTR_EXCLUSIVE_MASKED 2
static unsigned establish, ordinary, deferred;
static bool reject_exclusive;
static int eqos_intr(void *sc) { assert(sc); return 1; }
static const char *device_xname(device_t dev) { assert(dev); return "eqos0"; }
static void *fdtbus_intr_establish_xname(int node, unsigned idx, int ipl,
    int flags, int (*fn)(void *), void *arg, const char *name)
{
	assert(node==1 && idx==0 && ipl==IPL_NET && fn==eqos_intr && arg &&
	    !strcmp(name,"eqos0"));
	establish++;
	if (flags & FDT_INTR_EXCLUSIVE_MASKED)
		return reject_exclusive ? NULL : &irq_cookie;
	assert(flags==FDT_INTR_MPSAFE);
	ordinary++;
	return &irq_cookie;
}
static void config_interrupts(device_t dev, void (*fn)(device_t))
{
	assert(dev && fn);
	deferred++;
	fn(dev);
}
'''
tests = r'''
int
main(void)
{
	struct sun60i_a733_eqos_softc esc={.phandle=1,.affinity_ih=&irq_cookie};
	assert(sun60i_a733_eqos_explicit_affinity(1));
	assert(arm_fdt_intr_distribute(1,0,&irq_cookie)==0 && selected==(1u<<3));
	assert(arm_fdt_intr_distribute(1,0,NULL)==EINVAL);
	assert(arm_fdt_intr_distribute(1,1,&irq_cookie)==EINVAL);
	prop_len=5;
	assert(arm_fdt_intr_distribute(1,0,&irq_cookie)==EINVAL);
	assert(!sun60i_a733_eqos_explicit_affinity(1));
	prop_len=8;
	assert(pmu_fdt_intr_distribute(1,1,&irq_cookie)==0 && selected==1);
	prop_len=4; property_absent=1;
	assert(!sun60i_a733_eqos_explicit_affinity(1));
	assert(pmu_fdt_intr_distribute(1,1,&irq_cookie)==0 && selected==2);
	property_absent=0; provider=3;
	assert(!sun60i_a733_eqos_explicit_affinity(1));
	provider=2;
	node_error=1;
	assert(arm_fdt_intr_distribute(1,0,&irq_cookie)==ENXIO);
	node_error=0; type="ethernet";
	assert(arm_fdt_intr_distribute(1,0,&irq_cookie)==ENXIO);
	type="cpu"; mpidr=0x700;
	assert(arm_fdt_intr_distribute(1,0,&irq_cookie)==ENOENT);
	mpidr=0x600;
	const unsigned before=calls;
	running=1;
	sun60i_a733_eqos_affinity(&esc);
	assert(masks==1 && diagnostics==1 && calls==before);
	running=9; cpus[1].ci_schedstate.spc_flags=SPCF_NOINTR;
	sun60i_a733_eqos_affinity(&esc);
	assert(masks==2 && diagnostics==2 && calls==before);
	cpus[1].ci_schedstate.spc_flags=SPCF_OFFLINE;
	sun60i_a733_eqos_affinity(&esc);
	assert(masks==3 && diagnostics==3 && calls==before);
	cpus[1].ci_schedstate.spc_flags=0;
	route_error=EOPNOTSUPP;
	sun60i_a733_eqos_affinity(&esc);
	assert(masks==4 && diagnostics==4 && calls==before+1);
	route_error=0;
	sun60i_a733_eqos_affinity(&esc);
	assert(masks==5 && diagnostics==4 && calls==before+2 && !alloc);
	assert(sun60i_a733_eqos_establish_irq(&esc,1,&esc)==&irq_cookie);
	assert(establish==1 && ordinary==0 && deferred==1 && masks==6);
	reject_exclusive=true;
	assert(sun60i_a733_eqos_establish_irq(&esc,1,&esc)==&irq_cookie);
	assert(establish==3 && ordinary==1 && deferred==1 && masks==6 &&
	    diagnostics==5);
	property_absent=1;
	assert(sun60i_a733_eqos_establish_irq(&esc,1,&esc)==&irq_cookie);
	assert(establish==4 && ordinary==2 && deferred==1 && masks==6 &&
	    diagnostics==5);
	puts("PASS ARM FDT affinity, PMU implicit route, A733 fallback unmask");
}
'''
with tempfile.TemporaryDirectory(prefix="a733-affinity-contract-") as tmp:
    src = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    src.write_text(fixture + common + pmu + eqos + tests)
    cc = shlex.split(os.environ.get("CC", "cc"))
    cflags = shlex.split(os.environ.get("A733_AFFINITY_CFLAGS", ""))
    subprocess.run([*cc, "-DMULTIPROCESSOR", "-std=c11", "-Wall", "-Wextra",
                    "-Werror", *cflags, str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
