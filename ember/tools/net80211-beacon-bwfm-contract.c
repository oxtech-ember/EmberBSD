/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD native bwfm scan contract and portable host services. */
/* Complete production bwfm scan adapter. Firmware channel decoding is modeled. */
#include <sys/types.h>
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <net80211/ieee80211_beacon_ies.h>
#define ETHER_ADDR_LEN 6
#define IEEE80211_ADDR_COPY(a,b) memcpy(a,b,6)
#ifndef __arraycount
#define __arraycount(a) (sizeof(a)/sizeof((a)[0]))
#endif
#ifndef __UNCONST
#define __UNCONST(p) ((void *)(uintptr_t)(const void *)(p))
#endif
#define IEEE80211_CHAN_MAX 255
#define IEEE80211_T_FH 1
#define IEEE80211_F_SCAN 1
#define IEEE80211_FC0_SUBTYPE_BEACON 0x80
#define IEEE80211_FH_CHAN(a,b) (((a)-1)*80+(b))
#define WPA_OUI_TYPE 1
#define WPA_OUI 0xf25000
#if defined(__NetBSD__)
#include <sys/endian.h>
#else
#ifndef le16toh
#define le16toh(x) le16dec(&(uint16_t){(x)})
#endif
#ifndef le32toh
#define le32toh(x) le32dec(&(uint32_t){(x)})
#endif
static uint16_t le16dec(const void *p)
{ const uint8_t *b=p; return b[0]|(uint16_t)b[1]<<8; }
static uint32_t le32dec(const void *p)
{ const uint8_t *b=p; return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24; }
#endif
#include "bwfm-layouts.h"
struct ieee80211com {
    int ic_phytype, ic_flags;
    struct { unsigned ic_freq; } ic_channels[256];
    struct { unsigned is_rx_elem_toosmall,is_rx_elem_toobig,is_rx_badchan; } ic_stats;
};
struct bwfm_softc { struct ieee80211com sc_ic; int sc_io_type; };
static unsigned added;
static struct ieee80211_scanparams observed;
static unsigned bwfm_scan_channel(unsigned ctl, unsigned spec, unsigned type)
{ (void)spec; (void)type; return ctl; }
static unsigned bwfm_scan_rssi(int dbm) { return dbm+100; }
static void ieee80211_add_scan(struct ieee80211com *ic,
    const struct ieee80211_scanparams *sp, const struct ieee80211_frame *wh,
    int subtype, int rssi, int stamp)
{
    (void)ic;(void)wh;(void)subtype;(void)rssi;(void)stamp;
    observed=*sp; added++;
}
#include "bwfm-scan.inc"
static void check(const uint8_t *suffix,size_t n,bool accept, bool security)
{
    struct bwfm_softc sc={0};
    size_t len=sizeof(struct bwfm_bss_info)+12+n;
    struct bwfm_bss_info *b=calloc(1,len);
    assert(b);
    sc.sc_ic.ic_flags=IEEE80211_F_SCAN;
    sc.sc_ic.ic_channels[6].ic_freq=2437;
    b->ctl_ch=6;b->ie_offset=sizeof(*b);b->ie_length=12+n;
    b->ssid_len=1;b->ssid[0]='x';b->nrates=1;b->rates[0]=2;
    uint8_t *ie=(uint8_t *)b+b->ie_offset;
    ie[0]=199;ie[1]=10; /* Preserve existing firmware minimum window size. */
    memcpy(ie+12,suffix,n);
    added=0;bwfm_scan_node(&sc,b,len);
    assert(added==(unsigned)accept);
    if(accept) {
        assert(observed.sp_chan==6);
        assert((observed.sp_wpa!=NULL)==security);
        if(suffix[0]==5) assert(observed.sp_timoff==12 && observed.sp_tim[1]>=4);
    }
    free(b);
}
int main(void)
{
    for(unsigned n=0;n<4;n++) { uint8_t tim[6]={5,0};tim[1]=n;check(tim,n+2,false,false); }
    const uint8_t tim[]={5,4,7,9,0,0}; check(tim,sizeof(tim),true,false);
    const uint8_t ds[]={3,0,6,0};check(ds,sizeof(ds),false,false);
    const uint8_t tail[]={127};check(tail,sizeof(tail),false,false);
    const uint8_t wpa[]={221,4,0,0x50,0xf2,1};check(wpa,sizeof(wpa),true,true);
    puts("bwfm scan adapter: short TIM/DS, tail, exact-end WPA and TIM offset pass");
    return 0;
}
