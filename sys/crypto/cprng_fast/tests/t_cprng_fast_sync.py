#!/usr/bin/env python3
"""Exercise the actual synchronous per-CPU fast reseed implementation."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
a = p.parse_args()
source = (Path(__file__).resolve().parents[1] / 'cprng_fast.c').read_text()
fn = re.search(r'\nint\ncprng_fast_reseed_sync\(void\)\n\{.*?\n\}', source, re.S)
assert fn
code = r'''
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define CPRNG_FAST_SEED_BYTES 32
#define KASSERT(x) assert(x)
#define ENTROPY_HARDFAIL 4
#define ENTROPY_REQUIRE_FULL 8
static int ready, held, seeds, wiped, interrupts, extract_error;
static unsigned current_epoch=8;
struct ev { int ev_count; } event;
struct cprng_fast { unsigned epoch; struct ev *reseed_evcnt; unsigned char seed[32]; } state={0,&event,{0}};
static void *cprng_fast_percpu=&state;
static int cpu_intr_p(void) { return 0; }
static int cpu_softintr_p(void) { return 0; }
static int entropy_ready(void) { return ready; }
static unsigned entropy_epoch(void) { return current_epoch; }
static int entropy_extract(void *p,size_t n,int f) {
 assert(!held && f==(ENTROPY_HARDFAIL|ENTROPY_REQUIRE_FULL)); if (extract_error) return EAGAIN;
 memset(p,0x5a,n); seeds++; current_epoch++; return 0; }
static void *percpu_getref(void *p) { assert(!held); held=1; return p; }
static void percpu_putref(void *p) { assert(held && p==&state); held=0; }
static int splvm(void) { assert(held); interrupts=1; return 42; }
static void splx(int s) { assert(s==42 && interrupts); interrupts=0; }
static void cprng_fast_seed(struct cprng_fast *p,void *seed) {
 assert(interrupts); memcpy(p->seed,seed,32); }
static void explicit_memset(void *p,int x,size_t n) {
 memset(p,x,n); wiped++; }
''' + fn.group() + r'''
int main(void) {
 assert(cprng_fast_reseed_sync()==EAGAIN && !seeds && !state.epoch);
 ready=1; extract_error=1;
 assert(cprng_fast_reseed_sync()==EAGAIN && !seeds && !state.epoch);
 extract_error=0;
 assert(cprng_fast_reseed_sync()==0);
 assert(seeds==1 && wiped==2 && !held && !interrupts);
 assert(state.epoch==8 && current_epoch==9 && event.ev_count==1);
 for (unsigned i=0;i<32;i++) assert(state.seed[i]==0x5a);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c = Path(td) / 'sync.c'; out = Path(td) / 'sync'
    c.write_text(code)
    subprocess.run([a.cc, '-Werror', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(c), '-o', str(out)], check=True)
    subprocess.run([str(out)], check=True)
print('synchronous fast seed, no-entropy refusal and epoch race passed')
