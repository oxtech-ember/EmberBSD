/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual ifattach/media_init/media_change/setmode, modeled kernel boundaries. */
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#define __UNCONST(a) ((void *)(uintptr_t)(const void *)(a))
#include "media-types.inc"
#define setbit(a,i) ((a)[(i)/8] |= 1U << ((i)%8))
#define isset(a,i) ((a)[(i)/8] & (1U << ((i)%8)))
#define isclr(a,i) (!isset(a,i))
#define IASSERT(c,m) assert(c)
#define IEEE80211_DPRINTF(...) ((void)0)
#define IEEE80211_BEACON_LOCK_INIT(...) ((void)0)
#define LIST_INSERT_HEAD(...) ((void)0)
#define IF_Mbps(a) ((uint64_t)(a)*1000000)
#define DLT_IEEE802_11 105
struct ieee80211_frame_addr4 { uint8_t bytes[30]; };
struct ifnet { uint64_t if_baudrate; };
struct ifmedia_entry { int ifm_media; };
struct ifmediareq { int ifm_active; };
struct ifmedia {
 struct ifmedia_entry entries[128], *ifm_cur;
 unsigned count;
};
typedef int (*ifm_change_cb_t)(struct ifnet *);
typedef void (*ifm_stat_cb_t)(struct ifnet *,struct ifmediareq *);
typedef int ieee80211_media_lock_t;
struct ieee80211_node { struct ieee80211_rateset ni_rates; };
struct ieee80211com {
 struct ifnet *ic_ifp;
 struct ieee80211_node *ic_bss;
 uint8_t ic_myaddr[6]; void *ic_rawbpf;
 uint8_t ic_chan_avail[IEEE80211_CHAN_BYTES],ic_chan_active[IEEE80211_CHAN_BYTES];
 struct ieee80211_channel ic_channels[256], *ic_curchan,*ic_des_chan,*ic_ibss_chan;
 enum ieee80211_phymode ic_curmode;
 enum ieee80211_opmode ic_opmode;
 enum ieee80211_phytype ic_phytype;
 unsigned ic_modecaps,ic_caps,ic_flags;
 unsigned ic_bintval,ic_bmisstimeout,ic_dtim_period,ic_lintval,ic_txpowlimit;
 int (*ic_reset)(struct ifnet *);
 int ic_fixed_rate;
 struct ieee80211_rateset ic_sup_rates[IEEE80211_MODE_MAX];
 struct ifmedia ic_media;
};
const struct ieee80211_channel ieee80211_channel_anyc={0,0};
static struct ieee80211com *instance;
static void if_printf(struct ifnet *ifp,const char *fmt,...)
{ (void)ifp;(void)fmt; }
static void ether_ifattach(struct ifnet *p,const uint8_t *mac)
{ (void)p;(void)mac; }
static void bpf_attach2(struct ifnet *p,int type,size_t size,void **bp)
{ (void)p;(void)type;(void)size;(void)bp; }
#define ieee80211_crypto_attach(ic) ((void)(ic))
#define ieee80211_node_attach(ic) ((void)(ic))
#define ieee80211_proto_attach(ic) ((void)(ic))
#define ieee80211_add_vap(ic) ((void)(ic))
#define ieee80211_sysctl_attach(ic) ((void)(ic))
#define ieee80211_init_link_state(ic) ((void)(ic))
#define ieee80211_node_lateattach(ic) ((void)(ic))
#define ieee80211_reset_erp(ic) ((void)(ic))
#define ieee80211_wme_initparams(ic) ((void)(ic))
static int ieee80211_default_reset(struct ifnet *p)
{ (void)p; return ENETRESET; }
static struct ieee80211com *ieee80211_find_instance(struct ifnet *p)
{ return instance && instance->ic_ifp==p ? instance : NULL; }
static void ifmedia_init_with_lock(struct ifmedia *m,int mask,
    ifm_change_cb_t change,ifm_stat_cb_t stat,ieee80211_media_lock_t *lock)
{ (void)mask;(void)change;(void)stat;(void)lock; memset(m,0,sizeof(*m)); }
static void ifmedia_add(struct ifmedia *m,int word,int data,void *aux)
{ (void)data;(void)aux;assert(m->count<128);m->entries[m->count++].ifm_media=word; }
static void ifmedia_set(struct ifmedia *m,int word)
{ for(unsigned i=0;i<m->count;i++) if(m->entries[i].ifm_media==word){m->ifm_cur=&m->entries[i];return;} assert(0); }
static void ieee80211_media_status(struct ifnet *p,struct ifmediareq *m)
{ (void)p;m->ifm_active=IFM_MAKEWORD(IFM_IEEE80211,IFM_AUTO,0,0); }
#include "media-native.inc"
static unsigned checks,failures;
#define CHECK(c) do{checks++;if(!(c)){failures++;fprintf(stderr,"line%u: %s\n",(unsigned)__LINE__,#c);}}while(0)
static void driver_channels(struct ieee80211com *ic)
{
#include "media-driver.inc"
}
static int has_media(struct ieee80211com *ic,int rate,int mode)
{
 int word=IFM_MAKEWORD(IFM_IEEE80211,rate,mode,0);
 for(unsigned i=0;i<ic->ic_media.count;i++)
  if(ic->ic_media.entries[i].ifm_media==word)return 1;
 return 0;
}
static int select_media(struct ieee80211com *ic,int rate,int mode)
{
 int word=IFM_MAKEWORD(IFM_IEEE80211,rate,mode,0);
 /* The service rejects absent media just as an ioctl lookup must. */
 if(!has_media(ic,rate,mode))return EINVAL;
 ifmedia_set(&ic->ic_media,word);
 return ieee80211_media_change(ic->ic_ifp);
}
int main(void)
{
 struct ieee80211com ic={0}; struct ifnet ifp={0};
 struct ieee80211_node bss={0};
 ic.ic_bss=&bss;
 ic.ic_ifp=&ifp;ic.ic_opmode=IEEE80211_M_STA;ic.ic_phytype=IEEE80211_T_OFDM;
 ic.ic_fixed_rate=IEEE80211_FIXED_RATE_NONE;instance=&ic;
 driver_channels(&ic);
 ieee80211_ifattach(&ic);
 CHECK((ic.ic_modecaps&(1U<<IEEE80211_MODE_11G))!=0);
 CHECK((ic.ic_modecaps&(1U<<IEEE80211_MODE_11A))!=0);
 CHECK((ic.ic_modecaps&(1U<<IEEE80211_MODE_11B))!=0);
 ieee80211_media_init(&ic,ieee80211_media_change,ieee80211_media_status);
 CHECK(has_media(&ic,IFM_IEEE80211_OFDM6,IFM_IEEE80211_11G));
 CHECK(has_media(&ic,IFM_IEEE80211_OFDM6,IFM_IEEE80211_11A));
 CHECK(has_media(&ic,IFM_IEEE80211_OFDM54,IFM_IEEE80211_11G));
 CHECK(select_media(&ic,IFM_IEEE80211_OFDM6,IFM_IEEE80211_11G)==ENETRESET);
 CHECK(ic.ic_curmode==IEEE80211_MODE_11G);
 CHECK(ic.ic_fixed_rate==4);
 for(unsigned ch=1;ch<=13;ch++)CHECK(isset(ic.ic_chan_active,ch));
 CHECK(!isset(ic.ic_chan_active,36));
 CHECK(select_media(&ic,IFM_IEEE80211_OFDM6,IFM_IEEE80211_11G)==0);
 CHECK(select_media(&ic,IFM_IEEE80211_OFDM24,IFM_IEEE80211_11G)==ENETRESET);
 CHECK(ic.ic_curmode==IEEE80211_MODE_11G && ic.ic_fixed_rate==8);
 CHECK(select_media(&ic,IFM_IEEE80211_OFDM54,IFM_IEEE80211_11G)==ENETRESET);
 CHECK(ic.ic_curmode==IEEE80211_MODE_11G && ic.ic_fixed_rate==11);
 CHECK(select_media(&ic,IFM_IEEE80211_DS11,IFM_IEEE80211_11A)==EINVAL);
 CHECK(select_media(&ic,IFM_IEEE80211_OFDM6,IFM_IEEE80211_11B)==EINVAL);
 CHECK(ic.ic_curmode==IEEE80211_MODE_11G && ic.ic_fixed_rate==11);
 CHECK(select_media(&ic,IFM_IEEE80211_OFDM6,IFM_IEEE80211_11A)==ENETRESET);
 CHECK(ic.ic_curmode==IEEE80211_MODE_11A && ic.ic_fixed_rate==0);
 CHECK(isset(ic.ic_chan_active,36)&&!isset(ic.ic_chan_active,8));
 CHECK(select_media(&ic,IFM_IEEE80211_DS11,IFM_IEEE80211_11B)==ENETRESET);
 CHECK(ic.ic_curmode==IEEE80211_MODE_11B&&ic.ic_fixed_rate==3);
 CHECK(isset(ic.ic_chan_active,8)&&!isset(ic.ic_chan_active,36));
 CHECK(select_media(&ic,IFM_AUTO,0)==ENETRESET);
 CHECK(ic.ic_curmode==IEEE80211_MODE_AUTO&&ic.ic_fixed_rate==IEEE80211_FIXED_RATE_NONE);
 CHECK(isset(ic.ic_chan_active,8)&&isset(ic.ic_chan_active,36));
 printf("rtw88 native channel/media contract: %u checks, %u failures\n",checks,failures);
 return failures!=0;
}
