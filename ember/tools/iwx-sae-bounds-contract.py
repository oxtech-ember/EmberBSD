#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors. AI-assisted (OpenAI Codex).
"""Check real firmware capability parsing and allocation failure paths."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--cflags', default='')
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    driver = root / 'ember/development/iwx-be200/src'
    source = (driver / 'if_iwx.c').read_text()
    register = (driver / 'if_iwxreg.h').read_text()
    monitor = re.search(r'(?m)^int\niwx_alloc_fw_monitor_block\([\s\S]+?^}', source).group()
    cases = []
    for name in ('API_CHANGES_SET', 'ENABLED_CAPABILITIES'):
        start = source.index('\t\tcase IWX_UCODE_TLV_' + name + ': {')
        end = source.index('\n\t\tcase ', start + 1)
        cases.append(source[start:end])
    start = source.index('\t\tm = m_gethdr(M_DONTWAIT, MT_DATA);', source.index('\niwx_send_cmd('))
    end = source.index('\t\tcmd = mtod(', start)
    allocation = source[start:end]
    reset = re.search(r'(?m)^#define IWX_CSR_GP_CNTRL_REG_FLAG_SW_RESET[^\n]+', register).group()
    fixture = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define DPRINTF(x) do {} while(0)
#define howmany(x,y) (((x)+(y)-1)/(y))
#define setbit(a,b) ((a)[(b)/8] |= (uint8_t)(1U<<((b)%8)))
#define le32toh(x) (x)
#define IWX_NUM_UCODE_TLV_API 64
#define IWX_NUM_UCODE_TLV_CAPA 64
#define IWX_UCODE_TLV_API_CHANGES_SET 1
#define IWX_UCODE_TLV_ENABLED_CAPABILITIES 2
struct iwx_ucode_api { uint32_t api_index, api_flags; };
struct iwx_ucode_capa { uint32_t api_index, api_capa; };
struct iwx_dma_info { uint32_t size; };
struct iwx_softc { struct iwx_dma_info fw_mon; void *sc_dmat, *sc_dev; uint8_t sc_ucode_api[8], sc_enabled_capa[8]; };
static int dma_calls, dma_failures;
static uint32_t dma_size;
static int iwx_dma_contig_alloc(void *tag,struct iwx_dma_info *info,uint32_t size,int alignment) {
    (void)tag; CHECK(alignment==0); dma_calls++; dma_size=size;
    if (dma_failures) { dma_failures--; return ENOMEM; }
    info->size=size; return 0;
}
/* MONITOR */
static int parse_flags(struct iwx_softc *sc,int kind,void *tlv_data,size_t tlv_len) {
    int err=0;
    switch(kind) {
/* CASES */
    default: return EINVAL;
    }
parse_out:
    return err;
}
#define M_DONTWAIT 0
#define MT_DATA 0
#define M_EXT 1
struct mbuf { int m_flags; };
static struct mbuf storage;
static int fail_header, fail_ext, frees, last_error;
static size_t requested;
static struct mbuf *m_gethdr(int flags,int type) {
    CHECK(flags==0 && type==0); storage.m_flags=0;
    return fail_header ? NULL : &storage;
}
static void extmalloc(struct mbuf *m,size_t length,int flags) {
    CHECK(m==&storage && flags==0); requested=length;
    if (!fail_ext) m->m_flags |= M_EXT;
}
#define MEXTMALLOC(m,n,f) extmalloc(m,n,f)
static void m_freem(struct mbuf *m) { CHECK(m==&storage); frees++; }
static const char *device_xname(void *device) { (void)device; return "fixture"; }
static struct mbuf *command_buffer(struct iwx_softc *sc,size_t totlen) {
    struct mbuf *m;
    int err=0;
    last_error=0;
/* ALLOCATION */
    return m;
out:
    last_error=err; return NULL;
}
/* RESET */
int main(void) {
    struct iwx_softc sc={0};
    struct iwx_ucode_api api={0,UINT32_C(0x80000000)};
    CHECK(parse_flags(&sc,1,&api,sizeof(api))==0 && sc.sc_ucode_api[3]==0x80);
    api.api_index=1;
    CHECK(parse_flags(&sc,2,&api,sizeof(api))==0 && sc.sc_enabled_capa[7]==0x80);
    for (int kind=1;kind<=2;kind++) {
        api.api_index=2; CHECK(parse_flags(&sc,kind,&api,sizeof(api))==EINVAL);
        api.api_index=UINT32_MAX; CHECK(parse_flags(&sc,kind,&api,sizeof(api))==EINVAL);
        CHECK(parse_flags(&sc,kind,&api,sizeof(api)-1)==EINVAL);
    }
    CHECK(IWX_CSR_GP_CNTRL_REG_FLAG_SW_RESET==UINT32_C(0x80000000));
    CHECK(iwx_alloc_fw_monitor_block(&sc,10,11)==EINVAL && dma_calls==0);
    CHECK(iwx_alloc_fw_monitor_block(&sc,26,0)==EINVAL && dma_calls==0);
    CHECK(iwx_alloc_fw_monitor_block(&sc,32,11)==EINVAL && dma_calls==0);
    dma_failures=1;
    CHECK(iwx_alloc_fw_monitor_block(&sc,26,11)==0 && dma_calls==2 && dma_size==(1U<<25));
    CHECK(iwx_alloc_fw_monitor_block(&sc,26,11)==0 && dma_calls==2);
    sc.fw_mon.size=0; dma_calls=0; dma_failures=30;
    CHECK(iwx_alloc_fw_monitor_block(&sc,26,11)==ENOMEM && dma_calls==16 && sc.fw_mon.size==0);
    fail_header=1;
    CHECK(command_buffer(&sc,4096)==NULL && last_error==ENOMEM && frees==0 && requested==0);
    fail_header=0; fail_ext=1;
    CHECK(command_buffer(&sc,4096)==NULL && last_error==ENOMEM && frees==1 && requested==4096);
    fail_ext=0;
    CHECK(command_buffer(&sc,4096)==&storage && last_error==0 && frees==1 && requested==4096);
    printf("iwx firmware bounds: %u checks passed\n",checks);
    return 0;
}
'''
    fixture = fixture.replace('/* MONITOR */', monitor).replace('/* CASES */', '\n'.join(cases))
    fixture = fixture.replace('/* ALLOCATION */', allocation).replace('/* RESET */', reset)
    args.build_dir.mkdir(parents=True, exist_ok=True)
    check = args.build_dir.resolve() / 'bounds.c'
    check.write_text(fixture)
    output = check.with_suffix('')
    command = shlex.split(args.cc) + ['-std=c11', '-Wall', '-Wextra', '-Werror']
    command += shlex.split(args.cflags) + [str(check), '-o', str(output)]
    (args.build_dir / 'compile_commands.json').write_text(json.dumps([
        {'directory': str(args.build_dir.resolve()), 'file': str(check), 'arguments': command}], indent=2))
    subprocess.run(command, check=True)
    subprocess.run(shlex.split(os.environ.get('TEST_RUNNER', '')) + [str(output)], check=True)


if __name__ == '__main__':
    main()
