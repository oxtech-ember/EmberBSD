/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD actual CSA/BA/CCMP consumer contracts, AI-assisted. */
/* Actual Action producer/receive arm and BA/data/CCMP/Ethernet consumers.
 * Negotiated-peer admission and kernel services remain explicit models. */
#define IEEE80211_NODE_AMSDU_RX 0x40
#define main rxba_prior_ccmp_main
#include "rxba-ccmp-contract.inc"
#undef main
static unsigned rxba_output_count, rxba_output_seq[4096];
#define IEEE80211_S_RUN 4
#define IEEE80211_S_INIT 0
#define ieee80211_node_is_authorized(ni) ((ni)->authorized)
static uint64_t unit_now,unit_deadline,unit_token;
static uint32_t unit_profile_generation;
static int unit_schedule_profile_change;
static int unit_schedule_error,unit_response_error;
static unsigned unit_positive_posts;
static uint64_t unit_clock(struct ieee80211com *ic) { (void)ic;return unit_now; }
static int unit_schedule(struct ieee80211com *ic,uint64_t deadline,uint64_t token)
{ (void)ic;unit_deadline=deadline;unit_token=token;if(unit_schedule_profile_change){unit_schedule_profile_change=0;unit_profile_generation++;}return unit_schedule_error; }
static void unit_cancel(struct ieee80211com *ic) { (void)ic;unit_deadline=unit_token=0; }
static const struct ieee80211_rxba_ops unit_ops={unit_clock,unit_schedule,unit_cancel};
static void m_freem(struct mbuf *m) {
 while(m) {struct mbuf *n=m->m_next;
  if(m->m_flags&M_PKTHDR)while(!SLIST_EMPTY(&m->m_pkthdr.tags)) {
   struct m_tag *tag=SLIST_FIRST(&m->m_pkthdr.tags);SLIST_REMOVE_HEAD(&m->m_pkthdr.tags,m_tag_link);free(tag);
  }
  free(m);m=n;
 }
}
static int (*unit_state_hook)(struct ieee80211com *,int,int);
static int ieee80211_new_state(struct ieee80211com *ic,int state,int arg)
{ if(unit_state_hook)return unit_state_hook(ic,state,arg);(void)arg;ic->ic_state=state;ic->ic_bss->ni_ht20.valid=0;return 0; }
#define IEEE80211_S_ASSOC 3
#include "rxba-stack-constants.inc"
static unsigned ieee80211_chan2ieee(struct ieee80211com *ic,struct ieee80211_channel *channel)
{ (void)ic;return channel->ieee; }
#include "native-rxba-ht20.inc"
#include "native-rxba.inc"

/* Actual Action producer and owning queue admission. Kernel allocator, prepend,
 * node references and synchronous if_start service are modeled explicitly. */
#define IEEE80211_M_IBSS 0
#define IEEE80211_M_AHDEMO 3
#define IEEE80211_M_HOSTAP 6
#define IEEE80211_M_MONITOR 8
#define IEEE80211_NODE_STAT(...) ((void)0)
#define M_DONTWAIT 0
#define IFQ_MAXLEN 256
#ifndef htole16
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define htole16(x) (x)
#else
#define htole16(x) __builtin_bswap16(x)
#endif
#endif
#define M_SETCTX(m,n) ((m)->m_pkthdr.ctx=(n))
#define IF_ENQUEUE(q,p) do { assert((q)->m==NULL);(q)->m=(p);(q)->ifq_len++; } while(0)
#define IF_DROP(q) ((q)->ifq_drops++)
static int unit_mgt_alloc_fail,unit_prepend_fail;
static struct mbuf *unit_mgtframe(uint8_t **p,int len)
{
 if(unit_mgt_alloc_fail)return NULL;
 struct mbuf *m=calloc(1,sizeof(*m));assert(m);
 m->m_data=m->storage+64;m->m_len=m->m_pkthdr.len=len;m->m_flags=M_PKTHDR;
 *p=m->m_data;return m;
}
#define ieee80211_getmgtframe unit_mgtframe
static struct mbuf *unit_prepend(struct mbuf *m,unsigned len)
{
 if(unit_prepend_fail){m_freem(m);return NULL;}
 assert(m->m_data-m->storage>=(int)len);
 m->m_data-=len;m->m_len+=len;m->m_pkthdr.len+=len;return m;
}
#define M_PREPEND(m,len,how) ((m)=unit_prepend((m),(len)))
static unsigned unit_declined_posts;
static void if_start_lock(struct ifnet *ifp)
{
 struct ieee80211com *ic=ifp->model_ic;
 struct mbuf *m=ic->ic_mgtq.m;assert(m&&m->m_pkthdr.ctx);
 uint8_t *p=m->m_data;
 CHECK(m->m_len==33 && p[24]==3 && p[25]==1);
 CHECK(!(ic->ic_bss->ni_flags & IEEE80211_NODE_ASSOC_PENDING));
 if(p[27]==0 && p[28]==0) {
  unsigned tid=((unsigned)p[29]|(unsigned)p[30]<<8)>>2&15;
  CHECK(tid<8 && ic->ic_bss->ni_rxba.tid[tid]!=NULL);
  unit_positive_posts++;
 }
 if(p[27]==37 && p[28]==0)unit_declined_posts++;
 ic->ic_mgtq.m=NULL;ic->ic_mgtq.ifq_len--;
 ieee80211_free_node(m->m_pkthdr.ctx);m->m_pkthdr.ctx=NULL;m_freem(m);
}
#include "native-rxba-output.inc"
#include "native-rxba-crypto-encap.inc"
#include <arpa/inet.h>
#include "ieee80211_amsdu.h"
#define IEEE80211_NO_HOSTAP 1
#define IEEE80211_DEBUGVAR(x)
#define IEEE80211_DISCARD(...) ((void)0)
#define IEEE80211_DISCARD_MAC(...) ((void)0)
#define IEEE80211_NODE_STAT_ADD(...) ((void)0)
#define IFF_SIMPLEX 1
#define BPF_D_IN 1
#define ETHERTYPE_PAE 0x888e
#define LLC_SNAP_LSAP 0xaa
#define LLC_UI 3
#define ETHER_ALIGN 2
#define MHLEN 200
#define MT_DATA 0
#define MCLGET(m,how) ((m)->m_flags|=M_EXT)
#define MGETHDR(m,how,type) do { uint8_t *p;(m)=unit_mgtframe(&p,0); } while(0)
#define IEEE80211_NODE_LOCK(nt) ((void)(nt))
#define IEEE80211_NODE_UNLOCK(nt) ((void)(nt))
#define ticks 1
#define if_statinc(ifp,name) ((ifp)->name++)
#define if_statadd(ifp,name,value) ((ifp)->name+=(value))
#define ETHER_IS_MULTICAST(a) ((a)[0]&1)
#define IFQ_ENQUEUE(q,m,error) do { (void)(q);m_freem(m);(error)=0; } while(0)
#define vlan_set_tag(m,tag) ((void)(m),(void)(tag))
struct ether_header { uint8_t ether_dhost[6],ether_shost[6];uint16_t ether_type; } __packed;
struct llc { uint8_t llc_dsap,llc_ssap,llc_control;struct { uint8_t org_code[3];uint16_t ether_type; } __packed llc_snap; } __packed;
static void bpf_mtap3(void *p,struct mbuf *m,int dir) { (void)p;(void)m;(void)dir; }
static void m_copy_rcvif(struct mbuf *a,struct mbuf *b) { a->m_pkthdr.rcvif_index=b->m_pkthdr.rcvif_index; }
static void m_cat(struct mbuf *a,struct mbuf *b) { while(a->m_next)a=a->m_next;a->m_next=b; }
static struct mbuf *m_copypacket(struct mbuf *m,int how) { (void)how;return duplicate(m); }
static void if_percpuq_enqueue(void *q,struct mbuf *m)
{
 (void)q;CHECK(m->m_len>=16);
 if(m->m_len>=16) rxba_output_seq[rxba_output_count++]=m->m_data[14]|(unsigned)m->m_data[15]<<8;
 m_freem(m);
}
#include "native-rxba-input.inc"
#include "native-rxba-action.inc"
static int cpu_intr_p(void) { return 0; }
static struct ieee80211_node *ieee80211_fakeup_adhoc_node(void *table,const uint8_t *address)
{ (void)table;(void)address;return NULL; }
static void unit_mgmt_dispatch(struct ieee80211com *ic,struct mbuf *m,struct ieee80211_node *ni,int subtype,int rssi,uint32_t stamp)
{ (void)rssi;(void)stamp;CHECK(subtype==IEEE80211_FC0_SUBTYPE_ACTION);unit_recv_action(ic,m,ni); }
#include "native-rxba-common-input.inc"

/* Driver callback faults exercise actual generic key mutation ownership. */
static int unit_key_alloc_fail,unit_key_set_fail,unit_key_delete_fail,unit_attach_fail;
static unsigned unit_key_calls;
static void unit_assert_ciphertext_purged(struct ieee80211com *ic)
{
 if(ic->ic_bss==NULL)return;
 for(unsigned t=0;t<8;t++) {
  struct ieee80211_rxba_tid *r=ic->ic_bss->ni_rxba.tid[t];
  if(r)for(unsigned i=0;i<64;i++)CHECK(r->slot[i].m==NULL);
 }
}
static int unit_key_alloc(struct ieee80211com *ic,const struct ieee80211_key *key,ieee80211_keyix *tx,ieee80211_keyix *rx)
{ (void)key;unit_assert_ciphertext_purged(ic);unit_key_calls++;*tx=0;*rx=IEEE80211_KEYIX_NONE;return !unit_key_alloc_fail; }
static int unit_key_set(struct ieee80211com *ic,const struct ieee80211_key *key,const uint8_t *mac)
{ (void)key;(void)mac;unit_assert_ciphertext_purged(ic);unit_key_calls++;return !unit_key_set_fail; }
static int unit_key_delete(struct ieee80211com *ic,const struct ieee80211_key *key)
{ (void)key;unit_assert_ciphertext_purged(ic);unit_key_calls++;return !unit_key_delete_fail; }
static void *unit_key_attach(struct ieee80211com *ic,struct ieee80211_key *key)
{ unit_assert_ciphertext_purged(ic);unit_key_calls++;return unit_attach_fail?NULL:ccmp_attach(ic,key); }
#undef IEEE80211_NODE_LOCK
#undef IEEE80211_NODE_UNLOCK
#define IEEE80211_NODE_LOCK(nt) do { assert(!(nt)->locked);(nt)->locked=1; } while(0)
#define IEEE80211_NODE_UNLOCK(nt) do { assert((nt)->locked);(nt)->locked=0; } while(0)
#define IEEE80211_NODE_IS_LOCKED(nt) ((nt)->locked)
#define IEEE80211_NODE_SAVEQ_DRAIN(ni,qlen) do { (void)(ni);(qlen)=0; } while(0)
#define free(p,type) free(p)
#include "native-rxba-node.inc"
#undef free
struct unit_fixture {
 struct ifnet ifp;
 struct ieee80211com ic;
 struct ieee80211_node ni;
 struct ieee80211_key tx;
 struct ieee80211_channel channel;
};
static int unit_profile(struct ieee80211com *ic,struct ieee80211_ht20_profile *p)
{ (void)ic;*p=(struct ieee80211_ht20_profile){unit_profile_generation,0xffff,0xffff,IEEE80211_HT20_READY_ALL,16};return 0; }
static void unit_apply(void) { }
static void unit_negotiate(struct unit_fixture *f)
{
 struct ieee80211_ht20_profile profile;uint8_t ies[52]={0};
 f->ic.ic_ht20_admit=unit_profile;unit_profile_generation=1;
 f->ic.ic_wme.wme_sta_apply=unit_apply;f->ic.ic_flags|=IEEE80211_F_WME;f->ic.ic_caps|=IEEE80211_C_WME;
 f->ni.ni_flags|=IEEE80211_NODE_QOS|IEEE80211_NODE_WME_VALID;
 /* The HT gate reads the station's own RSN element: CCMP pairwise. */
 static uint8_t unit_rsn[22]={48,20,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,2,0,0};
 f->ic.ic_opt_ie=unit_rsn;f->ic.ic_opt_ie_len=sizeof(unit_rsn);
 f->channel.ieee=36;f->ni.ni_chan=f->ic.ic_curchan=&f->channel;
 f->ic.ic_state=IEEE80211_S_ASSOC;
 int prepared=ieee80211_ht20_prepare(&f->ic,&f->ni,1,&profile);
 CHECK(prepared==1);
 if(prepared!=1)return; /* No profile to write or offer on this path. */
 ieee80211_ht20_write(ies,&profile);ieee80211_ht20_offer(&f->ic,&f->ni,0,&profile,NULL,NULL);
 struct ieee80211_ht20_node offer=f->ni.ni_ht20;
 ies[28]=61;ies[29]=22;ies[30]=36;
 CHECK(ieee80211_ht20_response(&f->ic,&f->ni,0x10,&offer,ies,sizeof(ies),1)==0);
 CHECK(f->ni.ni_ht20.valid && !f->ni.ni_ht20.offered);
 CHECK((f->ni.ni_flags&IEEE80211_NODE_AMSDU_RX)!=0);
 CHECK(ieee80211_ht20_state_request(&f->ic,IEEE80211_S_RUN)==0);
 f->ic.ic_state=IEEE80211_S_RUN;
}
static int unit_setup_mode(struct unit_fixture *f,unsigned head,unsigned window,unsigned timeout,int ba)
{
 memset(f,0,sizeof(*f));rxba_kmem_fail_at=0;unit_mgt_alloc_fail=unit_prepend_fail=0;unit_schedule_error=unit_response_error=unit_schedule_profile_change=0;unit_positive_posts=0;unit_now=1000;unit_deadline=unit_token=0;
 rxba_output_count=0;memset(rxba_output_seq,0,sizeof(rxba_output_seq));
 unit_key_alloc_fail=unit_key_set_fail=unit_key_delete_fail=unit_attach_fail=0;unit_key_calls=0;
 f->ic.ic_crypto.cs_key_alloc=unit_key_alloc;f->ic.ic_crypto.cs_key_set=unit_key_set;f->ic.ic_crypto.cs_key_delete=unit_key_delete;
 f->ic.ic_ifp=&f->ifp;f->ifp.model_ic=&f->ic;f->ni.references=1;f->ic.ic_bss=&f->ni;f->ni.ni_ic=&f->ic;
 f->ic.ic_myaddr[0]=f->ni.ni_macaddr[0]=f->ni.ni_bssid[0]=2;
 f->ic.ic_myaddr[5]=1;f->ni.ni_macaddr[5]=f->ni.ni_bssid[5]=2;
 f->ifp.if_percpuq=&f->ic;f->ic.ic_flags=IEEE80211_F_PRIVACY;f->ic.ic_recv_mgmt=unit_mgmt_dispatch;f->ic.ic_state=IEEE80211_S_RUN;f->ic.ic_opmode=IEEE80211_M_STA;f->ic.ic_rxba_ops=&unit_ops;
 f->ni.authorized=1;unit_negotiate(f);
 init_key(&f->ic,&f->ni.ni_ucastkey,0,0x19);init_key(&f->ic,&f->tx,0,0x19);
 if(!ba)return 1;
 unsigned params=2|(window<<6);
 uint8_t req[9]={3,0,1,params,params>>8,timeout,timeout>>8,head<<4,head>>4};
 struct mbuf action={0};action.m_data=action.storage;action.m_len=action.m_pkthdr.len=33;
 action.m_data[0]=0xd0;memcpy(action.m_data+4,f->ic.ic_myaddr,6);
 memcpy(action.m_data+10,f->ni.ni_bssid,6);memcpy(action.m_data+16,f->ni.ni_bssid,6);
 memcpy(action.m_data+24,req,9);
 CHECK(ieee80211_input(&f->ic,duplicate(&action),&f->ni,0,0)==IEEE80211_FC0_TYPE_MGT);
 CHECK(f->ni.ni_rxba.tid[0]!=NULL && unit_positive_posts==1);
 if(f->ni.ni_rxba.tid[0]==NULL) {
  ieee80211_rxba_reset(&f->ic,&f->ni);unit_cancel(&f->ic);
  ccmp_detach(&f->ni.ni_ucastkey);ccmp_detach(&f->tx);return 0;
 }
 return 1;
}
static int unit_setup(struct unit_fixture *f,unsigned head,unsigned window,unsigned timeout)
{ return unit_setup_mode(f,head,window,timeout,1); }
static void unit_close(struct unit_fixture *f)
{
 ieee80211_rxba_reset(&f->ic,&f->ni);
 ccmp_detach(&f->ni.ni_ucastkey);ccmp_detach(&f->tx);
 CHECK(rxba_kmem_live==0 && unit_token==0 && f->ni.references==1);
}
static struct mbuf *unit_wire_tid(struct unit_fixture *f,unsigned seq,uint64_t pn,unsigned tid)
{
 struct mbuf m={0};m.m_data=m.storage+64;m.m_len=m.m_pkthdr.len=26+8+31;m.m_flags=M_PKTHDR;
 uint8_t *p=m.m_data;p[0]=0x88;p[1]=0x42;
 memcpy(p+4,f->ic.ic_myaddr,6);memcpy(p+10,f->ni.ni_bssid,6);p[16]=2;
 p[22]=seq<<4;p[23]=seq>>4;p[24]=tid;
 uint8_t *payload=p+34;payload[0]=payload[1]=0xaa;payload[2]=3;payload[6]=8;
 payload[8]=seq;payload[9]=seq>>8;
 for(unsigned i=10;i<31;i++)payload[i]=0x31+i;
 f->tx.wk_keytsc=pn-1;CHECK(ccmp_encap(&f->tx,&m,0)==1);
 return duplicate(&m);
}
static struct mbuf *unit_wire(struct unit_fixture *f,unsigned seq,uint64_t pn)
{ return unit_wire_tid(f,seq,pn,0); }
static void unit_input(struct unit_fixture *f,unsigned seq,uint64_t pn)
{
 struct mbuf *m=unit_wire(f,seq,pn);
 CHECK(ieee80211_input(&f->ic,m,&f->ni,0,0)==IEEE80211_FC0_TYPE_DATA);
}
static unsigned unit_quiet_calls;
static void unit_quiet(struct ieee80211com *ic)
{
 CHECK(ic->ic_state==IEEE80211_S_RUN && ic->ic_bss->ni_ht20.valid);
 unit_quiet_calls++;
}
/* Exercise actual common input, Action dispatch and shared RX teardown. */
static void test_csa_action_exit(void)
{
 /* The management dispatch must also reject an absent current peer. */
 struct ieee80211com empty={0};struct mbuf absent={0};
 empty.ic_opmode=IEEE80211_M_STA;empty.ic_state=IEEE80211_S_RUN;
 absent.m_data=absent.storage;absent.m_len=absent.m_pkthdr.len=33;
 absent.m_data[0]=0xd0;absent.m_data[24]=3;
 unit_recv_action(&empty,&absent,NULL);
 CHECK(empty.ic_stats.is_rx_mgtdiscard==1);
 for(unsigned kind=0;kind<12;kind++) {
  struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
  unit_input(&f,11,11);CHECK(unit_token!=0 && f.ni.ni_rxba.bytes!=0);
  f.ic.ic_sta_quiet=unit_quiet;unit_quiet_calls=0;
  struct mbuf action={0};action.m_data=action.storage;
  action.m_len=action.m_pkthdr.len=31;
  uint8_t *p=action.m_data;p[0]=0xd0;
  memcpy(p+4,f.ic.ic_myaddr,6);memcpy(p+10,f.ni.ni_macaddr,6);
  memcpy(p+16,f.ni.ni_bssid,6);
  const uint8_t csa[]={0,4,37,3,1,64,0};memcpy(p+24,csa,sizeof(csa));
  if(kind==1)memset(p+4,255,6);
  if(kind==2) { const uint8_t ext[]={4,4,1,118,64,0};memcpy(p+24,ext,sizeof(ext));action.m_len=action.m_pkthdr.len=30; }
  if(kind==3)action.m_len=action.m_pkthdr.len=26; /* Truncated CSA action. */
  if(kind==4)p[15]^=1;
  if(kind==5)p[21]^=1;
  if(kind==6)p[9]^=1;
  if(kind==7)p[1]=IEEE80211_FC1_MORE_FRAG;
  if(kind==8)f.ic.ic_sta_quiet=NULL;
  if(kind==9)p[25]=2; /* A TPC request is not CSA. */
  struct mbuf *received=duplicate(&action);
  if(kind>=10) {
   /* Split the two-byte Action prefix at both possible mbuf boundaries. */
   unsigned split=kind==10?24:25;
   struct mbuf *tail=calloc(1,sizeof(*tail));assert(tail);
   tail->m_data=tail->storage;tail->m_len=received->m_len-split;
   memcpy(tail->m_data,received->m_data+split,tail->m_len);
   received->m_len=split;received->m_next=tail;
  }
  ieee80211_input(&f.ic,received,&f.ni,0,0);
  unsigned want=kind<4 || kind>=10;
  CHECK(unit_quiet_calls==want);
  if(want) {
   CHECK(f.ic.ic_state==IEEE80211_S_INIT && !f.ni.ni_ht20.valid);
   CHECK(f.ni.ni_rxba.bytes==0 && unit_token==0 && rxba_output_count==0);
  }
  unit_close(&f);
 }
}
static void test_absolute_deadline(void)
{
 struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
 unit_input(&f,11,11);uint64_t stale=unit_token;
 CHECK(rxba_output_count==0 && unit_deadline==1100);
 unit_now=1050;unit_input(&f,12,12);
 CHECK(unit_deadline==1100 && unit_token!=stale);
 unit_now=1099;ieee80211_rxba_age(&f.ic,&f.ni,unit_token);
 CHECK(rxba_output_count==0 && unit_deadline==1100);
 unit_now=1100;ieee80211_rxba_age(&f.ic,&f.ni,stale);
 CHECK(rxba_output_count==0);
 ieee80211_rxba_age(&f.ic,&f.ni,unit_token);
 CHECK(rxba_output_count==2 && rxba_output_seq[0]==11 && rxba_output_seq[1]==12);
 CHECK(unit_token==0);
 unit_input(&f,10,100);CHECK(rxba_output_count==2);
 unit_close(&f);
 if(!unit_setup(&f,10,64,1))return;
 CHECK(unit_deadline==1002);
 unit_input(&f,11,11);unit_now=1002;
 ieee80211_rxba_age(&f.ic,&f.ni,unit_token);
 CHECK(f.ni.ni_rxba.tid[0]==NULL && f.ni.ni_rxba.bytes==0 && rxba_output_count==0);
 unit_close(&f);
}
static void test_window_and_wrap(void)
{
 struct unit_fixture f;if(!unit_setup(&f,4095,64,0))return;
 unit_input(&f,0,2);CHECK(rxba_output_count==0);
 unit_input(&f,4095,1);
 CHECK(rxba_output_count==2 && rxba_output_seq[0]==4095 && rxba_output_seq[1]==0);
 unit_input(&f,0,3);CHECK(rxba_output_count==2);
 unit_close(&f);
 if(!unit_setup(&f,10,4,0))return;
 unit_input(&f,2058,1);CHECK(rxba_output_count==0 && f.ni.ni_rxba.tid[0]->head==10);
 unit_input(&f,13,13);unit_input(&f,14,14);
 CHECK(f.ni.ni_rxba.tid[0]->head==11 && rxba_output_count==0);
 unit_now=1100;ieee80211_rxba_age(&f.ic,&f.ni,unit_token);
 CHECK(rxba_output_count==2 && rxba_output_seq[0]==13 && rxba_output_seq[1]==14);
 CHECK(f.ni.ni_rxba.tid[0]->head==15);
 unit_close(&f);
}
static void test_key_and_mic_order(void)
{
 struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
 unit_input(&f,11,11);CHECK(rxba_output_count==0);
 uint64_t old_token=unit_token;
 ieee80211_rxba_key_change(&f.ic,&f.ni.ni_ucastkey);
 CHECK(unit_token==0 && f.ni.ni_rxba.tid[0]!=NULL);
 memset(f.ni.ni_ucastkey.wk_key,0x27,16);CHECK(ccmp_setkey(&f.ni.ni_ucastkey));
 memset(f.tx.wk_key,0x27,16);CHECK(ccmp_setkey(&f.tx));
 unit_now=1200;ieee80211_rxba_age(&f.ic,&f.ni,old_token);
 CHECK(rxba_output_count==0);
 unit_input(&f,10,1);unit_input(&f,11,2);
 CHECK(rxba_output_count==2 && rxba_output_seq[0]==10 && rxba_output_seq[1]==11);
 CHECK(((struct ccmp_ctx *)f.ni.ni_ucastkey.wk_private)->cc_rxrsc[0]==2);
 unit_close(&f);
 if(!unit_setup(&f,10,64,0))return;
 struct mbuf *m=unit_wire(&f,10,1);m->m_data[m->m_len-1]^=1;
 CHECK(ieee80211_rxba_input(&f.ic,&f.ni,m)==1);
 CHECK(rxba_output_count==0 && f.ic.ic_stats.is_rx_ccmpmic==1);
 CHECK(((struct ccmp_ctx *)f.ni.ni_ucastkey.wk_private)->cc_rxrsc[0]==0);
 unit_input(&f,11,1);CHECK(rxba_output_count==1 && rxba_output_seq[0]==11);
 unit_close(&f);
}

static int unit_request(struct unit_fixture *f,unsigned head,unsigned timeout,unsigned token)
{
 uint8_t req[9]={3,0,token,2,16,timeout,timeout>>8,head<<4,head>>4};
 return ieee80211_rxba_action(&f->ic,&f->ni,req,sizeof(req));
}
static void test_candidate_preflight(void)
{
 struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
 unsigned posted=unit_positive_posts;unit_schedule_error=EIO;
 CHECK(unit_request(&f,20,1,2)==EIO);
 CHECK(unit_positive_posts==posted && f.ni.ni_rxba.bytes==0);
 CHECK(f.ic.ic_state==IEEE80211_S_INIT && !f.ni.ni_ht20.valid && unit_token==0);
 unit_schedule_error=0;unit_close(&f);
 if(!unit_setup(&f,10,64,10))return;
 unit_input(&f,11,11);
 struct ieee80211_rxba_tid *old=f.ni.ni_rxba.tid[0];
 size_t bytes=f.ni.ni_rxba.bytes;posted=unit_positive_posts;
 unit_response_error=ENOBUFS;f.ic.ic_mgtq.ifq_maxlen=f.ic.ic_mgtq.ifq_len=1;
 CHECK(unit_request(&f,20,5,2)==ENOBUFS);
 CHECK(f.ni.ni_rxba.tid[0]==old && f.ni.ni_rxba.bytes==bytes);
 CHECK(unit_positive_posts==posted && unit_deadline==1011);
 unit_response_error=0;f.ic.ic_mgtq.ifq_len=0;unit_close(&f);
 if(!unit_setup(&f,10,64,0))return;
 posted=unit_positive_posts;
 f.ic.ic_rxba_serial=UINT64_MAX-1;
 CHECK(unit_request(&f,20,1,2)==EOVERFLOW);
 CHECK(unit_positive_posts==posted && !f.ni.ni_ht20.valid && f.ni.ni_rxba.bytes==0);
 CHECK(f.ic.ic_state==IEEE80211_S_INIT && unit_token==0);
 unit_close(&f);
 if(!unit_setup(&f,10,64,0))return;
 posted=unit_positive_posts;unit_now=UINT64_MAX-1;
 CHECK(unit_request(&f,20,1,2)==EOVERFLOW);
 CHECK(unit_positive_posts==posted && !f.ni.ni_ht20.valid && f.ni.ni_rxba.bytes==0);
 unit_close(&f);
}
static int unit_ioctl_key(struct unit_fixture *f,unsigned len,unsigned marker)
{
 struct ieee80211req_key req={0};struct ieee80211req ioc={0};
 req.ik_type=IEEE80211_CIPHER_AES_CCM;req.ik_keyix=IEEE80211_KEYIX_NONE;
 req.ik_flags=IEEE80211_KEY_XMIT|IEEE80211_KEY_RECV;req.ik_keylen=len;
 memcpy(req.ik_macaddr,f->ni.ni_bssid,6);memset(req.ik_keydata,marker,16);
 ioc.i_len=sizeof(req);ioc.i_data=&req;return ieee80211_ioctl_setkey(&f->ic,&ioc);
}
static void test_actual_key_lifecycle(void)
{
 struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
 unit_input(&f,11,11);uint64_t token=unit_token,epoch=f.ni.ni_rxba.key_epoch;
 void *initial_context=f.ni.ni_ucastkey.wk_private;
 CHECK(ieee80211_crypto_newkey(&f.ic,IEEE80211_CIPHER_AES_CCM,IEEE80211_KEY_RECV,&f.ni.ni_ucastkey)==1);
 CHECK(f.ni.ni_ucastkey.wk_private!=initial_context && unit_token==0 && f.ni.ni_rxba.key_epoch>epoch);
 CHECK((f.ni.ni_flags&IEEE80211_NODE_AMSDU_RX)!=0);
 epoch=f.ni.ni_rxba.key_epoch;
 CHECK(unit_ioctl_key(&f,16,0x27)==0);
 CHECK(f.ni.ni_rxba.key_epoch>epoch && unit_token==0 && f.ni.references==1);
 CHECK(f.ni.ni_ucastkey.wk_cipher==&ccmp && unit_key_calls>0);
 unit_now=1200;ieee80211_rxba_age(&f.ic,&f.ni,token);CHECK(rxba_output_count==0);
 memset(f.tx.wk_key,0x27,16);CHECK(ccmp_setkey(&f.tx));
 unit_input(&f,10,1);unit_input(&f,11,2);CHECK(rxba_output_count==2);
 /* Same-pointer replacement, failed setkey validation and failed device set
  * must all invalidate queued ciphertext before replay-domain mutation. */
 unit_input(&f,13,4);epoch=f.ni.ni_rxba.key_epoch;token=unit_token;
 CHECK(unit_ioctl_key(&f,15,0x28)==EIO);
 CHECK(f.ni.ni_rxba.key_epoch>epoch && unit_token==0 && f.ni.references==1);
 ieee80211_rxba_age(&f.ic,&f.ni,token);CHECK(rxba_output_count==2);
 CHECK(unit_ioctl_key(&f,16,0x27)==0);
 unit_input(&f,13,5);epoch=f.ni.ni_rxba.key_epoch;unit_key_set_fail=1;
 CHECK(unit_ioctl_key(&f,16,0x29)==EIO);
 CHECK(f.ni.ni_rxba.key_epoch>epoch && unit_token==0 && f.ni.references==1);
 unit_key_set_fail=0;CHECK(unit_ioctl_key(&f,16,0x27)==0);
 unit_input(&f,13,6);epoch=f.ni.ni_rxba.key_epoch;
 struct ieee80211_cipher cipher=ccmp;cipher.ic_attach=unit_key_attach;
 ciphers[IEEE80211_CIPHER_AES_CCM]=&cipher;unit_attach_fail=1;
 void *context=f.ni.ni_ucastkey.wk_private;
 CHECK(unit_ioctl_key(&f,16,0x29)==ENXIO);
 CHECK(f.ni.ni_rxba.key_epoch>epoch && unit_token==0 && f.ni.references==1);
 CHECK(f.ni.ni_ucastkey.wk_private==context && f.ni.ni_ucastkey.wk_cipher==&ccmp);
 unit_attach_fail=0;ciphers[IEEE80211_CIPHER_AES_CCM]=&ccmp;
 unit_input(&f,13,7);epoch=f.ni.ni_rxba.key_epoch;
 f.ni.ni_ucastkey.wk_keyix=IEEE80211_KEYIX_NONE;unit_key_alloc_fail=1;
 CHECK(unit_ioctl_key(&f,16,0x29)==ENXIO);
 CHECK(f.ni.ni_rxba.key_epoch>epoch && unit_token==0 && f.ni.references==1);
 unit_key_alloc_fail=0;f.ni.ni_ucastkey.wk_keyix=0;
 /* Delete failure still detaches software context after purging. */
 unit_input(&f,13,8);epoch=f.ni.ni_rxba.key_epoch;unit_key_delete_fail=1;
 CHECK(ieee80211_crypto_delkey(&f.ic,&f.ni.ni_ucastkey)==1);
 CHECK(f.ni.ni_rxba.key_epoch>epoch && unit_token==0);
 CHECK(f.ni.ni_ucastkey.wk_cipher==&ieee80211_cipher_none);
 CHECK(f.ic.ic_stats.is_crypto_delkey==1);
 ieee80211_rxba_reset(&f.ic,&f.ni);ccmp_detach(&f.tx);
 CHECK(rxba_kmem_live==0 && f.ni.references==1);
}
/* Compose the actual negotiation producer and beacon preflight/reset with
 * actual ADDBA, queued encrypted data, replay state and numeric timer owners.
 * The complete management receiver/native INIT owner runs in HT/enable suites. */
static void test_beacon_contract_purge(void)
{
 for(unsigned kind=0;kind<7;kind++) {
  struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
  unit_input(&f,11,11);uint64_t token=unit_token;
  struct ccmp_ctx *ctx=f.ni.ni_ucastkey.wk_private;
  CHECK(token!=0 && f.ni.ni_rxba.bytes!=0 && ctx->cc_rxrsc[0]==0);
  struct ieee80211_frame wh={0};uint8_t body[12+52]={0};
  memset(wh.i_addr1,255,6);memcpy(wh.i_addr2,f.ni.ni_macaddr,6);
  memcpy(wh.i_addr3,f.ni.ni_bssid,6);
  struct ieee80211_ht20_profile profile;
  CHECK(unit_profile(&f.ic,&profile)==0);
  ieee80211_ht20_write(body+12,&profile);
  body[40]=61;body[41]=22;body[42]=36;
  size_t len=sizeof(body);
  if(kind==1)body[44]=6;
  if(kind==2)body[14]^=0x10;
  if(kind==3)len--;
  if(kind==4)unit_profile_generation++;
  if(kind==5)f.ic.ic_ht20_admit=NULL;
  if(kind==6)wh.i_addr2[0]^=2;
  int error=ieee80211_ht20_beacon(&f.ic,&f.ni,&wh,body,len);
  if(kind>0 && kind<6) {
   CHECK(error==EINVAL && !f.ni.ni_ht20.valid);
   CHECK(!(f.ni.ni_flags&IEEE80211_NODE_AMSDU_RX));
   CHECK(f.ni.ni_rxba.bytes==0 && f.ni.ni_rxba.epoch==0 && unit_token==0);
   CHECK(ctx->cc_rxrsc[0]==0 && rxba_output_count==0 && f.ni.references==1);
   unit_now+=1000;ieee80211_rxba_age(&f.ic,&f.ni,token);
   CHECK(ctx->cc_rxrsc[0]==0 && rxba_output_count==0);
  } else {
   CHECK(error==0 && f.ni.ni_ht20.valid && unit_token==token);
   unit_input(&f,10,10);
   CHECK(rxba_output_count==2 && ctx->cc_rxrsc[0]==11);
  }
  unit_close(&f);
 }
}

/* The native enable fixture owns Ethernet -> actual encap -> plaintext-size
 * admission. Its cipher service is modeled. Here the corresponding three-
 * address QoS/LLC payload executes actual generic header insertion and CCMP.
 * This is a compositional byte-budget proof, not a full driver crypto export. */
static void test_ht20_real_ccmp_budget(void)
{
 const unsigned mtus[]={32,1499,1500};
 for(unsigned which=0;which<2;which++)for(unsigned i=0;i<sizeof(mtus)/sizeof(mtus[0]);i++) {
  struct unit_fixture f;if(!unit_setup_mode(&f,10,64,0,0))return;
  init_key(&f.ic,&f.ic.ic_nw_keys[0],0,0x47);f.ic.ic_def_txkey=0;
  const struct ieee80211_cipher *cipher=f.ni.ni_ucastkey.wk_cipher;
  if(which)f.ni.ni_ucastkey.wk_cipher=&ieee80211_cipher_none;
  struct ieee80211_key *key=which?&f.ic.ic_nw_keys[0]:&f.ni.ni_ucastkey;
  uint64_t node_pn=f.ni.ni_ucastkey.wk_keytsc,default_pn=f.ic.ic_nw_keys[0].wk_keytsc;
  CHECK(ieee80211_crypto_getucastkey(&f.ic,&f.ni)==key);
  CHECK(f.ni.ni_ucastkey.wk_keytsc==node_pn && f.ic.ic_nw_keys[0].wk_keytsc==default_pn);
  unsigned mtu=mtus[i];uint8_t *p;
  struct mbuf *m=unit_mgtframe(&p,(int)(26+8+mtu));assert(m);
  p[0]=0x88;p[1]=0x41;
  memcpy(p+4,f.ni.ni_bssid,6);memcpy(p+10,f.ic.ic_myaddr,6);p[16]=2;
  p[26]=p[27]=0xaa;p[28]=3;p[32]=8;
  for(unsigned j=0;j<mtu;j++)p[34+j]=(uint8_t)(j^0x5a);
  uint64_t pn=key->wk_keytsc;
  CHECK(ieee80211_crypto_encap(&f.ic,&f.ni,m)==key);
  CHECK(m->m_pkthdr.len==(int)(mtu+50) && m->m_len==m->m_pkthdr.len);
  CHECK((unsigned)m->m_pkthdr.len+4==mtu+54 && key->wk_keytsc==pn+1);
  if(which)CHECK(f.ni.ni_ucastkey.wk_keytsc==node_pn);
  else CHECK(f.ic.ic_nw_keys[0].wk_keytsc==default_pn);
  CHECK(ccmp_decap(key,m,26)==1);
  CHECK(m->m_pkthdr.len==(int)(mtu+34));
  for(unsigned j=0;j<mtu;j++)CHECK(m->m_data[34+j]==(uint8_t)(j^0x5a));
  m_freem(m);f.ni.ni_ucastkey.wk_cipher=cipher;
  ccmp_detach(&f.ic.ic_nw_keys[0]);unit_close(&f);
 }
}

static void test_actual_node_reuse(void)
{
 struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
 unit_input(&f,11,11);uint64_t old=unit_token;uint32_t attempt=f.ni.ni_ht20.attempt;
 struct ieee80211_node *map[1]={&f.ni};f.ic.ic_sta.nt_keyixmap=map;f.ic.ic_sta.nt_keyixmax=1;
 f.ni.ni_ucastkey.wk_rxkeyix=0;f.ni.references++;
 node_cleanup(&f.ni);
 CHECK(f.ni.ni_ht20.attempt!=attempt && !f.ni.ni_ht20.valid);
 CHECK(f.ni.ni_rxba.epoch==0 && f.ni.ni_rxba.bytes==0 && unit_token==0);
 CHECK(f.ni.references==1 && map[0]==NULL && !f.ic.ic_sta.locked);
 CHECK(f.ni.ni_ucastkey.wk_cipher==&ieee80211_cipher_none);
 unit_now=1300;ieee80211_rxba_age(&f.ic,&f.ni,old);CHECK(rxba_output_count==0);
 /* Reuse the same node address. New association gets a new shared serial;
  * an old callback token cannot deliver into it. */
 CHECK(unit_ioctl_key(&f,16,0x19)==0);unit_negotiate(&f);
 CHECK(unit_request(&f,20,0,3)==0);CHECK(f.ni.ni_rxba.tid[0]!=NULL);
 unit_input(&f,21,21);uint64_t fresh=unit_token;
 ieee80211_rxba_age(&f.ic,&f.ni,old);
 CHECK(unit_token==fresh && rxba_output_count==0);
 unit_now=1400;ieee80211_rxba_age(&f.ic,&f.ni,fresh);
 CHECK(rxba_output_count==1 && rxba_output_seq[0]==21);
 /* Cleanup while node-table lock is already held must not recurse. */
 f.ic.ic_sta.locked=1;node_cleanup(&f.ni);CHECK(f.ic.ic_sta.locked==1);f.ic.ic_sta.locked=0;
 ccmp_detach(&f.tx);CHECK(rxba_kmem_live==0 && f.ni.references==1 && unit_token==0);
}
static struct mbuf *unit_bar(struct unit_fixture *f,unsigned seq,unsigned control)
{
 struct mbuf m={0};m.m_data=m.storage;m.m_len=m.m_pkthdr.len=20;m.m_data[0]=0x84;
 memcpy(m.m_data+4,f->ic.ic_myaddr,6);memcpy(m.m_data+10,f->ni.ni_bssid,6);
 m.m_data[16]=control;m.m_data[17]=control>>8;m.m_data[18]=seq<<4;m.m_data[19]=seq>>4;
 return duplicate(&m);
}
static void test_actual_bar_and_delba(void)
{
 struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
 unit_input(&f,12,12);uint64_t token=unit_token;
 struct mbuf *m=unit_bar(&f,12,4);m->m_data[4]^=2;
 CHECK(ieee80211_input(&f.ic,m,&f.ni,0,0)==IEEE80211_FC0_TYPE_CTL);
 CHECK(rxba_output_count==0 && unit_token==token);
 m=unit_bar(&f,12,0);CHECK(ieee80211_input(&f.ic,m,&f.ni,0,0)==IEEE80211_FC0_TYPE_CTL);
 CHECK(rxba_output_count==0 && unit_token==token);
 m=unit_bar(&f,12,4);CHECK(ieee80211_input(&f.ic,m,&f.ni,0,0)==IEEE80211_FC0_TYPE_CTL);
 CHECK(rxba_output_count==1 && rxba_output_seq[0]==12 && unit_token==0);
 uint8_t del[6]={3,2,0,0,39,0};
 CHECK(ieee80211_rxba_action(&f.ic,&f.ni,del,sizeof(del))==0);
 CHECK(f.ni.ni_rxba.tid[0]!=NULL); /* Receiver-initiated DELBA is TX BA. */
 del[3]=8;CHECK(ieee80211_rxba_action(&f.ic,&f.ni,del,sizeof(del))==0);
 CHECK(f.ni.ni_rxba.tid[0]==NULL && f.ni.ni_rxba.bytes==0);
 unit_close(&f);
}
static void test_clock_serial_quarantine(void)
{
 struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
 unit_input(&f,11,11);unit_now=999;
 ieee80211_rxba_age(&f.ic,&f.ni,unit_token);
 CHECK(f.ic.ic_state==IEEE80211_S_INIT && !f.ni.ni_ht20.valid);
 CHECK(unit_token==0 && f.ni.ni_rxba.bytes==0 && rxba_output_count==0);
 unit_close(&f);
 if(!unit_setup(&f,10,64,0))return;
 unit_input(&f,11,11);unit_now=999;unit_input(&f,12,12);
 CHECK(f.ic.ic_state==IEEE80211_S_INIT && !f.ni.ni_ht20.valid);
 unit_close(&f);
 if(!unit_setup(&f,10,64,0))return;
 unit_input(&f,11,11);unit_now=999;
 struct mbuf *m=unit_bar(&f,11,4);ieee80211_input(&f.ic,m,&f.ni,0,0);
 CHECK(f.ic.ic_state==IEEE80211_S_INIT && !f.ni.ni_ht20.valid);
 unit_close(&f);
 if(!unit_setup(&f,10,64,0))return;
 unit_input(&f,11,11);f.ic.ic_rxba_serial=UINT64_MAX-1;
 CHECK(unit_ioctl_key(&f,16,0x27)==0);
 CHECK(f.ic.ic_state==IEEE80211_S_INIT && !f.ni.ni_ht20.valid);
 CHECK(!ieee80211_rxba_ready(&f.ic) && f.ni.ni_rxba.bytes==0);
 unit_close(&f);
 if(!unit_setup(&f,10,64,0))return;
 unsigned posts=unit_positive_posts;unit_now=999;
 CHECK(unit_request(&f,20,0,3)==EINVAL);
 CHECK(unit_positive_posts==posts && f.ic.ic_state==IEEE80211_S_INIT && !f.ni.ni_ht20.valid);
 unit_close(&f);
 if(!unit_setup(&f,10,64,0))return;
 posts=unit_positive_posts;unit_now=UINT64_MAX;
 CHECK(unit_request(&f,20,0,3)==EOVERFLOW);
 CHECK(unit_positive_posts==posts && f.ic.ic_state==IEEE80211_S_INIT && !f.ni.ni_ht20.valid);
 unit_close(&f);
 if(!unit_setup(&f,10,64,0))return;
 unit_now=UINT64_MAX;unit_input(&f,10,1);
 CHECK(rxba_output_count==0 && f.ic.ic_state==IEEE80211_S_INIT);
 unit_close(&f);

}
static void test_owned_storage_budget(void)
{
 struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
 size_t metadata=f.ni.ni_rxba.bytes;struct mbuf *m=unit_wire(&f,11,11);
 m->m_flags|=M_EXT;m->m_ext.ext_size=IEEE80211_RXBA_BUDGET+1;
 ieee80211_input(&f.ic,m,&f.ni,0,0);
 CHECK(f.ni.ni_rxba.bytes==metadata && unit_token==0);
 m=unit_wire(&f,11,11);m->m_flags|=M_EXT;
 m->m_ext.ext_size=IEEE80211_RXBA_BUDGET-metadata-sizeof(*m);
 ieee80211_input(&f.ic,m,&f.ni,0,0);
 CHECK(f.ni.ni_rxba.bytes==metadata && unit_token==0); /* Inline control state is part of the quota. */
 m=unit_wire(&f,11,11);m->m_flags|=M_EXT;m->m_ext.ext_size=4096;
 ieee80211_input(&f.ic,m,&f.ni,0,0);
 CHECK(f.ni.ni_rxba.bytes==metadata+sizeof(*m)+4096);
 unit_close(&f);
 if(!unit_setup(&f,10,64,0))return;
 metadata=f.ni.ni_rxba.bytes;m=unit_wire(&f,11,11);
 struct mbuf *tail=calloc(1,sizeof(*tail));assert(tail);tail->m_data=tail->storage;
 tail->m_len=m->m_len-26;memcpy(tail->m_data,m->m_data+26,tail->m_len);m->m_len=26;m->m_next=tail;
 struct m_tag *tag=calloc(1,sizeof(*tag)+64);assert(tag);tag->m_tag_len=64;
 SLIST_INSERT_HEAD(&m->m_pkthdr.tags,tag,m_tag_link);
 ieee80211_input(&f.ic,m,&f.ni,0,0);
 CHECK(f.ni.ni_rxba.bytes==metadata+2*sizeof(*m)+sizeof(*tag)+64);
 unit_close(&f);
 if(!unit_setup(&f,0,64,0))return;
 for(unsigned tid=1;tid<8;tid++) {
  unsigned params=2|(tid<<2)|(64<<6);uint8_t req[9]={3,0,tid+1,params,params>>8,0,0,0,0};
  CHECK(ieee80211_rxba_action(&f.ic,&f.ni,req,sizeof(req))==0);
 }
 for(unsigned tid=0;tid<8;tid++)for(unsigned seq=1;seq<64;seq++) {
  m=unit_wire_tid(&f,seq,tid*100+seq,tid);m->m_flags|=M_EXT;m->m_ext.ext_size=4096;
  ieee80211_input(&f.ic,m,&f.ni,0,0);
  CHECK(f.ni.ni_rxba.bytes<=IEEE80211_RXBA_BUDGET);
 }
 unsigned held=0;for(unsigned t=0;t<8;t++)for(unsigned i=0;i<64;i++)held+=f.ni.ni_rxba.tid[t]->slot[i].m!=NULL;
 CHECK(held>0 && held<8*63 && rxba_output_count==0);
 unit_close(&f);
}
static void test_storage_walk_bounds(void)
{
 for(unsigned kind=0;kind<5;kind++) {
  struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
  size_t before=f.ni.ni_rxba.bytes;struct mbuf *m=unit_wire(&f,11,11);
  if(kind<2) {
   struct mbuf *tail=m;
   for(unsigned i=1;i<(kind?17U:16U);i++) {
    tail->m_next=calloc(1,sizeof(*tail));assert(tail->m_next);
    tail=tail->m_next;tail->m_data=tail->storage;
   }
  } else if(kind<4) {
   for(unsigned i=0;i<(kind==3?17U:16U);i++) {
    struct m_tag *tag=calloc(1,sizeof(*tag)+1);assert(tag);tag->m_tag_len=1;
    SLIST_INSERT_HEAD(&m->m_pkthdr.tags,tag,m_tag_link);
   }
  } else {m->m_flags|=M_EXT;m->m_ext.ext_size=1;}
  ieee80211_input(&f.ic,m,&f.ni,0,0);
  if(kind==0 || kind==2)CHECK(f.ni.ni_rxba.bytes>before && unit_token!=0);
  else CHECK(f.ni.ni_rxba.bytes==before && unit_token==0);
  unit_close(&f);
 }
}
static struct mbuf *unit_null_wire(struct unit_fixture *f,unsigned seq,uint64_t pn,int qos,int payload)
{
 struct mbuf m={0};unsigned hdr=qos?26:24;m.m_data=m.storage+64;m.m_flags=M_PKTHDR;
 m.m_len=m.m_pkthdr.len=hdr+8+(payload?31:0);uint8_t *p=m.m_data;
 p[0]=qos?0xc8:0x48;p[1]=0x42;memcpy(p+4,f->ic.ic_myaddr,6);memcpy(p+10,f->ni.ni_bssid,6);p[16]=2;
 p[22]=seq<<4;p[23]=seq>>4;
 if(payload) {uint8_t *llc=p+hdr+8;llc[0]=llc[1]=0xaa;llc[2]=3;llc[6]=8;llc[8]=seq;llc[9]=seq>>8;}
 f->tx.wk_keytsc=pn-1;CHECK(ccmp_encap(&f->tx,&m,0)==1);return duplicate(&m);
}
static void test_null_crypto_order(void)
{
 struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
 struct mbuf *m=unit_null_wire(&f,11,11,1,1);
 ieee80211_input(&f.ic,m,&f.ni,0,0);
 CHECK(rxba_output_count==0 && ((struct ccmp_ctx *)f.ni.ni_ucastkey.wk_private)->cc_rxrsc[0]==0);
 unit_input(&f,10,10);
 CHECK(rxba_output_count==1 && rxba_output_seq[0]==10);
 CHECK(((struct ccmp_ctx *)f.ni.ni_ucastkey.wk_private)->cc_rxrsc[0]==11 && f.ifp.if_ierrors==0);
 unit_input(&f,12,12);CHECK(rxba_output_count==2 && rxba_output_seq[1]==12);
 m=unit_null_wire(&f,13,13,1,1);ieee80211_input(&f.ic,m,&f.ni,0,0);
 CHECK(rxba_output_count==2 && ((struct ccmp_ctx *)f.ni.ni_ucastkey.wk_private)->cc_rxrsc[0]==13);
 ieee80211_rxba_reset(&f.ic,&f.ni);
 for(unsigned qos=0;qos<2;qos++) {
  unsigned seq=20+qos*10;uint64_t pn=qos?14:1;
  unsigned before=rxba_output_count,errors=f.ifp.if_ierrors;
  struct mbuf plain={0};plain.m_data=plain.storage;plain.m_len=plain.m_pkthdr.len=qos?26:24;
  plain.m_data[0]=qos?0xc8:0x48;plain.m_data[1]=2;
  memcpy(plain.m_data+4,f.ic.ic_myaddr,6);memcpy(plain.m_data+10,f.ni.ni_bssid,6);
  ieee80211_input(&f.ic,duplicate(&plain),&f.ni,0,0);
  CHECK(rxba_output_count==before && f.ifp.if_ierrors==errors);
  m=unit_null_wire(&f,seq,pn,qos,1);ieee80211_input(&f.ic,m,&f.ni,0,0);
  CHECK(rxba_output_count==before && f.ifp.if_ierrors==errors);
  m=unit_null_wire(&f,seq+1,pn+1,qos,1);ieee80211_input(&f.ic,m,&f.ni,0,0);
  CHECK(rxba_output_count==before);
  struct ccmp_ctx *ctx=f.ni.ni_ucastkey.wk_private;uint64_t *floor=qos?&ctx->cc_rxrsc[0]:&f.ni.ni_ucastkey.wk_keyrsc;
  CHECK(*floor==pn+1);
  m=unit_null_wire(&f,seq+2,pn+100,qos,1);m->m_data[m->m_len-1]^=0x80;
  ieee80211_input(&f.ic,m,&f.ni,0,0);CHECK(*floor==pn+1 && rxba_output_count==before);
  m=unit_null_wire(&f,seq+3,pn+1,qos,1);ieee80211_input(&f.ic,m,&f.ni,0,0);
  CHECK(*floor==pn+1 && rxba_output_count==before);
 }
 unit_close(&f);
}
/*
 * An AP commonly requests block ack right after association, while the
 * driver still owns a deferred RUN or before the key handshake. Without
 * a session that request is premature, not a contract violation: decline
 * it (status 37) and keep the negotiated profile and the association.
 */
static void test_premature_addba_declined(void)
{
 for(unsigned kind=0;kind<2;kind++) {
  struct unit_fixture f;if(!unit_setup_mode(&f,10,64,0,0))return;
  if(kind==0)f.ic.ic_state=IEEE80211_S_ASSOC; else f.ni.authorized=0;
  unit_declined_posts=0;
  CHECK(unit_request(&f,20,0,7)==0);
  CHECK(unit_declined_posts==1 && unit_positive_posts==0);
  CHECK(f.ic.ic_state==(kind==0?IEEE80211_S_ASSOC:IEEE80211_S_RUN));
  CHECK(f.ni.ni_ht20.valid && f.ni.ni_rxba.epoch==0 && f.ni.ni_rxba.bytes==0);
  /* The driver queries the mask after every frame; with no session that
   * query is empty and must not withdraw the association either. */
  CHECK(ieee80211_rxba_mask(&f.ic,&f.ni)==0);
  CHECK(f.ni.ni_ht20.valid && f.ic.ic_state!=IEEE80211_S_INIT);
  /* A premature DELBA has no session to remove and is ignored. */
  uint8_t delba[6]={3,2,0,0x08,1,0};
  CHECK(ieee80211_rxba_action(&f.ic,&f.ni,delba,sizeof(delba))!=0);
  CHECK(f.ni.ni_ht20.valid && f.ic.ic_state!=IEEE80211_S_INIT);
  unit_close(&f);
 }
}
static void test_owned_admission_withdrawal(void)
{
 struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
 unit_input(&f,11,11);f.ni.authorized=0;unit_input(&f,12,12);
 CHECK(f.ic.ic_state==IEEE80211_S_INIT && !f.ni.ni_ht20.valid && f.ni.ni_rxba.bytes==0);
 unit_close(&f);
 if(!unit_setup(&f,10,64,0))return;
 unit_input(&f,11,11);unit_profile_generation++;unit_input(&f,12,12);
 CHECK(f.ic.ic_state==IEEE80211_S_INIT && !f.ni.ni_ht20.valid && unit_token==0 && rxba_output_count==0);
 unit_close(&f);
 if(!unit_setup(&f,10,64,0))return;
 unit_profile_generation++;CHECK(ieee80211_rxba_mask(&f.ic,&f.ni)==0);
 CHECK(f.ic.ic_state==IEEE80211_S_INIT && !f.ni.ni_ht20.valid);
 unit_close(&f);
 if(!unit_setup(&f,10,64,0))return;
 unsigned posts=unit_positive_posts;unit_schedule_profile_change=1;
 CHECK(unit_request(&f,20,1,2)==EINVAL);
 CHECK(f.ic.ic_state==IEEE80211_S_INIT && !f.ni.ni_ht20.valid && unit_token==0 && unit_positive_posts==posts);
 unit_close(&f);
 if(!unit_setup(&f,10,64,0))return;
 ieee80211_rxba_reset(&f.ic,&f.ni);f.ic.ic_rxba_serial=UINT64_MAX-1;
 CHECK(unit_request(&f,20,0,2)==EOVERFLOW);
 CHECK(f.ic.ic_state==IEEE80211_S_INIT && !f.ni.ni_ht20.valid);
 unit_close(&f);
}
static void test_actual_allocation_failures(void)
{
 for(unsigned kind=0;kind<4;kind++) {
  struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
  unit_input(&f,11,11);size_t bytes=f.ni.ni_rxba.bytes;
  struct ieee80211_rxba_tid *old=f.ni.ni_rxba.tid[0];unsigned posts=unit_positive_posts,live=rxba_kmem_live;
  if(kind<2)rxba_kmem_fail_at=rxba_kmem_calls+kind+1;
  else if(kind==2)unit_mgt_alloc_fail=1;
  else unit_prepend_fail=1;
  CHECK(unit_request(&f,20,1,2)==ENOMEM);
  CHECK(f.ni.ni_rxba.bytes==bytes && f.ni.ni_rxba.tid[0]==old && old->head==10);
  CHECK(rxba_kmem_live==live && f.ni.references==1 && unit_positive_posts==posts && unit_deadline==1100);
  rxba_kmem_fail_at=0;unit_mgt_alloc_fail=unit_prepend_fail=0;unit_close(&f);
 }
}
static void test_repeat_replace_and_tid_deadlines(void)
{
 struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
 unit_input(&f,11,11);unit_input(&f,10,10);unit_input(&f,13,13);
 struct ieee80211_rxba_tid *old=f.ni.ni_rxba.tid[0];size_t bytes=f.ni.ni_rxba.bytes;
 unsigned posts=unit_positive_posts;uint64_t token=unit_token;
 CHECK(unit_request(&f,10,0,1)==0);
 CHECK(f.ni.ni_rxba.tid[0]==old && old->head==12 && old->slot[13].m!=NULL && f.ni.ni_rxba.bytes==bytes);
 CHECK(unit_positive_posts==posts+1 && unit_deadline==1100 && unit_token!=token);
 token=unit_token;CHECK(unit_request(&f,30,0,2)==0);
 CHECK(f.ni.ni_rxba.tid[0]!=old && f.ni.ni_rxba.tid[0]->head==30);
 CHECK(f.ni.ni_rxba.bytes==sizeof(*old) && unit_token==0);
 ieee80211_rxba_age(&f.ic,&f.ni,token);CHECK(rxba_output_count==2);
 unit_close(&f);
 if(!unit_setup(&f,10,64,0))return;
 unit_input(&f,11,11);unit_now=1050;
 unsigned params=2|(1<<2)|(64<<6);uint8_t req[9]={3,0,2,params,params>>8,0,0,(uint8_t)(20<<4),20>>4};
 CHECK(ieee80211_rxba_action(&f.ic,&f.ni,req,sizeof(req))==0);
 struct mbuf *m=unit_wire_tid(&f,21,21,1);ieee80211_input(&f.ic,m,&f.ni,0,0);
 CHECK(unit_deadline==1100);
 CHECK(unit_request(&f,30,0,3)==0);CHECK(unit_deadline==1150);
 unit_now=1100;ieee80211_rxba_age(&f.ic,&f.ni,unit_token);CHECK(rxba_output_count==0 && unit_deadline==1150);
 unit_now=1150;ieee80211_rxba_age(&f.ic,&f.ni,unit_token);
 CHECK(rxba_output_count==1 && rxba_output_seq[0]==21);
 unit_close(&f);
}
static void test_invalid_queued_frames(void)
{
 struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
 unit_input(&f,11,11);uint64_t token=unit_token;size_t bytes=f.ni.ni_rxba.bytes;
 for(unsigned kind=0;kind<6;kind++) {
  struct mbuf *m=unit_wire(&f,10,10);
  switch(kind) {
  case 0:m->m_data[1]|=IEEE80211_FC1_MORE_FRAG;break;
  case 1:m->m_data[1]|=IEEE80211_FC1_ORDER;break;
  case 2:m->m_data[22]|=1;break;
  case 3:m->m_data[4]^=2;break;
  case 4:m->m_flags&=~M_PKTHDR;break;
  case 5:m->m_pkthdr.len--;break;
  }
  ieee80211_input(&f.ic,m,&f.ni,0,0);
  CHECK(rxba_output_count==0 && unit_token==token && f.ni.ni_rxba.bytes==bytes && f.ni.ni_rxba.tid[0]->head==10);
 }
 unit_input(&f,10,10);CHECK(rxba_output_count==2 && rxba_output_seq[0]==10 && rxba_output_seq[1]==11);
 unit_close(&f);
}
static struct mbuf *unit_amsdu_wire(struct unit_fixture *f,unsigned seq,uint64_t pn,unsigned total,int malformed)
{
 assert(total==48 || total==3840);
 struct mbuf m={0};m.m_data=m.storage+64;m.m_flags=M_PKTHDR;
 m.m_len=m.m_pkthdr.len=34+total;
 assert((size_t)m.m_len+8<=sizeof(m.storage)-64);
 uint8_t *p=m.m_data;p[0]=0x88;p[1]=0x42;
 memcpy(p+4,f->ic.ic_myaddr,6);memcpy(p+10,f->ni.ni_bssid,6);p[16]=2;
 p[22]=seq<<4;p[23]=seq>>4;p[24]=0x80;
 for(unsigned i=0;i<2;i++) {
  uint8_t *sub=p+34+i*24;
  memcpy(sub,f->ic.ic_myaddr,6);memcpy(sub+6,f->ni.ni_bssid,6);
  sub[13]=10;sub[14]=sub[15]=0xaa;sub[16]=3;sub[20]=8;
  sub[22]=seq*2+i;sub[23]=(seq*2+i)>>8;
 }
 if(malformed)p[34+24+13]=255;
 f->tx.wk_keytsc=pn-1;CHECK(ccmp_encap(&f->tx,&m,0)==1);
 return duplicate(&m);
}
static void test_amsdu_current_without_ba(void)
{
 for(unsigned stale=0;stale<7;stale++) {
  struct unit_fixture f;if(!unit_setup_mode(&f,10,64,0,0))return;
  CHECK(f.ni.ni_rxba.epoch==0 && unit_positive_posts==0);
  CHECK(f.ni.ni_ht20.valid && (f.ni.ni_flags&IEEE80211_NODE_AMSDU_RX)!=0);
  if(stale==1)unit_profile_generation++;
  if(stale==2)f.ni.authorized=0;
  if(stale==3)f.ic.ic_ht20_admit=NULL;
  if(stale==4)f.ni.ni_bssid[5]^=4;
  if(stale==5)f.ni.ni_ht20.attempt++;
  if(stale==6)f.ic.ic_opt_ie=NULL; /* No CCMP RSN request any more. */
  ieee80211_input(&f.ic,unit_amsdu_wire(&f,10,10,48,0),&f.ni,0,0);
  if(stale==0)CHECK(rxba_output_count==2 && f.ni.ni_ht20.valid);
  else {
   CHECK(rxba_output_count==0 && f.ic.ic_state==IEEE80211_S_INIT && !f.ni.ni_ht20.valid);
   CHECK((f.ni.ni_flags&IEEE80211_NODE_AMSDU_RX)==0);
   CHECK(((struct ccmp_ctx *)f.ni.ni_ucastkey.wk_private)->cc_rxrsc[0]==0);
  }
  unit_close(&f);
 }
}
static void test_negotiated_amsdu_crypto_order(void)
{
 struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
 ieee80211_input(&f.ic,unit_amsdu_wire(&f,11,11,48,0),&f.ni,0,0);
 CHECK(rxba_output_count==0);
 ieee80211_input(&f.ic,unit_amsdu_wire(&f,10,10,48,0),&f.ni,0,0);
 CHECK(rxba_output_count==4 && rxba_output_seq[0]==20 && rxba_output_seq[1]==21 && rxba_output_seq[2]==22 && rxba_output_seq[3]==23);
 CHECK(((struct ccmp_ctx *)f.ni.ni_ucastkey.wk_private)->cc_rxrsc[0]==11);
 ieee80211_input(&f.ic,unit_amsdu_wire(&f,12,12,48,1),&f.ni,0,0);
 CHECK(rxba_output_count==4); /* Validate all subframes before delivering any. */
 struct mbuf *m=unit_amsdu_wire(&f,13,13,48,0);m->m_data[m->m_len-1]^=1;
 ieee80211_input(&f.ic,m,&f.ni,0,0);CHECK(rxba_output_count==4);
 ieee80211_input(&f.ic,unit_amsdu_wire(&f,14,14,3840,0),&f.ni,0,0);
 CHECK(rxba_output_count==4); /* Existing total-length security limit. */
 ieee80211_input(&f.ic,unit_amsdu_wire(&f,16,16,48,0),&f.ni,0,0);
 CHECK(f.ni.ni_rxba.bytes>sizeof(struct ieee80211_rxba_tid));
 CHECK(unit_ioctl_key(&f,16,0x19)==0);
 CHECK((f.ni.ni_flags&IEEE80211_NODE_AMSDU_RX)!=0 && unit_token==0);
 CHECK(ieee80211_crypto_delkey(&f.ic,&f.ni.ni_ucastkey)==1);
 CHECK((f.ni.ni_flags&IEEE80211_NODE_AMSDU_RX)==0);
 ieee80211_rxba_reset(&f.ic,&f.ni);ccmp_detach(&f.tx);
 CHECK(rxba_kmem_live==0 && f.ni.references==1);
 if(!unit_setup(&f,10,64,0))return;
 ieee80211_input(&f.ic,unit_amsdu_wire(&f,11,11,48,0),&f.ni,0,0);
 ieee80211_ht20_reset(&f.ni);
 CHECK((f.ni.ni_flags&IEEE80211_NODE_AMSDU_RX)==0 && unit_token==0 && rxba_output_count==0);
 unit_close(&f);
}
#ifndef RXBA_SHARED_MAIN
#define RXBA_SHARED_MAIN main
#endif
int RXBA_SHARED_MAIN(void)
{
 test_csa_action_exit();test_absolute_deadline();test_window_and_wrap();test_key_and_mic_order();test_candidate_preflight();test_actual_key_lifecycle();test_actual_node_reuse();test_beacon_contract_purge();test_ht20_real_ccmp_budget();test_actual_bar_and_delba();test_clock_serial_quarantine();test_owned_storage_budget();test_null_crypto_order();test_owned_admission_withdrawal();test_premature_addba_declined();test_actual_allocation_failures();test_repeat_replace_and_tid_deadlines();test_invalid_queued_frames();test_negotiated_amsdu_crypto_order();test_storage_walk_bounds();test_amsdu_current_without_ba();
 printf("RX BA actual primitive/CCMP units: %u checks, %u failures\n",checks,failures);
 return failures!=0;
}
