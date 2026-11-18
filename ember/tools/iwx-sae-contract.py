#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors. AI-assisted (OpenAI Codex).
"""Exercise production iwx management admission and mbuf replacement."""
import argparse
import json
import os
import pathlib
import shlex
import subprocess


def function(source, name):
    start = source.index("\n" + name + "(") + 1
    end = source.index("\n}\n", start) + 3
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--cflags", default="")
    parser.add_argument("--build-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    driver = root / "ember/development/iwx-be200/src"
    source = (driver / "if_iwx.c").read_text()
    prelude = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "if_iwx_ht.h"
#define NBPFILTER 0
#define IEEE80211_S_INIT 0
#define IEEE80211_S_SCAN 1
#define IEEE80211_F_SCAN 0x10
#define IEEE80211_S_RUN 4
#define IWX_FLAG_BGSCAN 0x200
#define IEEE80211_FC0_TYPE_MASK 0x0c
#define IEEE80211_FC0_TYPE_MGT 0
#define IEEE80211_FC0_SUBTYPE_MASK 0xf0
#define IEEE80211_FC0_SUBTYPE_ASSOC_REQ 0
#define IEEE80211_FC0_SUBTYPE_REASSOC_REQ 0x20
#define IEEE80211_FC0_SUBTYPE_BEACON 0x80
#define IEEE80211_FC0_SUBTYPE_PROBE_RESP 0x50
#define IEEE80211_FC0_SUBTYPE_ACTION 0xd0
#define IEEE80211_MAX_LEN 2312
#define IEEE80211_CHAN_ANYC NULL
#define M_DONTWAIT 0
#define MT_DATA 1
#define M_EXT 1
#define MHLEN 128
#define IWX_HT_ASSOC_MAX 512
#define nitems(a) (sizeof(a)/sizeof((a)[0]))
#define atomic_load_relaxed(p) (*(p))
#define mtod(m,t) ((t)(m)->data)
#define IC2IFP(ic) ((ic)->ic_ifp)
struct ifnet { void *if_softc; unsigned if_oerrors; };
#define if_statinc(ifp,field) ((ifp)->field++)
struct ieee80211_frame { uint8_t i_fc[2], i_dur[2], i_addr1[6], i_addr2[6],
    i_addr3[6], i_seq[2]; };
#define IEEE80211_ADDR_EQ(a,b) (memcmp((a),(b),6) == 0)
#define IEEE80211_ADDR_LEN 6
#define IEEE80211_ADDR_COPY(d,s) memcpy((d),(s),6)
struct ieee80211_frame_min { uint8_t bytes[16]; };
struct ieee80211_channel { int number; };
struct ieee80211_node { struct ieee80211_channel *ni_chan; unsigned refs;
    uint8_t ni_bssid[6]; int8_t ni_rssi_dbm; bool ni_rssi_dbm_valid; };
struct mbuf { int m_len, m_flags; struct { int len; } m_pkthdr;
    uint64_t tag; uint8_t data[4096]; };
struct ieee80211com {
    int ic_state; struct ieee80211_channel ic_channels[16];
    struct ieee80211_channel *ic_ibss_chan, *ic_curchan;
    void (*ic_recv_mgmt)(struct ieee80211com *, struct mbuf *,
        struct ieee80211_node *, int, int, uint32_t);
    struct ifnet *ic_ifp;
    struct ieee80211_node *ic_bss;
    int ic_scan, ic_flags; void *ic_rawbpf;
    struct { unsigned is_rx_chanmismatch; } ic_stats;
};
struct iwx_softc {
    struct ieee80211com sc_ic;
    void (*sc_recv_mgmt)(struct ieee80211com *, struct mbuf *,
        struct ieee80211_node *, int, int, uint32_t);
    uint8_t sc_ht_buf[2][IWX_HT_ASSOC_MAX];
    int sc_ht_mode, sc_flags;
};
struct ieee80211_rxinfo { int rxi_rssi; uint32_t rxi_tstamp; };
static int admitted, assoc, action, csa, ordinary, failalloc, failcluster, failtags;
static int freed, iwx_ht_mode = 1;
static struct ieee80211_node rxnode;
static int splnet(void) { return 0; }
static void splx(int s) { assert(s == 0); }
static int ieee80211_chan2ieee(struct ieee80211com *ic,
    struct ieee80211_channel *c) { assert(ic != NULL); return c->number; }
static struct ieee80211_node *ieee80211_find_rxnode(struct ieee80211com *ic,
    struct ieee80211_frame_min *wh) { assert(ic && wh); return &rxnode; }
static void ieee80211_free_node(struct ieee80211_node *ni)
    { assert(ni == &rxnode); }
static void m_set_rcvif(struct mbuf *m, void *ifp) { assert(m && ifp); }
static int scan_primary = -1;
static int scan_malformed;
static bool ieee80211_sta_beacon_offchannel(const uint8_t *body, size_t len,
    unsigned chan) { assert(body && len >= 12); return !scan_malformed &&
    scan_primary >= 0 && scan_primary != (int)chan; }
static void iwx_ht_rx_assoc(struct iwx_softc *sc, struct mbuf *m)
    { assert(sc && m); assoc++; }
static void iwx_ht_rx_action(struct iwx_softc *sc, struct mbuf *m)
    { assert(sc && m); action++; }
static int scan_observe_calls, scan_observe_result = 1;
static unsigned scan_observe_chan; static int scan_observe_rssi;
static int8_t scan_observe_dbm; static uint32_t scan_observe_stamp;
static bool ieee80211_scan_observe(struct ieee80211com *ic, const void *frame,
    size_t len, unsigned int chan, int rssi, int8_t dbm, uint32_t stamp)
    { assert(ic && frame && len >= 36); scan_observe_calls++;
      scan_observe_chan=chan; scan_observe_rssi=rssi; scan_observe_dbm=dbm;
      scan_observe_stamp=stamp; return scan_observe_result; }
static void iwx_csa_rx(struct iwx_softc *sc, struct mbuf *m)
    { assert(sc && m); assert(assoc == 1 && action == 1); csa++; }
static void saved_recv(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, int subtype, int rssi, uint32_t stamp)
    { assert(ic && m && ni && subtype == 0 && rssi == -42 && stamp == 99);
      assert(assoc == 1 && action == 1 && csa == 1); ordinary++; }
static void ieee80211_input(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, int rssi, uint32_t stamp)
    { if (admitted) ic->ic_recv_mgmt(ic,m,ni,0,rssi,stamp); free(m); }
static void iwx_ht_local(struct iwx_softc *sc, struct ieee80211_channel *c,
    struct iwx_ht_local *loc) { assert(sc && c); memset(loc,0,sizeof(*loc)); loc->nss=1; }
static void iwx_ht_print_ies(struct iwx_softc *sc, const char *what,
    const uint8_t *p, size_t n) { assert(sc && what && p && n); }
static struct mbuf *m_gethdr(int how, int type)
    { assert(how == 0 && type == 1); return failalloc ? NULL : calloc(1,sizeof(struct mbuf)); }
static void m_cluster(struct mbuf *m, int how)
    { assert(how == 0); if (!failcluster) m->m_flags |= M_EXT; }
#define MCLGET(m,how) m_cluster(m,how)
static void m_freem(struct mbuf *m) { freed++; free(m); }
static void m_copydata(struct mbuf *m, size_t off, size_t len, void *out)
    { assert(off+len <= (size_t)m->m_pkthdr.len); memcpy(out,m->data+off,len); }
int m_tag_copy_chain(struct mbuf *n, const struct mbuf *m)
    { if (failtags) return 0; n->tag=m->tag; return 1; }
static struct ieee80211_node *ieee80211_ref_node(struct ieee80211_node *ni)
    { assert(ni == &rxnode); ni->refs++; return ni; }
static struct mbuf *ieee80211_getmgtframe(uint8_t **p, unsigned len)
    { struct mbuf *m=m_gethdr(0,1); assert(len<=IEEE80211_MAX_LEN);
      if (m) { *p=m->data; }
      return m; }
static unsigned queued; static int output_error;
static int ieee80211_mgmt_output(struct ieee80211com *ic,
    struct ieee80211_node *ni, struct mbuf *m, int subtype, int timer,
    int wme, const void *ht, const void *vht, const void *chan, void *ba)
    { assert(ic && ni==&rxnode && subtype==0xd0 && timer==0 && wme==0);
      assert(!ht && !vht && !chan && !ba && m->m_len==9);
      assert(m->data[0]==3 && m->data[1]==1 && m->data[2]==7);
      if (!output_error) { queued++; }
      m_freem(m); return output_error; }
'''
    # Node references are observed by both RX and action output services.
    prelude = prelude.replace(
        'assert(ni == &rxnode); }',
        'assert(ni == &rxnode); if (ni->refs) ni->refs--; }', 1)
    definitions = "void\n" + function(source, "iwx_rx_frame")
    if "\niwx_recv_mgmt(" in source:
        definitions += "void\n" + function(source, "iwx_recv_mgmt")
        hook = "iwx_recv_mgmt"
    else:
        hook = "saved_recv"
    definitions += "struct mbuf *\n" + function(source, "iwx_ht_tx_assoc")
    output = (root / "sys/net80211/ieee80211_output.c").read_text()
    definitions += "int\n" + function(output, "ieee80211_send_action")
    definitions += "void\n" + function(source, "iwx_ht_send_action")
    tests = r'''
int main(int argc, char **argv)
{
    struct iwx_softc sc = {0}; struct ieee80211_rxinfo rxi = {-42,99};
    struct ifnet ifp = { .if_softc = &sc }; sc.sc_ic.ic_ifp = &ifp;
    struct mbuf *m, *n; struct ieee80211_node node;
    sc.sc_ic.ic_recv_mgmt = HOOK; sc.sc_recv_mgmt = saved_recv;
    if (argc == 1 || strcmp(argv[1], "admission") == 0) {
        m=calloc(1,sizeof(*m)); assert(m != NULL);
        m->m_len=m->m_pkthdr.len=24;
        iwx_rx_frame(&sc,m,1,0,0,0,0,&rxi);
        assert(assoc == 0 && action == 0 && ordinary == 0);
        m=calloc(1,sizeof(*m)); assert(m != NULL);
        m->m_len=m->m_pkthdr.len=24;
        admitted=1; iwx_rx_frame(&sc,m,1,0,0,0,0,&rxi);
        assert(assoc == 1 && action == 1 && ordinary == 1);
        /* Only scan observations bypass native input, and the common owner
         * validates beacon primary channel and records the result. */
#define RX(home,rxchan) do { \
    m=calloc(1,sizeof(*m)); assert(m != NULL); \
    m->m_len=m->m_pkthdr.len=36; m->data[0]=0x80; \
    memset(m->data+10,0x07,6); memset(m->data+16,0x07,6); \
    if (home==1 || home==2) memcpy(m->data+10,rxnode.ni_bssid,6); \
    if (home==1 || home==3) memcpy(m->data+16,rxnode.ni_bssid,6); \
    assoc=action=csa=0; iwx_rx_frame(&sc,m,rxchan,0,0,0,0,&rxi); \
} while (0)
        sc.sc_ic.ic_bss = &rxnode; sc.sc_ic.ic_state = IEEE80211_S_RUN;
        sc.sc_ic.ic_channels[1].number=1;
        rxnode.ni_chan = &sc.sc_ic.ic_channels[1];
        memset(rxnode.ni_bssid, 0x02, 6); admitted=1;
        sc.sc_flags=IWX_FLAG_BGSCAN; freed=0; RX(false,1);
        assert(scan_observe_calls==1 && scan_observe_chan==1 &&
            scan_observe_rssi==-42 && scan_observe_dbm==-127 &&
            scan_observe_stamp==99 && ordinary==1 && freed==1);
        /* Foreground SCAN and pre-ACK INIT observe even a repeated BSSID. */
        sc.sc_flags=0; sc.sc_ic.ic_flags=IEEE80211_F_SCAN;
        sc.sc_ic.ic_state=IEEE80211_S_SCAN; freed=0; RX(true,1);
        assert(scan_observe_calls==2 && ordinary==1 && freed==1);
        sc.sc_ic.ic_state=IEEE80211_S_INIT; freed=0; RX(true,1);
        assert(scan_observe_calls==3 && ordinary==1 && freed==1);
        /* In foreground RUN, exact home frames retain native management
         * effects even while F_SCAN is set; foreign frames are scan-only. */
        sc.sc_ic.ic_state=IEEE80211_S_RUN; sc.sc_flags=0;
        sc.sc_ic.ic_flags=IEEE80211_F_SCAN; freed=0; RX(2,1);
        assert(scan_observe_calls==4 && ordinary==1 && freed==1);
        freed=0; RX(3,1);
        assert(scan_observe_calls==5 && ordinary==1 && freed==1);
        sc.sc_ic.ic_flags=IEEE80211_F_SCAN; freed=0; RX(true,1);
        assert(scan_observe_calls==5 && ordinary==2 && freed==0);
        freed=0; RX(false,1);
        assert(scan_observe_calls==6 && ordinary==2 && freed==1);
        /* Foreign receive channel and an explicit advertised-primary
         * mismatch cannot update the live home association. */
        sc.sc_ic.ic_flags=0; sc.sc_flags=IWX_FLAG_BGSCAN;
        freed=0; RX(true,7);
        assert(ordinary==2 && scan_observe_calls==6 && freed==1);
        sc.sc_flags=0; sc.sc_ic.ic_flags=IEEE80211_F_SCAN;
        scan_primary=7; freed=0; RX(true,1);
        assert(ordinary==2 && scan_observe_calls==6 && freed==1);
        /* Malformed home IE framing remains on native input so its CSA
         * fail-closed handler can withdraw the live connection. */
        scan_primary=-1; scan_malformed=1; freed=0; RX(true,1);
        assert(ordinary==3 && scan_observe_calls==6 && freed==0);
        scan_malformed=0;
        /* Background home frames are native only on the configured home
         * channel and with valid advertised channel information. */
        sc.sc_ic.ic_flags=0; sc.sc_flags=IWX_FLAG_BGSCAN; freed=0;
        RX(true,1);
        assert(scan_observe_calls==6 && ordinary==4 && freed==0);
        /* Foreground INIT also observes a different BSS. */
        sc.sc_flags=0; sc.sc_ic.ic_flags=IEEE80211_F_SCAN;
        sc.sc_ic.ic_state=IEEE80211_S_INIT; freed=0; RX(false,1);
        assert(scan_observe_calls==7 && ordinary==4 && freed==1);
        sc.sc_ic.ic_flags=0; sc.sc_flags=0;
        /* Native home input continues to retain its dBm on the owned node. */
        sc.sc_ic.ic_flags=0; sc.sc_flags=0; sc.sc_ic.ic_state=IEEE80211_S_RUN;
        rxnode.ni_rssi_dbm_valid=false; RX(true,1);
        assert(rxnode.ni_rssi_dbm_valid &&
            rxnode.ni_rssi_dbm==iwx_ht_dbm(rxi.rxi_rssi));
#undef RX
        freed = 0; sc.sc_flags = 0;
        assoc = action = csa = 1; ordinary = 1;
        sc.sc_ic.ic_state = 0; sc.sc_ic.ic_bss = NULL;
    }
    if (argc == 1 || strcmp(argv[1], "tags") == 0) {
        node.ni_chan=&sc.sc_ic.ic_channels[1];
        for (int failure=0;failure<4;failure++) {
            failalloc=failure==1; failcluster=failure==2; failtags=failure==3;
            m=calloc(1,sizeof(*m)); assert(m != NULL);
            m->m_len=m->m_pkthdr.len=128;
            m->tag=UINT64_C(0x123456789abcdef0);
            /* Valid vendor IE makes replacement exceed inline mbuf space. */
            m->data[28]=221; m->data[29]=98;
            n=iwx_ht_tx_assoc(&sc,m,&node);
            assert(n->tag == UINT64_C(0x123456789abcdef0));
            assert(failure ? n==m : n!=m); free(n);
        }
        assert(freed == 3);
        /* The mode is taken over by association requests only. */
        assert(sc.sc_ht_mode == 1);
        m=calloc(1,sizeof(*m)); assert(m != NULL);
        m->m_len=m->m_pkthdr.len=128; iwx_ht_mode=3;
        m->data[0]=0x40; n=iwx_ht_tx_assoc(&sc,m,&node);
        assert(n == m && sc.sc_ht_mode == 1);
        m->data[0]=0x20; n=iwx_ht_tx_assoc(&sc,m,&node);
        assert(sc.sc_ht_mode == 3); free(n);
        /* Legacy mode sends the request as net80211 built it. */
        m=calloc(1,sizeof(*m)); assert(m != NULL);
        m->m_len=m->m_pkthdr.len=128; iwx_ht_mode=0;
        n=iwx_ht_tx_assoc(&sc,m,&node);
        assert(n == m && sc.sc_ht_mode == 0); free(m);
        iwx_ht_mode=1;
    }
    if (argc == 1 || strcmp(argv[1], "action") == 0) {
        const uint8_t body[9]={3,1,7}; sc.sc_ic.ic_bss=&rxnode;
        failalloc=failcluster=failtags=0;
        assert(ieee80211_send_action(&sc.sc_ic,&rxnode,body,1)==EINVAL);
        assert(ieee80211_send_action(&sc.sc_ic,&rxnode,body,SIZE_MAX)==EINVAL);
        failalloc=1;
        assert(ieee80211_send_action(&sc.sc_ic,&rxnode,body,9)==ENOMEM);
        assert(rxnode.refs==0 && queued==0); failalloc=0;
        output_error=ENOBUFS; iwx_ht_send_action(&sc,body,9);
        assert(rxnode.refs==0 && queued==0 && ifp.if_oerrors==1);
        output_error=EACCES; iwx_ht_send_action(&sc,body,9);
        assert(rxnode.refs==0 && queued==0 && ifp.if_oerrors==2);
        output_error=0; iwx_ht_send_action(&sc,body,9);
        assert(rxnode.refs==1 && queued==1); ieee80211_free_node(&rxnode);
    }
    rxnode.ni_chan = NULL;
    puts("PASS: iwx scan routing, management admission, and action output");
    return 0;
}
'''.replace("HOOK", hook)
    args.build_dir.mkdir(parents=True, exist_ok=True)
    path = args.build_dir / "iwx-sae.c"
    path.write_text(prelude + definitions + tests)
    command = [args.cc, "-std=c99", "-Wall", "-Werror",
               *shlex.split(args.cflags), "-I" + str(driver), str(path),
               "-o", str(args.build_dir / "iwx-sae")]
    (args.build_dir / "compile_commands.json").write_text(json.dumps([
        {"directory": str(root), "file": str(path), "arguments": command}], indent=2))
    subprocess.run(command, check=True)
    subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                   [str(args.build_dir / "iwx-sae")], check=True)


if __name__ == "__main__":
    main()
