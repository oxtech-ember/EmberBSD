#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Exercises functions extracted from their owning source and header.
from pathlib import Path
import argparse, re, subprocess, tempfile

parser = argparse.ArgumentParser(description="Exercise actual NetBSD GART bind/map with synthetic DMA segments")
parser.add_argument("--source", required=True, type=Path, help="EmberBSD source root")
parser.add_argument("--cc", required=True, type=Path, help="Selected native compiler")
args = parser.parse_args()
base = args.source / "sys/external/bsd/drm2/dist/drm/amd/amdgpu"
src = (base / "amdgpu_gart.c").read_text()
hdr = (base / "amdgpu_gart.h").read_text()
protos=[]; funcs=[]
for name in ('amdgpu_gart_map','amdgpu_gart_bind'):
 pattern=r'int\s+'+name+r'\(struct amdgpu_device \*adev, uint64_t gpu_start,.*?\);'
 m=re.search(pattern,hdr,re.S); assert m; protos.append(m.group())
 pattern=r'int\s+'+name+r'\(struct amdgpu_device \*adev, uint64_t gpu_start,.*?\n\}'
 m=re.search(pattern,src,re.S); assert m; funcs.append(m.group())
preamble=r'''#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#define PAGE_SIZE 4096
#define AMDGPU_GPU_PAGE_SIZE 4096
#define AMDGPU_GPU_PAGES_IN_CPU_PAGE 1
#define CTASSERT(x) _Static_assert(x, #x)
#define KASSERT(x) assert(x)
#define WARN(...) ((void)0)
#define __diagused __attribute__((unused))
typedef uint64_t bus_size_t;
typedef uint64_t bus_addr_t;
struct segment { uint64_t ds_addr, ds_len; };
struct dma_map { unsigned dm_nsegs; struct segment dm_segs[4]; };
typedef struct dma_map *bus_dmamap_t;
struct page { int unused; };
struct amdgpu_device { struct { int ready; unsigned num_cpu_pages; void *ptr; } gart; };
static uint64_t got_addr[16],got_flags[16],got_index[16]; static unsigned got_count, pre_count, post_count;
static void amdgpu_gmc_set_pte_pde(struct amdgpu_device *a,void *d,uint64_t i,uint64_t addr,uint64_t flags) { (void)a; (void)d; assert(got_count<16); got_index[got_count]=i; got_addr[got_count]=addr; got_flags[got_count++]=flags; }
static void amdgpu_gart_pre_update(struct amdgpu_device *a,unsigned s,unsigned n) {(void)a;(void)s;(void)n;pre_count++;}
static void amdgpu_gart_post_update(struct amdgpu_device *a,unsigned s,unsigned n) {(void)a;(void)s;(void)n;post_count++;}
'''
test=r'''
static int check(unsigned i,uint64_t idx,uint64_t addr,uint64_t flags) { if(got_index[i]!=idx || got_addr[i]!=addr || got_flags[i]!=flags) { fprintf(stderr,"entry %u got index=%llu addr=%llx flags=%llx\n",i,(unsigned long long)got_index[i],(unsigned long long)got_addr[i],(unsigned long long)got_flags[i]); return 1; } return 0; }
int main(void) {
 struct amdgpu_device d={.gart={.ready=1,.num_cpu_pages=64,.ptr=&d}};
 struct dma_map bind={.dm_nsegs=2,.dm_segs={{0x120000,4096},{0x340000,4096}}};
 struct dma_map map={.dm_nsegs=3,.dm_segs={{0x120000,8192},{0x340000,8192},{0x560000,8192}}};
 uint64_t flags=UINT64_C(0x0600000000000077); int bad=0;
 assert(amdgpu_gart_bind(&d,0x3000,2,NULL,&bind,flags)==0);
 assert(got_count==2 && pre_count==1 && post_count==1);
 bad |= check(0,3,0x120000,flags); bad |= check(1,4,0x340000,flags);
 got_count=0; assert(amdgpu_gart_map(&d,0x7000,2,4096,&map,flags,&d)==0);
 assert(got_count==2); bad |= check(0,7,0x121000,flags); bad |= check(1,8,0x340000,flags);
 got_count=0; assert(amdgpu_gart_map(&d,0x9000,2,8192,&map,flags,&d)==0);
 assert(got_count==2); bad |= check(0,9,0x340000,flags); bad |= check(1,10,0x341000,flags);
 got_count=0; assert(amdgpu_gart_map(&d,0xb000,2,12288,&map,flags,&d)==0);
 assert(got_count==2); bad |= check(0,11,0x341000,flags); bad |= check(1,12,0x560000,flags);
 if(bad) return 1; puts("PASS bind/map high PTE flags and offset crossing"); return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="ember-gart-test-") as tmp:
    cfile = Path(tmp) / "gart-test.c"
    binary = Path(tmp) / "gart-test"
    cfile.write_text(preamble + "\n".join(protos) + "\n" + "\n".join(funcs) + test)
    subprocess.run([str(args.cc), "-fuse-ld=lld", "-Wall", "-Wextra", "-Wno-unused-parameter",
                    str(cfile), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
