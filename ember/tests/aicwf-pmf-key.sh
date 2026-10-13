#!/bin/sh
# Run the real key callbacks against rekey, duplicate and retired confirmations.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aicwf-pmf-key.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/check.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
typedef unsigned u_int;
#define AICWF_KEY_SLOT_PAIRWISE 4
#define AICWF_KEY_SLOTS 5
#define AICWF_HWKEY_NONE 255
#define AICWF_STA_NONE 255
#define AICWF_CIPHER_CCMP 2
#define IEEE80211_CIPHER_AES_CCM 3
#define AICWF_MM_KEY_ADD_REQ 1
#define AICWF_MM_KEY_ADD_CFM 2
#define AICWF_MM_KEY_DEL_REQ 3
#define AICWF_MM_KEY_DEL_CFM 4
#define AICWF_ME_SET_CONTROL_PORT_REQ 5
#define AICWF_ME_SET_CONTROL_PORT_CFM 6
#define explicit_memset memset
#define device_printf(...) ((void)0)
enum aicwf_task_cmd { TASK_SET, TASK_DELETE };
struct aicwf_task { int t_work; enum aicwf_task_cmd t_cmd; bool t_group; unsigned t_keyix,t_cipher,t_keylen; uint64_t t_epoch; uint8_t t_key[32];};
struct aicwf_softc {int sc_lock,sc_dev,sc_epoch_lock,sc_taskpool,sc_taskq; bool sc_connected,sc_ptk_valid; uint64_t sc_sae_epoch,sc_pmf_rx_pn; uint8_t sc_ptk[16],sc_hwkey[5],sc_ap,sc_vif;};
static bool node_spin_held;
static void mutex_enter(int *p) {assert(!node_spin_held);assert(!*p);*p=1;}
static void mutex_exit(int *p) {assert(*p);*p=0;}
static void mutex_spin_enter(int *p) {assert(!*p);*p=1;}
static void mutex_spin_exit(int *p) {assert(*p);*p=0;}
struct cipher {unsigned ic_cipher;};
struct ieee80211_key {struct cipher *wk_cipher;unsigned wk_keyix,wk_flags,wk_keylen;uint8_t wk_key[32];};
struct ifnet {void *if_softc;};
struct ieee80211com {struct ifnet *ic_ifp;};
#define IEEE80211_KEY_GROUP 1
#define PR_NOWAIT 0
static struct aicwf_task queued;
static void *pool_cache_get(int pool,int flags) {(void)pool;(void)flags;return &queued;}
static void workqueue_enqueue(int q,int *work,void *cpu) {(void)q;(void)work;(void)cpu;}
static unsigned adds,deletes,ports,mode;

static int aicwf_cmd_reply(struct aicwf_softc *sc, unsigned id,const void *p,size_t n,unsigned cid,void *reply,size_t len,bool strict,uint64_t epoch)
{
    (void)cid; (void)p; assert(strict && !sc->sc_lock && epoch==sc->sc_sae_epoch);
    if(id==AICWF_MM_KEY_ADD_REQ) {
        assert(len==2 && n==44); adds++;
        if(mode==1) return EPROTO;
        memset(reply,0,len); ((uint8_t *)reply)[1]=7;
        if(mode==2) {sc->sc_sae_epoch++;sc->sc_connected=false;}
        if(mode==3) {((uint8_t *)reply)[0]=1;}
        if(mode==4) {((uint8_t *)reply)[1]=255;}
    } else if(id==AICWF_MM_KEY_DEL_REQ) {deletes++;assert(!len && n==1);}
    else {assert(id==AICWF_ME_SET_CONTROL_PORT_REQ); ports++;}
    return 0;
}
C
for fn in aicwf_key_set_cb aicwf_key_delete_cb aicwf_key_task; do
 type=void; [ "$fn" != aicwf_key_task ] || type=int
 awk -v fn="$fn" -v type="$type" '$0 ~ "^"fn"\\("{copy=1;print "static "type} copy{print} copy&&/^}/{exit}' "$src/sys/dev/sdmmc/if_aicwf.c" >> "$work/check.c"
done
cat >> "$work/check.c" <<'C'
int main(void)
{
    struct aicwf_softc sc={.sc_connected=true,.sc_sae_epoch=1};
    struct aicwf_task t={.t_epoch=1,.t_keylen=16,.t_cipher=IEEE80211_CIPHER_AES_CCM,.t_key={1}};
    aicwf_key_set_cb(&sc,&t); assert(adds==1 && ports==1 && sc.sc_ptk_valid && sc.sc_hwkey[4]==7);
    sc.sc_pmf_rx_pn=99; aicwf_key_set_cb(&sc,&t);
    assert(adds==1 && ports==2 && sc.sc_pmf_rx_pn==99);
    t.t_key[0]=2; aicwf_key_set_cb(&sc,&t);
    assert(adds==2 && sc.sc_ptk_valid && sc.sc_ptk[0]==2 && sc.sc_pmf_rx_pn==0);
    t.t_key[0]=1; aicwf_key_delete_cb(&sc,&t); assert(deletes==0 && sc.sc_ptk_valid);
    t.t_epoch=0; aicwf_key_set_cb(&sc,&t); aicwf_key_delete_cb(&sc,&t);
    assert(adds==2 && deletes==0 && sc.sc_ptk_valid);
    t.t_epoch=1;t.t_key[0]=2; aicwf_key_delete_cb(&sc,&t);
    assert(deletes==1 && !sc.sc_ptk_valid && sc.sc_ptk[0]==0 && sc.sc_hwkey[4]==255);
    for(mode=1;mode<=4;mode++) {
        sc.sc_sae_epoch=t.t_epoch=1;sc.sc_connected=true;sc.sc_ptk_valid=false;
        unsigned before=ports;
        aicwf_key_set_cb(&sc,&t);
        assert(!sc.sc_ptk_valid && ports==before);
    }
    mode=0;sc.sc_sae_epoch=t.t_epoch=2;sc.sc_connected=false;
    unsigned before=adds; aicwf_key_set_cb(&sc,&t);assert(adds==before);
    sc.sc_connected=true;t.t_keylen=32;aicwf_key_set_cb(&sc,&t);assert(adds==before);
    t.t_keylen=16;t.t_group=true;t.t_keyix=1;
    aicwf_key_set_cb(&sc,&t);assert(!sc.sc_ptk_valid && sc.sc_hwkey[1]==7);
    struct ifnet ifp={.if_softc=&sc};struct ieee80211com ic={.ic_ifp=&ifp};
    struct cipher cipher={IEEE80211_CIPHER_AES_CCM};
    struct ieee80211_key key={.wk_cipher=&cipher,.wk_keylen=16};
    /* DELETE is called under net80211's IPL_NET node lock. */
    node_spin_held=true;
    assert(aicwf_key_task(&ic,&key,TASK_DELETE)==1);
    assert(queued.t_epoch==sc.sc_sae_epoch && !sc.sc_epoch_lock);
    node_spin_held=false;
    puts("PASS: PTK confirmation, stale lifetime, duplicate PN, rekey, deletion and spin context");

    return 0;
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror "$work/check.c" -o "$work/check"
"$work/check"
