#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise actual CO5300 parser, row, byte order and mmap boundary code."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', required=True, type=Path)
p.add_argument('--host-prefix', required=True, type=Path)
p.add_argument('--work', required=True, type=Path)
a = p.parse_args()
host = a.host_prefix.resolve(strict=True)
cc = (host / 'bin/clang').resolve(strict=True)
ld = (host / 'bin' / ('ld64.lld' if sys.platform == 'darwin' else 'ld.lld')).resolve(strict=True)
if not cc.is_relative_to(host) or not ld.is_relative_to(host):
    p.error('compiler and linker must resolve within host prefix')
env = dict(os.environ, PATH=os.pathsep.join((str(host / 'bin'), str(host / 'gnu/bin'))))
src = (a.source / 'sys/dev/spi/co5300.c').read_text()
def extract(name):
    pos = src.index('\n' + name + '(')
    start = src.rfind('\nstatic ', 0, pos)
    end = src.index('\n}', pos) + 2
    return src[start + 1:end] + '\n'
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <sys/types.h>
#define PAGE_MASK 4095
#define PROT_EXEC 4
#define BUS_SPACE_MAP_LINEAR 1
#define BUS_SPACE_MAP_CACHEABLE 2
typedef uintptr_t vaddr_t;
typedef uint64_t paddr_t;
#define MAX(a,b) ((a) > (b) ? (a) : (b))
struct co5300_softc { void *sc_spi, *sc_bst, *sc_dev; unsigned int sc_width, sc_height;
    uintptr_t sc_fb; size_t sc_rowbytes, sc_alloc;
    uint8_t *sc_shadow, *sc_snapshot, *sc_wire, *sc_dirty;
    int sc_lock, sc_cv; uint32_t sc_rows_changed, sc_rows_sent, sc_row_errors;
    int sc_last_error; bool sc_ready, sc_stop; };
struct sysctlnode { void *sysctl_data; size_t sysctl_size; };
#define SYSCTLFN_ARGS struct sysctlnode *rnode
#define SYSCTLFN_CALL(node) node
static char status_text[128];
static int sysctl_lookup(struct sysctlnode *n) {
    assert(n->sysctl_size <= sizeof(status_text));
    memcpy(status_text, n->sysctl_data, n->sysctl_size); return 0;
}
struct call { uint8_t op,cmd; size_t len; unsigned int width; uint8_t data[8]; };
static struct call calls[8]; static int ncalls, fail_call, delays, last_ms;
static uint16_t *mutate_fb;
#define SPI_F_ERROR 1
struct spi_chunk { struct spi_chunk *next; const uint8_t *data; int count; unsigned int chunk_bus_width; };
struct spi_transfer { struct spi_chunk *first; int st_flags, st_errno; };
static void spi_transfer_init(struct spi_transfer *st) { memset(st,0,sizeof(*st)); }
static void spi_chunk_init(struct spi_chunk *c, int n, const uint8_t *data, void *rx) {
    (void)rx; memset(c,0,sizeof(*c)); c->data=data;c->count=n;c->chunk_bus_width=1;
}
static void spi_transfer_add(struct spi_transfer *st, struct spi_chunk *c) {
    if (!st->first) st->first=c; else st->first->next=c;
}
static int spi_transfer(void *bus, struct spi_transfer *st) {
    (void)bus; assert(ncalls < 8); struct call *c=&calls[ncalls++];
    struct spi_chunk *head=st->first, *data=head->next;
    assert(head->count==4 && head->chunk_bus_width==1);
    assert(head->data[1]==0 && head->data[3]==0);
    c->op=head->data[0]; c->cmd=head->data[2];
    if(data) { assert(data->count<=8);c->len=data->count;c->width=data->chunk_bus_width;
        memcpy(c->data,data->data,data->count); } else { c->len=0; c->width=1; }
    if (mutate_fb && ncalls == 3) { mutate_fb[0]=0xffff; mutate_fb=NULL; }
    if (fail_call==ncalls) { st->st_flags=SPI_F_ERROR;st->st_errno=EIO; }
    return 0;
}
static void spi_wait(struct spi_transfer *st) { (void)st; }
static unsigned int extracts, maps;
static bool extract_ok = true;
static void *pmap_kernel(void) { return (void *)0x1234; }
static bool pmap_extract(void *p, vaddr_t va, paddr_t *pa) {
    assert(p == (void *)0x1234); extracts++;
    if (!extract_ok) return false;
    *pa = (paddr_t)va + 0x80000000; return true;
}
static paddr_t bus_space_mmap(void *tag, paddr_t pa, off_t off, int prot, int flags) {
    assert(tag == (void *)0x5678 && off == 0 && prot == 3);
    assert(flags == (BUS_SPACE_MAP_LINEAR | BUS_SPACE_MAP_CACHEABLE));
    maps++; return (pa >> 12) | 0x40000000;
}
static int hz = 1000;
#define CO5300_RATE 20
static struct co5300_softc *worker_sc;
static jmp_buf worker_return;
static int reports, waits;
static void mutex_enter(int *p) { (void)p; }
static void mutex_exit(int *p) { (void)p; }
static void cv_wait(int *c, int *m) { (void)c; (void)m; assert(!"unexpected wait"); }
static int cv_timedwait(int *c, int *m, int ticks) {
    (void)c; (void)m; assert(ticks==51); waits++; worker_sc->sc_stop=true; return 0;
}
#define aprint_error_dev(dev, ...) ((void)(dev), reports++)
static _Noreturn void kthread_exit(int e) { assert(e==0); longjmp(worker_return,1); }

static int kpause(const char *name, bool intr, int ticks, void *lock) {
    (void)name;(void)intr;(void)lock;delays++;last_ms=ticks;return 0;
}
'''
suffix = r'''
int main(void) {
    uint8_t init[] = {0x11,0,0,80, 0x36,2,0,5,0x12,0x34};
    struct co5300_softc sc = {0};
    uint8_t maxinit[1028]={0};
    assert(co5300_init_valid(maxinit,1024));
    assert(!co5300_init_valid(maxinit,0) && !co5300_init_valid(maxinit,1028));
    uint8_t maxdelay[]={0,0,0x13,0x88};
    assert(co5300_init_valid(maxdelay,sizeof(maxdelay)));
    maxdelay[3]++; assert(!co5300_init_valid(maxdelay,sizeof(maxdelay)));
    uint8_t twodelays[]={0,0,0x09,0xc4,0,0,0x09,0xc5};
    assert(!co5300_init_valid(twodelays,sizeof(twodelays)));
    assert(co5300_init_valid(init,sizeof(init)));
    assert(!co5300_init_valid(init,3)); assert(!co5300_init_valid(init,sizeof(init)-1));
    assert(co5300_init_sequence(&sc,init,sizeof(init)-1)==EINVAL && ncalls==0);
    assert(co5300_init_sequence(&sc,init,sizeof(init))==0 && ncalls==2);
    assert(calls[0].op==2 && calls[0].cmd==0x11 && calls[0].len==0);
    assert(calls[1].cmd==0x36 && calls[1].len==2 && calls[1].data[1]==0x34);
    assert(delays==2 && last_ms==6);
    hz=128; assert(co5300_ticks(10)==3 && co5300_ticks(50)==8);
    uint16_t fb[4] = {0xf800,0x07e0,0x001f,0x1234};
    uint8_t shadow[8]={0}, snapshot[8], wire[8], dirty[1]={1};
    uint8_t expected[]={0xf8,0,0x07,0xe0,0,0x1f,0x12,0x34};
    sc.sc_width=4; sc.sc_fb=(uintptr_t)fb; sc.sc_rowbytes=8;
    sc.sc_shadow=shadow;sc.sc_snapshot=snapshot;sc.sc_wire=wire;sc.sc_dirty=dirty;
    ncalls=0; fail_call=3; assert(co5300_row(&sc,0)==EIO && ncalls==3);
    assert(dirty[0] && !memcmp(shadow,"\0\0\0\0\0\0\0\0",8));
    assert(calls[0].cmd==0x2a && calls[0].data[3]==3 && calls[0].width==1);
    assert(calls[1].cmd==0x2b && calls[1].width==1);
    assert(calls[2].op==0x32 && calls[2].cmd==0x2c && calls[2].width==4);
    assert(!memcmp(calls[2].data,expected,8));
    ncalls=0; fail_call=0; assert(co5300_row(&sc,0)==0 && ncalls==3 && !dirty[0]);
    assert(!memcmp(shadow,fb,8));ncalls=0;assert(co5300_row(&sc,0)==0 && ncalls==0);
    fb[0]=0xabcd;fail_call=1;assert(co5300_row(&sc,0)==EIO && ncalls==1);
    ncalls=0; fail_call=0; mutate_fb=fb;
    assert(co5300_row(&sc,0)==0 && ncalls==3);
    assert(calls[2].data[0]==0xab && calls[2].data[1]==0xcd && fb[0]==0xffff);
    ncalls=0; assert(co5300_row(&sc,0)==0 && ncalls==3);
    assert(calls[2].data[0]==0xff && calls[2].data[1]==0xff);
    assert(sc.sc_rows_changed==5 && sc.sc_rows_sent==3 && sc.sc_row_errors==2 && sc.sc_last_error==EIO);
    struct sysctlnode status_node={ .sysctl_data=&sc };
    assert(co5300_sysctl_status(&status_node)==0);
    char expected_status[128];
    snprintf(expected_status,sizeof(expected_status),
        "rows_changed=5 rows_sent=3 errors=2 last_error=%d",EIO);
    assert(!strcmp(status_text,expected_status));
    assert(co5300_mmap_valid(0,8192) && co5300_mmap_valid(4096,8192));
    assert(!co5300_mmap_valid(-1,8192) && !co5300_mmap_valid(1,8192));
    assert(!co5300_mmap_valid(8192,8192));
    sc.sc_fb=0x100000; sc.sc_alloc=8192; sc.sc_bst=(void *)0x5678;
    assert(co5300_mmap(&sc,NULL,4096,3)==((0x80101000ULL>>12)|0x40000000));
    assert(extracts==1 && maps==1);
    assert(co5300_mmap(&sc,NULL,0,PROT_EXEC)==(paddr_t)-1);
    assert(co5300_mmap(&sc,NULL,-1,3)==(paddr_t)-1);
    assert(co5300_mmap(&sc,NULL,1,3)==(paddr_t)-1);
    assert(co5300_mmap(&sc,NULL,8192,3)==(paddr_t)-1);
    assert(extracts==1 && maps==1);
    extract_ok=false; assert(co5300_mmap(&sc,NULL,0,3)==(paddr_t)-1);
    assert(extracts==2 && maps==1);
    uint16_t fb3[12]={0}; uint8_t sh3[24]={0}, dirty3[3]={1,1,1};
    sc.sc_fb=(uintptr_t)fb3; sc.sc_height=3; sc.sc_shadow=sh3;sc.sc_dirty=dirty3;
    sc.sc_ready=true;sc.sc_stop=false;worker_sc=&sc;ncalls=0;fail_call=1;hz=1000;
    if (setjmp(worker_return)==0) co5300_worker(&sc);
    assert(ncalls==1 && reports==1 && waits==1 && sc.sc_stop);
    puts("PASS CO5300 init, RGB565 wire order, dirty-row retry and mmap bounds");
}
'''
a.work.mkdir(parents=True, exist_ok=True)
cfile = a.work / 'co5300-test.c'
exe = a.work / 'co5300-test'
names = ('co5300_send', 'co5300_ticks', 'co5300_init_valid', 'co5300_init_sequence', 'co5300_to_wire', 'co5300_row', 'co5300_sysctl_status', 'co5300_mmap_valid', 'co5300_mmap', 'co5300_worker')
cfile.write_text(prefix + ''.join(extract(n) for n in names) + suffix)
subprocess.run([str(cc), '-fuse-ld=lld', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                str(cfile), '-o', str(exe)], env=env, check=True)
subprocess.run([str(exe)], env=env, check=True)
