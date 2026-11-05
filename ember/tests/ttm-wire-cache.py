#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the production NetBSD TTM wiring path with controlled VM pages."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True, type=Path)
parser.add_argument('--cc', required=True, type=Path)
args = parser.parse_args()
path = args.source / 'sys/external/bsd/drm2/dist/drm/ttm/ttm_tt.c'
text = path.read_text(encoding='utf-8')
match = re.search(r'^int\s+ttm_tt_wire\(struct ttm_tt \*ttm\)\s*\{.*?^\}', text, re.M | re.S)
if not match:
    raise SystemExit('production ttm_tt_wire not found')
fixture = r'''
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#define PAGE_SHIFT 12
#define ptoa(i) ((i) << PAGE_SHIFT)
#define ISSET(x,y) ((x)&(y))
#define TTM_PAGE_FLAG_SWAPPED 1
#define RW_READER 0
#define KASSERT(x) assert(x)
#define KASSERTMSG(x, ...) assert(x)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
enum {tt_unpopulated, tt_cached, tt_wc, tt_uc};
struct vm_page { int id; };
struct page { struct vm_page p_vmp; };
struct uvm_object { void *vmobjlock; };
struct ttm_tt { struct uvm_object *swap_storage; struct page **pages; unsigned num_pages, page_flags; int state, caching_state; };
static struct page backing[3];
static int wire_error, locked, lookups, flushes, wires;
static int uvm_obj_wirepages(struct uvm_object *o, unsigned a, unsigned b, void *c) {
    (void)o; (void)c; assert(a==0 && b==(3u<<PAGE_SHIFT)); wires++; return wire_error;
}
static void rw_enter(void *l, int mode) { (void)l; (void)mode; assert(!locked); locked=1; }
static void rw_exit(void *l) { (void)l; assert(locked); locked=0; }
static struct vm_page *uvm_pagelookup(struct uvm_object *o, unsigned offset) {
    (void)o; assert(locked && offset==(unsigned)lookups*(1u<<PAGE_SHIFT));
    assert(lookups<3); return &backing[lookups++].p_vmp;
}
void drm_clflush_pages(struct page **pages, unsigned long count) {
    assert(!locked && lookups==3 && count==3);
    for(unsigned i=0;i<3;i++) assert(pages[i]==&backing[i]);
    flushes++;
}
'''
test = r'''
static void run(int caching, int err, int expected_flush) {
    struct uvm_object obj={0}; struct page *pages[3]={0};
    struct ttm_tt tt={.swap_storage=&obj,.pages=pages,.num_pages=3,
                      .page_flags=TTM_PAGE_FLAG_SWAPPED,.state=tt_unpopulated,.caching_state=caching};
    wire_error=err; locked=lookups=flushes=wires=0;
    int result=ttm_tt_wire(&tt);
    if(flushes!=expected_flush) {
        fprintf(stderr,"FAIL caching=%d error=%d expected flush=%d actual=%d\n",caching,err,expected_flush,flushes);
        assert(flushes==expected_flush);
    }
    assert(result==-err && wires==1 && !locked);
    assert(lookups==(err?0:3));
    if(!err) for(int i=0;i<3;i++) assert(pages[i]==&backing[i]);
    if(!err && caching==tt_wc) {
        /* Simulate unwire/repopulate: each wiring requires a fresh flush. */
        for(int i=0;i<3;i++) pages[i]=NULL;
        lookups=0; result=ttm_tt_wire(&tt);
        assert(result==0 && wires==2 && lookups==3 && flushes==2 && !locked);
    }
}
int main(void) {
    run(tt_cached,0,0); run(tt_wc,0,1); run(tt_uc,0,1);
    run(tt_wc,EIO,0); puts("PASS ttm wire cache policy and rewire"); return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='ember-ttm-wire-') as tmp:
    c = Path(tmp) / 'test.c'
    binary = Path(tmp) / 'test'
    c.write_text(fixture + '\n' + match.group(0) + '\n' + test, encoding='utf-8')
    subprocess.run([str(args.cc), '-fuse-ld=lld', '-std=c11', '-Wall', '-Wextra',
                    '-Werror', str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
