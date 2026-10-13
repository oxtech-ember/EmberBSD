#!/bin/sh
# Verify peer, protection, replay and bounds before the SA Query state machine.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aicwf-pmf.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/check.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "if_aicwf_sae.h"
static const uint8_t self[6]={2,0,0,0,0,1}, peer[6]={2,0,0,0,0,2};
int main(void)
{
    uint8_t f[1600]={0}, out[28], saved[36];
    uint64_t pn=0;
    unsigned checks=0;
#define CHECK(x) do { assert(x); checks++; } while(0)
    f[0]=0xc0; memcpy(f+4,self,6); memcpy(f+10,peer,6); memcpy(f+16,peer,6); f[24]=7;
    CHECK(aicwf_pmf_unprot_valid(f,26,self,peer));
    f[24]=6; CHECK(aicwf_pmf_unprot_valid(f,26,self,peer));
    f[0]=0xa0; CHECK(aicwf_pmf_unprot_valid(f,26,self,peer));
    f[24]=3; CHECK(!aicwf_pmf_unprot_valid(f,26,self,peer)); f[24]=7;
    for(size_t n=0;n<26;n++) CHECK(!aicwf_pmf_unprot_valid(f,n,self,peer));
    CHECK(!aicwf_pmf_unprot_valid(f,27,self,peer));
    for(unsigned i=4;i<=16;i+=6) {f[i]^=2; CHECK(!aicwf_pmf_unprot_valid(f,26,self,peer)); f[i]^=2;}
    f[1]=0x40; CHECK(!aicwf_pmf_unprot_valid(f,26,self,peer)); f[1]=8;
    CHECK(aicwf_pmf_unprot_valid(f,26,self,peer)); f[1]=4;
    CHECK(!aicwf_pmf_unprot_valid(f,26,self,peer)); f[1]=0;
    f[22]=1; CHECK(!aicwf_pmf_unprot_valid(f,26,self,peer)); f[22]=0;
    f[0]=0xd0; f[24]=8; f[25]=0;
    CHECK(aicwf_pmf_query_valid(f,28,self,peer,peer));
    f[25]=1; CHECK(aicwf_pmf_query_valid(f,28,self,peer,peer));
    f[25]=2; CHECK(!aicwf_pmf_query_valid(f,28,self,peer,peer)); f[25]=1;
    f[24]=10; CHECK(!aicwf_pmf_query_valid(f,28,self,peer,peer)); f[24]=8;
    f[1]=0x40; CHECK(!aicwf_pmf_query_valid(f,28,self,peer,peer));
    /* CCMP header + minimal SA Query, no authentication bytes fabricated. */
    f[24]=9;f[25]=0;f[26]=0;f[27]=0x20;f[28]=f[29]=f[30]=f[31]=0;
    f[32]=8;f[33]=1;f[34]=0x12;f[35]=0x34;
    const uint32_t status=0x0200600c | (5u<<15);
    CHECK(aicwf_pmf_query_rx(f,36,self,peer,status,5,8,out,&pn));
    CHECK(pn==9 && out[1]==0 && out[24]==8 && out[27]==0x34);
    CHECK(!aicwf_pmf_query_rx(f,36,self,peer,status,5,9,out,&pn));
    CHECK(!aicwf_pmf_query_rx(f,36,self,peer,status,5,10,out,&pn));
    CHECK(!aicwf_pmf_query_rx(f,36,self,peer,status,4,0,out,&pn));
    for(size_t n=0;n<36;n++) CHECK(!aicwf_pmf_query_rx(f,n,self,peer,status,5,0,out,&pn));
    CHECK(!aicwf_pmf_query_rx(f,1537,self,peer,status,5,0,out,&pn));
    for(unsigned b=5;b<=9;b++) CHECK(!aicwf_pmf_query_rx(f,36,self,peer,status|(1u<<b),5,0,out,&pn));
    const unsigned must[]={13,14,25};
    for(unsigned i=0;i<3;i++) CHECK(!aicwf_pmf_query_rx(f,36,self,peer,status&~(1u<<must[i]),5,0,out,&pn));
    CHECK(!aicwf_pmf_query_rx(f,36,self,peer,status&~0x1c,5,0,out,&pn));
    memcpy(saved,f,36);
    const unsigned offsets[]={0,1,4,10,16,22,26,27,32,33};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        memcpy(f,saved,36); f[offsets[i]]^=2;
        CHECK(!aicwf_pmf_query_rx(f,36,self,peer,status,5,0,out,&pn));
    }
    memcpy(f,saved,36); f[24]=f[25]=f[28]=f[29]=f[30]=f[31]=0xff;
    CHECK(aicwf_pmf_query_rx(f,36,self,peer,status,5,0,out,&pn));
    CHECK(pn==UINT64_C(0xffffffffffff));
    printf("PASS: %u PMF peer/protection/replay/bounds checks\n", checks);
    return 0;
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror -I"$src/sys/dev/sdmmc" "$work/check.c" -o "$work/check"
"$work/check"
