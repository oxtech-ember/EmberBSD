#!/bin/sh
# Exercise actual ioctl and disconnect functions across a sleeping command.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aicwf-sae-lifetime.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/check.c" <<'C'
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <assert.h>
typedef unsigned long u_long;
#define SIOCG80211 1
#define SIOCS80211 2
#define KAUTH_NETWORK_INTERFACE 0
#define KAUTH_REQ_NETWORK_INTERFACE_SETPRIV 0
#define KM_SLEEP 0
#define IEEE80211_SAE_VERSION 1
#define IEEE80211_M_STA 0
#define IEEE80211_S_AUTH 2
#define IEEE80211_S_RUN 4
#define IEEE80211_S_SCAN 1
#define IEEE80211_SAE_CONFIGURE 0
#define IEEE80211_SAE_AUTH_STATUS 1
#define IEEE80211_SAE_TX_FRAME 2
#define IEEE80211_SAE_SET_IGTK 3
#define IEEE80211_SAE_DELETE_IGTK 4
#define IEEE80211_SAE_TX_SA_QUERY 6
#define AICWF_HWKEY_NONE 255
#define AICWF_STA_NOTICE_LEN 28
#define RTM_IEEE80211_STA_TX 1
#define RTM_IEEE80211_STA_SCAN 2
#define AICWF_SM_EXTERNAL_AUTH_RSP 10
#define AICWF_SM_EXTERNAL_AUTH_CFM 11
#define AICWF_MM_KEY_DEL_REQ 12
#define AICWF_MM_KEY_DEL_CFM 13
#define AICWF_MM_KEY_ADD_REQ 14
#define AICWF_MM_KEY_ADD_CFM 15
#define AICWF_STA_NONE 255
#define AICWF_CIPHER_BIP 5
#define ETHER_ADDR_LEN 6
#define IEEE80211_ADDR_EQ(a,b) (memcmp(a,b,6)==0)
#define IEEE80211_IS_MULTICAST(a) ((a)[0]&1)
#define explicit_memset memset
#define kmem_zalloc(n,f) calloc(1,n)
#define kmem_free(p,n) free(p)
#define kauth_cred_get() 0
#define kauth_authorize_network(...) 0
#define device_printf(...) ((void)0)
struct ieee80211_node {uint8_t ni_bssid[6];};
struct ieee80211com {void *ic_ifp; int ic_opmode, ic_state; struct ieee80211_node *ic_bss; uint8_t ic_myaddr[6];};
struct ieee80211req_sae {uint32_t generation; uint16_t version, op, len, reserved[2]; uint8_t bssid[6], data[1536];};
struct ieee80211req {int i_len,i_val; void *i_data;};
struct aicwf_softc {struct ieee80211com sc_ic; int sc_dev, sc_vif, sc_lock, sc_epoch_lock, sc_if; uint16_t sc_sae_caps; bool sc_if_attached,sc_sae_enabled,sc_sae_pending,sc_sae_authenticated,sc_connecting,sc_connected; uint32_t sc_sae_generation; uint64_t sc_sae_epoch; bool sc_ptk_valid; uint64_t sc_pmf_rx_pn; uint8_t sc_ptk[16]; uint8_t sc_sae_bssid[6],sc_igtk[2];};
static void mutex_enter(int *p) { assert(!*p); *p = 1; }
static void mutex_exit(int *p) { assert(*p); *p = 0; }
#define mutex_spin_enter mutex_enter
#define mutex_spin_exit mutex_exit
static int copyin(void *src,void *dst,size_t n) {memcpy(dst,src,n); return 0;}
#ifndef __NetBSD__
static uint16_t le16dec(const void *ptr) {const uint8_t *p=ptr; return p[0]+256*p[1];}
static void le16enc(void *ptr,uint16_t v) {uint8_t *p=ptr;p[0]=v;p[1]=v>>8;}
#endif
static int splnet(void) {return 0;}
static void splx(int s) {(void)s;}
static void aicwf_reorder_flush(struct aicwf_softc *sc,int i,bool b) {(void)sc;(void)i;(void)b;}
static void ieee80211_new_state(struct ieee80211com *ic,int state,int arg) {ic->ic_state=state;(void)arg;}
#include "if_aicwf_sae.h"
static int aicwf_sae_tx(struct aicwf_softc *sc,const uint8_t *p,size_t n,uint64_t epoch,bool query) {(void)query;(void)sc;(void)p;(void)n;(void)epoch;return 0;}
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
static void aicwf_disconnect_ind(struct aicwf_softc *, const uint8_t *, size_t);
static void aicwf_sta_retire_locked(struct aicwf_softc *sc, uint8_t *tx, bool *a, uint8_t *scan, bool *b)
{ assert(sc->sc_lock); (void)tx; (void)scan; *a = *b = false; }
static void rt_ieee80211msg(void *ifp, int code, void *p, size_t n)
{ (void)ifp; (void)code; (void)p; (void)n; assert(false); }
static void aicwf_disconnect(struct aicwf_softc *);
static int aicwf_sae_ioctl(struct aicwf_softc *, u_long, struct ieee80211req *);
static int interruption, disconnect_requests;
static int aicwf_cmd(struct aicwf_softc *sc, uint16_t id, const void *p,
    size_t n, uint16_t cid, void *cfm, size_t clen)
{ (void)sc; (void)id; (void)p; (void)n; (void)cid; (void)cfm; (void)clen;
  disconnect_requests++; return 0; }
#define AICWF_SM_DISCONNECT_REQ 20
#define AICWF_SM_DISCONNECT_CFM 21
#define IEEE80211_REASON_AUTH_LEAVE 3

static int aicwf_cmd_reply(struct aicwf_softc *sc, uint16_t id,
    const void *param, size_t plen, uint16_t cid, void *cfm, size_t clen,
    bool strict, uint64_t epoch)
{
    (void)param; (void)plen; (void)cid;
    assert(strict && epoch == sc->sc_sae_epoch);
    assert(id == AICWF_MM_KEY_ADD_REQ || id == AICWF_MM_KEY_DEL_REQ ||
        id == AICWF_SM_EXTERNAL_AUTH_RSP);
    int mode = interruption;
    interruption = 0;
    if (mode == 3) {
        struct ieee80211req_sae req = {.version=1, .op=IEEE80211_SAE_CONFIGURE,
            .generation=43, .len=1, .data={1}, .bssid={2,0,0,0,0,2}};
        struct ieee80211req ireq = {.i_len=sizeof(req), .i_data=&req};
        assert(aicwf_sae_ioctl(sc, SIOCS80211, &ireq) == 0);
    } else if (mode) {
        if (sc->sc_connected) {
            uint8_t ind[6] = {0}; ind[2] = sc->sc_vif;
            aicwf_disconnect_ind(sc, ind, sizeof(ind));
        } else
            aicwf_disconnect(sc);
        if (mode == 2) {

            sc->sc_sae_epoch++;
            sc->sc_sae_generation++;
            sc->sc_sae_authenticated = sc->sc_connected = true;
            sc->sc_ic.ic_state = IEEE80211_S_RUN;
            sc->sc_igtk[0] = 9;
        }
    }
    if (id == AICWF_MM_KEY_ADD_REQ) {
        /* Only status and hardware index are meaningful wire bytes. */
        assert(clen == 2);
        memset(cfm, 0, clen);
        ((uint8_t *)cfm)[1] = 7;
    } else {
        assert(clen == 0);
    }
    return 0;
}
C
for pair in aicwf_sae_advance:void aicwf_disconnect_ind:void aicwf_sae_current:bool aicwf_sae_ioctl:int aicwf_sae_clear_keys:void aicwf_disconnect:void; do
    name=${pair%:*}
    type=${pair#*:}
    awk -v name="$name" -v type="$type" '$0 ~ "^" name "\\(" {copy=1; print "static " type}
        copy {print} copy && /^}/ {exit}' \
        "$src/sys/dev/sdmmc/if_aicwf.c" >> "$work/check.c"
done
cat >> "$work/check.c" <<'C'
int main(void)
{
    struct ieee80211_node node = {{2,0,0,0,0,1}};
    struct aicwf_softc sc;
    struct ieee80211req_sae req;
    struct ieee80211req ireq = {.i_len=sizeof(req), .i_data=&req};
    unsigned op, mode;
    for (op = 0; op < 4; op++) {
        for (mode = 0; mode < 3; mode++) {
            memset(&sc, 0, sizeof(sc));
            memset(&req, 0, sizeof(req));
            sc.sc_ic.ic_bss = &node;
            sc.sc_ic.ic_state = IEEE80211_S_RUN;
            sc.sc_if_attached = sc.sc_sae_enabled = true;
            sc.sc_sae_authenticated = sc.sc_connected = true;
            sc.sc_sae_epoch = 17;
            sc.sc_sae_generation = 42;
            memcpy(sc.sc_sae_bssid, node.ni_bssid, 6);
            memset(sc.sc_igtk, AICWF_HWKEY_NONE, 2);
            req.version = IEEE80211_SAE_VERSION;
            req.generation = 42;
            memcpy(req.bssid, node.ni_bssid, 6);
            req.op = IEEE80211_SAE_SET_IGTK;
            req.len = 24;
            req.data[0] = 4;
            if (op == 1 || op == 2) {
                sc.sc_igtk[0] = 6;
                req.op = IEEE80211_SAE_DELETE_IGTK;
                req.len = 2;
            } else if (op == 3) {
                req.op = IEEE80211_SAE_AUTH_STATUS;
                req.len = 2;
                req.data[0] = 0;
                sc.sc_connecting = sc.sc_sae_pending = true;
                sc.sc_sae_authenticated = sc.sc_connected = false;
                sc.sc_ic.ic_state = IEEE80211_S_AUTH;
            }
            interruption = mode;
            if (op == 2) {
                aicwf_sae_clear_keys(&sc);
            } else {
                int result = aicwf_sae_ioctl(&sc, SIOCS80211, &ireq);
                assert(result == (mode ? ESTALE : 0));
            }
            if (mode == 2)
                assert(sc.sc_igtk[0] == 9 && sc.sc_sae_authenticated);
            else if (mode == 1)
                assert(sc.sc_igtk[0] == AICWF_HWKEY_NONE &&
                    !sc.sc_sae_authenticated);
            else if (op == 0)
                assert(sc.sc_igtk[0] == 7);
            else
                assert(sc.sc_igtk[0] == AICWF_HWKEY_NONE);
        }
    }
    sc.sc_connected = sc.sc_sae_enabled = sc.sc_sae_authenticated = true;
    sc.sc_sae_caps = 3;
    sc.sc_igtk[0] = 7;
    sc.sc_igtk[1] = AICWF_HWKEY_NONE;
    interruption = 3;
    disconnect_requests = 0;
    aicwf_disconnect(&sc);
    assert(sc.sc_sae_generation == 43 && !sc.sc_connected);
    assert(disconnect_requests == 1 && sc.sc_igtk[0] == AICWF_HWKEY_NONE);
    /* Foreign or short disconnect indications cannot retire this link. */
    uint8_t ind[6] = {0};
    sc.sc_connected = sc.sc_sae_authenticated = true;
    sc.sc_ic.ic_state = IEEE80211_S_RUN;
    sc.sc_vif = 1;
    const uint64_t epoch = sc.sc_sae_epoch;
    aicwf_disconnect_ind(&sc, ind, sizeof(ind));
    assert(sc.sc_connected && sc.sc_sae_epoch == epoch);
    ind[2] = 1;
    for (size_t n = 0; n < sizeof(ind); n++) {
        aicwf_disconnect_ind(&sc, ind, n);
        assert(sc.sc_connected && sc.sc_sae_epoch == epoch);
    }
    aicwf_disconnect_ind(&sc, ind, sizeof(ind));
    assert(!sc.sc_connected && sc.sc_sae_epoch == epoch + 1);
    puts("PASS: 21 SAE lifetime and disconnect VIF/bounds cases");


    return 0;
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror ${AICWF_SAE_LIFETIME_CFLAGS:-} -I"$src/sys/dev/sdmmc" \
    "$work/check.c" -o "$work/check"
"$work/check"
