/* SPDX-License-Identifier: BSD-2-Clause */
/* Explicit kernel registration/service model for the owning attach function. */
#include <stdarg.h>
#include <time.h>
#include <limits.h>
#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
#define hz 1000
static void getnanouptime(struct timespec *ts) { ts->tv_sec=1;ts->tv_nsec=0; }
#define KM_SLEEP 0
#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
#define PRI_NONE 0
#define SOFTINT_NET 0
/* Distinct service-model bits; routing owns RUNNING=1/OACTIVE=2. */
#define IFF_UP 32
#define IFF_BROADCAST 4
#define IFF_SIMPLEX 8
#define IFF_MULTICAST 16
#define DLT_IEEE802_11 105
#define IEEE80211_T_OFDM 4
#define IEEE80211_ADDR_LEN 6
#define setbit(a,n) ((a)[(n)/8]|=1U<<((n)%8))
#define isset(a,n) enable_isset((a),(n))
static int enable_fail_workqueue,enable_fail_softint;
static unsigned enable_allocations,enable_releases,enable_registrations,enable_state_errors;
static kmutex_t enable_queue_lock={.initialized=1};
static int enable_parent_state(struct ieee80211com *,enum ieee80211_state,int);
static void rtw88_net_state_work(struct work *,void *);
static int rtw88_net_init(struct ifnet *);
static void rtw88_net_stop(struct ifnet *p,int disable) { (void)p;(void)disable; }
static int rtw88_net_ioctl(struct ifnet *p,u_long cmd,void *data)
{ (void)p;(void)cmd;(void)data;return 0; }
static void rtw88_net_watchdog(struct ifnet *p) { (void)p; }
static int rtw88_net_media_change(struct ifnet *p) { (void)p;return 0; }
static void rtw88_net_scan(void *p) { (void)p; }
static void rtw88_net_rx_frame(void *cookie,const uint8_t *frame,size_t len,
    const struct rtw88_rx_info *info)
{ (void)cookie;(void)frame;(void)len;(void)info; }
void *kmem_zalloc(size_t n,int flags)
{ (void)flags;void *p=calloc(1,n);assert(p);enable_allocations++;return p; }
void kmem_free(void *p,size_t n)
{ (void)n;enable_releases++;free(p); }
static void callout_init(int *c,int flags) { (void)c;(void)flags; }
static void callout_setfunc(int *c,void (*fn)(void *),void *cookie)
{ (void)c;(void)fn;(void)cookie; }
static void callout_destroy(int *c) { (void)c; }
static void callout_halt(int *c,void *lock) { (void)c;assert(lock==NULL); }
static void callout_schedule(int *c,unsigned ticks) { (void)c;(void)ticks; }
static unsigned mstohz(unsigned ms) { return ms; }
/* Process sleep/clock boundary: the real kernel owns Giant release semantics. */
static int (*enable_pause_hook)(unsigned);
static int (*enable_drain_pause_hook)(unsigned);
static int enable_drain_complete(unsigned);
static inline int kpause(const char *name,bool intr,int ticks,kmutex_t *lock)
{
    (void)name;(void)intr;assert(lock==NULL);assert(ticks>0);
    route_ticks+=(unsigned)ticks;
    if(strcmp(name,"rtwdrn")==0)
        return enable_drain_pause_hook?enable_drain_pause_hook((unsigned)ticks):
            enable_drain_complete((unsigned)ticks);
    return enable_pause_hook?enable_pause_hook((unsigned)ticks):EWOULDBLOCK;
}
static int workqueue_create(struct workqueue **out,const char *name,
    void (*fn)(struct work *,void *),void *cookie,int priority,int ipl,int flags)
{ (void)name;(void)fn;(void)cookie;(void)priority;(void)ipl;(void)flags;
  if(enable_fail_workqueue)return ENOMEM;
  *out=calloc(1,sizeof(**out));assert(*out);return 0; }
static void workqueue_destroy(struct workqueue *q) { free(q); }
static void *softint_establish(int flags,void (*fn)(void *),void *cookie)
{ (void)flags;(void)fn;return enable_fail_softint?NULL:cookie; }
const char *device_xname(device_t d) { (void)d;return "rtwce0"; }
void aprint_error_dev(device_t d,const char *fmt,...)
{ (void)d;enable_state_errors++;va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap); }
static size_t enable_strlcpy(char *dst,const char *src,size_t n)
{ size_t len=strlen(src);if(n) { size_t copy=len<n-1?len:n-1;memcpy(dst,src,copy);dst[copy]=0; }return len; }
#define strlcpy enable_strlcpy
#define IFQ_SET_READY(q) ((void)(q))
static void if_initialize(struct ifnet *p) { p->if_snd.ifq_lock=&enable_queue_lock; }
static void if_register(struct ifnet *p) { (void)p;enable_registrations++; }
static void *if_percpuq_create(struct ifnet *p) { return p; }
static void if_printf(struct ifnet *p,const char *fmt,...)
{ (void)p;(void)fmt; }
static void ether_ifattach(struct ifnet *p,const uint8_t *mac)
{ (void)p;(void)mac; }
static void bpf_attach2(struct ifnet *p,int type,size_t size,void **bp)
{ (void)p;(void)type;(void)size;(void)bp; }
#ifdef __NetBSD__
static void ieee80211_init(void) {}
#endif
/* Key-table initialization service only. Full crypto attach/callback and
 * key/PN lifetime behavior is exercised by the RXBA/crypto contract. */
static void ieee80211_crypto_attach(struct ieee80211com *ic)
{
    ic->ic_def_txkey=IEEE80211_KEYIX_NONE;
    for(unsigned i=0;i<4;i++)
        ic->ic_nw_keys[i]=(struct ieee80211_key){.wk_cipher=&ieee80211_cipher_none};
}
#define ieee80211_add_vap(ic) ((void)(ic))
#define ieee80211_sysctl_attach(ic) ((void)(ic))
#define ieee80211_init_link_state(ic) ((void)(ic))
static int ieee80211_default_reset(struct ifnet *p) { (void)p;return ENETRESET; }
static void ieee80211_node_attach(struct ieee80211com *ic)
{ ic->ic_node_cleanup=cleanup_node; }
static void ieee80211_node_lateattach(struct ieee80211com *ic)
{
    assert(ic->ic_bss==NULL);
    ic->ic_bss=calloc(1,sizeof(*ic->ic_bss));assert(ic->ic_bss);
    ic->ic_bss->ni_ic=ic;
}
static void ieee80211_proto_attach(struct ieee80211com *ic)
{
    ic->ic_rxba_ops=NULL;ic->ic_rxba_serial=0;
    ic->ic_newstate=enable_parent_state;
    ic->ic_fixed_rate=IEEE80211_FIXED_RATE_NONE;
    ic->ic_fragthreshold=4096;
}
static void ieee80211_media_status(struct ifnet *p,struct ifmediareq *media)
{ (void)p;media->ifm_active=IFM_MAKEWORD(IFM_IEEE80211,IFM_AUTO,0,0); }
static void ifmedia_init_with_lock(struct ifmedia *m,int mask,
    ifm_change_cb_t change,ifm_stat_cb_t status,ieee80211_media_lock_t *lock)
{ (void)mask;(void)change;(void)status;(void)lock;memset(m,0,sizeof(*m)); }
static void ifmedia_add(struct ifmedia *m,int media,int data,void *aux)
{ (void)data;(void)aux;assert(m->count<128);m->entries[m->count++].ifm_media=media; }
static void ifmedia_set(struct ifmedia *m,int media)
{
    for(unsigned i=0;i<m->count;i++)
        if(m->entries[i].ifm_media==media) { m->ifm_cur=&m->entries[i];return; }
    assert(0);
}
#define IF_Mbps(rate) ((uint64_t)(rate)*1000000)
#ifndef __UNCONST
#define __UNCONST(p) ((void *)(uintptr_t)(const void *)(p))
#endif
const struct ieee80211_channel ieee80211_channel_anyc={0,0};
#undef IEEE80211_IS_CHAN_2GHZ
#undef IEEE80211_IS_CHAN_FHSS
#define IEEE80211_IS_CHAN_2GHZ(c) _IEEE80211_IS_CHAN(c,2GHZ)
#define IEEE80211_IS_CHAN_FHSS(c) _IEEE80211_IS_CHAN(c,FHSS)
#include "enable-shared.inc"
