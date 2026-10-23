/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: native scan quality consumer contract, AI-assisted. */
/* Execute actual add_scan, node_compare and scan copyout. No dBm ABI extension. */
#include "signal-scan-model.inc"
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#include "native-layouts.h"
#define IEEE80211_DPRINTF(...) ((void)0)
#define IEEE80211_FC0_SUBTYPE_PROBE_RESP 0x50
#define IEEE80211_R_DOSORT 1
#define M_DEVBUF 0
static unsigned checks,failures;
static struct ieee80211_node *selected_node;
static void check(bool b,const char *s) { checks++;if(!b){failures++;fprintf(stderr,"%s\n",s);} }
static struct ieee80211_node *ieee80211_find_node(struct ieee80211_node_table *nt,const uint8_t *a) { (void)nt;(void)a;return selected_node; }
static void ieee80211_setup_node(struct ieee80211_node_table *nt,struct ieee80211_node *ni,const uint8_t *a) { (void)nt;(void)ni;(void)a; }
static void ieee80211_set_chan(struct ieee80211com *ic,struct ieee80211_node *ni,struct ieee80211_channel *ch) { (void)ic;ni->ni_chan=ch; }
static void ieee80211_free_node(struct ieee80211_node *ni) { (void)ni; }
static int ieee80211_setup_rates(struct ieee80211_node *ni,const uint8_t *r,const uint8_t *x,int flags) { (void)x;assert(flags==IEEE80211_R_DOSORT);ni->ni_rates.rs_nrates=r[1];memcpy(ni->ni_rates.rs_rates,r+2,r[1]);return 0; }
#define malloc(n,t,f) malloc(n)
#define free(p,t) free(p)
#define M_NOWAIT 0
#include "signal-add-scan.inc"
#undef malloc
#undef free
#include "get_scan_result.h"
#include "signal-node-compare.inc"
int rtw88_signal_scan_check(unsigned weak,unsigned strong) {
    struct ieee80211com ic={.ic_node_getrssi=node_getrssi};
    struct ieee80211_node a={.ni_ic=&ic},b={.ni_ic=&ic};
    struct ieee80211_scanparams sp={0};struct ieee80211_frame wh={0};
    uint8_t ssid[]={0,1,'x'},rates[]={1,1,12},stamp[8]={0};
    struct ieee80211req_scan_result sa={0},sb={0};
    ic.ic_bss=&a;ic.ic_curchan=&ic.ic_channels[1];
    sp.sp_ssid=ssid;sp.sp_rates=rates;sp.sp_tstamp=stamp;sp.sp_chan=1;
    selected_node=&a;ieee80211_add_scan(&ic,&sp,&wh,0x50,(int)weak,1);
    selected_node=&b;ieee80211_add_scan(&ic,&sp,&wh,0x50,(int)strong,2);
    check(a.ni_rssi==50 && b.ni_rssi==80,"actual scan nodes retain weak/strong unsigned quality");
    check(ieee80211_node_compare(&ic,&b,&a)>0 && ieee80211_node_compare(&ic,&a,&b)<0,
        "actual node selection prefers the stronger candidate");
    get_scan_result(&sa,&a);get_scan_result(&sb,&b);
    check(sa.isr_rssi==50 && sb.isr_rssi==80,"actual scan copyout retains relative quality");
    check(!(sa.isr_pad[1]&IEEE80211_SCAN_RSSI_DBM) && !(sb.isr_pad[1]&IEEE80211_SCAN_RSSI_DBM),
        "relative quality never asserts absolute dBm validity");
    printf("rtw88 signal scan: %u checks, %u failures\n",checks,failures);
    return failures!=0;
}
