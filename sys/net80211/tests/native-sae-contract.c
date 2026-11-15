/* SPDX-License-Identifier: BSD-2-Clause */
/* Only kernel services/cipher execution are fixtures; owner and BIP are real. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef unsigned u_int;
typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint64_t u_int64_t;
#ifndef SAE_TEST_NO_ATOMIC64
#define __HAVE_ATOMIC64_LOADSTORE 1
#endif
#define atomic_load_relaxed(p) (*(p))
#define atomic_store_relaxed(p,v) (*(p)=(v))
#define IEEE80211_ADDR_LEN 6
#define IEEE80211_KEYBUF_SIZE 16
#define IEEE80211_MICBUF_SIZE 16
#define IEEE80211_KEYIX_NONE 65535
#define IEEE80211_WEP_NKID 4
#define IEEE80211_KEY_XMIT 1
#define IEEE80211_KEY_RECV 2
#define IEEE80211_KEY_GROUP 4
#define IEEE80211_KEY_SWCRYPT 16
#define IEEE80211_KEY_COMMON 7
#define IEEE80211_CIPHER_AES_CCM 3
#define IEEE80211_C_AES_CCM 8
#define IEEE80211_F_WPA 3
#define IEEE80211_F_WPA2 2
#define IEEE80211_F_PRIVACY 16
#define IEEE80211_NWID_LEN 32
#define IEEE80211_TRANS_WAIT 5
#define IEEE80211_FC0_TYPE_MASK 12
#define IEEE80211_FC0_TYPE_MGT 0
#define IEEE80211_FC0_TYPE_DATA 8
#define IEEE80211_FC0_SUBTYPE_NODATA 0x40
#define IEEE80211_FC1_DIR_MASK 3
#define IEEE80211_FC1_MORE_FRAG 4
#define IEEE80211_FC1_WEP 64
#define IEEE80211_FC1_ORDER 128
#define IEEE80211_M_STA 1
#define IEEE80211_NODE_AUTH 1
#define IEEE80211_ADDR_EQ(a,b) (memcmp(a,b,6)==0)
#define IEEE80211_ADDR_COPY(a,b) memcpy(a,b,6)
#define IEEE80211_IS_MULTICAST(a) ((a)[0]&1)
#define KM_SLEEP 0x00000001
#define KM_NOSLEEP 0x00000002
#define M_DONTWAIT 0
#define M_NOWAIT 0
#define MT_DATA 0
#define MT_HEADER 0
#define M_PKTHDR 2
#define MHLEN 256
#define MCLBYTES 2048
#define roundup(n,a) (((n)+(a)-1)/(a)*(a))
#define IASSERT(c,args) assert(c)
#define IEEE80211_DPRINTF(ic,...) test_debug(ic)
#define M_EXT 1
#define IFQ_MAXLEN 8
#define RTM_IEEE80211_SAE 109
#define MIN(a,b) ((a)<(b)?(a):(b))
#define mtod(m,t) ((t)(m)->m_data)
enum ieee80211_state {IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH,
    IEEE80211_S_ASSOC, IEEE80211_S_RUN};
/* ABI */
#include "ieee80211_rsn.h"
#include "ieee80211_bip.h"
struct ieee80211com;
struct ieee80211_key;
struct mbuf;
struct ieee80211_cipher {
    unsigned ic_cipher;
    void *(*ic_attach)(struct ieee80211com *, struct ieee80211_key *);
    int (*ic_setkey)(struct ieee80211_key *);
    void (*ic_detach)(struct ieee80211_key *);
    unsigned ic_header;
    int (*ic_encap)(struct ieee80211_key *,struct mbuf *,uint8_t);
};
struct ieee80211_key {
    unsigned wk_keylen, wk_flags, wk_keyix, wk_rxkeyix;
    uint64_t wk_keyrsc, wk_keytsc;
    uint8_t wk_key[32];
    const struct ieee80211_cipher *wk_cipher;
    void *wk_private;
};
struct ieee80211_node { uint16_t ni_rxseqs[17]; uint8_t ni_bssid[6], ni_essid[32], ni_esslen;
    unsigned ni_txseqs[1], ni_flags, ni_inact_reload; struct ieee80211com *ni_ic; struct ieee80211_key ni_ucastkey; };
struct ieee80211_frame { uint8_t i_fc[2], i_dur[2], i_addr1[6], rest[14]; };
struct m_tag { struct m_tag *next; unsigned m_tag_id, m_tag_len; };
struct mbuf { uint8_t storage[2048], *m_data; int m_len, m_flags;
    struct { int len; } m_pkthdr; bool readonly; struct m_tag *tags;
    struct ieee80211_node *node; struct mbuf *next; };
struct ifqueue { int ifq_len, ifq_maxlen; struct mbuf *head; };
struct ifnet { int if_timer; };
struct ieee80211com { struct ieee80211_sae *ic_sae; uint64_t ic_sae_serial;
    struct ieee80211_node *ic_bss; struct ifnet *ic_ifp;
    struct ifqueue ic_mgtq; unsigned ic_caps, ic_flags, ic_mgt_timer;
    int ic_opmode; unsigned ic_inact_run; enum ieee80211_state ic_state;
    uint8_t ic_myaddr[6], *ic_opt_ie; size_t ic_opt_ie_len;
    struct ieee80211_key ic_nw_keys[4]; unsigned ic_def_txkey;
    struct { unsigned is_tx_nodefkey; } ic_stats; };
struct aesenc { uint8_t key[16]; };
static void test_debug(const struct ieee80211com *ic){(void)ic;}
static unsigned checks, setkeys, fences, unfences, events;
static uint16_t last_event;
static bool fail_alloc, fail_cipher, reject_crypto, cipher_present = true;
static uint16_t le16dec(const void *v) { const uint8_t *p=v; return p[0]|(uint16_t)p[1]<<8; }
static void le16enc(void *v, uint16_t n) { uint8_t *p=v; p[0]=n; p[1]=n>>8; }
static void explicit_memset(void *v, int c, size_t n) { volatile uint8_t *p=v; while(n--) *p++=c; }
static void *kmem_zalloc(size_t n, int f) {
    assert(n != 0 && (f == KM_SLEEP || f == KM_NOSLEEP));
    if (f == KM_NOSLEEP && fail_alloc)
        return NULL;
    void *p = calloc(1, n);
    /* kmem(9): KM_SLEEP cannot fail. Host OOM cannot model a kernel return. */
    if (f == KM_SLEEP && p == NULL)
        abort();
    return p;
}
static void kmem_free(void *p,size_t n) { (void)n; free(p); }
static struct m_tag *m_tag_find(struct mbuf *m, unsigned id) {
    for (struct m_tag *t = m->tags; t; t = t->next)
        if (t->m_tag_id == id)
            return t;
    return NULL;
}
static struct m_tag *m_tag_get(unsigned id, size_t n, int f) {
    (void)f;
    struct m_tag *t = fail_alloc ? NULL : calloc(1, sizeof(*t) + n);
    if (t) {
        t->m_tag_id = id;
        t->m_tag_len = n;
    }
    return t;
}
static void m_tag_prepend(struct mbuf *m,struct m_tag *t){t->next=m->tags;m->tags=t;}
static struct mbuf *mbuf_new(void) {
    struct mbuf *m = fail_alloc ? NULL : calloc(1, sizeof(*m));
    if (m) {
        m->m_data = m->storage;
        m->m_flags = M_PKTHDR;
    }
    return m;
}
#define MGETHDR(m,f,t) ((m)=mbuf_new())
#define MCLGET(m,f) ((m)->m_flags|=M_EXT)
#define M_SETCTX(m,n) ((m)->node=(n))
static void enqueue(struct ifqueue *q,struct mbuf *m){m->next=q->head;q->head=m;q->ifq_len++;}
#define IF_ENQUEUE(q,m) enqueue(q,m)
static void m_copydata(struct mbuf *m,size_t off,size_t n,void *p){assert(off+n<=(size_t)m->m_pkthdr.len);memcpy(p,m->m_data+off,n);}
static void m_adj(struct mbuf *m,int n){assert(n<0);m->m_len+=n;m->m_pkthdr.len+=n;}
static void m_freem(struct mbuf *m) {
    if (!m)
        return;
    while (m->tags) {
        struct m_tag *t = m->tags;
        m->tags = t->next;
        free(t);
    }
    free(m);
}
static void if_start_lock(struct ifnet *p){(void)p;}
static struct ieee80211_node *ieee80211_ref_node(struct ieee80211_node *n){return n;}
bool ieee80211_sae_port_allowed(const struct ieee80211com *);
/* NODE AUTHORIZATION */
static void ieee80211_key_update_begin(struct ieee80211com *ic){(void)ic;fences++;}
static void ieee80211_key_update_end(struct ieee80211com *ic){(void)ic;unfences++;}
static void *cipher_attach(struct ieee80211com *ic,struct ieee80211_key *k){(void)ic;(void)k;return fail_alloc?NULL:calloc(1,8);}
static int cipher_setkey(struct ieee80211_key *k){(void)k;setkeys++;return !fail_cipher;}
static void cipher_detach(struct ieee80211_key *k){free(k->wk_private);}
static unsigned encryptions;
static int cipher_encap(struct ieee80211_key *k,struct mbuf *m,uint8_t id)
{ (void)k;assert(id==0 && m->m_len>=32);encryptions++;return 1; }
static const struct ieee80211_cipher ieee80211_cipher_ccmp={3,cipher_attach,cipher_setkey,cipher_detach,8,cipher_encap};
static const struct ieee80211_cipher *ieee80211_crypto_cipher(unsigned c){return c==3&&cipher_present?&ieee80211_cipher_ccmp:NULL;}
static const struct ieee80211_cipher none={6,NULL,NULL,NULL,0,NULL};
static int ieee80211_hdrspace(struct ieee80211com *ic,void *p){(void)ic;(void)p;return 24;}
void ieee80211_sae_key_changed(struct ieee80211com *,const struct ieee80211_key *);
static int ieee80211_crypto_delkey(struct ieee80211com *ic, struct ieee80211_key *k) {
    ieee80211_sae_key_changed(ic, k);
    if (k->wk_private)
        cipher_detach(k);
    memset(k, 0, sizeof(*k));
    k->wk_cipher = &none;
    return 1;
}
static void ieee80211_crypto_delglobalkeys(struct ieee80211com *ic){for(unsigned i=0;i<4;i++)ieee80211_crypto_delkey(ic,&ic->ic_nw_keys[i]);}
int ieee80211_sae_state(struct ieee80211com *,int);
static int ieee80211_new_state(struct ieee80211com *ic, enum ieee80211_state n, int arg) {
    (void)arg;
    int e = ieee80211_sae_state(ic, n);
    if (!e)
        ic->ic_state = n;
    return e;
}
static void rt_ieee80211msg(struct ifnet *p,int type,const void *buf,size_t n){
    (void)p;assert(type==109);const struct ieee80211req_sae *e=buf;
    assert(n==offsetof(struct ieee80211req_sae,data)+e->len);last_event=e->op;events++;}
/* Deterministic block-service fixture; BIP KATs use actual AES separately. */
static void aes_setenckey128(struct aesenc *a,const uint8_t k[16]){memcpy(a->key,k,16);}
static void aes_enc(const void *ctx,const uint8_t in[16],uint8_t out[16],int rounds){
    const struct aesenc *a=ctx;assert(rounds==10);
    for(unsigned i=0;i<16;i++)out[i]=(uint8_t)(in[(i+1)%16]^a->key[i]^0x63);}
#include "ieee80211_sae.h"
static struct ieee80211_key *ieee80211_crypto_decap(struct ieee80211com *ic,
    struct ieee80211_node *ni,struct mbuf **mp,int hdr){
    (void)ic;struct mbuf *m=*mp;assert(hdr==24);if(reject_crypto)return NULL;
    int strip=ieee80211_sae_rx_status(ic,&ni->ni_ucastkey,m)==1?8:16;
    memmove(m->m_data+24,m->m_data+32,m->m_pkthdr.len-24-strip);
    m->m_len-=strip;m->m_pkthdr.len-=strip;return &ni->ni_ucastkey;}
/* AUTH VALIDATOR */
#include "ieee80211_sae.h"
/* IMPLEMENTATION */
#define IEEE80211_SEQ_SEQ_SHIFT 4
#define IEEE80211_FC1_RETRY 8
/* ACTUAL DUPLICATE */
struct ieee80211_qosframe { uint8_t header[24], i_qos[2]; };
#define ieee80211_has_qos(wh) (((wh)->i_fc[0]&0x8c)==0x88)
#define IEEE80211_QOS_AMSDU 0x80
/* ACTUAL AMSDU SELECTION */
#define ieee80211_cipher_none none
#define M_LEADINGSPACE(m) ((m)->readonly ? 0 : (m)->m_data-(m)->storage)
static unsigned prepend_replacements,pullup_replacements;
static struct mbuf *m_gethdr(int how,int type){(void)how;(void)type;return mbuf_new();}
static struct mbuf *m_getcl(int how, int type, int flags) {
    (void)how;
    (void)type;
    struct mbuf *m = mbuf_new();
    if (m)
        m->m_flags = flags | M_EXT;
    return m;
}
static void m_align(struct mbuf *m,unsigned len)
{ assert(len<=MHLEN);m->m_data=m->storage+((MHLEN-len)&~7U); }
static struct mbuf *m_prepend(struct mbuf *m,unsigned len,int how)
{ (void)len;(void)how;prepend_replacements++;m_freem(m);return NULL; }
static struct mbuf *m_pullup(struct mbuf *m,int len)
{ (void)len;pullup_replacements++;m_freem(m);return NULL; }
/* ACTUAL TX ALLOCATION */
static void check(bool v,const char *name,unsigned line){checks++;if(!v){fprintf(stderr,"FAIL %u: %s\n",line,name);exit(1);}}
#define CHECK(v) check((v),#v,__LINE__)
static uint8_t selected_rsn[]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,8,0xc0,0};
static struct ieee80211req_sae request(struct ieee80211com *ic,unsigned op,size_t len){
    struct ieee80211req_sae r={0};r.version=1;r.generation=ic->ic_sae->generation;r.op=op;r.len=len;
    memcpy(r.bssid,ic->ic_sae->bssid,6);return r;}
static int config(struct ieee80211com *ic,uint32_t generation){
    struct ieee80211req_sae r=request(ic,1,1);r.generation=generation;r.data[0]=1;
    memcpy(r.bssid,ic->ic_bss->ni_bssid,6);return ieee80211_sae_request(ic,&r);}
static void connect(struct ieee80211com *ic,uint32_t generation){
    CHECK(config(ic,generation)==0);CHECK(ieee80211_new_state(ic,IEEE80211_S_AUTH,0)==0);
    CHECK(last_event==IEEE80211_SAE_START);struct ieee80211req_sae r=request(ic,2,2);
    CHECK(ieee80211_sae_request(ic,&r)==0);CHECK(ic->ic_state==IEEE80211_S_ASSOC);
    CHECK(ieee80211_new_state(ic,IEEE80211_S_RUN,0)==0);CHECK(!(ic->ic_bss->ni_flags & IEEE80211_NODE_AUTH));}
static int install(struct ieee80211com *ic,unsigned id,uint8_t material){
    assert(ic->ic_sae != NULL);
    struct ieee80211req_key k={0};k.ik_type=3;k.ik_keyix=id;k.ik_keylen=16;
    k.ik_flags=id==65535?3:7;memcpy(k.ik_macaddr,ic->ic_sae->bssid,6);memset(k.ik_keydata,material,16);
    struct ieee80211req_sae r=request(ic,7,sizeof(k));memcpy(r.data,&k,sizeof(k));return ieee80211_sae_request(ic,&r);}
static struct mbuf *frame(const struct ieee80211com *ic, uint8_t type, size_t len) {
    assert(ic->ic_sae != NULL);
    struct mbuf *m = mbuf_new();
    if (m == NULL)
        abort();
    m->m_len = m->m_pkthdr.len = len;
    m->m_data[0] = type;
    struct ieee80211_frame *header = mtod(m, struct ieee80211_frame *);
    memset(header->i_dur, 0, sizeof(header->i_dur));
    memset(header->rest, 0, sizeof(header->rest));
    memcpy(m->m_data + 4, ic->ic_myaddr, 6);
    memcpy(m->m_data + 10, ic->ic_sae->bssid, 6);
    memcpy(m->m_data + 16, ic->ic_sae->bssid, 6);
    return m;
}
static void bip_mic(struct mbuf *m,const uint8_t key[16]){
    uint8_t aad[20],mic[16];struct aesenc a;size_t len=m->m_pkthdr.len;
    aad[0]=m->m_data[0];aad[1]=m->m_data[1]&~0x38;memcpy(aad+2,m->m_data+4,18);
    memset(m->m_data+len-8,0,8);const struct ieee80211_bip_part parts[]={{aad,20},{m->m_data+24,len-24}};
    aes_setenckey128(&a,key);CHECK(ieee80211_bip_cmac(sae_aes,&a,parts,2,mic));memcpy(m->m_data+len-8,mic,8);}
static bool key_counters(const struct ieee80211_key *key,uint64_t tx,uint64_t rx)
{ return key->wk_keytsc==tx && key->wk_keyrsc==rx; }
static unsigned hw_calls, hw_fences;
static int hw_error;
static bool hw_retire;
static void hw_fence(struct ieee80211com *ic) { (void)ic; hw_fences++; }
static int hw_replace(struct ieee80211com *ic, uint64_t epoch,
    const struct ieee80211_sae_hw_key *keys, size_t count)
{
    CHECK(epoch == ieee80211_sae_serial(ic));
    CHECK(count <= 7);
    for (size_t i = 0; i < count; i++) {
        CHECK(keys[i].cipher == ((keys[i].id == 4 || keys[i].id == 5) ?
            IEEE80211_SAE_HW_BIP : IEEE80211_CIPHER_AES_CCM));
        CHECK(keys[i].instance != 0 && keys[i].instance <= epoch);
    }
    struct mbuf *m = frame(ic, 0xc0, 40);
    CHECK(ieee80211_sae_tag_tx(ic, ic->ic_bss, m) != 0);
    CHECK(!ieee80211_sae_tx_current(ic, ic->ic_bss, m));
    m_freem(m);
    hw_calls++;
    if (hw_retire) ieee80211_sae_retire(ic);
    return hw_error;
}
static const struct ieee80211_sae_hw_ops hw_ops = {
    .fence = hw_fence, .replace = hw_replace, .flags = IEEE80211_SAE_HW_NO_QOS_TX
};
static void hardware_contract(struct ieee80211com *ic)
{
    ieee80211_sae_detach(ic);
    CHECK(ieee80211_sae_attach_hw(ic, &hw_ops) == 0);
    CHECK(ieee80211_sae_qos_tx_allowed(ic));
    connect(ic, 100);
    CHECK(!ieee80211_sae_qos_tx_allowed(ic));
    CHECK(!ic->ic_sae->hw_blocked);
    hw_calls = 0;
    CHECK(install(ic, 65535, 0x71) == 0);
    CHECK(hw_calls == 1 && hw_fences > 0);
    uint64_t epoch = ieee80211_sae_serial(ic);
    CHECK(install(ic, 65535, 0x71) == 0 && hw_calls == 1);
    struct mbuf *m = frame(ic, 0xc0, 40);
    CHECK(ieee80211_sae_rx_status(ic, &ic->ic_bss->ni_ucastkey, m) == -1);
    CHECK(ieee80211_sae_tag_rx(ic, m, epoch, 65535, 3, 6) == EACCES);
    CHECK(ieee80211_sae_tag_rx(ic, m, epoch, 65535, 2, 7) == EACCES);
    CHECK(ieee80211_sae_tag_rx(ic, m, epoch-1, 65535, 3, 7) == EACCES);
    fail_alloc = true;
    CHECK(ieee80211_sae_tag_rx(ic, m, epoch, 65535, 3, 7) == ENOMEM);
    fail_alloc = false;
    CHECK(ieee80211_sae_tag_rx(ic, m, epoch, 65535, 3,
        IEEE80211_SAE_RX_AUTHENTICATED | IEEE80211_SAE_RX_IV_PRESENT |
        IEEE80211_SAE_RX_MIC_STRIPPED) == 0);
    CHECK(ieee80211_sae_rx_status(ic, &ic->ic_bss->ni_ucastkey, m) == 1);
    CHECK(ieee80211_sae_rx_status(ic, &ic->ic_nw_keys[0], m) == -1);
    struct mbuf *aggregate = frame(ic, 0x88, 60);
    CHECK(ieee80211_sae_tag_rx_amsdu(ic, aggregate, epoch, 65535, 3, 7, 0, false) == EINVAL);
    CHECK(ieee80211_sae_tag_rx_amsdu(ic, aggregate, epoch, 65535, 3, 7, 128, false) == EINVAL);
    CHECK(ieee80211_sae_tag_rx_amsdu(ic, aggregate, epoch, 65535, 3, 7, 1, true) == 0);
    uint64_t aggregate_epoch = 0; unsigned aggregate_index = 0; bool aggregate_last = false;
    CHECK(ieee80211_sae_rx_amsdu(ic, &ic->ic_bss->ni_ucastkey, aggregate,
        &aggregate_epoch, &aggregate_index, &aggregate_last));
    CHECK(aggregate_epoch == epoch && aggregate_index == 1 && aggregate_last);
    aggregate->m_data[1] = 64; aggregate->m_data[24] = 0x80;
    CHECK(!input_amsdu_selection(ic, ic->ic_bss, aggregate));
    aggregate->m_data[1] = 0;
    CHECK(input_amsdu_selection(ic, ic->ic_bss, aggregate));
    ic->ic_bss->ni_rxseqs[8] = 0x100;
    aggregate->m_data[1] = 72;
    CHECK(ieee80211_rx_duplicate(ic, ic->ic_bss, aggregate, 8, 0x100));
    m_freem(aggregate);
    aggregate = frame(ic, 0x88, 60); aggregate->m_data[1] = 72;
    CHECK(ieee80211_sae_tag_rx_amsdu(ic, aggregate, epoch, 65535, 3, 7, 2, true) == 0);
    CHECK(!ieee80211_rx_duplicate(ic, ic->ic_bss, aggregate, 8, 0x100));
    aggregate->m_data[1] = 8;
    CHECK(ieee80211_rx_duplicate(ic, ic->ic_bss, aggregate, 8, 0x100));
    aggregate->m_data[1] = 72;
    ((struct sae_rx_packet *)(m_tag_find(aggregate, SAE_RX_TAG)+1))->epoch--;
    CHECK(ieee80211_rx_duplicate(ic, ic->ic_bss, aggregate, 8, 0x100));
    m_freem(aggregate);
    struct ieee80211req_sae r = request(ic, IEEE80211_SAE_SET_IGTK, 24);
    r.data[0] = 4; memset(r.data+8, 0x55, 16);
    CHECK(ieee80211_sae_request(ic, &r) == 0);
    r = request(ic, IEEE80211_SAE_SET_PORT, 1); r.data[0] = 1;
    CHECK(ieee80211_sae_request(ic, &r) == 0);
    CHECK(ieee80211_sae_port_allowed(ic));
    unsigned prior_fences = hw_fences;
    r.data[0] = 0;
    CHECK(ieee80211_sae_request(ic, &r) == 0);
    CHECK(hw_fences > prior_fences);
    r.data[0] = 1;
    CHECK(ieee80211_sae_request(ic, &r) == 0);
    CHECK(ieee80211_sae_port_allowed(ic));
    r = request(ic, IEEE80211_SAE_DELETE_IGTK, 2); r.data[0] = 4;
    CHECK(ieee80211_sae_request(ic, &r) == 0);
    CHECK(ic->ic_sae->hw_epoch == ieee80211_sae_serial(ic));
    CHECK(install(ic, 1, 0x33) == 0);
    struct mbuf *group = frame(ic, 0x08, 60); group->m_data[4] = 1;
    CHECK(ieee80211_sae_tag_rx(ic, group, ieee80211_sae_serial(ic), 1, 3, 7) == 0);
    CHECK(ieee80211_sae_rx_status(ic, &ic->ic_nw_keys[1], group) == 1);
    CHECK(ieee80211_sae_rx_status(ic, &ic->ic_bss->ni_ucastkey, group) == -1);
    m_freem(group);
    CHECK(install(ic, 65535, 0x72) == 0);
    CHECK(ieee80211_sae_rx_status(ic, &ic->ic_bss->ni_ucastkey, m) == -1);
    m_freem(m);
    m = frame(ic, 0xd0, 36); m->m_data[1] = 64;
    m->m_data[32] = 8; m->m_data[33] = 0;
    CHECK(ieee80211_sae_tag_rx(ic, m, ieee80211_sae_serial(ic), 65535, 3, 7) == 0);
    CHECK(ieee80211_sae_input(ic, ic->ic_bss, &m) == 1);
    CHECK(m->m_pkthdr.len == 28); m_freem(m);
    hw_error = EIO;
    CHECK(install(ic, 65535, 0x73) == EIO);
    CHECK(!ieee80211_sae_port_allowed(ic));
    hw_error = 0; hw_retire = true;
    CHECK(install(ic, 65535, 0x74) != 0);
    CHECK(ic->ic_sae->phase == SAE_RETIRED);
    hw_retire = false;
    connect(ic, 101); CHECK(install(ic, 65535, 0x75) == 0);
    m = frame(ic, 0xc0, 34); m->m_data[1] = 64; m->m_data[32] = 3;
    CHECK(ieee80211_sae_tag_rx(ic, m, ieee80211_sae_serial(ic), 65535, 3, 7) == 0);
    CHECK(ieee80211_sae_input(ic, ic->ic_bss, &m) == 1);
    CHECK(m->m_pkthdr.len == 26); m_freem(m);
    struct ieee80211req_sae off = request(ic, IEEE80211_SAE_CONFIGURE, 1);
    off.data[0] = 0;
    hw_error = EIO;
    CHECK(ieee80211_sae_request(ic, &off) == EIO);
    CHECK(ieee80211_sae_active(ic));
    hw_error = 0;
    unsigned before_disable = hw_calls;
    CHECK(ieee80211_sae_request(ic, &off) == 0);
    CHECK(hw_calls == before_disable + 1);
    CHECK(ieee80211_sae_qos_tx_allowed(ic));
    m = frame(ic, 0x08, 40);
    CHECK(ieee80211_sae_tx_current(ic, ic->ic_bss, m)); m_freem(m);
    for (unsigned operation = 0; operation < 3; operation++) {
        for (unsigned stale = 0; stale < 2; stale++) {
            connect(ic, 110 + operation * 2 + stale);
            CHECK(install(ic, 65535, 0x61) == 0);
            struct ieee80211req_sae change = request(ic, IEEE80211_SAE_SET_IGTK, 24);
            change.data[0] = 4; memset(change.data+8, 0x62, 16);
            CHECK(ieee80211_sae_request(ic, &change) == 0);
            if (operation == 0) {
                memset(change.data+8, 0x63, 16);
            } else if (operation == 1) {
                struct ieee80211req_del_key dk = {0};
                dk.idk_keyix = 255; memcpy(dk.idk_macaddr, ic->ic_sae->bssid, 6);
                change = request(ic, IEEE80211_SAE_DELETE_KEY, sizeof(dk));
                memcpy(change.data, &dk, sizeof(dk));
            } else {
                change = request(ic, IEEE80211_SAE_SET_PORT, 1); change.data[0] = 1;
                CHECK(ieee80211_sae_request(ic, &change) == 0);
                change.data[0] = 0; CHECK(ieee80211_sae_request(ic, &change) == 0);
                change.data[0] = 1;
            }
            hw_retire = stale != 0; hw_error = stale ? 0 : EIO;
            CHECK(ieee80211_sae_request(ic, &change) == (stale ? ESTALE : EIO));
            CHECK(!ieee80211_sae_port_allowed(ic) && ic->ic_sae->hw_blocked);
            CHECK(!ic->ic_sae->hw_reopen);
            hw_retire = false; hw_error = 0;
            ieee80211_sae_retire(ic);
        }
    }
    connect(ic, 120); CHECK(install(ic, 65535, 0x64) == 0);
    prior_fences = hw_fences;
    ieee80211_sae_key_changed(ic, &ic->ic_bss->ni_ucastkey);
    CHECK(hw_fences > prior_fences && ic->ic_sae->hw_blocked);
    ieee80211_sae_retire(ic);
}
/* Firmware model for a TX provider. It keeps one TX counter for each key
 * instance, as firmware does. A slot with an equal instance stays programmed. */
struct fw_slot { uint64_t instance, last, tx_pn; unsigned adds, removes; bool installed; };
static struct fw_slot fw[7];
static unsigned hwtx_calls;
static int hwtx_error;
static bool hwtx_retire;
static int hwtx_replace(struct ieee80211com *ic, uint64_t epoch,
    const struct ieee80211_sae_hw_key *keys, size_t count)
{
    uint64_t wanted[7] = {0};
    CHECK(epoch == ieee80211_sae_serial(ic));
    CHECK(count <= 7);
    hwtx_calls++;
    for (size_t i = 0; i < count; i++) {
        unsigned slot = keys[i].id == IEEE80211_SAE_HW_PTK ? 0 : keys[i].id + 1U;
        CHECK(slot < 7 && wanted[slot] == 0);
        CHECK(keys[i].instance != 0 && keys[i].instance <= epoch);
        wanted[slot] = keys[i].instance;
    }
    for (unsigned slot = 0; slot < 7; slot++)
        if (fw[slot].installed && fw[slot].instance != wanted[slot]) {
            fw[slot].installed = false;
            fw[slot].removes++;
        }
    if (hwtx_error != 0)
        return hwtx_error;
    for (unsigned slot = 0; slot < 7; slot++)
        if (wanted[slot] != 0 && !fw[slot].installed) {
            /* A new instance never reuses an earlier identity. */
            CHECK(wanted[slot] > fw[slot].last);
            fw[slot].instance = fw[slot].last = wanted[slot];
            fw[slot].tx_pn = 0;
            fw[slot].installed = true;
            fw[slot].adds++;
        }
    if (hwtx_retire) ieee80211_sae_retire(ic);
    return 0;
}
#define HWTX (IEEE80211_SAE_HW_KEEP_KEYS | IEEE80211_SAE_HW_TX_DATA | \
    IEEE80211_SAE_HW_TX_MGMT)
static const struct ieee80211_sae_hw_ops hwtx_ops = {
    .fence = hw_fence, .replace = hwtx_replace,
    .flags = HWTX | IEEE80211_SAE_HW_TX_BA
};
static const struct ieee80211_sae_hw_ops hwtx_noqos_ops = {
    .fence = hw_fence, .replace = hwtx_replace,
    .flags = HWTX | IEEE80211_SAE_HW_NO_QOS_TX
};
static struct ieee80211_sae_hw_ops hw_probe_ops = {
    .fence = hw_fence, .replace = hwtx_replace
};
/* A TX frame with crypto headroom. EAPOL selects the 802.1X LLC header. */
static struct mbuf *txframe(struct ieee80211com *ic, uint8_t fc0, uint8_t fc1,
    size_t len, bool eapol)
{
    assert(ic->ic_sae != NULL);
    struct mbuf *m = mbuf_new();
    if (m == NULL)
        abort();
    assert(len >= 24 && len <= sizeof(m->storage) - 32);
    m->m_data = m->storage + 32;
    m->m_len = m->m_pkthdr.len = len;
    memset(m->m_data, 0, len);
    m->m_data[0] = fc0;
    m->m_data[1] = fc1;
    memcpy(m->m_data + 4, ic->ic_sae->bssid, 6);
    memcpy(m->m_data + 10, ic->ic_myaddr, 6);
    memcpy(m->m_data + 16, ic->ic_sae->bssid, 6);
    if (eapol)
        memcpy(m->m_data + 24, "\xaa\xaa\x03\0\0\0\x88\x8e", 8);
    return m;
}
/* Tag a frame as common output does, then return the owner's disposition. */
static int tx_decision(struct ieee80211com *ic, uint8_t fc0, uint8_t fc1,
    size_t len, bool eapol)
{
    struct mbuf *m = txframe(ic, fc0, fc1, len, eapol);
    int result = -2;
    if (ieee80211_sae_tag_tx(ic, ic->ic_bss, m) == 0)
        result = ieee80211_sae_hw_tx(ic, ic->ic_bss, m);
    m_freem(m);
    return result;
}
static int set_port(struct ieee80211com *ic, bool open)
{
    struct ieee80211req_sae r = request(ic, IEEE80211_SAE_SET_PORT, 1);
    r.data[0] = open;
    return ieee80211_sae_request(ic, &r);
}
static int set_igtk(struct ieee80211com *ic, unsigned id, uint8_t material)
{
    struct ieee80211req_sae r = request(ic, IEEE80211_SAE_SET_IGTK, 24);
    r.data[0] = id; memset(r.data + 8, material, 16);
    return ieee80211_sae_request(ic, &r);
}
static int delete_ptk(struct ieee80211com *ic)
{
    struct ieee80211req_del_key dk = {0};
    dk.idk_keyix = 255; memcpy(dk.idk_macaddr, ic->ic_sae->bssid, 6);
    struct ieee80211req_sae r = request(ic, IEEE80211_SAE_DELETE_KEY, sizeof(dk));
    memcpy(r.data, &dk, sizeof(dk));
    return ieee80211_sae_request(ic, &r);
}
static bool material_spent(const struct ieee80211com *ic, uint8_t material)
{
    uint8_t key[16], check[16];
    memset(key, material, sizeof(key));
    sae_key_check(key, check);
    return sae_check_spent(ic->ic_sae, check);
}
static void hardware_tx_contract(struct ieee80211com *ic)
{
    struct ieee80211_key *ptk = &ic->ic_bss->ni_ucastkey;
    ieee80211_sae_detach(ic);
    /* One authority allocates the pairwise TX PN, retained keys are a
     * prerequisite, and QoS queues need protected firmware Block Ack. */
    const unsigned invalid[] = {
        IEEE80211_SAE_HW_TX_DATA, IEEE80211_SAE_HW_TX_MGMT,
        IEEE80211_SAE_HW_TX_DATA | IEEE80211_SAE_HW_TX_MGMT | IEEE80211_SAE_HW_TX_BA,
        IEEE80211_SAE_HW_KEEP_KEYS | IEEE80211_SAE_HW_TX_DATA | IEEE80211_SAE_HW_TX_BA,
        IEEE80211_SAE_HW_KEEP_KEYS | IEEE80211_SAE_HW_TX_MGMT | IEEE80211_SAE_HW_TX_BA,
        IEEE80211_SAE_HW_KEEP_KEYS | IEEE80211_SAE_HW_TX_BA,
        IEEE80211_SAE_HW_TX_BA, HWTX,
        HWTX | IEEE80211_SAE_HW_TX_BA | IEEE80211_SAE_HW_NO_QOS_TX,
        HWTX | IEEE80211_SAE_HW_TX_BA | 0x20, 0x20 };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        hw_probe_ops.flags = invalid[i];
        CHECK(ieee80211_sae_attach_hw(ic, &hw_probe_ops) == EINVAL && ic->ic_sae == NULL);
    }
    hw_probe_ops.flags = IEEE80211_SAE_HW_KEEP_KEYS;
    CHECK(ieee80211_sae_attach_hw(ic, &hw_probe_ops) == 0);
    ieee80211_sae_detach(ic);
    CHECK(ieee80211_sae_attach_hw(ic, &hwtx_ops) == 0);
    memset(fw, 0, sizeof(fw)); hwtx_calls = 0;
    CHECK(ieee80211_sae_agg_tx_allowed(ic));
    connect(ic, 200);
    CHECK(ieee80211_sae_qos_tx_allowed(ic));
    CHECK(!ieee80211_sae_agg_tx_allowed(ic));
    /* Before the pairwise key only EAPOL and non-robust management are plain. */
    CHECK(tx_decision(ic, 0x08, 1, 60, true) == 0);
    CHECK(tx_decision(ic, 0x08, 1, 60, false) == -1);
    CHECK(tx_decision(ic, 0x08, 0x41, 60, false) == -1);
    CHECK(install(ic, 65535, 0x81) == 0);
    CHECK(fw[0].installed && fw[0].adds == 1 && fw[0].removes == 0);
    uint64_t first = fw[0].instance;
    CHECK(first != 0 && first == ieee80211_sae_serial(ic));
    CHECK(!ieee80211_sae_agg_tx_allowed(ic));
    fw[0].tx_pn = 1000;
    /* IGTK and GTK changes keep the programmed pairwise instance. */
    CHECK(set_igtk(ic, 4, 0x55) == 0);
    CHECK(fw[5].installed && fw[5].instance > first);
    CHECK(fw[0].installed && fw[0].instance == first && fw[0].adds == 1 &&
        fw[0].removes == 0 && fw[0].tx_pn == 1000);
    CHECK(set_port(ic, true) == 0);
    CHECK(ieee80211_sae_port_allowed(ic) && ieee80211_sae_agg_tx_allowed(ic));
    CHECK(install(ic, 1, 0x33) == 0 && fw[2].installed);
    fw[0].tx_pn = 2000;
    CHECK(install(ic, 1, 0x35) == 0 && fw[2].adds == 2 && fw[2].removes == 1);
    CHECK(install(ic, 2, 0x36) == 0 && fw[3].installed);
    CHECK(set_igtk(ic, 5, 0x56) == 0 && fw[6].installed);
    CHECK(set_igtk(ic, 4, 0x57) == 0 && fw[5].adds == 2);
    CHECK(fw[0].installed && fw[0].instance == first && fw[0].adds == 1 &&
        fw[0].removes == 0 && fw[0].tx_pn == 2000);
    CHECK(ieee80211_sae_port_allowed(ic) && ieee80211_sae_agg_tx_allowed(ic));
    /* Firmware owns pairwise TX: the owner selects it for each frame. */
    CHECK(tx_decision(ic, 0x08, 0x41, 60, false) == 1);
    CHECK(tx_decision(ic, 0x88, 0x41, 60, false) == 1);
    CHECK(tx_decision(ic, 0x08, 0x41, 60, true) == 1);
    /* With the pairwise key in place plain EAPOL is not transmitted. */
    CHECK(tx_decision(ic, 0x08, 1, 60, true) == -1);
    CHECK(tx_decision(ic, 0x08, 1, 60, false) == -1);
    CHECK(tx_decision(ic, 0x48, 1, 24, false) == 0);
    CHECK(tx_decision(ic, 0xa4, 0, 24, false) == 0);
    CHECK(tx_decision(ic, 0xc0, 0, 26, false) == 1);
    CHECK(tx_decision(ic, 0x40, 0, 40, false) == 0);
    struct mbuf *m = txframe(ic, 0xc0, 0, 26, false);
    CHECK(ieee80211_sae_tag_tx(ic, ic->ic_bss, m) == 0);
    CHECK(m->m_data[1] & IEEE80211_FC1_WEP);
    /* The protected disposition needs the run phase, the key and no fence. */
    CHECK(ieee80211_sae_hw_tx(ic, ic->ic_bss, m) == 1);
    ic->ic_sae->phase = SAE_ASSOC;
    CHECK(ieee80211_sae_hw_tx(ic, ic->ic_bss, m) == -1);
    ic->ic_sae->phase = SAE_RUN;
    ptk->wk_keylen = 0;
    CHECK(ieee80211_sae_hw_tx(ic, ic->ic_bss, m) == -1);
    ptk->wk_keylen = 16;
    ic->ic_sae->hw_blocked = true;
    CHECK(ieee80211_sae_hw_tx(ic, ic->ic_bss, m) == -1);
    ic->ic_sae->hw_blocked = false;
    CHECK(ieee80211_sae_hw_tx(ic, ic->ic_bss, m) == 1);
    /* Software CCMP cannot use the pairwise key: one TX PN owner. */
    unsigned encrypted = encryptions;
    CHECK(ieee80211_crypto_encap(ic, ic->ic_bss, m) == NULL);
    CHECK(encryptions == encrypted && key_counters(ptk, 0, 0));
    /* A robust frame that lost its Protected bit is not transmitted. */
    m->m_data[1] &= ~IEEE80211_FC1_WEP;
    CHECK(ieee80211_sae_hw_tx(ic, ic->ic_bss, m) == -1);
    m_freem(m);
    m = txframe(ic, 0x08, 0x41, 60, false);
    CHECK(ieee80211_sae_tag_tx(ic, ic->ic_bss, m) == 0);
    CHECK(ieee80211_crypto_encap(ic, ic->ic_bss, m) == NULL);
    CHECK(encryptions == encrypted && !prepend_replacements && !pullup_replacements);
    m_freem(m);
    /* Without a programmed pairwise instance firmware cannot encrypt. */
    ic->ic_sae->instance[0] = 0;
    CHECK(tx_decision(ic, 0x08, 0x41, 60, false) == -1);
    CHECK(!ieee80211_sae_agg_tx_allowed(ic));
    ic->ic_sae->instance[0] = first;
    CHECK(tx_decision(ic, 0x08, 0x41, 60, false) == 1);
    /* Port close and reopen republish the set without touching any key. */
    unsigned calls = hwtx_calls;
    CHECK(set_port(ic, false) == 0);
    CHECK(!ieee80211_sae_agg_tx_allowed(ic));
    CHECK(tx_decision(ic, 0x08, 0x41, 60, false) < 0);
    CHECK(set_port(ic, true) == 0 && hwtx_calls == calls + 1);
    CHECK(ieee80211_sae_agg_tx_allowed(ic));
    for (unsigned slot = 0; slot < 7; slot++)
        CHECK(fw[slot].removes == (slot == 2 || slot == 5 ? 1U : 0U));
    CHECK(fw[0].adds == 1 && fw[0].instance == first && fw[0].tx_pn == 2000);
    /* The same pairwise key again is a no-op for owner and firmware. */
    calls = hwtx_calls;
    CHECK(install(ic, 65535, 0x81) == 0 && hwtx_calls == calls);
    /* A pairwise rekey creates a new instance with a new counter. */
    CHECK(ic->ic_sae->tx_count > 0);
    CHECK(install(ic, 65535, 0x82) == 0 && ic->ic_sae->tx_count == 0);
    CHECK(fw[0].installed && fw[0].removes == 1 && fw[0].adds == 2 &&
        fw[0].instance > first && fw[0].tx_pn == 0);
    CHECK(ieee80211_sae_port_allowed(ic) && ieee80211_sae_agg_tx_allowed(ic));
    uint64_t second = fw[0].instance;
    fw[0].tx_pn = 300;
    /* Retired pairwise material cannot become a new instance. */
    calls = hwtx_calls;
    CHECK(install(ic, 65535, 0x81) == EINVAL && hwtx_calls == calls);
    CHECK(fw[0].instance == second && fw[0].tx_pn == 300);
    CHECK(ieee80211_sae_port_allowed(ic));
    CHECK(delete_ptk(ic) == 0 && !fw[0].installed);
    CHECK(!ieee80211_sae_port_allowed(ic) && !ieee80211_sae_agg_tx_allowed(ic));
    CHECK(install(ic, 65535, 0x82) == EINVAL && !fw[0].installed);
    CHECK(install(ic, 65535, 0x81) == EINVAL && !fw[0].installed);
    CHECK(install(ic, 65535, 0x83) == 0 && fw[0].installed &&
        fw[0].instance > second && fw[0].tx_pn == 0);
    CHECK(set_port(ic, true) == 0 && ieee80211_sae_agg_tx_allowed(ic));
    /* A firmware error during a group rekey never opens the port. */
    hwtx_error = EIO;
    CHECK(install(ic, 1, 0x37) == EIO);
    CHECK(!ieee80211_sae_port_allowed(ic) && ic->ic_sae->hw_blocked);
    /* Firmware key state is uncertain: the owner retires the session. */
    CHECK(ic->ic_sae->phase == SAE_RETIRED && ptk->wk_keylen == 0);
    CHECK(material_spent(ic, 0x83));
    CHECK(!ieee80211_sae_agg_tx_allowed(ic));
    CHECK(set_port(ic, true) != 0 && !ieee80211_sae_port_allowed(ic));
    CHECK(tx_decision(ic, 0x08, 0x41, 60, false) < 0);
    CHECK(tx_decision(ic, 0x08, 1, 60, false) < 0);
    CHECK(tx_decision(ic, 0xc0, 0, 26, false) < 0);
    m = txframe(ic, 0x08, 0x41, 60, false);
    CHECK(ieee80211_sae_hw_tx(ic, ic->ic_bss, m) == -1);
    CHECK(ieee80211_crypto_encap(ic, ic->ic_bss, m) == NULL);
    m_freem(m);
    hwtx_error = 0;
    /* Firmware reset: the session ends and the lost counter is never reused. */
    ieee80211_sae_retire(ic); memset(fw, 0, sizeof(fw));
    CHECK(ptk->wk_keylen == 0 && !ieee80211_sae_port_allowed(ic));
    CHECK(!ieee80211_sae_agg_tx_allowed(ic));
    CHECK(install(ic, 65535, 0x84) == ESTALE && !fw[0].installed);
    CHECK(set_port(ic, true) == ESTALE);
    connect(ic, 201);
    CHECK(install(ic, 65535, 0x83) == EINVAL && !fw[0].installed);
    CHECK(install(ic, 65535, 0x91) == 0 && fw[0].installed && fw[0].tx_pn == 0);
    CHECK(set_igtk(ic, 4, 0x58) == 0 && set_port(ic, true) == 0);
    CHECK(ieee80211_sae_agg_tx_allowed(ic));
    fw[0].tx_pn = 500;
    CHECK(!material_spent(ic, 0x91) && ic->ic_sae->instance[0] != 0 &&
        ic->ic_sae->instance[5] != 0);
    ieee80211_sae_retire(ic); memset(fw, 0, sizeof(fw));
    CHECK(material_spent(ic, 0x91));
    for (unsigned slot = 0; slot < 7; slot++)
        CHECK(ic->ic_sae->instance[slot] == 0);
    connect(ic, 202);
    CHECK(install(ic, 65535, 0x91) == EINVAL && !fw[0].installed);
    /* A failed pairwise install leaves no usable key and a closed port.
     * Firmware may have used the key, so it is not accepted again. */
    hwtx_error = EIO;
    CHECK(install(ic, 65535, 0xa1) == EIO);
    CHECK(ptk->wk_keylen == 0 && ic->ic_sae->hw_blocked);
    CHECK(ic->ic_sae->phase == SAE_RETIRED);
    CHECK(set_igtk(ic, 4, 0x59) == ESTALE);
    CHECK(set_port(ic, true) != 0 && !ieee80211_sae_port_allowed(ic));
    CHECK(!ieee80211_sae_agg_tx_allowed(ic));
    CHECK(tx_decision(ic, 0x08, 0x41, 60, false) < 0);
    hwtx_error = 0;
    CHECK(install(ic, 65535, 0xa1) == ESTALE);
    connect(ic, 208);
    CHECK(install(ic, 65535, 0xa1) == EINVAL && !fw[0].installed);
    CHECK(install(ic, 65535, 0xa2) == 0 && fw[0].installed);
    CHECK(set_igtk(ic, 4, 0x59) == 0 && set_port(ic, true) == 0);
    CHECK(ieee80211_sae_agg_tx_allowed(ic));
    /* A key removed outside the owner also fences firmware and ends TX. */
    CHECK(install(ic, 1, 0x38) == 0 && ic->ic_sae->instance[2] != 0);
    ieee80211_sae_key_changed(ic, &ic->ic_nw_keys[1]);
    CHECK(ic->ic_sae->instance[2] == 0 && ic->ic_sae->instance[0] != 0);
    CHECK(!material_spent(ic, 0xa2));
    ieee80211_sae_key_changed(ic, ptk);
    CHECK(material_spent(ic, 0xa2) && ic->ic_sae->instance[0] == 0);
    CHECK(ic->ic_sae->hw_blocked && !ieee80211_sae_agg_tx_allowed(ic));
    /* The ended instance is not usable again, and the owner gives no
     * later set with it to the provider. */
    calls = hwtx_calls;
    CHECK(install(ic, 65535, 0xa2) == EINVAL);
    CHECK(set_igtk(ic, 5, 0x5d) == EINVAL && hwtx_calls == calls);
    CHECK(tx_decision(ic, 0x08, 0x41, 60, false) < 0);
    ieee80211_sae_retire(ic);
    connect(ic, 203);
    CHECK(install(ic, 65535, 0xa2) == EINVAL);
    ieee80211_sae_retire(ic);
    /* A failed pairwise rekey ends the old instance too. The owner never
     * gives a set with an ended instance to the provider. */
    connect(ic, 205);
    CHECK(install(ic, 65535, 0xb1) == 0 && set_igtk(ic, 4, 0x5b) == 0);
    CHECK(set_port(ic, true) == 0);
    hwtx_error = EIO;
    CHECK(install(ic, 65535, 0xb2) == EIO);
    hwtx_error = 0;
    CHECK(ic->ic_sae->phase == SAE_RETIRED && ptk->wk_keylen == 0);
    CHECK(ic->ic_sae->instance[0] == 0);
    CHECK(material_spent(ic, 0xb1) && material_spent(ic, 0xb2));
    calls = hwtx_calls;
    CHECK(install(ic, 65535, 0xb1) == ESTALE);
    CHECK(set_igtk(ic, 5, 0x5c) == ESTALE && hwtx_calls == calls);
    CHECK(set_port(ic, true) != 0 && !ieee80211_sae_port_allowed(ic));
    CHECK(ic->ic_sae->hw_blocked && !ieee80211_sae_agg_tx_allowed(ic));
    connect(ic, 209);
    CHECK(install(ic, 65535, 0xb1) == EINVAL && install(ic, 65535, 0xb2) == EINVAL);
    ieee80211_sae_retire(ic);
    /* A failed pairwise removal also ends the instance. */
    connect(ic, 206);
    CHECK(install(ic, 65535, 0xb3) == 0);
    hwtx_error = EIO;
    CHECK(delete_ptk(ic) == EIO);
    hwtx_error = 0;
    CHECK(material_spent(ic, 0xb3) && ic->ic_sae->instance[0] == 0);
    CHECK(ic->ic_sae->phase == SAE_RETIRED && install(ic, 65535, 0xb3) == ESTALE);
    connect(ic, 211);
    CHECK(install(ic, 65535, 0xb3) == EINVAL);
    ieee80211_sae_retire(ic);
    /* The session ended while firmware programmed a new pairwise key. */
    connect(ic, 207);
    hwtx_retire = true;
    CHECK(install(ic, 65535, 0xb4) == ESTALE);
    hwtx_retire = false;
    CHECK(material_spent(ic, 0xb4) && ptk->wk_keylen == 0);
    memset(fw, 0, sizeof(fw));
    /* The owner remembers the last eight pairwise keys that firmware used. */
    for (unsigned i = 0; i < 9; i++) {
        connect(ic, 240 + i);
        CHECK(install(ic, 65535, 0xc0 + i) == 0);
        ieee80211_sae_retire(ic); memset(fw, 0, sizeof(fw));
    }
    CHECK(!material_spent(ic, 0xc0));
    for (unsigned i = 1; i < 9; i++)
        CHECK(material_spent(ic, 0xc0 + i));
    /* A repeated disable does not give keys of another mode to firmware. */
    connect(ic, 260);
    struct ieee80211req_sae off = request(ic, IEEE80211_SAE_CONFIGURE, 1);
    off.data[0] = 0;
    calls = hwtx_calls;
    CHECK(ieee80211_sae_request(ic, &off) == 0 && hwtx_calls == calls + 1);
    CHECK(!ieee80211_sae_active(ic));
    ic->ic_nw_keys[1].wk_keylen = 16;
    ic->ic_nw_keys[1].wk_cipher = &ieee80211_cipher_ccmp;
    CHECK(ieee80211_sae_request(ic, &off) == 0 && hwtx_calls == calls + 1);
    ic->ic_nw_keys[1].wk_keylen = 0;
    ic->ic_nw_keys[1].wk_cipher = &none;
    /* Firmware TX without protected firmware Block Ack keeps the non-QoS
     * path: no aggregation, and firmware still owns the pairwise TX PN. */
    ieee80211_sae_detach(ic);
    CHECK(ieee80211_sae_attach_hw(ic, &hwtx_noqos_ops) == 0);
    memset(fw, 0, sizeof(fw));
    connect(ic, 270);
    CHECK(install(ic, 65535, 0xd1) == 0 && set_igtk(ic, 4, 0x5e) == 0);
    CHECK(set_port(ic, true) == 0 && ieee80211_sae_port_allowed(ic));
    CHECK(!ieee80211_sae_qos_tx_allowed(ic) && !ieee80211_sae_agg_tx_allowed(ic));
    CHECK(tx_decision(ic, 0x08, 0x41, 60, false) == 1);
    m = txframe(ic, 0x08, 0x41, 60, false);
    CHECK(ieee80211_sae_tag_tx(ic, ic->ic_bss, m) == 0);
    CHECK(ieee80211_crypto_encap(ic, ic->ic_bss, m) == NULL);
    m_freem(m);
    /* The owner counts the frames of one instance and ends the session
     * before the firmware PN can wrap. */
    uint64_t counted = ic->ic_sae->tx_count;
    CHECK(tx_decision(ic, 0x08, 0x41, 60, false) == 1);
    CHECK(tx_decision(ic, 0x40, 0, 40, false) == 0);
    CHECK(ic->ic_sae->tx_count == counted + 1);
    ic->ic_sae->tx_count = SAE_HW_TX_LIMIT - 1;
    CHECK(tx_decision(ic, 0x08, 0x41, 60, false) == 1);
    CHECK(ieee80211_sae_port_allowed(ic));
    CHECK(tx_decision(ic, 0x08, 0x41, 60, false) == -1);
    CHECK(ic->ic_sae->phase == SAE_RETIRED && ptk->wk_keylen == 0);
    CHECK(material_spent(ic, 0xd1) && ic->ic_sae->tx_count == 0);
    /* A provider without firmware TX has no firmware disposition. It
     * keeps its behavior: a failed pairwise rekey can be done again. */
    ieee80211_sae_detach(ic);
    CHECK(ieee80211_sae_attach_hw(ic, &hw_ops) == 0);
    connect(ic, 204);
    CHECK(install(ic, 65535, 0xa2) == 0 && set_igtk(ic, 4, 0x5a) == 0);
    CHECK(set_port(ic, true) == 0 && !ieee80211_sae_agg_tx_allowed(ic));
    CHECK(tx_decision(ic, 0x08, 0x41, 60, false) == 0);
    CHECK(ic->ic_sae->tx_count == 0);
    hw_error = EIO;
    CHECK(install(ic, 65535, 0xa3) == EIO && ic->ic_sae->phase == SAE_RUN);
    hw_error = 0;
    CHECK(ptk->wk_keylen == 16 && !material_spent(ic, 0xa3));
    CHECK(install(ic, 65535, 0xa2) == 0);
    CHECK(set_igtk(ic, 5, 0x5f) == 0 && !ic->ic_sae->hw_blocked);
    CHECK(install(ic, 65535, 0xa3) == 0);
    ieee80211_sae_key_changed(ic, &ic->ic_nw_keys[1]);
    ieee80211_sae_retire(ic);
}
int main(void){
#ifdef SAE_TEST_NO_ATOMIC64
    struct ieee80211com unsupported={0};
    CHECK(ieee80211_sae_attach(&unsupported)==EOPNOTSUPP);
    CHECK(ieee80211_sae_serial(&unsupported)==0 && unsupported.ic_sae==NULL);
    puts("PASS: native SAE unavailable without atomic64 publication");return 0;
#endif
    struct ieee80211_node ni={.ni_bssid={2,0,0,0,0,1},.ni_essid={'a'},.ni_esslen=1};
    struct ifnet ifp={0};struct ieee80211com ic={.ic_bss=&ni,.ic_ifp=&ifp,.ic_opmode=1,
        .ic_flags=18,.ic_opt_ie=selected_rsn,.ic_opt_ie_len=sizeof(selected_rsn),.ic_myaddr={2,0,0,0,0,2}};
    ni.ni_ic=&ic;ni.ni_ucastkey.wk_cipher=&none;for(unsigned i=0;i<4;i++)ic.ic_nw_keys[i].wk_cipher=&none;
    /* NOSLEEP pressure must not manufacture a failed sleeping allocation. */
    fail_alloc=true;
    CHECK(kmem_zalloc(8,KM_NOSLEEP)==NULL);
    uint8_t *sleeping=kmem_zalloc(8,KM_SLEEP);
    CHECK(sleeping!=NULL);
    CHECK(memcmp(sleeping,(const uint8_t[8]){0},8)==0);
    kmem_free(sleeping,8);
    CHECK(ieee80211_sae_attach(&ic)==0);
    fail_alloc=false;
    CHECK(ieee80211_sae_attach(&ic)==EALREADY);
    cipher_present=false;CHECK(config(&ic,7)==EOPNOTSUPP);CHECK(ic.ic_sae->generation==0);cipher_present=true;
    CHECK(config(&ic,0)==EINVAL);selected_rsn[20]=0x80;CHECK(config(&ic,7)==EINVAL);
    CHECK(ic.ic_sae->generation==0);selected_rsn[20]=0xc0;CHECK(config(&ic,7)==0);
    CHECK(config(&ic,7)==0);CHECK(config(&ic,8)==EBUSY);
    CHECK(ieee80211_new_state(&ic,IEEE80211_S_ASSOC,0)==EACCES);
    CHECK(ic.ic_sae->phase==SAE_RETIRED);CHECK(config(&ic,7)==EINVAL);
    connect(&ic,8);
    ieee80211_node_authorize(&ni);CHECK(!(ni.ni_flags & IEEE80211_NODE_AUTH));
    struct ieee80211req_sae r=request(&ic,9,1);r.data[0]=1;
    CHECK(ieee80211_sae_request(&ic,&r)==EACCES);
    CHECK(!ieee80211_sae_data_allowed(&ic,&ni,NULL,false));
    CHECK(ieee80211_sae_data_allowed(&ic,&ni,NULL,true));
    CHECK(install(&ic,65535,0x11)==0);CHECK(ieee80211_sae_request(&ic,&r)==EACCES);
    /* Actual allocator and by-value crypto wrapper: even a cluster-sized
     * robust frame must retain the caller's mbuf through CCMP prepend. */
    const unsigned bodies[]={2,9,MHLEN-32,MHLEN-31,1536-24};
    for(unsigned i=0;i<sizeof(bodies)/sizeof(bodies[0]);i++) {
        uint8_t *body;struct mbuf *mg=ieee80211_getmgtframe(&body,bodies[i]);
        if (mg == NULL)
            abort();
        CHECK(body == mg->m_data);
        mg->m_len=mg->m_pkthdr.len=bodies[i];body[0]=0;body[1]=1;
        M_PREPEND(mg, 24, M_NOWAIT);
        if (mg == NULL)
            abort();
        CHECK(M_LEADINGSPACE(mg) >= 8);
        mg->m_data[0]=0xd0;memcpy(mg->m_data+4,ic.ic_sae->bssid,6);
        CHECK(ieee80211_sae_tag_tx(&ic,&ni,mg)==0);
        unsigned encrypted=encryptions;uint8_t *original=mg->m_data;
        CHECK(ieee80211_crypto_encap(&ic,&ni,mg)==&ni.ni_ucastkey);
        CHECK(encryptions==encrypted+1 && mg->m_data==original-8);
        CHECK(!prepend_replacements && !pullup_replacements);
        CHECK(ieee80211_crypto_encap(&ic,&ni,mg)==&ni.ni_ucastkey);
        CHECK(encryptions==encrypted+1);m_freem(mg);
    }
    for(unsigned bad=0;bad<3;bad++) {
        struct mbuf *mg=frame(&ic,0xc0,26);
        if(bad){memmove(mg->storage+32,mg->m_data,26);mg->m_data=mg->storage+32;}
        if(bad==1)mg->readonly=true;
        if(bad==2)mg->m_len=23;
        CHECK(ieee80211_sae_tag_tx(&ic,&ni,mg)==0);
        unsigned encrypted=encryptions;
        CHECK(ieee80211_crypto_encap(&ic,&ni,mg)==NULL);
        CHECK(!prepend_replacements && !pullup_replacements && encryptions==encrypted);
        m_freem(mg);
    }
    uint8_t *unused=NULL;
    fail_alloc=true;CHECK(ieee80211_getmgtframe(&unused,9)==NULL);
    CHECK(ieee80211_getmgtframe(&unused,512)==NULL);fail_alloc=false;
    CHECK(ieee80211_getmgtframe(&unused,MCLBYTES-31)==NULL);
    CHECK(ieee80211_getmgtframe(&unused,UINT32_MAX)==NULL);

    r=request(&ic,4,24);r.data[0]=4;memset(r.data+8,0x22,16);
    CHECK(ieee80211_sae_request(&ic,&r)==0);
    r=request(&ic,9,1);r.data[0]=1;CHECK(ieee80211_sae_request(&ic,&r)==0);CHECK(ni.ni_flags & IEEE80211_NODE_AUTH);
    CHECK(!ieee80211_sae_data_allowed(&ic,&ni,NULL,false));
    CHECK(ieee80211_sae_data_allowed(&ic,&ni,&ni.ni_ucastkey,false));
    ic.ic_opmode=0;CHECK(!ieee80211_sae_data_allowed(&ic,&ni,&ni.ni_ucastkey,false));
    CHECK(!ieee80211_sae_port_allowed(&ic));ic.ic_opmode=1;
    ic.ic_flags=0;CHECK(ieee80211_sae_data_allowed(&ic,&ni,&ni.ni_ucastkey,false));
    CHECK(!ieee80211_sae_data_allowed(&ic,&ni,NULL,false));ic.ic_flags=18;
    struct ieee80211_key alien=ni.ni_ucastkey;
    CHECK(!ieee80211_sae_data_allowed(&ic,&ni,&alien,false));
    ni.ni_ucastkey.wk_keytsc=100;ni.ni_ucastkey.wk_keyrsc=90;unsigned before=setkeys;
    CHECK(install(&ic,65535,0x11)==0);CHECK(setkeys==before&&key_counters(&ni.ni_ucastkey,100,90));
    CHECK(ni.ni_flags & IEEE80211_NODE_AUTH);
    CHECK(install(&ic,1,0x51)==0);CHECK(ni.ni_flags & IEEE80211_NODE_AUTH);
    CHECK(install(&ic,1,0x52)==0);CHECK(ni.ni_flags & IEEE80211_NODE_AUTH);
    r=request(&ic,4,24);r.data[0]=4;memset(r.data+8,0x23,16);
    CHECK(ieee80211_sae_request(&ic,&r)==0);CHECK(ni.ni_flags & IEEE80211_NODE_AUTH);
    CHECK(install(&ic,65535,0x12)==0);CHECK(ni.ni_flags & IEEE80211_NODE_AUTH);
    ni.ni_ucastkey.wk_keytsc=100;
    fail_alloc=true;CHECK(install(&ic,65535,0x33)==ENOMEM);fail_alloc=false;
    CHECK(!(ni.ni_flags & IEEE80211_NODE_AUTH)&&key_counters(&ni.ni_ucastkey,100,0));
    fail_cipher=true;CHECK(install(&ic,65535,0x33)==EIO);fail_cipher=false;
    cipher_present=false;CHECK(install(&ic,65535,0x33)==EOPNOTSUPP);cipher_present=true;
    CHECK(!(ni.ni_flags & IEEE80211_NODE_AUTH)&&key_counters(&ni.ni_ucastkey,100,0));
    struct mbuf *data=frame(&ic,8,32);
    CHECK(ieee80211_sae_tag_tx(&ic,&ni,data)==0);
    CHECK(!ieee80211_sae_tx_current(&ic,&ni,data));m_freem(data);
    data=frame(&ic,8,32);memcpy(data->m_data+24,"\xaa\xaa\x03\0\0\0\x88\x8e",8);
    CHECK(ieee80211_sae_tag_tx(&ic,&ni,data)==0);
    CHECK(ieee80211_sae_tx_current(&ic,&ni,data));m_freem(data);
    struct mbuf *tx=frame(&ic,0xc0,26);memcpy(tx->m_data+4,ni.ni_bssid,6);
    CHECK(ieee80211_sae_tag_tx(&ic,&ni,tx)==0);CHECK(tx->m_data[1]&64);
    CHECK(ieee80211_sae_tx_current(&ic,&ni,tx));CHECK(ieee80211_sae_crypto_tx(&ic,&ni,tx)==0);
    ieee80211_sae_crypto_done(tx);CHECK(ieee80211_sae_crypto_tx(&ic,&ni,tx)==1);
    CHECK(install(&ic,65535,0x33)==0);CHECK(!ieee80211_sae_tx_current(&ic,&ni,tx));m_freem(tx);
    r=request(&ic,9,1);r.data[0]=1;r.generation--;CHECK(ieee80211_sae_request(&ic,&r)==ESTALE);
    r.generation++;r.bssid[5]++;CHECK(ieee80211_sae_request(&ic,&r)==ESTALE);
    struct mbuf *m=frame(&ic,0xc0,26);m->m_data[24]=6;before=events;
    CHECK(ieee80211_sae_input(&ic,&ni,&m)==-1);CHECK(events==before+1&&last_event==IEEE80211_SAE_UNPROT_DISCONNECT);m_freem(m);
    m=frame(&ic,0xd0,44);m->m_data[1]=64;m->m_data[32]=8;m->m_data[33]=1;
    reject_crypto=true;CHECK(ieee80211_sae_input(&ic,&ni,&m)==-1);reject_crypto=false;
    CHECK(ieee80211_sae_input(&ic,&ni,&m)==1&&last_event==IEEE80211_SAE_RX_SA_QUERY);m_freem(m);
    for(size_t len=0;len<42;len++){m=frame(&ic,0xc0,len);m->m_data[1]=64;
        CHECK(ieee80211_sae_input(&ic,&ni,&m)==-1);m_freem(m);}
    for(unsigned frag=1;frag<16;frag++){m=frame(&ic,0xc0,42);m->m_data[1]=64;m->m_data[22]=frag;
        CHECK(ieee80211_sae_input(&ic,&ni,&m)==-1);m_freem(m);}
    m=frame(&ic,0xc0,42);m->m_data[1]=64;m->m_data[27]=0x40;
    CHECK(ieee80211_sae_input(&ic,&ni,&m)==-1);m_freem(m);
    m=frame(&ic,0xd1,28);CHECK(ieee80211_sae_input(&ic,&ni,&m)==-1);m_freem(m);
    for(unsigned flag=4;flag<=128;flag*=32){m=frame(&ic,0xc0,42);m->m_data[1]=64|flag;
        CHECK(ieee80211_sae_input(&ic,&ni,&m)==-1);m_freem(m);}
    m=frame(&ic,0xd0,44);memset(m->m_data+4,255,6);m->m_data[24]=3;
    m->m_data[26]=76;m->m_data[27]=16;m->m_data[28]=4;m->m_data[30]=1;
    bip_mic(m,ic.ic_sae->igtk[0].key);m->m_data[43]^=1;
    CHECK(ieee80211_sae_input(&ic,&ni,&m)==-1&&ic.ic_sae->igtk[0].ipn==0);
    m->m_data[43]^=1;CHECK(ieee80211_sae_input(&ic,&ni,&m)==0&&ic.ic_sae->igtk[0].ipn==1);
    m->m_pkthdr.len=m->m_len=44;CHECK(ieee80211_sae_input(&ic,&ni,&m)==-1);m_freem(m);
    r=request(&ic,4,24);r.data[0]=4;memset(r.data+8,0x23,16);CHECK(ieee80211_sae_request(&ic,&r)==0);
    CHECK(ic.ic_sae->igtk[0].ipn==1);
    tx=frame(&ic,0xc0,26);memcpy(tx->m_data+4,ni.ni_bssid,6);CHECK(ieee80211_sae_tag_tx(&ic,&ni,tx)==0);
    r=request(&ic,1,1);r.data[0]=0;ieee80211_sae_retire(&ic);
    CHECK(!(ni.ni_flags & IEEE80211_NODE_AUTH)&&!ic.ic_sae->igtk[0].installed);CHECK(!ieee80211_sae_tx_current(&ic,&ni,tx));m_freem(tx);
    CHECK(ieee80211_sae_request(&ic,&r)==0);CHECK(ieee80211_sae_request(&ic,&r)==0);
    connect(&ic,9);CHECK(ieee80211_sae_request(&ic,&r)==ESTALE);CHECK(ic.ic_sae->generation==9);
    ieee80211_sae_retire(&ic);CHECK(config(&ic,10)==0);
    CHECK(ieee80211_new_state(&ic,IEEE80211_S_AUTH,0)==0);
    for(unsigned fc=0;fc<=0x38;fc+=8){m=frame(&ic,0xb0,30);m->m_data[1]=fc;m->m_data[24]=3;m->m_data[26]=1;
        CHECK(ieee80211_sae_input(&ic,&ni,&m)==1&&last_event==IEEE80211_SAE_RX_FRAME);m_freem(m);}
    r=request(&ic,2,2);r.data[0]=1;
    CHECK(ieee80211_sae_request(&ic,&r)==0);CHECK(ic.ic_sae->phase==SAE_RETIRED&&!(ni.ni_flags & IEEE80211_NODE_AUTH));
    connect(&ic,11);CHECK(install(&ic,65535,0x41)==0);
    m=frame(&ic,0xc0,42);m->m_data[1]=64;m->m_data[32]=3;
    CHECK(ieee80211_sae_input(&ic,&ni,&m)==1);
    CHECK(ic.ic_state==IEEE80211_S_SCAN&&ic.ic_sae->phase==SAE_RETIRED);m_freem(m);
    CHECK(!ieee80211_sae_data_allowed(&ic,&ni,NULL,true));
    CHECK(config(&ic,12)==0);uint64_t serial=ic.ic_sae->serial;
    ieee80211_sae_state_request(&ic,IEEE80211_S_SCAN);
    CHECK(ic.ic_sae->phase==SAE_CONFIGURED&&ic.ic_sae->serial==serial);
    unsigned previous_events=events;
    fail_alloc=true;CHECK(ieee80211_new_state(&ic,IEEE80211_S_AUTH,0)==EACCES);fail_alloc=false;
    CHECK(events==previous_events);
    CHECK(ic.ic_sae->phase==SAE_RETIRED);
    CHECK(config(&ic,13)==0);ieee80211_sae_state_request(&ic,IEEE80211_S_INIT);
    CHECK(ic.ic_sae->phase==SAE_RETIRED);
    serial=ieee80211_sae_serial(&ic);ieee80211_sae_leave(&ic);
    CHECK(ieee80211_sae_serial(&ic)==serial);
    CHECK(ieee80211_sae_state(&ic,IEEE80211_S_INIT)==0);
    CHECK(ieee80211_sae_serial(&ic)==serial);
    ieee80211_sae_retire(&ic);CHECK(ieee80211_sae_serial(&ic)>serial);
    serial=ieee80211_sae_serial(&ic);ieee80211_sae_detach(&ic);
    CHECK(ieee80211_sae_serial(&ic)>serial);serial=ieee80211_sae_serial(&ic);
    CHECK(ieee80211_sae_attach(&ic)==0);CHECK(ieee80211_sae_serial(&ic)>serial);
    CHECK(config(&ic,13)==0);ieee80211_sae_retire(&ic);
    hardware_contract(&ic);
    hardware_tx_contract(&ic);
    ic.ic_sae->generation=13;
    ic.ic_sae->serial=UINT64_MAX;CHECK(config(&ic,14)==EINVAL);CHECK(ic.ic_sae->generation==13);
    CHECK(fences==unfences);ieee80211_sae_detach(&ic);CHECK(ic.ic_sae==NULL);
    printf("PASS: %u native SAE lifetime/key/PMF checks\n",checks);return 0;
}
