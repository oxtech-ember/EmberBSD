#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Exercise the production key queue and callbacks with controlled sleep points.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bwfm-key-epoch.XXXXXXXX")
if [ "${BWFM_TEST_KEEP:-0}" != 1 ]; then
    trap 'rm -rf "$work"' EXIT HUP INT TERM
else
    printf 'Test sources: %s\n' "$work"
fi
for fn in bwfm_key_snapshot bwfm_key_current bwfm_key_write bwfm_key_write_int \
    bwfm_key_set bwfm_key_set_cb bwfm_key_delete bwfm_key_delete_cb; do
    awk -v fn="$fn" '$0 ~ "^" fn "\\(" {copy=1}
        copy {print} copy && /^}/ {exit}' "$src/sys/dev/ic/bwfm.c" > "$work/$fn.h"
    test -s "$work/$fn.h"
done
awk '/^struct bwfm_wsec_key {/ {copy=1} copy {print}
    copy && /^};/ {exit}' "$src/sys/dev/ic/bwfmreg.h" > "$work/key-wire.h"
awk '/^struct bwfm_cmd_key {/ {copy=1} copy {print}
    copy && /^};/ {exit}' "$src/sys/dev/ic/bwfmvar.h" > "$work/key-command.h"
cat > "$work/check.c" <<'C'
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define IEEE80211_ADDR_LEN 6
#define IEEE80211_ADDR_EQ(a,b) (memcmp((a),(b),6) == 0)
#define IEEE80211_ADDR_COPY(a,b) memcpy((a),(b),6)
#define IEEE80211_IS_MULTICAST(a) ((a)[0] & 1)
#define IEEE80211_KEY_GROUP 1
#define PR_NOWAIT 0
#define BWFM_C_SET_VAR 263
#define BWFM_WPA_AUTH_WPA2_PSK 128
#define BWFM_WSEC_WEP 1
#define BWFM_WSEC_TKIP 2
#define BWFM_WSEC_AES 4
#define DEVNAME(sc) "test-bwfm"
static uint32_t host_to_le32(uint32_t value) { return value; }
#ifdef htole32
#undef htole32
#endif
#define htole32(x) host_to_le32(x)
#define explicit_memset mock_explicit_memset
enum { IEEE80211_M_STA, IEEE80211_M_HOSTAP };
enum { IEEE80211_CIPHER_WEP, IEEE80211_CIPHER_TKIP,
    IEEE80211_CIPHER_AES_CCM, IEEE80211_CIPHER_UNKNOWN };
enum { BWFM_TASK_KEY_SET, BWFM_TASK_KEY_DELETE };
struct ieee80211_cipher { int ic_cipher; const char *ic_name; };
struct ieee80211_key { unsigned wk_flags, wk_keyix, wk_keylen;
    uint8_t wk_key[32]; const struct ieee80211_cipher *wk_cipher; };
struct ifnet { void *if_softc; };
struct ieee80211_node { uint8_t ni_bssid[6]; };
struct ieee80211com { struct ifnet *ic_ifp; struct ieee80211_node *ic_bss;
    int ic_opmode; };
struct bwfm_softc;
struct protocol { int (*proto_set_dcmd)(struct bwfm_softc *, int, int, char *, size_t); };
struct bwfm_softc { struct ieee80211com sc_ic; struct ifnet sc_if;
    bool sc_detaching, sc_sae_enabled; uint32_t sc_sta_generation;
    void *sc_freetask, *sc_taskq; int sc_control_lock;
    const struct protocol *sc_proto_ops; };
struct work { int unused; };
#include "key-wire.h"
#include "key-command.h"
struct bwfm_task { struct work t_work; struct bwfm_softc *t_sc;
    int t_cmd; struct bwfm_cmd_key t_key; };
static struct bwfm_task job;
static struct bwfm_softc *active;
static unsigned checks, failures, writes, reads, locks, frees, enqueued, wipes;
static unsigned mutate_lock, mutate_write, fail_write;
static bool mutate_read, fail_read, fail_alloc, locked;
static struct bwfm_wsec_key installed;
static char last_name[16];
static void check(bool pass, const char *why)
{ checks++; if (!pass) { failures++; fprintf(stderr,"FAIL: %s\n",why); } }
static void explicit_memset(void *p, int value, size_t n)
{ memset(p,value,n); wipes++; }
static void mutex_enter(int *lock)
{ (void)lock; check(!locked,"one command owner"); locked=true; locks++;
  if (locks == mutate_lock) active->sc_sta_generation++; }
static void mutex_exit(int *lock) { (void)lock; locked=false; }
static void *pool_cache_get(void *pool, int flags)
{ (void)pool; (void)flags; return fail_alloc ? NULL : &job; }
static void pool_cache_put(void *pool, const void *item)
{ (void)pool; check(item == &job,"return owned task"); frees++; }
static void workqueue_enqueue(void *queue, const struct work *work, void *arg)
{ (void)queue; (void)arg; check(work == &job.t_work,"queue owned key task"); enqueued++; }
/* Keep the production proto_set_dcmd callback signature. */
// cppcheck-suppress constParameterCallback
static int set_command(struct bwfm_softc *sc, int ifidx, int cmd, char *p, size_t len)
{
    size_t offset = strlen(p) + 1;
    check(locked && ifidx == 0 && cmd == BWFM_C_SET_VAR,"guarded firmware write");
    check(offset < len,"bounded firmware payload");
    writes++; snprintf(last_name,sizeof(last_name),"%s",p);
    if (!strcmp(p,"wsec_key")) {
        check(len-offset == sizeof(installed),"wire key size");
        memcpy(&installed,p+offset,sizeof(installed));
    }
    if (writes == mutate_write) sc->sc_sta_generation++;
    return writes == fail_write ? EIO : 0;
}
static int bwfm_fwvar_var_get_int(struct bwfm_softc *sc, const char *name, uint32_t *p)
{ check(!strcmp(name,"wsec"),"read only cipher mask"); reads++; *p=0;
  if (mutate_read) sc->sc_sta_generation++;
  return fail_read ? EIO : 0; }
static const struct protocol protocol = { set_command };
static bool
#include "bwfm_key_snapshot.h"
static bool
#include "bwfm_key_current.h"
static int
#include "bwfm_key_write.h"
static int
#include "bwfm_key_write_int.h"
int
#include "bwfm_key_set.h"
static void
#include "bwfm_key_set_cb.h"
int
#include "bwfm_key_delete.h"
static void
#include "bwfm_key_delete_cb.h"
static const struct ieee80211_cipher ccmp={IEEE80211_CIPHER_AES_CCM,"CCMP"};
static const struct ieee80211_cipher wep={IEEE80211_CIPHER_WEP,"WEP"};
static const struct ieee80211_cipher unsupported={IEEE80211_CIPHER_UNKNOWN,"unknown"};
static void fixture(struct bwfm_softc *sc, struct ieee80211_node *bss,
    struct ieee80211_key *key)
{
    memset(sc,0,sizeof(*sc)); memset(bss,0,sizeof(*bss)); memset(key,0,sizeof(*key));
    memset(&job,0,sizeof(job)); memset(&installed,0,sizeof(installed));
    writes=reads=locks=frees=enqueued=wipes=0;
    mutate_lock=mutate_write=fail_write=0;
    mutate_read=fail_read=fail_alloc=locked=false;
    active=sc; sc->sc_ic.ic_ifp=&sc->sc_if; sc->sc_if.if_softc=sc;
    sc->sc_ic.ic_bss=bss; sc->sc_ic.ic_opmode=IEEE80211_M_STA;
    sc->sc_sta_generation=42; sc->sc_proto_ops=&protocol;
    bss->ni_bssid[0]=2; bss->ni_bssid[5]=1;
    key->wk_cipher=&ccmp; key->wk_keylen=16; memset(key->wk_key,0x5a,sizeof(key->wk_key));
}
static void queue_key(struct bwfm_softc *sc, const struct ieee80211_key *key)
{ check(bwfm_key_set(&sc->sc_ic,key,sc->sc_ic.ic_bss->ni_bssid)==1,"queue key"); }
int main(void)
{
    struct bwfm_softc sc; struct ieee80211_node bss; struct ieee80211_key key;
    unsigned i;
    fixture(&sc,&bss,&key); queue_key(&sc,&key);
    key.wk_key[0]=0; bwfm_key_set_cb(&sc,&job.t_key);
    check(writes==3 && reads==1 && installed.data[0]==0x5a,"owned WPA2 key copy and setup");
    check(wipes==4,"erase local wire key and each iovar buffer");
    fixture(&sc,&bss,&key); sc.sc_sae_enabled=true; queue_key(&sc,&key);
    bwfm_key_set_cb(&sc,&job.t_key);
    check(writes==1 && reads==0,"SAE installs key without rewriting PMF policy");
    for (i=0;i<6;i++) {
        fixture(&sc,&bss,&key); queue_key(&sc,&key);
        switch(i) {
        case 0: sc.sc_sta_generation++; break;
        case 1: bss.ni_bssid[5]++; break;
        case 2: sc.sc_sae_enabled=true; break;
        case 3: sc.sc_ic.ic_bss=NULL; break;
        case 4: sc.sc_ic.ic_opmode=IEEE80211_M_HOSTAP; break;
        case 5: sc.sc_detaching=true; break;
        }
        bwfm_key_set_cb(&sc,&job.t_key);
        check(writes==0 && wipes==1,"reject stale key and erase local buffer");
    }
    for (i=1;i<=3;i++) {
        fixture(&sc,&bss,&key); queue_key(&sc,&key); mutate_lock=i;
        bwfm_key_set_cb(&sc,&job.t_key);
        check(writes==i-1,"recheck epoch after every command lock wait");
    }
    for (i=1;i<=2;i++) {
        fixture(&sc,&bss,&key); queue_key(&sc,&key); mutate_write=i;
        bwfm_key_set_cb(&sc,&job.t_key);
        check(writes==i && reads==0,"stop policy changes after epoch changes during write");
    }
    fixture(&sc,&bss,&key); queue_key(&sc,&key); mutate_read=true;
    bwfm_key_set_cb(&sc,&job.t_key);
    check(writes==2 && reads==1,"do not write cipher mask after stale read");
    for(i=1;i<=3;i++) {
        fixture(&sc,&bss,&key); queue_key(&sc,&key); fail_write=i;
        bwfm_key_set_cb(&sc,&job.t_key);
        check(writes==i,"stop after firmware write error");
    }
    fixture(&sc,&bss,&key); queue_key(&sc,&key); fail_read=true;
    bwfm_key_set_cb(&sc,&job.t_key); check(writes==2,"failed read cannot feed cipher write");
    fixture(&sc,&bss,&key); queue_key(&sc,&key); job.t_key.mac[5]++;
    bwfm_key_set_cb(&sc,&job.t_key); check(writes==0,"pairwise key requires selected peer");
    fixture(&sc,&bss,&key); key.wk_flags=IEEE80211_KEY_GROUP;
    queue_key(&sc,&key); memset(job.t_key.mac,0xff,6); bwfm_key_set_cb(&sc,&job.t_key);
    check(writes==3 && installed.flags==BWFM_WSEC_PRIMARY_KEY,"station group key preserved");
    for(i=0;i<3;i++) {
        fixture(&sc,&bss,&key); key.wk_keyix=2;
        check(bwfm_key_delete(&sc.sc_ic,&key)==1,"queue delete");
        if(i==1) sc.sc_sta_generation++;
        if(i==2) mutate_lock=1;
        bwfm_key_delete_cb(&sc,&job.t_key);
        check(writes==(i==0) && installed.len==0,"old deletion cannot remove new association keys");
        check(wipes==2,"erase delete buffers");
    }
    fixture(&sc,&bss,&key); sc.sc_ic.ic_opmode=IEEE80211_M_HOSTAP;
    queue_key(&sc,&key); sc.sc_sta_generation++; bss.ni_bssid[5]++;
    bwfm_key_set_cb(&sc,&job.t_key); check(writes==3,"AP key path retains behavior");
    fixture(&sc,&bss,&key); key.wk_cipher=&wep; key.wk_keylen=5;
    queue_key(&sc,&key); sc.sc_sta_generation++; sc.sc_ic.ic_bss=NULL;
    bwfm_key_set_cb(&sc,&job.t_key); check(writes==3,"static WEP key survives station epochs");
    fixture(&sc,&bss,&key); key.wk_cipher=&unsupported; queue_key(&sc,&key);
    bwfm_key_set_cb(&sc,&job.t_key); check(writes==0 && wipes==1,"unsupported cipher erased");
    fixture(&sc,&bss,&key); sc.sc_ic.ic_bss=NULL;
    check(bwfm_key_set(&sc.sc_ic,&key,bss.ni_bssid)==0 &&
        frees==1 && enqueued==0 && wipes==1,"missing peer releases and erases task");
    fixture(&sc,&bss,&key); fail_alloc=true;
    check(bwfm_key_delete(&sc.sc_ic,&key)==0 && enqueued==0,"allocation failure queues nothing");
    fixture(&sc,&bss,&key); sc.sc_detaching=true;
    check(bwfm_key_set(&sc.sc_ic,&key,bss.ni_bssid)==0 &&
        bwfm_key_delete(&sc.sc_ic,&key)==0 && enqueued==0,
        "detach rejects key producers");
    printf("%s: %u production key-epoch assertions\n",failures?"FAIL":"PASS",checks);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$work" "$work/check.c" -o "$work/check"
"$work/check"
