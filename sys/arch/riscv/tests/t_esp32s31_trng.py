#!/usr/bin/env python3
"""Exercise the actual bounded S31 TRNG sequence with a synthetic register bus."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
source=Path(__file__).resolve().parents[1]/'espressif/esp_trng.c'
s=source.read_text()
for name, expected in {'CLOCK_REG':'0x2071003cU','TRNG_DATE':'0x208140fcU',
                       'TRNG_CONF':'0x20814000U','TRNG_DATA':'0x20814048U'}.items():
    match=re.search(r'^#define\s+'+name+r'\s+(\S+)',s,re.M)
    assert match and match.group(1)==expected, name
sample_count=int(re.search(r'^#define\s+TRNG_SAMPLES\s+(\d+)',s,re.M).group(1))
max_bits=int(re.search(r'^#define\s+TRNG_MAX_BITS\s+(\d+)',s,re.M).group(1))
header=s[s.index('struct esp_trng_io {'):s.index('};',s.index('struct esp_trng_io {'))+2]
m=re.search(r'\nstatic int\nesp_trng_collect\([^)]*\)\n\{.*?\n\}',s,re.S)
assert m, 'missing real collection function'
harness=r'''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#define TRNG_SAMPLES @SAMPLES@
#define TRNG_MAX_BITS @BITS@
#define CLOCK_REG 0
#define TRNG_DATE 1
#define TRNG_CONF 2
#define TRNG_DATA 3
#define CLOCK_EN (1U << 30)
#define CLOCK_RST (1U << 31)
#define DATE_CLK (1U << 28)
#define CONF_SAMPLE (1U << 31)
#define CONF_NOISE (1U << 30)
'''+header+r'''
static uint32_t regs[4], saved[3]; static unsigned reads, waits; static unsigned mode;
static uint32_t rd(uint32_t r) { if(r!=CLOCK_REG)assert(regs[CLOCK_REG]&CLOCK_EN); if(r==TRNG_DATA){assert((regs[TRNG_DATE]&DATE_CLK)!=0);assert((regs[TRNG_CONF]&(CONF_SAMPLE|CONF_NOISE))==(CONF_SAMPLE|CONF_NOISE));assert(waits>reads);reads++;return mode==1?42:mode==2?(reads%4):reads*2654435761U;} return regs[r]; }
static void wr(uint32_t r,uint32_t v) { assert(r<3);regs[r]=v; if(r==CLOCK_REG && (v&CLOCK_RST)){regs[TRNG_DATE]=0;regs[TRNG_CONF]=0;} }
static void pause_us(unsigned n) { assert(n>=1000);waits++; }
'''+m.group()+r'''
int main(void) {
 const struct esp_trng_io io={rd,wr,pause_us}; uint32_t samples[TRNG_SAMPLES];
 regs[0]=0x83;regs[1]=0x104;regs[2]=0x208|CONF_NOISE;for(int i=0;i<3;i++)saved[i]=regs[i];saved[2]&=~CONF_NOISE;
 assert(esp_trng_collect(&io,samples)==TRNG_MAX_BITS);assert(reads==TRNG_SAMPLES);assert(waits>=TRNG_SAMPLES);
 for(int i=0;i<3;i++)assert(regs[i]==saved[i]);
 reads=waits=0;mode=1;assert(esp_trng_collect(&io,samples)==0);assert(reads==TRNG_SAMPLES);reads=waits=0;mode=2;assert(esp_trng_collect(&io,samples)==0);assert(reads==TRNG_SAMPLES);
 for(int i=0;i<3;i++)assert(regs[i]==saved[i]);
 reads=waits=0;regs[2]|=CONF_SAMPLE;assert(esp_trng_collect(&io,samples)==-1);assert(reads==0 && waits==0);
 regs[2]=saved[2];regs[0]|=CLOCK_RST;assert(esp_trng_collect(&io,samples)==-1);assert(reads==0 && waits==0);
 return 0;
}
'''
harness=harness.replace('@SAMPLES@',str(sample_count)).replace('@BITS@',str(max_bits))
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'test.c';out=Path(td)/'test';c.write_text(harness)
 subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('bounded TRNG sequence, stuck source, clock restoration and noise shutdown passed')
