#!/bin/sh
# Exercise the driver's real beacon parser, including adjacent-channel reception.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aicwf-channel.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
awk '/^aicwf_scan_result\(/ {copy=1; print "static void"}
copy {print} copy && /^}/ {exit}' \
    "$src/sys/dev/sdmmc/if_aicwf.c" > "$work/scan.h"
cat > "$work/check.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned int u_int;
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#ifndef __UNCONST
#define __UNCONST(p) ((void *)(uintptr_t)(const void *)(p))
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#define ETHER_ADDR_LEN 6
#define IEEE80211_ADDR_EQ(a, b) (memcmp((a), (b), 6) == 0)
#endif
#define IEEE80211_DPRINTF(...) ((void)0)
#define IEEE80211_CHAN_MAX 255
#define IEEE80211_NWID_LEN 32
#define IEEE80211_RATE_MAXSIZE 15
#define IEEE80211_F_SCAN 1
#define IEEE80211_FC0_TYPE_MASK 0x0c
#define IEEE80211_FC0_TYPE_MGT 0
#define IEEE80211_FC0_SUBTYPE_MASK 0xf0
#define IEEE80211_FC0_SUBTYPE_BEACON 0x80
#define IEEE80211_FC0_SUBTYPE_PROBE_RESP 0x50
enum { IEEE80211_ELEMID_SSID=0, IEEE80211_ELEMID_RATES=1,
    IEEE80211_ELEMID_DSPARMS=3, IEEE80211_ELEMID_TIM=5,
    IEEE80211_ELEMID_COUNTRY=7, IEEE80211_ELEMID_ERP=42,
    IEEE80211_ELEMID_RSN=48, IEEE80211_ELEMID_XRATES=50,
    IEEE80211_ELEMID_HTINFO=61, IEEE80211_ELEMID_RSNX=244 };
struct ieee80211_frame { uint8_t i_fc[2], i_dur[2],
    i_addr1[6], i_addr2[6], i_addr3[6], i_seq[2]; };
struct ieee80211_scanparams {
    uint8_t *sp_ssid, *sp_rates, *sp_xrates, *sp_country, *sp_tim;
    uint8_t *sp_wpa, *sp_rsnx, *sp_tstamp;
    unsigned sp_bintval, sp_capinfo, sp_chan, sp_bchan, sp_timoff, sp_erp;
};
struct ieee80211com {
    struct { unsigned ic_freq; } ic_channels[256];
    unsigned ic_flags;
};
struct aicwf_ft_bss { uint8_t bssid[6]; uint16_t mdid;
    uint8_t r0kh_len; uint8_t r0kh[45]; };
struct aicwf_softc { struct ieee80211com sc_ic; bool sc_if_attached;
    struct aicwf_ft_bss sc_ft_bss[4]; };
static unsigned got_chan, got_rx, added;
static unsigned le16dec(const void *p)
{ const uint8_t *b=p; return b[0] | b[1] << 8; }
static unsigned ieee80211_mhz2ieee(unsigned f, int flags)
{ (void)flags; return f < 3000 ? (f-2407)/5 : (f-5000)/5; }
static int splnet(void) { return 0; }
static void splx(int s) { (void)s; }
static void ieee80211_add_scan(struct ieee80211com *ic,
    const struct ieee80211_scanparams *sp, const struct ieee80211_frame *wh,
    int subtype, int rssi, int stamp)
{
    (void)ic; (void)wh; (void)subtype; (void)rssi; (void)stamp;
    added++; got_chan=sp->sp_chan; got_rx=sp->sp_bchan;
}
#include "scan.h"
static void check(unsigned rx, const uint8_t *ies, size_t n, unsigned expected)
{
    struct aicwf_softc sc = {.sc_if_attached=true};
    uint8_t ind[160]={0};
    unsigned freq=rx <= 13 ? 2407+5*rx : 5000+5*rx;
    for (unsigned i=1; i<=13; i++) sc.sc_ic.ic_channels[i].ic_freq=2407+5*i;
    sc.sc_ic.ic_channels[36].ic_freq=5180;
    sc.sc_ic.ic_channels[60].ic_freq=5300;
    sc.sc_ic.ic_flags=IEEE80211_F_SCAN;
    ind[0]=36+5+n; ind[4]=freq; ind[5]=freq>>8;
    ind[9]=(uint8_t)-50; ind[12]=0x80;
    const uint8_t basic[]={0,0,1,1,2};
    memcpy(ind+48,basic,sizeof(basic));
    if(n) memcpy(ind+53,ies,n);
    added=0;
    aicwf_scan_result(&sc,ind,53+n);
    assert(added == (expected != 0));
    if(expected) { assert(got_chan==expected); assert(got_rx==rx); }
}
int main(void)
{
    const uint8_t ds[]={3,1,6};
    uint8_t ht[24]={61,22,11};
    uint8_t both[27]={3,1,6,61,22,11};
    const uint8_t invalid[]={3,1,14}, cross[]={3,1,36};
    const uint8_t short_ds[]={3,0}, short_ht[]={61,1,11};
    const uint8_t truncated[]={3,1};
    check(3,ds,sizeof(ds),6);
    check(6,ds,sizeof(ds),6);
    check(3,ht,sizeof(ht),11);
    check(3,both,sizeof(both),6);
    check(3,invalid,sizeof(invalid),0);
    check(3,cross,sizeof(cross),0);
    check(3,short_ds,sizeof(short_ds),3);
    check(3,short_ht,sizeof(short_ht),3);
    check(3,truncated,sizeof(truncated),3);
    check(60,NULL,0,60);
    ht[2]=36; check(60,ht,sizeof(ht),36);
    ht[2]=6; check(60,ht,sizeof(ht),0);
    check(60,ds,sizeof(ds),60);

    /* A mobility-domain element lands in the per-BSSID FT cache. */
    struct aicwf_softc sc;
    static uint8_t ind[160];
    size_t o;

    memset(&sc, 0, sizeof(sc));
    sc.sc_if_attached = true;
    for (unsigned i = 1; i <= 13; i++) sc.sc_ic.ic_channels[i].ic_freq = 2407 + 5 * i;
    sc.sc_ic.ic_channels[36].ic_freq = 5180;
    sc.sc_ic.ic_flags = IEEE80211_F_SCAN;
    o = 12;
    ind[o++] = 0x80; ind[o++] = 0; ind[o++] = 0; ind[o++] = 0;
    memcpy(ind + o, "\0\0\0\0\0\0", 6); o += 6;
    memcpy(ind + o, "\x11\x22\x33\x44\x55\x66", 6); o += 6;
    memcpy(ind + o, "\x11\x22\x33\x44\x55\x66", 6); o += 6;
    ind[o++] = 0; ind[o++] = 0;
    memset(ind + o, 0, 8); o += 8;
    ind[o++] = 100; ind[o++] = 0;
    ind[o++] = 0x21; ind[o++] = 0x04;
    ind[o++] = 0; ind[o++] = 4; memcpy(ind + o, "test", 4); o += 4;
    ind[o++] = 1; ind[o++] = 4; memcpy(ind + o, "\x0b\x16\x2c\x82", 4); o += 4;
    ind[o++] = 54; ind[o++] = 6;
    ind[o++] = 0x11; ind[o++] = 0x58;
    ind[o++] = 3; ind[o++] = 0xaa; ind[o++] = 0xbb; ind[o++] = 0xcc;
    ind[0] = (o - 12) & 0xff; ind[1] = (o - 12) >> 8;
    ind[4] = 5180 & 0xff; ind[5] = 5180 >> 8;
    ind[9] = (uint8_t)-50;
    aicwf_scan_result(&sc, ind, o);
    if (sc.sc_ft_bss[0].mdid != 0x5811 || sc.sc_ft_bss[0].r0kh_len != 3 ||
        sc.sc_ft_bss[0].r0kh[0] != 0xaa) {
        printf("FT1 fail mdid=%u r0kh_len=%u\n",
            sc.sc_ft_bss[0].mdid, sc.sc_ft_bss[0].r0kh_len);
        assert(0);
    }

    /* A truncated MDIE (two octets only) must be ignored. */
    memset(&sc, 0, sizeof(sc));
    sc.sc_if_attached = true;
    for (unsigned i = 1; i <= 13; i++) sc.sc_ic.ic_channels[i].ic_freq = 2407 + 5 * i;
    sc.sc_ic.ic_channels[36].ic_freq = 5180;
    sc.sc_ic.ic_flags = IEEE80211_F_SCAN;
    o = 12;
    ind[o++] = 0x80; ind[o++] = 0; ind[o++] = 0; ind[o++] = 0;
    memcpy(ind + o, "\0\0\0\0\0\0", 6); o += 6;
    memcpy(ind + o, "\x11\x22\x33\x44\x55\x66", 6); o += 6;
    memcpy(ind + o, "\x11\x22\x33\x44\x55\x66", 6); o += 6;
    ind[o++] = 0; ind[o++] = 0;
    memset(ind + o, 0, 8); o += 8;
    ind[o++] = 100; ind[o++] = 0;
    ind[o++] = 0x21; ind[o++] = 0x04;
    ind[o++] = 0; ind[o++] = 4; memcpy(ind + o, "test", 4); o += 4;
    ind[o++] = 1; ind[o++] = 4; memcpy(ind + o, "\x0b\x16\x2c\x82", 4); o += 4;
    ind[o++] = 54; ind[o++] = 2;
    ind[o++] = 0x11; ind[o++] = 0x58;
    ind[0] = (o - 12) & 0xff; ind[1] = (o - 12) >> 8;
    ind[4] = 5180 & 0xff; ind[5] = 5180 >> 8;
    ind[9] = (uint8_t)-50;
    aicwf_scan_result(&sc, ind, o);
    if (sc.sc_ft_bss[0].mdid != 0) {
        printf("FT2 fail mdid=%u\n", sc.sc_ft_bss[0].mdid);
        assert(0);
    }

    puts("PASS: 15 scan channel, band and mobility-domain cases");
    return 0;
    return 0;
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror "$work/check.c" -o "$work/check"
"$work/check"
