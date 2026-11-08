/* SPDX-License-Identifier: BSD-2-Clause */
/* Execute real Ethernet decap, shared BA, RX handoff and deferred key/state
 * consumers. Only kernel services and firmware commands are modeled. */
#define RXBA_SHARED_MAIN aicwf_prior_rxba_main
#include "net80211-rxba-contract.c"
#include <time.h>

#define __SHIFTOUT(v,m) (((v)&(m)) >> __builtin_ctz(m))
#define ETHER_HDR_LEN 14
#define ETHER_ADDR_LEN 6
#define le16dec(p) ((uint16_t)((p)[0] | (p)[1] << 8))
#define roundup(n,m) (((n)+(m)-1)/(m)*(m))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define __UNCONST(p) ((void *)(uintptr_t)(const void *)(p))
#define MCLBYTES 2048
#define PR_NOWAIT 0
#ifndef IFF_RUNNING
#define IFF_RUNNING 0x40
#endif
#define device_printf(...) ((void)0)
#define mutex_owned(p) (*(p))
struct work { unsigned unused; };
enum aicwf_task_cmd { AICWF_TASK_KEY_SET, AICWF_TASK_KEY_DELETE, AICWF_TASK_NEWSTATE };
struct aicwf_task {
 struct work t_work;enum aicwf_task_cmd t_cmd;enum ieee80211_state t_state;
 int t_arg;unsigned t_cipher,t_keyix,t_keylen;bool t_group;
 uint64_t t_epoch,t_rx_serial,t_join_serial,t_configure_serial;
 uint8_t t_key[IEEE80211_KEYBUF_SIZE];
};
struct aicwf_softc {
 struct ieee80211com sc_ic;struct ifnet sc_if;
 int sc_lock,sc_epoch_lock,sc_rx_lock,sc_dev,sc_taskpool,sc_taskq;
 struct { unsigned delay; } sc_rxba_ch;
 struct ieee80211_rxba_offload sc_rxba;
 uint64_t sc_rx_serial,sc_rx_assoc,sc_rx_join,sc_sae_epoch,sc_join_serial,sc_configure_serial;
 uint64_t sc_pmf_rx_pn;
 uint8_t sc_rx_vif,sc_rx_ap,sc_rx_bssid[6],sc_vif,sc_ap,sc_ptk[16],sc_hwkey[5];
 bool sc_rx_active,sc_if_attached,sc_connected,sc_ptk_valid;
 bool sc_connecting,sc_sae_enabled,sc_sae_pending,sc_sae_authenticated,sc_qos;
 uint8_t sc_sae_bssid[6],sc_igtk[2];
 struct {uint8_t bssid[6],r0kh_len;uint16_t mdid;} sc_ft_bss[4];
 unsigned sc_alive_rx,sc_alive_misses;
 struct { unsigned ev_count; } sc_ev_amsdu,sc_ev_ampdu,sc_ev_gap;
};
static struct aicwf_softc aic;
static struct ieee80211_node aic_peer;
static bool aic_node_lock;
static void (*aic_unlock_hook)(void), (*aic_command_hook)(void);
static unsigned aic_delivered,aic_ids[512],aic_key_adds,aic_ports;
static int aic_command_error;
static struct aicwf_task *aic_queued;
static void mutex_enter(int *p) {assert(!aic_node_lock && !*p);*p=1;}
static void mutex_exit(int *p) {assert(*p);*p=0;}
static void mutex_spin_enter(int *p) {assert(!*p);*p=1;}
static void mutex_spin_exit(int *p)
{
 assert(*p);*p=0;
 if(p==&aic.sc_epoch_lock && aic.sc_rx_lock && aic_unlock_hook) {
  void (*hook)(void)=aic_unlock_hook;aic_unlock_hook=NULL;hook();
 }
}
static void getnanouptime(struct timespec *ts)
{ts->tv_sec=(time_t)(unit_now/1000);ts->tv_nsec=(long)(unit_now%1000)*1000000;}
#define callout_stop(ch) ((ch)->delay=0)
#define callout_schedule(ch,n) ((ch)->delay=(n))
#define mstohz(n) (n)
static void m_set_rcvif(struct mbuf *m,struct ifnet *ifp)
{assert(ifp==&aic.sc_if);m->m_pkthdr.rcvif_index=7;}
static void aic_enqueue(void *q,struct mbuf *m)
{
 assert(q==&aic && aic.sc_epoch_lock && aic.sc_rx_lock);
 CHECK(m->m_nextpkt==NULL && m->m_pkthdr.rcvif_index==7);
 CHECK(m->m_len>=16 && aic_delivered<512);
 if(m->m_len>=16 && aic_delivered<512)aic_ids[aic_delivered++]=m->m_data[14];
 m_freem(m);
}
#define if_percpuq_enqueue aic_enqueue
static void *pool_cache_get(int pool,int flags)
{(void)pool;assert(flags==PR_NOWAIT);return calloc(1,sizeof(struct aicwf_task));}
static void workqueue_enqueue(int queue,struct work *work,void *cpu)
{(void)queue;(void)cpu;assert(aic_queued==NULL);aic_queued=(void *)work;}
static void aicwf_sae_event(struct aicwf_softc *sc,bool start,const uint8_t *p,size_t n)
{(void)sc;(void)start;(void)p;(void)n;}
static void aicwf_mgmt_rx(struct aicwf_softc *sc,const uint8_t *p,size_t n,uint32_t status,uint32_t flags)
{(void)sc;(void)p;(void)n;(void)status;(void)flags;}
static void aicwf_sta_tx_cfm(struct aicwf_softc *sc,const uint8_t *p,size_t n)
{(void)sc;(void)p;(void)n;}
static void aicwf_rx_retire(struct aicwf_softc *);
static int splnet(void) {return 0;}
static void splx(int s) {assert(s==0);}
static void aicwf_alive_lost(struct aicwf_softc *sc) {aicwf_rx_retire(sc);}
static void aicwf_assoc_report(struct aicwf_softc *sc,const uint8_t *p,size_t n)
{(void)sc;(void)p;(void)n;}
static void aicwf_sta_assoc(struct aicwf_softc *sc,const uint8_t *p,size_t n)
{(void)sc;(void)p;(void)n;}
static uint64_t aicwf_sae_advance_if(struct aicwf_softc *sc,uint64_t expected)
{if(expected!=sc->sc_sae_epoch)return 0;aicwf_rx_retire(sc);return ++sc->sc_sae_epoch;}
static void aicwf_recover_scan(struct aicwf_softc *sc,uint64_t epoch,uint64_t serial)
{(void)serial;assert(epoch==sc->sc_sae_epoch);}
static void aicwf_rx_msg(struct aicwf_softc *sc,const uint8_t *p,size_t n)
{assert(n==1 && p[0]==1);aicwf_rx_retire(sc);}
static int aicwf_cmd_reply(struct aicwf_softc *,unsigned,const void *,size_t,
    unsigned,void *,size_t,bool,uint64_t);
#include "aicwf-rxba.inc"
static int
aicwf_cmd_reply(struct aicwf_softc *sc,unsigned id,const void *p,size_t n,
    unsigned cid,void *reply,size_t len,bool strict,uint64_t epoch)
{
 (void)p;(void)cid;assert(sc==&aic && !sc->sc_lock && !sc->sc_epoch_lock);
 assert(strict && epoch==sc->sc_sae_epoch);
 if(aic_command_hook) {void (*hook)(void)=aic_command_hook;aic_command_hook=NULL;hook();}
 if(aic_command_error)return aic_command_error;
 if(id==AICWF_MM_KEY_ADD_REQ) {
  assert(n==44 && len==2);aic_key_adds++;memset(reply,0,len);((uint8_t *)reply)[1]=3;
 } else if(id==AICWF_ME_SET_CONTROL_PORT_REQ) {assert(n==2);aic_ports++;}
 else {assert(id==AICWF_MM_KEY_DEL_REQ && n==1);}
 return 0;
}
static const uint8_t aic_address[6]={2,1,2,3,4,5},aic_bssid[6]={2,9,8,7,6,5};
static void aic_begin(void)
{
 if(aic.sc_rx_serial==0)aicwf_rx_retire(&aic);
 aic.sc_rx_join=aic.sc_rx_serial;
 CHECK(aicwf_rx_begin(&aic));
}
static void
aic_reset(void)
{
 CHECK(aic.sc_rxba.bytes==0 && aic_queued==NULL);
 memset(&aic,0,sizeof(aic));memset(&aic_peer,0,sizeof(aic_peer));
 aic.sc_ic.ic_bss=&aic_peer;aic.sc_ic.ic_ifp=&aic.sc_if;
 aic.sc_if.if_softc=&aic;aic.sc_if.if_percpuq=&aic;aic.sc_if.if_flags=IFF_RUNNING;
 memcpy(aic.sc_ic.ic_myaddr,aic_address,6);memcpy(aic_peer.ni_bssid,aic_bssid,6);
 aic.sc_vif=2;aic.sc_ap=3;aic.sc_sae_epoch=7;
 aic.sc_if_attached=aic.sc_connected=true;
 unit_now=1000;unit_mgt_alloc_fail=unit_prepend_fail=0;unit_mgt_headroom=64;
 aic_unlock_hook=aic_command_hook=NULL;aic_command_error=0;
 aic_delivered=aic_key_adds=aic_ports=0;
 aic_begin();CHECK(aic.sc_rx_active);
}
static size_t
aic_frame(uint8_t *packet,unsigned seq,unsigned id,bool reorder,bool amsdu)
{
 uint8_t *wh=packet+60,*body=wh+26;
 memset(packet,0,160);packet[48]=AICWF_RX_FLAG_UPLOAD|(reorder?AICWF_RX_FLAG_REORDER:0);
 packet[49]=2;packet[50]=3;
 wh[0]=0x88;wh[1]=2;memcpy(wh+4,aic_address,6);memcpy(wh+10,aic_bssid,6);
 memcpy(wh+16,aic_bssid,6);wh[22]=(seq<<4)&0xff;wh[23]=seq>>4;wh[24]=15|(amsdu?0x80:0);
 if(amsdu) {
  for(unsigned i=0;i<2;i++) {
   uint8_t *sub=body+i*24;memcpy(sub,aic_address,6);memcpy(sub+6,aic_bssid,6);
   sub[13]=10;memcpy(sub+14,"\xaa\xaa\x03\0\0\0\x08\0",8);sub[22]=id+i;
  }
  return 74;
 }
 memcpy(body,"\xaa\xaa\x03\0\0\0\x08\0",8);body[8]=id;
 return 36;
}
static void aic_receive(unsigned seq,unsigned id,bool reorder,bool amsdu)
{uint8_t p[160];size_t n=aic_frame(p,seq,id,reorder,amsdu);aicwf_rx_data(&aic,p,n,aic.sc_rx_serial);}
static void aic_retire_hook(void) {aicwf_rx_retire(&aic);}
static void aic_key_run(void)
{
 struct aicwf_task *t=aic_queued;assert(t);aic_queued=NULL;
 if(t->t_cmd==AICWF_TASK_KEY_SET)aicwf_key_set_cb(&aic,t);
 else aicwf_key_delete_cb(&aic,t);
 free(t);
}
static void
test_aic_actual_reorder_lifetime(void)
{
 aic_reset();aic_receive(4094,1,true,false);aic_receive(0,4,true,false);
 CHECK(aic_delivered==1 && aic.sc_rxba_ch.delay==50);
 aic_receive(4095,2,true,true);
 CHECK(aic_delivered==4 && aic_ids[0]==1 && aic_ids[1]==2 && aic_ids[2]==3 && aic_ids[3]==4);
 aic_receive(0,9,true,false);CHECK(aic_delivered==4); /* Firmware duplicate. */
 aic_receive(2,5,true,false);unit_now=1049;aicwf_rxba_timeout(&aic);
 CHECK(aic_delivered==4 && aic.sc_rxba_ch.delay==1);
 unit_now=1050;aicwf_rxba_timeout(&aic);
 CHECK(aic_delivered==5 && aic_ids[4]==5 && aic.sc_ev_gap.ev_count==1);
 aic_receive(4,6,true,false);aic_receive(5,7,false,false);
 CHECK(aic_delivered==7 && aic_ids[5]==6 && aic_ids[6]==7);
 CHECK(aic.sc_rxba.bytes==0 && aic.sc_rxba_ch.delay==0);
 aic_receive(20,8,true,false);aic_receive(22,10,true,false);
 aic_unlock_hook=aic_retire_hook;aic_receive(21,9,true,false);
 CHECK(aic_delivered==8 && !aic.sc_rx_active && aic.sc_rxba.bytes==0);
 CHECK(aic_unlock_hook==NULL); /* Retired after mutation, before handoff. */
 aicwf_rxba_timeout(&aic);CHECK(aic_delivered==8);
 CHECK(!aicwf_rx_begin(&aic)); /* A canceled CONNECT cannot reopen RX. */
 uint64_t old=aic.sc_rx_serial;uint8_t p[160];size_t n=aic_frame(p,20,11,true,false);
 aic_begin();aicwf_rx_data(&aic,p,n,old);CHECK(aic_delivered==8);
 for(unsigned kind=0;kind<4;kind++) {
  n=aic_frame(p,20,11,true,false);
  if(kind==0)p[49]=4;
  if(kind==1)p[50]=4;
  if(kind==2)p[70]=4;
  if(kind==3)p[64]=4;
  aicwf_rx_data(&aic,p,n,aic.sc_rx_serial);CHECK(aic_delivered==8);
 }
 n=aic_frame(p,20,11,true,false);p[50]=255;
 aicwf_rx_data(&aic,p,n,aic.sc_rx_serial);CHECK(aic_delivered==9);
 aicwf_rx_retire(&aic);CHECK(aic.sc_rxba.bytes==0);
}
static void
test_aic_key_stop_and_batch_fences(void)
{
 struct ieee80211_key key={0};key.wk_cipher=&ccmp;key.wk_keylen=16;key.wk_key[0]=1;
 aic_reset();aic_receive(10,1,true,false);aic_receive(12,3,true,false);
 aic_node_lock=true;CHECK(aicwf_key_task(&aic.sc_ic,&key,AICWF_TASK_KEY_SET)==1);aic_node_lock=false;
 CHECK(!aic.sc_rx_active && aic.sc_rxba.bytes==0);
 aic_receive(11,2,true,false);CHECK(aic_delivered==1);
 aic_key_run();CHECK(aic.sc_rx_active && aic.sc_ptk_valid && aic_key_adds==1 && aic_ports==1);
 aic_receive(11,4,true,false);CHECK(aic_delivered==2);
 aic.sc_pmf_rx_pn=99;
 CHECK(aicwf_key_task(&aic.sc_ic,&key,AICWF_TASK_KEY_SET)==1);aic_key_run();
 CHECK(aic_key_adds==1 && aic.sc_pmf_rx_pn==99 && aic.sc_rx_active);
 key.wk_key[0]=9;CHECK(aicwf_key_task(&aic.sc_ic,&key,AICWF_TASK_KEY_DELETE)==1);
 CHECK(aic.sc_rx_active);aic_key_run();CHECK(aic.sc_ptk_valid && aic.sc_rx_active);
 key.wk_key[0]=2;CHECK(aicwf_key_task(&aic.sc_ic,&key,AICWF_TASK_KEY_SET)==1);
 aic_command_hook=aic_retire_hook;aic_key_run();CHECK(!aic.sc_rx_active && aic.sc_rx_assoc==0);
 aic_begin();key.wk_key[0]=3;
 CHECK(aicwf_key_task(&aic.sc_ic,&key,AICWF_TASK_KEY_SET)==1);
 aic_command_error=EIO;aic_key_run();CHECK(!aic.sc_rx_active);aic_command_error=0;
 aic_begin();aic_receive(30,5,true,false);aic_receive(32,7,true,false);
 CHECK(aicwf_newstate(&aic.sc_ic,IEEE80211_S_INIT,-1)==0);
 CHECK(!aic.sc_rx_active && aic.sc_rxba.bytes==0);free(aic_queued);aic_queued=NULL;
 aic_begin();
 uint8_t batch[200]={0},p[160];size_t n=aic_frame(p,40,8,true,false);
 batch[0]=1;batch[2]=AICWF_TYPE_MSG;batch[AICWF_RX_MSG_OFFSET]=1;
 size_t step=AICWF_RX_MSG_OFFSET+4;memcpy(batch+step,p,60+n);
 batch[step]=n;batch[step+1]=0;batch[step+2]=0;
 unsigned before=aic_delivered;aicwf_rx(&aic,batch,step+60+n,aic.sc_rx_serial);
 CHECK(aic_delivered==before && !aic.sc_rx_active);
 aicwf_rx_retire(&aic);
}
static struct ieee80211_key aic_pending_key;
static void aic_delete_pending(void)
{
 CHECK(!aic.sc_rx_active);
 CHECK(aicwf_key_task(&aic.sc_ic,&aic_pending_key,AICWF_TASK_KEY_DELETE)==1);
}
static void
test_aic_delete_during_key_install(void)
{
 aic_reset();memset(&aic_pending_key,0,sizeof(aic_pending_key));
 aic_pending_key.wk_cipher=&ccmp;aic_pending_key.wk_keylen=16;aic_pending_key.wk_key[0]=7;
 CHECK(aicwf_key_task(&aic.sc_ic,&aic_pending_key,AICWF_TASK_KEY_SET)==1);
 aic_command_hook=aic_delete_pending;aic_key_run();
 CHECK(!aic.sc_rx_active && aic_queued!=NULL);
 aic_receive(10,1,true,false);aic_receive(12,3,true,false);
 aic_key_run();CHECK(!aic.sc_rx_active && !aic.sc_ptk_valid && aic.sc_rxba.bytes==0);
 unsigned before=aic_delivered;unit_now+=50;aicwf_rxba_timeout(&aic);
 CHECK(aic_delivered==before);
 /* A later SET is not canceled again when the older DELETE executes. */
 CHECK(aicwf_key_task(&aic.sc_ic,&aic_pending_key,AICWF_TASK_KEY_SET)==1);aic_key_run();
 CHECK(aic.sc_rx_active);
 CHECK(aicwf_key_task(&aic.sc_ic,&aic_pending_key,AICWF_TASK_KEY_DELETE)==1);
 struct aicwf_task *deletion=aic_queued;aic_queued=NULL;
 aic_pending_key.wk_key[0]=8;
 CHECK(aicwf_key_task(&aic.sc_ic,&aic_pending_key,AICWF_TASK_KEY_SET)==1);
 aicwf_key_delete_cb(&aic,deletion);free(deletion);aic_key_run();
 CHECK(aic.sc_rx_active && aic.sc_ptk_valid && aic.sc_ptk[0]==8);
 aicwf_rx_retire(&aic);
}
static void
test_aic_old_ap_departure_preserves_join(void)
{
 uint8_t departure[6]={0},connection[13]={0};
 aic_reset();aicwf_rx_retire(&aic);
 aic.sc_connecting=true;aic.sc_rx_join=aic.sc_rx_serial;
 aic.sc_ic.ic_state=IEEE80211_S_ASSOC;
 departure[2]=aic.sc_vif;departure[4]=1;
 uint64_t join=aic.sc_rx_join;
 aicwf_disconnect_ind(&aic,departure,sizeof(departure));
 CHECK(!aic.sc_connected && aic.sc_connecting && aic.sc_rx_join==join);
 memcpy(connection+2,aic_bssid,6);connection[9]=aic.sc_vif;connection[10]=3;connection[12]=1;
 aicwf_connect_ind(&aic,connection,sizeof(connection));
 CHECK(aic.sc_connected && aic.sc_rx_active && aic.sc_ic.ic_state==IEEE80211_S_RUN);
 aicwf_rx_retire(&aic);
 aic.sc_connected=false;aic.sc_connecting=true;aic.sc_rx_join=aic.sc_rx_serial;
 aic.sc_ic.ic_state=IEEE80211_S_ASSOC;
 aicwf_rx_retire(&aic); /* Stop after CONNECT was sent, before indication. */
 aicwf_connect_ind(&aic,connection,sizeof(connection));
 CHECK(!aic.sc_rx_active && !aic.sc_connected && aic.sc_rx_assoc==0);
}
int main(void)
{
 test_aic_actual_reorder_lifetime();test_aic_key_stop_and_batch_fences();
 test_aic_delete_during_key_install();test_aic_old_ap_departure_preserves_join();
 CHECK(aic.sc_rxba.bytes==0 && rxba_kmem_live==0 && !aic.sc_epoch_lock && !aic.sc_rx_lock);
 printf("AIC actual offload RX/BA/lifetime: %u checks, %u failures\n",checks,failures);
 return failures!=0;
}
