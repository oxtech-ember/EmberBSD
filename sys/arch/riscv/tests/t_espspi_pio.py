#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run real ESP SPI PIO functions with a simulated register bank."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', required=True, type=Path, help='OS source tree')
p.add_argument('--host-prefix', required=True, type=Path)
p.add_argument('--work', required=True, type=Path)
a = p.parse_args()
host = a.host_prefix.resolve(strict=True)
cc = (host / 'bin/clang').resolve(strict=True)
ld = (host / 'bin' / ('ld64.lld' if sys.platform == 'darwin' else 'ld.lld')).resolve(strict=True)
if not cc.is_relative_to(host) or not ld.is_relative_to(host):
    p.error('compiler and linker must be in the host prefix')
env = dict(os.environ, PATH=os.pathsep.join((str(host / 'bin'), str(host / 'gnu/bin'))))
src = (a.source / 'sys/arch/riscv/espressif/esp_spi.c').read_text()
reg = (a.source / 'sys/arch/riscv/espressif/esp_spireg.h').read_text()
def extract(name):
    mark = '\n' + name + '('
    pos = src.index(mark)
    start = src.rfind('\nstatic ', 0, pos)
    end = src.index('\n}', pos) + 2
    if start < 0:
        raise ValueError(name)
    return src[start + 1:end] + '\n'

prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
typedef unsigned int u_int;
typedef unsigned int bus_size_t;
#define __BIT(n) (UINT32_C(1) << (n))
#define __BITS(h,l) ((UINT32_MAX >> (31-(h))) & (UINT32_MAX << (l)))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define KASSERT(x) assert(x)
#define SPI_MODE_0 0
#define SPI_MODE_1 1
#define SPI_MODE_2 2
#define SPI_MODE_3 3
#define SPI_MODE_CPOL 2
struct spi_chunk { struct spi_chunk *chunk_next; int chunk_count, chunk_wresid, chunk_rresid; const uint8_t *chunk_write,*chunk_wptr; uint8_t *chunk_read,*chunk_rptr; unsigned int chunk_bus_width; };
struct spi_transfer { struct spi_chunk *st_chunks; int error, completions; };
struct spi_controller { int sct_nslaves; };
struct esp_spi_softc { void *sc_syscon; struct spi_controller sc_spi; unsigned int sc_clkrst,sc_source_hz,sc_speed; uint32_t sc_clock,sc_user,sc_misc; };
static uint32_t regs[64], sysreg;
static unsigned int clocks, done_count, reset_count, writes, lockdepth;
static bool fail_update, fail_done;
struct event { uint32_t user,misc,dlen; uint8_t bytes[64]; };
static struct event events[8];
static uint32_t rd(struct esp_spi_softc *sc, unsigned int reg);
static void wr(struct esp_spi_softc *sc, unsigned int reg, uint32_t value);
#define ESPSPI_READ(sc,reg) rd(sc,reg)
#define ESPSPI_WRITE(sc,reg,value) wr(sc,reg,value)
static void delay(int n) { (void)n; }
static void syscon_lock(void *s) { (void)s; assert(lockdepth++ == 0); }
static void syscon_unlock(void *s) { (void)s; assert(--lockdepth == 0); }
static uint32_t syscon_read_4(void *s,unsigned int r) { (void)s; (void)r; return sysreg; }
static void syscon_write_4(void *s,unsigned int r,uint32_t v) { (void)s;(void)r;sysreg=v; }
static void spi_done(struct spi_transfer *st,int error) { st->error=error; st->completions++; }
'''
model = r'''
static uint32_t rd(struct esp_spi_softc *sc, unsigned int reg) {
    (void)sc;
    if (reg == ESPSPI_CMD && (regs[reg/4] & ESPSPI_UPDATE) && !fail_update)
        regs[reg/4] &= ~ESPSPI_UPDATE;
    if (reg == ESPSPI_INT_RAW && fail_done)
        return 0;
    return regs[reg/4];
}
static void wr(struct esp_spi_softc *sc, unsigned int reg, uint32_t v) {
    (void)sc; assert(reg < sizeof(regs)); writes++;
    if (reg == ESPSPI_CLK_GATE) reset_count++;
    if (reg == ESPSPI_INT_CLR) regs[ESPSPI_INT_RAW/4] = 0;
    regs[reg/4] = v;
    if (reg == ESPSPI_CMD && v == ESPSPI_USR) {
        assert(clocks < 8); struct event *e = &events[clocks++];
        e->user=regs[ESPSPI_USER/4]; e->misc=regs[ESPSPI_MISC/4];
        e->dlen=regs[ESPSPI_DLEN/4];
        memcpy(e->bytes, &regs[ESPSPI_W0/4], sizeof(e->bytes));
        if (e->user & ESPSPI_USR_MISO) regs[ESPSPI_W0/4] = 0x44332211;
        regs[ESPSPI_INT_RAW/4] = fail_done ? 0 : ESPSPI_TRANS_DONE; done_count++;
    }
}
'''
suffix = r'''
static struct spi_chunk chunk(int count, const uint8_t *tx, uint8_t *rx, unsigned int width) {
    struct spi_chunk c = {0}; c.chunk_count = c.chunk_wresid = c.chunk_rresid = count;
    c.chunk_write = c.chunk_wptr = tx; c.chunk_read = c.chunk_rptr = rx; c.chunk_bus_width=width; return c;
}
static void clear_events(void) { clocks=done_count=0; memset(events,0,sizeof(events)); }
int main(void) {
    struct esp_spi_softc sc = { .sc_source_hz=40000000, .sc_spi.sct_nslaves=1 };
    uint8_t bytes[70]; for (unsigned int i=0;i<sizeof(bytes);i++) bytes[i]=(uint8_t)i;
    uint32_t c; u_int hz; assert(esp_spi_clock(0,1,&c,&hz)==EINVAL);
    assert(esp_spi_clock(40000000,40000000,&c,&hz)==0 && c==ESPSPI_CLOCK_DIRECT && hz==40000000);
    assert(esp_spi_clock(40000000,13333333,&c,&hz)==0);
    unsigned int div=(((c>>18)&15)+1)*(((c>>12)&63)+1);
    assert((uint64_t)hz<=13333333 && (uint64_t)13333333*div>=40000000);
    assert(esp_spi_clock(40000000,39062,&c,&hz)==EINVAL);
    assert(esp_spi_clock(40000000,39063,&c,&hz)==0);
    assert(esp_spi_configure(&sc,0,SPI_MODE_0,10000000)==0);
    const int sizes[]={1,3,63,64,65};
    for(unsigned int k=0;k<sizeof(sizes)/sizeof(sizes[0]);k++) {
        int n=sizes[k]; struct spi_chunk c1=chunk(n,bytes,NULL,1); struct spi_transfer st={.st_chunks=&c1};
        clear_events(); assert(esp_spi_transfer(&sc,&st)==0); assert(st.completions==1 && st.error==0);
        assert(c1.chunk_wresid==0 && c1.chunk_rresid==0); assert(clocks==(n>64?2U:1U));
        unsigned int first=n>64?64U:(unsigned int)n;
        assert(events[0].dlen==first*8-1 && memcmp(events[0].bytes,bytes,first)==0);
        for (unsigned int i=first; i<((first+3U)&~3U); i++) assert(events[0].bytes[i]==0xff);
        if(n>64) { assert(events[0].misc & ESPSPI_CS_KEEP_ACTIVE);
            assert(events[1].dlen==7 && events[1].bytes[0]==bytes[64]);
            assert(!(events[1].misc & ESPSPI_CS_KEEP_ACTIVE));
        } else assert(!(events[0].misc & ESPSPI_CS_KEEP_ACTIVE));
    }
    struct spi_chunk a=chunk(1,bytes,NULL,1), b=chunk(3,bytes+1,NULL,4);
    struct spi_transfer st={.st_chunks=&a}; a.chunk_next=&b; clear_events();
    assert(esp_spi_transfer(&sc,&st)==0 && st.error==0 && st.completions==1 && clocks==2);
    assert(events[0].misc & ESPSPI_CS_KEEP_ACTIVE); assert(!(events[1].misc & ESPSPI_CS_KEEP_ACTIVE));
    assert(!(events[0].user & ESPSPI_FWRITE_QUAD)); assert(events[1].user & ESPSPI_FWRITE_QUAD);
    clear_events(); fail_update=true; a=chunk(1,bytes,NULL,1); st=(struct spi_transfer){.st_chunks=&a};
    unsigned int before=reset_count; assert(esp_spi_transfer(&sc,&st)==0 && st.error==ETIMEDOUT && st.completions==1);
    assert(a.chunk_wresid==1 && reset_count==before+1 && clocks==0);
    fail_update=false; clear_events(); st=(struct spi_transfer){.st_chunks=&a};
    assert(esp_spi_transfer(&sc,&st)==0 && st.error==0 && st.completions==1 && a.chunk_wresid==0);
    unsigned int resets_before=reset_count;
    fail_done=true; a=chunk(3,bytes,NULL,1); st=(struct spi_transfer){.st_chunks=&a};
    clear_events(); assert(esp_spi_transfer(&sc,&st)==0 && st.error==ETIMEDOUT && st.completions==1);
    assert(a.chunk_wresid==3 && reset_count==resets_before+1 && regs[ESPSPI_MISC/4]==sc.sc_misc);
    fail_done=false; a=chunk(3,bytes,NULL,1); st=(struct spi_transfer){.st_chunks=&a}; clear_events();
    assert(esp_spi_transfer(&sc,&st)==0 && st.error==0 && st.completions==1);
    uint8_t rx[3]={0}; a=chunk(3,NULL,rx,1); st=(struct spi_transfer){.st_chunks=&a}; clear_events();
    assert(esp_spi_transfer(&sc,&st)==0 && st.error==0 && st.completions==1);
    assert((events[0].user & ESPSPI_USR_MISO) && !(events[0].user & ESPSPI_USR_MOSI));
    assert(rx[0]==0x11 && rx[1]==0x22 && rx[2]==0x33 && a.chunk_rresid==0);
    assert(esp_spi_configure(&sc,0,SPI_MODE_0,20000000)==0);
    a=chunk(3,NULL,rx,1); st=(struct spi_transfer){.st_chunks=&a}; before=writes; clear_events();
    assert(esp_spi_transfer(&sc,&st)==0 && st.error==EINVAL && st.completions==1 && writes==before && clocks==0);
    puts("PASS actual ESP SPI clock, tails, CS, width, timeout and retry with mocked MMIO");
}
'''
a.work.mkdir(parents=True, exist_ok=True)
cfile = a.work / 'esp-spi-fixture.c'
exe = a.work / 'esp-spi-fixture'
cfile.write_text(prefix + '\n' + reg + '\n' + model + '\n' + '\n'.join(extract(x) for x in ('esp_spi_reset', 'esp_spi_clock', 'esp_spi_configure', 'esp_spi_wait', 'esp_spi_block', 'esp_spi_transfer')) + suffix)
subprocess.run([str(cc), '-fuse-ld=lld', '-std=c11', '-Wall', '-Werror', '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(cfile), '-o', str(exe)], env=env, check=True)
subprocess.run([str(exe)], env=env, check=True)
