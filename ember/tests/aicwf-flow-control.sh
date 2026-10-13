#!/bin/sh
# Run the real D80 SDIO wait with synthetic free-buffer register values.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aicwf-flow.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
awk '/^#define[[:space:]]+AICWF_(FLOW_CTRL|TX_BUFFER_SIZE)/ {print}
/^aicwf_tx_wait\(/ {copy=1; print "static int"}
copy {print} copy && /^}/ {exit}' \
    "$src/sys/dev/sdmmc/if_aicwf.c" > "$work/wait.h"
cat > "$work/check.c" <<'C'
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
typedef unsigned int u_int;
#ifndef __BITS
#define __BITS(h,l) (((1U << ((h)-(l)+1))-1) << (l))
#endif
#define IEEE80211_DPRINTF(...) ((void)0)
struct aicwf_softc { int sc_bus_lock, sc_sf; };
static unsigned raw, reads, pauses;
static void mutex_enter(int *p) { assert(!*p); *p=1; }
static void mutex_exit(int *p) { assert(*p); *p=0; }
static unsigned sdmmc_io_read_1(int sf, int reg)
{ (void)sf; assert(reg==3); reads++; return raw; }
static void sdmmc_pause(int us, void *p) { (void)us; (void)p; pauses++; }
#include "wait.h"
static void check(unsigned free_buffers, unsigned reserve, size_t len, int result)
{
    struct aicwf_softc sc={0}; raw=free_buffers; reads=pauses=0;
    assert(aicwf_tx_wait(&sc,len,reserve)==result);
    assert(reads==(result ? 50 : 1));
    assert(pauses==(result ? 50 : 0));
    assert(!sc.sc_bus_lock);
}
int main(void)
{
    check(128,2,1536,0); check(129,2,1536,0); check(130,2,1536,0);
    check(255,2,1536,0); check(3,2,1536,0);
    check(2,2,1,EBUSY); check(0,0,1,EBUSY);
    check(1,0,1536,0); check(1,0,1537,EBUSY);
    check(3,2,1537,EBUSY);
    puts("PASS: 10 D80 flow-control and bounded-wait cases");
    return 0;
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror "$work/check.c" -o "$work/check"
"$work/check"
