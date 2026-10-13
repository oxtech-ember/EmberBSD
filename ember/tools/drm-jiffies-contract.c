/* Origin: EmberBSD actual-source DRM nanosecond conversion contract, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
static int hz=100;
static unsigned checks;
#define CHECK(c) do { checks++; if(!(c)) { \
    fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
#include "jiffies-body.h"
typedef int64_t ktime_t;
typedef uint64_t u64;
#define MAX_SCHEDULE_TIMEOUT LONG_MAX
static ktime_t now=3000000000;
static ktime_t ktime_get(void) { return now; }
static ktime_t ns_to_ktime(int64_t t) { return t; }
static int ktime_after(ktime_t a,ktime_t b) { return a>b; }
static ktime_t ktime_sub(ktime_t a,ktime_t b) { return a-b; }
static int64_t ktime_to_ns(ktime_t t) { return t; }
#include "timeout-body.h"
int main(void)
{
    const int rates[]={100,128,250,1000,1024};
    const uint64_t ns[]={0,1,9999999,10000000,100000000,999999999,
        1000000000,20000000001,UINT64_MAX};
    CHECK(nsecs_to_jiffies64(100000000)==10); /* Physical CM5 failure. */
    for(unsigned r=0;r<sizeof(rates)/sizeof(rates[0]);r++) {
        hz=rates[r];
        for(unsigned i=0;i<sizeof(ns)/sizeof(ns[0]);i++)
            CHECK(nsecs_to_jiffies64(ns[i])==(uint64_t)(((__uint128_t)ns[i]*hz)/1000000000));
        uint64_t v=UINT64_C(0x123456789abcdef0);
        for(unsigned i=0;i<4096;i++) {
            v=v*UINT64_C(6364136223846793005)+1;
            CHECK(nsecs_to_jiffies64(v)==(uint64_t)(((__uint128_t)v*hz)/1000000000));
        }
        CHECK(drm_timeout_abs_to_jiffies(0)==0);
        CHECK(drm_timeout_abs_to_jiffies(now)==0);
        CHECK(drm_timeout_abs_to_jiffies(now-1)==0);
        CHECK(drm_timeout_abs_to_jiffies(now+1)==1);
        CHECK(drm_timeout_abs_to_jiffies(now+100000000)==hz/10+1);
    }
    printf("PASS: %u nanosecond/tick and absolute DRM deadline checks\n",checks);
    return 0;
}
