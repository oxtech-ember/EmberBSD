/* SPDX-License-Identifier: BSD-2-Clause */
/* Firmware-owned negotiation, actual shared window and packet ownership. */
static struct mbuf *
off_packet(unsigned id)
{
 uint8_t *p;
 struct mbuf *m=unit_mgtframe(&p,14);
 assert(m);memset(p,0,14);p[0]=(uint8_t)id;
 return m;
}
static void
off_free(struct mbuf *m)
{
 while(m) {struct mbuf *next=m->m_nextpkt;m->m_nextpkt=NULL;m_freem(m);m=next;}
}
static void
off_clear(struct ieee80211_rxba_output *out)
{
 for(unsigned i=0;i<IEEE80211_RXBA_OFFLOAD_TIDS;i++)off_free(out->ready[i].head);
 off_free(out->discard);off_free(out->rejected);memset(out,0,sizeof(*out));
}
static void
off_order(struct mbuf *m,const unsigned *ids,size_t count)
{
 size_t i=0;
 for(;m;m=m->m_nextpkt,i++) {CHECK(i<count);if(i<count)CHECK(m->m_data[0]==ids[i]);}
 CHECK(i==count);
}
static void
test_offload_chain_window_epochs(void)
{
 struct ieee80211_rxba_offload ba={0};
 struct ieee80211_rxba_output out={0};
 struct ieee80211_rxba_batch saved;
 unsigned live=rxba_kmem_live;
 CHECK(ieee80211_rxba_offload_configure(&ba,1,1,50,1000,&out)==0);
 CHECK(ieee80211_rxba_offload_start(&ba,15,1,1,1,4094,4,1000,&out)==0);
 struct mbuf *m=off_packet(2);m->m_nextpkt=off_packet(3);
 CHECK(ieee80211_rxba_offload_input(&ba,15,1,1,1,4095,1001,m,&out)==0);
 CHECK(out.ready[15].head==NULL && ba.deadline==1051 && ba.timer_token!=0);
 CHECK(ieee80211_rxba_offload_input(&ba,15,1,1,1,4094,1002,off_packet(1),&out)==0);
 const unsigned a[]={1,2,3};off_order(out.ready[15].head,a,3);
 CHECK(ba.timer_token==0 && ba.bytes==sizeof(struct ieee80211_rxba_tid));
 CHECK(ieee80211_rxba_offload_current(&ba,15,&out.ready[15]));
 saved=out.ready[15];off_clear(&out);
 CHECK(ieee80211_rxba_offload_input(&ba,15,1,1,1,1,1010,off_packet(5),&out)==0);
 CHECK(ieee80211_rxba_offload_input(&ba,15,1,1,1,0,1011,off_packet(4),&out)==0);
 const unsigned b[]={4,5};off_order(out.ready[15].head,b,2);off_clear(&out);
 CHECK(ieee80211_rxba_offload_input(&ba,15,1,1,1,0,1012,off_packet(6),&out)==EINVAL);
 CHECK(out.rejected!=NULL && out.ready[15].head==NULL);off_clear(&out);
 CHECK(ieee80211_rxba_offload_input(&ba,15,1,1,1,2050,1013,off_packet(7),&out)==EINVAL);
 CHECK(out.rejected!=NULL);off_clear(&out);
 CHECK(ieee80211_rxba_offload_start(&ba,15,1,1,2,20,4,1020,&out)==0);
 CHECK(!ieee80211_rxba_offload_current(&ba,15,&saved));
 CHECK(ieee80211_rxba_offload_input(&ba,15,1,1,2,22,1021,off_packet(8),&out)==0);
 CHECK(ieee80211_rxba_offload_input(&ba,15,1,1,2,30,1022,off_packet(9),&out)==0);
 const unsigned c[]={8};off_order(out.ready[15].head,c,1);off_clear(&out);
 uint64_t stale=ba.timer_token;
 CHECK(ieee80211_rxba_offload_input(&ba,15,1,1,2,31,1023,off_packet(10),&out)==0);
 CHECK(ba.deadline==1072 && ba.timer_token!=stale);
 ieee80211_rxba_offload_age(&ba,stale,1072,&out);
 CHECK(out.ready[15].head==NULL);
 ieee80211_rxba_offload_age(&ba,ba.timer_token,1071,&out);
 CHECK(out.ready[15].head==NULL && ba.deadline==1072);
 ieee80211_rxba_offload_age(&ba,ba.timer_token,1072,&out);
 const unsigned d[]={9,10};off_order(out.ready[15].head,d,2);
 CHECK(out.gaps==1 && ba.timer_token==0);off_clear(&out);
 CHECK(ieee80211_rxba_offload_input(&ba,15,1,1,2,33,1073,off_packet(11),&out)==0);
 CHECK(ieee80211_rxba_offload_stop(&ba,15,1,1,2,1,&out)==0);
 const unsigned e[]={11};off_order(out.ready[15].head,e,1);
 saved=out.ready[15];CHECK(ieee80211_rxba_offload_current(&ba,15,&saved));off_clear(&out);
 CHECK(ba.bytes==0 && ba.timer_token==0);
 CHECK(ieee80211_rxba_offload_input(&ba,15,1,1,2,34,1074,off_packet(12),&out)==ESTALE);
 CHECK(out.rejected!=NULL);off_clear(&out);
 CHECK(ieee80211_rxba_offload_start(&ba,15,1,1,2,34,4,1074,&out)==ESTALE);
 CHECK(ieee80211_rxba_offload_stop(&ba,15,1,1,2,0,&out)==0);
 CHECK(!ieee80211_rxba_offload_current(&ba,15,&saved));
 CHECK(ieee80211_rxba_offload_start(&ba,15,1,1,3,34,4,1074,&out)==0);
 CHECK(ieee80211_rxba_offload_input(&ba,15,1,1,3,35,1075,off_packet(13),&out)==0);
 stale=ba.timer_token;
 CHECK(ieee80211_rxba_offload_configure(&ba,1,2,50,1076,&out)==0);
 CHECK(out.discard!=NULL && ba.bytes==0);off_clear(&out);
 CHECK(ieee80211_rxba_offload_input(&ba,15,1,1,3,34,1077,off_packet(14),&out)==ESTALE);
 off_clear(&out);ieee80211_rxba_offload_age(&ba,stale,2000,&out);
 CHECK(out.ready[15].head==NULL);
 CHECK(ieee80211_rxba_offload_configure(&ba,1,1,50,1077,&out)==ESTALE);
 ieee80211_rxba_offload_retire(&ba,&out);off_clear(&out);
 CHECK(ieee80211_rxba_offload_configure(&ba,1,2,50,1078,&out)==ESTALE);
 CHECK(ieee80211_rxba_offload_configure(&ba,2,1,50,1078,&out)==0);
 CHECK(ieee80211_rxba_offload_start(&ba,15,2,1,3,34,4,1078,&out)==ESTALE);
 CHECK(ieee80211_rxba_offload_start(&ba,15,2,1,4,34,4,1078,&out)==0);
 ieee80211_rxba_offload_retire(&ba,&out);off_clear(&out);
 CHECK(rxba_kmem_live==live && ba.bytes==0);
}

static void
test_offload_invalid_and_allocation(void)
{
 struct ieee80211_rxba_offload ba={0};
 struct ieee80211_rxba_output out={0};
 unsigned live=rxba_kmem_live;
 CHECK(ieee80211_rxba_offload_configure(&ba,1,1,50,1000,&out)==0);
 for(unsigned kind=0;kind<5;kind++) {
  CHECK(ieee80211_rxba_offload_start(&ba,kind==0?16:0,1,1,
      kind==1?0:kind==2?UINT64_MAX:1,kind==3?4096:0,kind==4?65:4,1000,&out)==EINVAL);
  CHECK(ba.bytes==0);
 }
 rxba_kmem_fail_at=rxba_kmem_calls+1;
 CHECK(ieee80211_rxba_offload_start(&ba,0,1,1,1,0,4,1000,&out)==ENOMEM);
 rxba_kmem_fail_at=0;
 CHECK(ieee80211_rxba_offload_start(&ba,0,1,1,1,0,4,1000,&out)==ESTALE);
 CHECK(ieee80211_rxba_offload_start(&ba,0,1,1,2,0,4,1000,&out)==0);
 for(unsigned kind=0;kind<5;kind++) {
  struct mbuf *m=off_packet(1);
  if(kind==0)m->m_flags&=~M_PKTHDR;
  if(kind==1)m->m_pkthdr.len=13;
  if(kind==2)m->m_len=-1;
  if(kind==3){m->m_flags|=M_EXT;m->m_ext.ext_size=IEEE80211_RXBA_BUDGET;}
  if(kind==4) {
   struct mbuf *tail=m;
   for(unsigned n=0;n<IEEE80211_RXBA_PACKETS_MAX;n++){tail->m_nextpkt=off_packet(2);tail=tail->m_nextpkt;}
  }
  CHECK(ieee80211_rxba_offload_input(&ba,0,1,1,2,1,1001,m,&out)==EINVAL);
  CHECK(out.rejected==m && ba.bytes==sizeof(struct ieee80211_rxba_tid));off_clear(&out);
 }
 CHECK(ieee80211_rxba_offload_input(&ba,16,1,1,2,1,1001,off_packet(1),&out)==EINVAL);off_clear(&out);
 CHECK(ieee80211_rxba_offload_input(&ba,0,1,1,2,4096,1001,off_packet(1),&out)==EINVAL);off_clear(&out);
 CHECK(ieee80211_rxba_offload_input(&ba,0,1,1,2,1,1002,off_packet(1),&out)==0);
 CHECK(ieee80211_rxba_offload_input(&ba,0,1,1,2,1,1003,off_packet(2),&out)==EALREADY);
 CHECK(out.rejected!=NULL);off_clear(&out);
 ieee80211_rxba_offload_age(&ba,ba.timer_token,1001,&out);
 CHECK(ba.epoch==0 && ba.bytes==0 && out.discard!=NULL);off_clear(&out);
 CHECK(ieee80211_rxba_offload_configure(&ba,2,1,50,1004,&out)==0);
 CHECK(ieee80211_rxba_offload_start(&ba,0,2,1,3,0,4,1004,&out)==0);
 ba.serial=UINT64_MAX-1;
 CHECK(ieee80211_rxba_offload_input(&ba,0,2,1,3,1,1005,off_packet(1),&out)==EOVERFLOW);
 CHECK(ba.epoch==0 && ba.serial==UINT64_MAX && out.discard!=NULL);off_clear(&out);
 CHECK(ieee80211_rxba_offload_configure(&ba,3,1,50,1006,&out)==EINVAL);
 CHECK(rxba_kmem_live==live && ba.bytes==0);
}

static void
test_offload_tid_deadlines_and_budget(void)
{
 struct ieee80211_rxba_offload ba={0};
 struct ieee80211_rxba_output out={0};
 unsigned live=rxba_kmem_live;
 CHECK(ieee80211_rxba_offload_configure(&ba,1,1,50,1000,&out)==0);
 for(unsigned tid=0;tid<16;tid++) {
  CHECK(ieee80211_rxba_offload_start(&ba,tid,1,1,1,0,64,1000+tid,&out)==0);
  CHECK(ieee80211_rxba_offload_input(&ba,tid,1,1,1,2,1000+tid,off_packet(tid),&out)==0);
 }
 CHECK(ba.deadline==1050);
 ieee80211_rxba_offload_age(&ba,ba.timer_token,1050,&out);
 CHECK(out.ready[0].head!=NULL && out.gaps==1 && ba.deadline==1051);
 for(unsigned tid=1;tid<16;tid++)CHECK(out.ready[tid].head==NULL);
 off_clear(&out);
 ieee80211_rxba_offload_age(&ba,ba.timer_token,1065,&out);
 CHECK(out.gaps==15 && ba.timer_token==0);
 for(unsigned tid=1;tid<16;tid++) {
  const unsigned id[]={tid};off_order(out.ready[tid].head,id,1);
 }
 off_clear(&out);ieee80211_rxba_offload_retire(&ba,&out);off_clear(&out);
 CHECK(ieee80211_rxba_offload_configure(&ba,2,1,50,2000,&out)==0);
 CHECK(ieee80211_rxba_offload_start(&ba,0,2,1,2,0,4,2000,&out)==0);
 struct mbuf *first=off_packet(1);first->m_flags|=M_EXT;
 first->m_ext.ext_size=IEEE80211_RXBA_BUDGET/2;
 CHECK(ieee80211_rxba_offload_input(&ba,0,2,1,2,1,2001,first,&out)==0);
 size_t retained=ba.bytes;
 struct mbuf *second=off_packet(2);second->m_flags|=M_EXT;
 second->m_ext.ext_size=IEEE80211_RXBA_BUDGET/2;
 second->m_nextpkt=off_packet(3);
 CHECK(ieee80211_rxba_offload_input(&ba,0,2,1,2,2,2002,second,&out)==ENOBUFS);
 CHECK(out.rejected==second && out.rejected->m_nextpkt!=NULL && ba.bytes==retained);
 off_clear(&out);
 /* Displacement releases charged storage before admission of the new MPDU. */
 second=off_packet(4);second->m_flags|=M_EXT;second->m_ext.ext_size=IEEE80211_RXBA_BUDGET/2;
 CHECK(ieee80211_rxba_offload_input(&ba,0,2,1,2,8,2003,second,&out)==0);
 const unsigned id[]={1};off_order(out.ready[0].head,id,1);off_clear(&out);
 CHECK(ba.bytes==retained);
 ieee80211_rxba_offload_retire(&ba,&out);
 CHECK(out.discard==second && ba.bytes==0);off_clear(&out);
 CHECK(rxba_kmem_live==live);
}
