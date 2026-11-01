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
p.add_argument('--timeout-errno', type=int,
               help='override the host ETIMEDOUT value for portability checks')
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
#include <stdlib.h>
#include <time.h>
#ifdef ESPSPI_TEST_ETIMEDOUT
#undef ETIMEDOUT
#define ETIMEDOUT ESPSPI_TEST_ETIMEDOUT
#endif
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
#define SPI_F_3WIRE 0x0004
#define ESPSPI_SIO __BIT(17)
#define ESPSPI_TRACE_COMMANDS 24
#define ESPSPI_TRACE_SAMPLE 8
#define ESPSPI_TRACE_TEXT_SIZE 16384
typedef int kmutex_t;
struct esp_spi_trace_block { u_int block,count,width; uint32_t ctrl,user,misc,dlen;
    uint32_t dma_before_reset,dma_asserted,dma_released;
    uint32_t raw_before_clear,raw_after_clear,raw_done;
    uint32_t cmd_before_update,cmd_after_update,cmd_after_usr; int error;
    uint8_t first[4],last[4]; };
struct esp_spi_trace_chunk { bool valid,hash_valid,time_valid; u_int sequence,index,len,width;
    uint32_t hash,csr_start,csr_end; uint64_t mono_start_us,mono_end_us;
    uint8_t first[ESPSPI_TRACE_SAMPLE],last[ESPSPI_TRACE_SAMPLE];
    struct esp_spi_trace_block first_block,last_block; };
struct esp_spi_trace_snapshot { u_int sequence,command_count;
    struct esp_spi_trace_chunk commands[ESPSPI_TRACE_COMMANDS];
    struct esp_spi_trace_chunk pixel,latest; struct esp_spi_trace_block error_block;
    bool error_block_valid; int last_error; };
struct spi_chunk { struct spi_chunk *chunk_next; int chunk_count, chunk_wresid, chunk_rresid; const uint8_t *chunk_write,*chunk_wptr; uint8_t *chunk_read,*chunk_rptr; unsigned int chunk_bus_width; };
struct spi_transfer { struct spi_chunk *st_chunks; int error, completions, st_flags; };
struct spi_controller { int sct_nslaves; };
struct sysctlnode { void *sysctl_data; size_t sysctl_size; };
#define SYSCTLFN_ARGS struct sysctlnode *rnode
#define SYSCTLFN_CALL(node) node
#define KM_SLEEP 0
struct esp_spi_softc { void *sc_syscon; struct spi_controller sc_spi; unsigned int sc_clkrst,sc_source_hz,sc_speed; uint32_t sc_clock,sc_user,sc_misc; kmutex_t sc_trace_lock; u_int sc_trace_sequence,sc_trace_command_count; struct esp_spi_trace_chunk sc_trace_commands[ESPSPI_TRACE_COMMANDS],sc_trace_pixel,sc_trace_latest; struct esp_spi_trace_block sc_trace_error_block; bool sc_trace_error_block_valid; int sc_trace_last_error; };
static uint32_t regs[64], sysreg;
static unsigned int clocks, done_count, reset_count, writes, ctrl_writes, lockdepth;
static uint32_t last_ctrl;
static bool fail_update, fail_done; static int fail_done_at;
struct event { uint32_t ctrl,user,misc,dlen; uint8_t bytes[64]; };
static struct event events[8];
static int trace_mutex_depth; static char trace_text[20000];
static uint32_t fake_csr_time; static uint64_t fake_uptime_us;
static uint32_t csr_time_read(void) { fake_csr_time+=100; return fake_csr_time; }
static void getnanouptime(struct timespec *ts) { fake_uptime_us+=100;
    ts->tv_sec=(time_t)(fake_uptime_us/1000000);
    ts->tv_nsec=(long)(fake_uptime_us%1000000)*1000; }
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
static void mutex_enter(kmutex_t *m) { assert(*m==0); *m=1; trace_mutex_depth++; }
static void mutex_exit(kmutex_t *m) { assert(*m==1); *m=0; trace_mutex_depth--; }
static int sysctl_lookup(struct sysctlnode *n) { assert(n->sysctl_size<=sizeof(trace_text)); memcpy(trace_text,n->sysctl_data,n->sysctl_size); return 0; }
static void *kmem_alloc(size_t n,int flags) { (void)flags; return malloc(n); }
static void kmem_free(void *p,size_t n) { (void)n; free(p); }
'''
model = r'''
static uint32_t rd(struct esp_spi_softc *sc, unsigned int reg) {
    (void)sc;
    if (reg == ESPSPI_CMD && (regs[reg/4] & ESPSPI_UPDATE) && !fail_update)
        regs[reg/4] &= ~ESPSPI_UPDATE;
    if (reg == ESPSPI_CMD && (regs[reg/4] & ESPSPI_USR) &&
        (regs[ESPSPI_INT_RAW/4] & ESPSPI_TRANS_DONE))
        regs[reg/4] &= ~ESPSPI_USR;
    if (reg == ESPSPI_INT_RAW && (fail_done ||
        (fail_done_at != 0 && done_count == fail_done_at)))
        return 0;
    return regs[reg/4];
}
static void wr(struct esp_spi_softc *sc, unsigned int reg, uint32_t v) {
    (void)sc; assert(reg < sizeof(regs)); writes++;
    if (reg == ESPSPI_CLK_GATE) reset_count++;
    if (reg == ESPSPI_CTRL) { ctrl_writes++; last_ctrl=v; }
    if (reg == ESPSPI_INT_CLR) regs[ESPSPI_INT_RAW/4] = 0;
    regs[reg/4] = v;
    if (reg == ESPSPI_CMD && v == ESPSPI_USR) {
        assert(clocks < 8); struct event *e = &events[clocks++];
        e->ctrl=regs[ESPSPI_CTRL/4]; e->user=regs[ESPSPI_USER/4];
        e->misc=regs[ESPSPI_MISC/4];
        assert(e->user & UINT32_C(0x00000040));
        e->dlen=regs[ESPSPI_DLEN/4];
        memcpy(e->bytes, &regs[ESPSPI_W0/4], sizeof(e->bytes));
        if (e->user & ESPSPI_USR_MISO) regs[ESPSPI_W0/4] = 0x44332211;
        done_count++;
        regs[ESPSPI_INT_RAW/4] = fail_done ||
            (fail_done_at != 0 && done_count == fail_done_at) ? 0 : ESPSPI_TRANS_DONE;
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
    esp_spi_reset(&sc);
    /* S31 SPI_CLK_GATE bits 0/1 enable clocks; bit 2 selects PLL. */
    assert(regs[ESPSPI_CLK_GATE/4] == 3);
    assert(regs[ESPSPI_CTRL/4] == ESPSPI_CTRL_IDLE_HIGH);
    assert(ctrl_writes == 1 && last_ctrl == ESPSPI_CTRL_IDLE_HIGH);
    assert((regs[ESPSPI_USER1/4] & UINT32_C(0x07c00000)) ==
        UINT32_C(0x00400000));
    uint8_t bytes[70]; for (unsigned int i=0;i<sizeof(bytes);i++) bytes[i]=(uint8_t)i;
    uint32_t c; u_int hz; assert(esp_spi_clock(0,1,&c,&hz)==EINVAL);
    assert(esp_spi_clock(40000000,40000000,&c,&hz)==0 && c==ESPSPI_CLOCK_DIRECT && hz==40000000);
    assert(esp_spi_clock(40000000,13333333,&c,&hz)==0);
    unsigned int div=(((c>>18)&15)+1)*(((c>>12)&63)+1);
    assert((uint64_t)hz<=13333333 && (uint64_t)13333333*div>=40000000);
    assert(esp_spi_clock(40000000,39062,&c,&hz)==EINVAL);
    assert(esp_spi_clock(40000000,39063,&c,&hz)==0);
    assert(esp_spi_configure(&sc,0,SPI_MODE_0,10000000)==0);
    assert(sc.sc_user & UINT32_C(0x00000040));
    const int sizes[]={1,3,63,64,65};
    for(unsigned int k=0;k<sizeof(sizes)/sizeof(sizes[0]);k++) {
        int n=sizes[k]; struct spi_chunk c1=chunk(n,bytes,NULL,1); struct spi_transfer st={.st_chunks=&c1};
        clear_events(); assert(esp_spi_transfer(&sc,&st)==0); assert(st.completions==1 && st.error==0);
        assert(events[0].user & UINT32_C(0x00000040));
        assert(c1.chunk_wresid==0 && c1.chunk_rresid==0); assert(clocks==(n>64?2U:1U));
        unsigned int first=n>64?64U:(unsigned int)n;
        assert(events[0].dlen==first*8-1 && memcmp(events[0].bytes,bytes,first)==0);
        for (unsigned int i=first; i<((first+3U)&~3U); i++) assert(events[0].bytes[i]==0xff);
        if(n>64) { assert(events[0].misc & ESPSPI_CS_KEEP_ACTIVE);
            assert(events[1].dlen==7 && events[1].bytes[0]==bytes[64]);
            assert(!(events[1].misc & ESPSPI_CS_KEEP_ACTIVE));
        } else assert(!(events[0].misc & ESPSPI_CS_KEEP_ACTIVE));
    }
    /* IDF S31 CTRL.FREAD_DUAL/QUAD: bits 14/15; USER.FWRITE remains 12/13. */
    const uint32_t fread_dual=UINT32_C(1)<<14, fread_quad=UINT32_C(1)<<15;
    const uint32_t fread_mask=fread_dual|fread_quad;
    struct spi_chunk a=chunk(1,bytes,NULL,1), b=chunk(3,bytes+1,NULL,4);
    struct spi_chunk cchunk=chunk(1,bytes+4,NULL,1);
    struct spi_transfer st={.st_chunks=&a};
    a.chunk_next=&b; b.chunk_next=&cchunk; clear_events();
    assert(esp_spi_transfer(&sc,&st)==0 && st.error==0 && st.completions==1 && clocks==3);
    assert((events[0].ctrl & ESPSPI_CTRL_IDLE_HIGH)==ESPSPI_CTRL_IDLE_HIGH);
    assert((events[1].ctrl & ESPSPI_CTRL_IDLE_HIGH)==ESPSPI_CTRL_IDLE_HIGH);
    assert((events[2].ctrl & ESPSPI_CTRL_IDLE_HIGH)==ESPSPI_CTRL_IDLE_HIGH);
    assert((events[0].ctrl & fread_mask)==0);
    assert((events[1].ctrl & fread_mask)==fread_quad);
    assert((events[2].ctrl & fread_mask)==0);
    assert(events[0].misc & ESPSPI_CS_KEEP_ACTIVE);
    assert(events[1].misc & ESPSPI_CS_KEEP_ACTIVE);
    assert(!(events[2].misc & ESPSPI_CS_KEEP_ACTIVE));
    assert(!(events[0].user & ESPSPI_FWRITE_QUAD));
    assert(events[1].user & ESPSPI_FWRITE_QUAD);
    assert(!(events[2].user & ESPSPI_FWRITE_QUAD));
    a=chunk(3,bytes,NULL,2); st=(struct spi_transfer){.st_chunks=&a}; clear_events();
    assert(esp_spi_transfer(&sc,&st)==0 && st.error==0 && st.completions==1 && clocks==1);
    assert((events[0].ctrl & fread_mask)==fread_dual);
    assert(events[0].user & ESPSPI_FWRITE_DUAL);
    struct esp_spi_softc diag_sc={ .sc_source_hz=40000000,.sc_spi.sct_nslaves=1 };
    assert(esp_spi_configure(&diag_sc,0,SPI_MODE_0,10000000)==0);
    uint8_t cmd_a[]={0x02,0,0x11,0}, cmd_b[]={0x02,0,0x29,0};
    a=chunk(4,cmd_a,NULL,1); st=(struct spi_transfer){.st_chunks=&a}; clear_events();
    assert(esp_spi_transfer(&diag_sc,&st)==0 && st.error==0);
    a=chunk(4,cmd_b,NULL,1); st=(struct spi_transfer){.st_chunks=&a}; clear_events();
    assert(esp_spi_transfer(&diag_sc,&st)==0 && st.error==0);
    assert(diag_sc.sc_trace_command_count==2 &&
        diag_sc.sc_trace_commands[0].hash_valid &&
        diag_sc.sc_trace_commands[0].hash==esp_spi_trace_hash(cmd_a,4) &&
        diag_sc.sc_trace_commands[0].time_valid &&
        diag_sc.sc_trace_commands[0].csr_end>diag_sc.sc_trace_commands[0].csr_start &&
        diag_sc.sc_trace_commands[0].mono_end_us>diag_sc.sc_trace_commands[0].mono_start_us &&
        diag_sc.sc_trace_commands[1].first[2]==0x29);
    uint8_t black[70]={0};
    a=chunk(sizeof(black),black,NULL,4); st=(struct spi_transfer){.st_chunks=&a}; clear_events();
    assert(esp_spi_transfer(&diag_sc,&st)==0 && st.error==0 &&
        !diag_sc.sc_trace_pixel.valid);
    a=chunk(sizeof(bytes),bytes,NULL,4); st=(struct spi_transfer){.st_chunks=&a}; clear_events();
    assert(esp_spi_transfer(&diag_sc,&st)==0 && st.error==0 &&
        diag_sc.sc_trace_pixel.valid && diag_sc.sc_trace_pixel.len==sizeof(bytes) &&
        diag_sc.sc_trace_pixel.hash_valid &&
        diag_sc.sc_trace_pixel.hash==esp_spi_trace_hash(bytes,sizeof(bytes)) &&
        diag_sc.sc_trace_pixel.first_block.count==64 &&
        diag_sc.sc_trace_pixel.last_block.block==1 &&
        diag_sc.sc_trace_pixel.last_block.count==6 &&
        diag_sc.sc_trace_pixel.first_block.dma_asserted ==
            (ESPSPI_DMA_BASE|ESPSPI_TX_FIFO_RESET|ESPSPI_RX_FIFO_RESET) &&
        diag_sc.sc_trace_pixel.first_block.dma_released==ESPSPI_DMA_BASE &&
        diag_sc.sc_trace_pixel.time_valid &&
        diag_sc.sc_trace_pixel.first_block.ctrl & ESPSPI_FREAD_QUAD &&
        diag_sc.sc_trace_pixel.first_block.raw_before_clear==ESPSPI_TRANS_DONE &&
        diag_sc.sc_trace_pixel.first_block.raw_after_clear==0 &&
        diag_sc.sc_trace_pixel.first_block.raw_done==ESPSPI_TRANS_DONE &&
        diag_sc.sc_trace_pixel.first_block.cmd_after_update==0 &&
        diag_sc.sc_trace_pixel.first_block.cmd_after_usr==0);
    struct sysctlnode trace_node={ .sysctl_data=&diag_sc };
    assert(esp_spi_sysctl_trace(&trace_node)==0 &&
        strstr(trace_text,"cmd0=") != NULL &&
        strstr(trace_text,"pixel=") != NULL &&
        strstr(trace_text,"irq00001000>00000000/00001000") != NULL);
    diag_sc.sc_trace_pixel.valid=false;
    fail_done=true; a=chunk(3,bytes,NULL,4); st=(struct spi_transfer){.st_chunks=&a}; clear_events();
    assert(esp_spi_transfer(&diag_sc,&st)==0 && st.error==ETIMEDOUT &&
        diag_sc.sc_trace_pixel.valid &&
        diag_sc.sc_trace_pixel.first_block.error==ETIMEDOUT &&
        diag_sc.sc_trace_last_error==ETIMEDOUT);
    fail_done=false;

    struct esp_spi_softc full_sc={ .sc_source_hz=40000000,.sc_spi.sct_nslaves=1 };
    assert(esp_spi_configure(&full_sc,0,SPI_MODE_0,10000000)==0);
    uint8_t init_cmd[4];
    for (unsigned int i=0;i<ESPSPI_TRACE_COMMANDS;i++) {
        init_cmd[0]=0x02; init_cmd[1]=0; init_cmd[2]=(uint8_t)i;
        init_cmd[3]=(uint8_t)(0x80+i);
        a=chunk(sizeof(init_cmd),init_cmd,NULL,1);
        st=(struct spi_transfer){.st_chunks=&a}; clear_events();
        assert(esp_spi_transfer(&full_sc,&st)==0 && st.error==0);
    }
    assert(full_sc.sc_trace_command_count==ESPSPI_TRACE_COMMANDS);
    uint8_t full_pixels[150];
    for (unsigned int i=0;i<sizeof(full_pixels);i++) full_pixels[i]=(uint8_t)(i+1);
    a=chunk(sizeof(full_pixels),full_pixels,NULL,4);
    st=(struct spi_transfer){.st_chunks=&a}; clear_events();
    assert(esp_spi_transfer(&full_sc,&st)==0 && st.error==0 &&
        full_sc.sc_trace_pixel.valid && full_sc.sc_trace_latest.valid);
    fail_done_at=2; a=chunk(sizeof(full_pixels),full_pixels,NULL,4);
    st=(struct spi_transfer){.st_chunks=&a}; clear_events();
    assert(esp_spi_transfer(&full_sc,&st)==0 && st.error==ETIMEDOUT &&
        full_sc.sc_trace_error_block_valid &&
        full_sc.sc_trace_error_block.block==1 &&
        full_sc.sc_trace_error_block.error==ETIMEDOUT &&
        (full_sc.sc_trace_error_block.misc & ESPSPI_CS_KEEP_ACTIVE));
    fail_done_at=0;
    trace_node.sysctl_data=&full_sc;
    assert(esp_spi_sysctl_trace(&trace_node)==0 &&
        strstr(trace_text,"cmd23=") != NULL &&
        strstr(trace_text,"pixel=") != NULL &&
        strstr(trace_text,"latest=") != NULL &&
        strstr(trace_text," fail=1+64/w4:") != NULL);
    char timeout_field[32];
    snprintf(timeout_field,sizeof(timeout_field),"-e%d latest=",ETIMEDOUT);
    const char *failure=strstr(trace_text," fail=1+64/w4:");
    assert(failure != NULL && strstr(failure,timeout_field) != NULL);
    assert(trace_mutex_depth==0);
    clear_events(); fail_update=true; a=chunk(1,bytes,NULL,1); st=(struct spi_transfer){.st_chunks=&a};
    unsigned int before=reset_count; assert(esp_spi_transfer(&sc,&st)==0 && st.error==ETIMEDOUT && st.completions==1);
    assert(a.chunk_wresid==1 && reset_count==before+1 && clocks==0);
    assert(regs[ESPSPI_CTRL/4] == ESPSPI_CTRL_IDLE_HIGH);
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
    assert((events[0].ctrl & fread_mask)==0);
    assert(!(events[0].user & (ESPSPI_FWRITE_DUAL | ESPSPI_FWRITE_QUAD)));
    assert(rx[0]==0x11 && rx[1]==0x22 && rx[2]==0x33 && a.chunk_rresid==0);
    uint8_t command[]={0x03,0x00,0x04,0x00}, result[3]={0};
    struct spi_chunk command_chunk=chunk(4,command,NULL,1);
    struct spi_chunk result_chunk=chunk(3,NULL,result,1);
    command_chunk.chunk_next=&result_chunk;
    st=(struct spi_transfer){.st_chunks=&command_chunk,.st_flags=SPI_F_3WIRE};
    clear_events(); assert(esp_spi_transfer(&sc,&st)==0 && st.error==0 &&
        st.completions==1 && clocks==2);
    assert((events[0].user & ESPSPI_SIO) &&
        (events[0].user & ESPSPI_USR_MOSI) &&
        !(events[0].user & ESPSPI_USR_MISO));
    assert((events[1].user & ESPSPI_SIO) &&
        !(events[1].user & ESPSPI_USR_MOSI) &&
        (events[1].user & ESPSPI_USR_MISO));
    assert((events[0].misc & ESPSPI_CS_KEEP_ACTIVE) &&
        !(events[1].misc & ESPSPI_CS_KEEP_ACTIVE));
    assert(result[0]==0x11 && result[1]==0x22 && result[2]==0x33);
    assert(esp_spi_configure(&sc,0,SPI_MODE_0,20000000)==0);
    a=chunk(3,NULL,rx,1); st=(struct spi_transfer){.st_chunks=&a}; before=writes; clear_events();
    assert(esp_spi_transfer(&sc,&st)==0 && st.error==EINVAL && st.completions==1 && writes==before && clocks==0);
    puts("PASS actual ESP SPI clock, tails, CS, width, timeout and retry with mocked MMIO");
    return 0;
}
'''
a.work.mkdir(parents=True, exist_ok=True)
cfile = a.work / 'esp-spi-fixture.c'
exe = a.work / 'esp-spi-fixture'
cfile.write_text(prefix + '\n' + reg + '\n' + model + '\n' + '\n'.join(extract(x) for x in ('esp_spi_trace_hash', 'esp_spi_uptime_us', 'esp_spi_trace_time_start', 'esp_spi_trace_time_end', 'esp_spi_sysctl_trace', 'esp_spi_reset', 'esp_spi_clock', 'esp_spi_configure', 'esp_spi_wait', 'esp_spi_trace_init', 'esp_spi_block', 'esp_spi_transfer')) + suffix)
compile = [str(cc), '-fuse-ld=lld', '-std=c11', '-Wall', '-Werror', '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
if a.timeout_errno is not None:
    compile.append(f'-DESPSPI_TEST_ETIMEDOUT={a.timeout_errno}')
compile.extend([str(cfile), '-o', str(exe)])
subprocess.run(compile, env=env, check=True)
subprocess.run([str(exe)], env=env, check=True)
