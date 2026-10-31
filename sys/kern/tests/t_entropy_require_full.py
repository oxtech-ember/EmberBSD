#!/usr/bin/env python3
"""Test the real entropy extraction function's strict, locked policy."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser(); p.add_argument('--cc', required=True); a = p.parse_args()
source = (Path(__file__).resolve().parents[1] / 'kern_entropy.c').read_text()
header = (Path(__file__).resolve().parents[2] / 'sys/entropy.h').read_text()
assert re.search(r'^#define\s+ENTROPY_REQUIRE_FULL\s+0x08\b', header, re.M)
fn = re.search(r'\nint\nentropy_extract\([^)]*\)\n\{.*?\n\}', source, re.S)
assert fn
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#define ENTROPY_WAIT 1
#define ENTROPY_SIG 2
#define ENTROPY_HARDFAIL 4
#define ENTROPY_REQUIRE_FULL 8
#define ENTROPY_CAPACITY 32
#define MINENTROPYBITS 256
#define MINSAMPLES 256
#define NBBY 8
#define ISSET(x,y) ((x)&(y))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define KASSERT(x) assert(x)
#define ASSERT_SLEEPABLE() ((void)0)
#define __predict_false(x) (x)
#define atomic_store_relaxed(p,v) (*(p)=(v))
#define atomic_load_relaxed(p) (*(p))
struct ev { int ev_count; } entropy_extract_fail_evcnt, entropy_deplete_evcnt;
struct entropy { int lock,cv,pool; unsigned bitsneeded,samplesneeded,epoch; } pool,*E=&pool;
static struct proc { int p_pid; char *p_comm; } process,*curproc=&process;
static int hz=1,cold,entropy_depletion,locked,deplete_before_lock,extracts;
static int cpu_intr_p(void) { return 0; }
static int splhigh(void) { assert(!locked); locked=1; return 0; }
static void splx(int x) { (void)x; assert(locked); locked=0; }
static void mutex_enter(int *m) { (void)m; assert(!locked); locked=1;
 if (deplete_before_lock) { E->bitsneeded=128; E->samplesneeded=0; } }
static void mutex_exit(int *m) { (void)m; assert(locked); locked=0; }
static int entropy_request(size_t n,int f) { (void)n;(void)f;assert(locked);return 0; }
static int cv_timedwait_sig(int *c,int *l,int h) { (void)c;(void)l;(void)h;return EWOULDBLOCK; }
static void cv_timedwait(int *c,int *l,int h) { (void)c;(void)l;(void)h; }
static int ratecheck(struct timeval *a,const struct timeval *b) { (void)a;(void)b;return 0; }
static void entpool_extract(int *p,void *b,size_t n) { (void)p;assert(locked);memset(b,0x5a,n);extracts++; }
''' + fn.group() + r'''
int main(void) {
 unsigned char out[32];
 memset(out,0x77,sizeof(out)); deplete_before_lock=1;
 assert(entropy_extract(out,sizeof(out),ENTROPY_HARDFAIL|ENTROPY_REQUIRE_FULL)==EAGAIN);
 assert(!extracts && out[0]==0x77 && !locked);
 deplete_before_lock=0;
 assert(entropy_extract(out,sizeof(out),0)==0 && extracts==1 && out[0]==0x5a);
 E->bitsneeded=E->samplesneeded=0; entropy_depletion=1;
 assert(entropy_extract(out,sizeof(out),ENTROPY_HARDFAIL|ENTROPY_REQUIRE_FULL)==0);
 assert(extracts==2 && E->bitsneeded==256 && entropy_deplete_evcnt.ev_count==1);
 cold=1; entropy_depletion=0; E->bitsneeded=E->samplesneeded=0;
 assert(entropy_extract(out,sizeof(out),ENTROPY_HARDFAIL|ENTROPY_REQUIRE_FULL)==0);
 assert(extracts==3 && !locked);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c = Path(td) / 'entropy.c'; out = Path(td) / 'entropy'
    c.write_text(code)
    subprocess.run([a.cc, '-Werror', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(c), '-o', str(out)], check=True)
    subprocess.run([str(out)], check=True)
print('locked strict extraction rejects prior depletion and accepts its own depletion')
