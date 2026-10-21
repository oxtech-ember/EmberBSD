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
/* The modeled bitmap service accepts only channel indices 0..255.
 * The owning attach initializes IBSS to NULL or an array channel. ANYC or
 * foreign IBSS pointers are outside this contract, not repaired by this test.
 */
#ifdef NDEBUG
#error "The media channel-domain assertions must remain enabled"
#endif
static int
media_isset(const uint8_t *bits, unsigned index)
{
 assert(index < IEEE80211_CHAN_BYTES * 8U);
 return bits[index / 8] & (1U << (index % 8));
}
#define isset(a,i) media_isset((a),(i))
#define isclr(a,i) (!isset(a,i))
#define IASSERT(c,m) assert(c)
#define IEEE80211_DPRINTF(...) ((void)0)
#define IEEE80211_BEACON_LOCK_INIT(...) ((void)0)
#define LIST_INSERT_HEAD(...) ((void)0)
#define IF_Mbps(a) ((uint64_t)(a)*1000000)
#define DLT_IEEE802_11 105
struct ieee80211_frame_addr4 { uint8_t bytes[30]; };
struct ifnet { uint64_t if_baudrate; void *if_softc; unsigned if_flags; };
struct ifmedia_entry { int ifm_media; };
struct ifmediareq { int ifm_active; };
struct ifmedia {
 struct ifmedia_entry entries[128], *ifm_cur;
 unsigned count; int ifm_media;
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
 unsigned ic_nonerpsta, ic_longslotsta;
 void (*ic_updateslot)(struct ifnet *);
 enum ieee80211_state ic_state;
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
void ieee80211_reset_erp(struct ieee80211com *);
void ieee80211_set_shortslottime(struct ieee80211com *, int);
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
struct model_lock { int held; };
struct rtw88_net {
 struct ieee80211com ic;
 struct model_lock *lock;
 unsigned configured_media;
};
#define IFF_UP 0x0001
#define IFF_RUNNING 0x0040
#define IS_UP(ic) (((ic)->ic_ifp->if_flags & (IFF_UP | IFF_RUNNING)) == \
    (IFF_UP | IFF_RUNNING))
static unsigned init_calls, scan_requests, slot_notifications;
static void count_slot_notification(struct ifnet *ifp)
{ (void)ifp; slot_notifications++; }
static int init_error;
static void mutex_enter(struct model_lock *lock)
{ assert(!lock->held); lock->held=1; }
static void mutex_exit(struct model_lock *lock)
{ assert(lock->held); lock->held=0; }
static int rtw88_net_init(struct ifnet *ifp)
{ struct rtw88_net *net=ifp->if_softc; assert(!net->lock->held); init_calls++; return init_error; }
static int ieee80211_new_state(struct ieee80211com *ic, enum ieee80211_state state, int arg)
{ (void)arg; scan_requests++; ic->ic_state=state; return 0; }
#include "media-wrapper.inc"
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
static void
check_driver_media_refresh(void)
{
 struct rtw88_net net;
 struct ifnet ifp;
 struct ieee80211_node bss;
 struct model_lock lock;
 unsigned i, oldword;
 static const struct {
  enum ieee80211_phymode runtime;
  int rate, media_mode;
  unsigned forbidden;
 } cases[] = {
  { IEEE80211_MODE_11G, IFM_AUTO, IFM_IEEE80211_11G, 36 },
  { IEEE80211_MODE_11G, IFM_IEEE80211_OFDM6, IFM_IEEE80211_11G, 36 },
  { IEEE80211_MODE_11A, IFM_IEEE80211_OFDM6, IFM_AUTO, 8 },
  { IEEE80211_MODE_11G, IFM_IEEE80211_OFDM6, IFM_IEEE80211_11A, 8 }
 };

 for (i=0;i<__arraycount(cases);i++) {
  memset(&net,0,sizeof(net)); memset(&ifp,0,sizeof(ifp));
  memset(&bss,0,sizeof(bss)); memset(&lock,0,sizeof(lock));
  net.lock=&lock; net.ic.ic_ifp=&ifp; net.ic.ic_bss=&bss;
  net.ic.ic_opmode=IEEE80211_M_STA; net.ic.ic_phytype=IEEE80211_T_OFDM;
  net.ic.ic_fixed_rate=IEEE80211_FIXED_RATE_NONE;
  ifp.if_softc=&net; ifp.if_flags=IFF_UP|IFF_RUNNING; instance=&net.ic;
  driver_channels(&net.ic); ieee80211_ifattach(&net.ic);
  ieee80211_media_init(&net.ic,rtw88_net_media_change,ieee80211_media_status);
  net.configured_media=net.ic.ic_media.ifm_cur->ifm_media;
  /* Model sta_join's runtime-mode result; AUTO active channels are unchanged. */
  net.ic.ic_curmode=cases[i].runtime;
  net.ic.ic_curchan=&net.ic.ic_channels[cases[i].runtime==IEEE80211_MODE_11A ? 36 : 8];
  net.ic.ic_updateslot=count_slot_notification;
  CHECK(isset(net.ic.ic_chan_active,8)&&isset(net.ic.ic_chan_active,36));
  ifmedia_set(&net.ic.ic_media,IFM_MAKEWORD(IFM_IEEE80211,cases[i].rate,cases[i].media_mode,0));
  net.ic.ic_media.ifm_media=net.ic.ic_media.ifm_cur->ifm_media;
  init_calls=scan_requests=slot_notifications=0; init_error=0;
  CHECK(rtw88_net_media_change(&ifp)==0);
  CHECK(!isset(net.ic.ic_chan_active,cases[i].forbidden));
  CHECK(init_calls==1 && slot_notifications==1);
  CHECK(native_scan_req(&net.ic)==0);
  CHECK(!isset(net.ic.ic_chan_active,cases[i].forbidden));
  CHECK(scan_requests==1 && net.ic.ic_state==IEEE80211_S_SCAN);
  /* Repeated requests and rate-only changes keep a narrower active policy. */
  memset(net.ic.ic_chan_active,0,sizeof(net.ic.ic_chan_active));
  setbit(net.ic.ic_chan_active,cases[i].forbidden==36 ? 8 : 36);
  net.ic.ic_flags|=IEEE80211_F_USEPROT;
  init_calls=slot_notifications=0;
  CHECK(rtw88_net_media_change(&ifp)==0);
  CHECK(init_calls==0 && slot_notifications==0 && (net.ic.ic_flags&IEEE80211_F_USEPROT)!=0);
  CHECK(!isset(net.ic.ic_chan_active,cases[i].forbidden));
  ifmedia_set(&net.ic.ic_media,IFM_MAKEWORD(IFM_IEEE80211,IFM_IEEE80211_OFDM24,cases[i].media_mode,0));
  net.ic.ic_media.ifm_media=net.ic.ic_media.ifm_cur->ifm_media;
  CHECK(rtw88_net_media_change(&ifp)==0);
  CHECK(init_calls==1);
  CHECK(!isset(net.ic.ic_chan_active,cases[i].forbidden));
  CHECK((net.ic.ic_flags&IEEE80211_F_USEPROT)!=0 && slot_notifications==0);
  /* Existing init-failure behavior restores only the wrapper cache. */
  oldword=net.configured_media; init_error=EIO;
  ifmedia_set(&net.ic.ic_media,IFM_MAKEWORD(IFM_IEEE80211,IFM_IEEE80211_OFDM54,cases[i].media_mode,0));
  net.ic.ic_media.ifm_media=net.ic.ic_media.ifm_cur->ifm_media;
  CHECK(rtw88_net_media_change(&ifp)==EIO);
  CHECK(net.configured_media==oldword);
  CHECK(net.ic.ic_fixed_rate>=0 &&
      (net.ic.ic_sup_rates[net.ic.ic_curmode].rs_rates[net.ic.ic_fixed_rate]&IEEE80211_RATE_VAL)==108);
 }
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
 check_driver_media_refresh();
 printf("rtw88 native channel/media contract: %u checks, %u failures\n",checks,failures);
 return failures!=0;
}
