#!/bin/sh
# Exercise the real PMF receive admission and protected transmit descriptor.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aicwf-pmf-transport.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/check.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define IEEE80211_S_RUN 4
#define IEEE80211_SAE_VERSION 1
#define IEEE80211_SAE_UNPROT_DISCONNECT 0x103
#define IEEE80211_SAE_RX_SA_QUERY 0x102
#define RTM_IEEE80211_SAE 1
#define KM_NOSLEEP 1
#define IEEE80211_DPRINTF(...) ((void)0)
#define ETHER_ADDR_LEN 6
#define IEEE80211_ADDR_EQ(a,b) (memcmp(a,b,6)==0)
#define AICWF_KEY_SLOT_PAIRWISE 4
#define AICWF_HDR_LEN 4
#define AICWF_TXDESC_LEN 28
#define AICWF_TYPE_DATA 1
#define AICWF_HWQ_VO 3
#define AICWF_TID_NONE 255
#define AICWF_STA_NONE 255
#define AICWF_TX_MGMT 8
#define AICWF_TX_RESERVE 2
#ifndef __BIT
#define __BIT(n) (1u<<(n))
#endif
struct ieee80211_node {uint8_t ni_bssid[6];};
struct ieee80211com {int ic_state; uint8_t ic_myaddr[6];struct ieee80211_node *ic_bss; void *ic_ifp;};
struct ieee80211req_sae {uint32_t version,generation;uint16_t op,len;uint8_t bssid[6],reserved[2],data[1536];};
struct evcnt {unsigned ev_count;};
struct aicwf_softc {struct ieee80211com sc_ic;int sc_lock;bool sc_sae_enabled,sc_connected,sc_ptk_valid,sc_sae_authenticated;uint8_t sc_vif,sc_ap,sc_sae_bssid[6],sc_hwkey[5];uint32_t sc_sae_generation;uint64_t sc_sae_epoch,sc_pmf_rx_pn;uint8_t sc_txbuf[2048];struct evcnt sc_ev_sae_tx;};
static unsigned deliveries,writes;static bool failalloc;
static struct ieee80211req_sae event;
static void mutex_enter(int *p) {assert(!*p);*p=1;}
static void mutex_exit(int *p) {assert(*p);*p=0;}
static void *kmem_zalloc(size_t n,int flags) {assert(flags==KM_NOSLEEP);return failalloc?NULL:calloc(1,n);}
#define kmem_free(p,n) free(p)
#ifndef __NetBSD__
static void le16enc(void *v,uint16_t x) {uint8_t *p=v;p[0]=x;p[1]=x>>8;}
#endif
static unsigned aicwf_crc8(const uint8_t *p,size_t n) {(void)p;(void)n;return 0;}
static int aicwf_write(struct aicwf_softc *sc,size_t n,unsigned reserve)
{assert(sc->sc_lock && n>=32 && reserve==2);writes++;return 0;}
static void rt_ieee80211msg(void *ifp,unsigned type,void *p,size_t n)
{(void)ifp;assert(type==1 && n<=sizeof(event));memcpy(&event,p,n);deliveries++;}
#include "if_aicwf_sae.h"
C
for pair in aicwf_sae_tx:int aicwf_pmf_rx:void; do
 name=${pair%:*}; type=${pair#*:}
 awk -v name="$name" -v type="$type" '$0~"^"name"\\("{copy=1;print "static "type} copy{print} copy&&/^}/{exit}' "$src/sys/dev/sdmmc/if_aicwf.c" >> "$work/check.c"
done
cat >> "$work/check.c" <<'C'
int main(void)
{
    struct ieee80211_node node={{2,0,0,0,0,2}};
    struct aicwf_softc sc={.sc_ic={.ic_state=4,.ic_myaddr={2,0,0,0,0,1},.ic_bss=&node},.sc_sae_enabled=true,.sc_connected=true,.sc_ptk_valid=true,.sc_sae_authenticated=true,.sc_sae_epoch=7,.sc_sae_generation=11,.sc_ap=3};
    uint8_t f[36]={0xc0};memcpy(sc.sc_sae_bssid,node.ni_bssid,6);sc.sc_hwkey[4]=5;
    memcpy(f+4,sc.sc_ic.ic_myaddr,6);memcpy(f+10,node.ni_bssid,6);memcpy(f+16,node.ni_bssid,6);f[24]=7;
    aicwf_pmf_rx(&sc,f,26,0,0x40); assert(deliveries==1 && sc.sc_connected && sc.sc_ic.ic_state==4);
    assert(event.generation==11 && event.op==0x103 && event.len==26);
    failalloc=true;aicwf_pmf_rx(&sc,f,26,0,0x40);assert(deliveries==1);failalloc=false;
    aicwf_pmf_rx(&sc,f,26,0,0x140);assert(deliveries==1);
    sc.sc_ptk_valid=false;aicwf_pmf_rx(&sc,f,26,0,0x40);assert(deliveries==1);sc.sc_ptk_valid=true;
    f[0]=0xd0;f[1]=0x40;f[24]=1;f[27]=0x20;f[32]=8;f[33]=1;
    const uint32_t status=0x0200600c | (5u<<15);
    aicwf_pmf_rx(&sc,f,36,status,0x40);assert(deliveries==2 && sc.sc_pmf_rx_pn==1 && event.op==0x102);
    aicwf_pmf_rx(&sc,f,36,status,0x40);assert(deliveries==2);
    f[24]=100;aicwf_pmf_rx(&sc,f,36,status|0x100,0x40);assert(deliveries==2 && sc.sc_pmf_rx_pn==1);
    failalloc=true;aicwf_pmf_rx(&sc,f,36,status,0x40);assert(sc.sc_pmf_rx_pn==1);failalloc=false;
    assert(aicwf_sae_tx(&sc,event.data,28,7,true)==0 && writes==1);
    assert(sc.sc_txbuf[4+25]==3 && sc.sc_txbuf[4+26]==(8|128));
    sc.sc_ptk_valid=false;assert(aicwf_sae_tx(&sc,event.data,28,7,true)==ESTALE && writes==1);
    sc.sc_ptk_valid=true;assert(aicwf_sae_tx(&sc,event.data,28,6,true)==ESTALE && writes==1);
    sc.sc_connected=false;assert(aicwf_sae_tx(&sc,event.data,28,7,true)==ESTALE && writes==1);
    assert(aicwf_sae_tx(&sc,event.data,28,7,false)==0 && writes==2);
    assert(sc.sc_txbuf[4+25]==255 && sc.sc_txbuf[4+26]==8);
    puts("PASS: PMF event admission, replay commit, lifetime and robust TX descriptor");
    return 0;
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror -I"$src/sys/dev/sdmmc" "$work/check.c" -o "$work/check"
"$work/check"
