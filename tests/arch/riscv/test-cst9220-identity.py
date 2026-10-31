#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Developed with AI assistance.
"""Exercise the production CST9220 identification sequence with an I2C mock."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def function(source, name):
    pos = source.index('\n' + name + '(')
    start = source.rfind('\nstatic ', 0, pos)
    opening = source.index('{', pos)
    depth = 0
    for i in range(opening, len(source)):
        if source[i] == '{':
            depth += 1
        elif source[i] == '}':
            depth -= 1
            if not depth:
                return source[start + 1:i + 1]
    raise ValueError(name)


def main():
    p = argparse.ArgumentParser(); p.add_argument('--source',type=Path,required=True);p.add_argument('--clang',type=Path,required=True);a=p.parse_args()
    src=(a.source/'sys/dev/i2c/cst9220.c').read_text()
    defs=src[src.index('#define CST_REPORT'):src.index('struct cst9220_softc')]
    f=function((a.source/'sys/dev/i2c/cst9220_frame.h').read_text(),'cst9220_settle_ticks')+'\n'+'\n'.join(function(src,n) for n in ('cst_le16','cst_identify'))
    harness=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
static unsigned int hz=100;
#include <stdio.h>
#include <stddef.h>
#define MAX(a,b) ((a)>(b)?(a):(b))
#define mstohz(x) (x)
#define kpause(a,b,c,d) ((void)(c))
#define aprint_verbose_dev(...) ((void)0)
struct cst9220_softc {void *dev; bool legacy; uint16_t width,height;};
static unsigned project, bad_chip, legacy_probe;
static unsigned commands[16], ncmd, chip_reads;
static int cst_command(struct cst9220_softc *s, uint16_t reg) { (void)s; commands[ncmd++]=reg; return reg==CST_PROTOCOL_PROBE && legacy_probe ? EIO : 0; }
static int cst_io(struct cst9220_softc *s, bool read, uint16_t reg, void *buf, size_t n) {
 (void)s; assert(read); uint8_t *d=buf; memset(d,0,n);
 switch(reg) {
 case CST_MODE_STATUS: d[1]=0x1e; break;
 case CST_RESOLUTION: d[0]=0xe0;d[1]=1;d[2]=0xe0;d[3]=1;break;
 case CST_PROJECT_ID: d[0]=project&255;d[1]=project>>8;d[2]=bad_chip?0:0x20;d[3]=0x92;break;
 case CST_INFO_CHIP_ID: chip_reads++;d[1]=0x92;d[2]=0xca;d[3]=0xca;break;
 case CST_INFO_CHECKCODE: break;
 default: assert(0);
 } return 0;
}
'''
    tests=r'''
int main(void) {
 struct cst9220_softc s={0}; uint16_t p=0; project=0x6854;
 assert(cst_identify(&s,&p)==0 && p==0x6854 && !s.legacy && s.width==480 && s.height==480);
 assert(ncmd==3 && commands[0]==CST_PROTOCOL_PROBE && commands[1]==CST_NORMAL_MODE && commands[2]==CST_INFO_MODE && chip_reads==1);
 memset(&s,0,sizeof(s));ncmd=0;chip_reads=0;project=0x542f;legacy_probe=1;
 assert(cst_identify(&s,&p)==0 && s.legacy && p==0x542f && chip_reads==0);
 assert(ncmd==5 && commands[4]==CST_NORMAL_MODE);
 memset(&s,0,sizeof(s));ncmd=0;project=0x6854;bad_chip=1;legacy_probe=0;
 assert(cst_identify(&s,&p)==ENXIO && commands[ncmd-1]==CST_NORMAL_MODE);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        c=Path(tmp)/'test.c'; exe=Path(tmp)/'test';c.write_text(defs+'\n'+harness+'\n'+f+'\n'+tests)
        subprocess.run([str(a.clang),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('CST9220 identity mock passed')
if __name__ == '__main__': main()
