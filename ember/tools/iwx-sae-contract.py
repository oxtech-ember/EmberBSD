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
#define IEEE80211_S_SCAN 1
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
struct ieee80211_frame { uint8_t i_fc[2], rest[22]; };
struct ieee80211_frame_min { uint8_t bytes[24]; };
struct ieee80211_channel { int number; };
struct ieee80211_node { struct ieee80211_channel *ni_chan; unsigned refs; };
struct mbuf { int m_len, m_flags; struct { int len; } m_pkthdr;
    uint64_t tag; uint8_t data[4096]; };
struct ieee80211com {
    int ic_state; struct ieee80211_channel ic_channels[16];
    struct ieee80211_channel *ic_ibss_chan, *ic_curchan;
    void (*ic_recv_mgmt)(struct ieee80211com *, struct mbuf *,
        struct ieee80211_node *, int, int, uint32_t);
    struct ifnet *ic_ifp;
    struct ieee80211_node *ic_bss;
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
static void ieee80211_free_node(struct ieee80211_node *ni) { assert(ni == &rxnode); }
static void m_set_rcvif(struct mbuf *m, void *ifp) { assert(m && ifp); }
static void iwx_ht_rx_assoc(struct iwx_softc *sc, struct mbuf *m)
    { assert(sc && m); assoc++; }
static void iwx_ht_rx_action(struct iwx_softc *sc, struct mbuf *m)
    { assert(sc && m); action++; }
static int scanrx, scanrx_calls;
static int iwx_scan_assoc_rx(struct iwx_softc *sc, struct mbuf *m, int chanidx,
    struct ieee80211_rxinfo *rxi)
    { assert(sc && m && rxi && chanidx == 1); scanrx_calls++; return scanrx; }
static void iwx_csa_rx(struct iwx_softc *sc, struct mbuf *m)
    { assert(sc && m); assert(assoc == 1 && action == 1); csa++; }
static void saved_recv(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, int subtype, int rssi, uint32_t stamp)
    { assert(ic && m && ni && subtype == 0 && rssi == -42 && stamp == 99);
      assert(assoc == 1 && action == 1 && csa == 1); ordinary++; }
static void ieee80211_input(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, int rssi, uint32_t stamp)
    { if (admitted) ic->ic_recv_mgmt(ic,m,ni,0,rssi,stamp); }
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
        admitted=1; iwx_rx_frame(&sc,m,1,0,0,0,0,&rxi);
        assert(assoc == 1 && action == 1 && ordinary == 1);
        /* Only a scan while associated diverts frames. */
#define RX() do { assoc = action = csa = 0; \
    iwx_rx_frame(&sc,m,1,0,0,0,0,&rxi); } while (0)
        assert(scanrx_calls == 0 && rxnode.ni_chan == &sc.sc_ic.ic_channels[1]);
        sc.sc_flags = IWX_FLAG_BGSCAN; scanrx = 1; RX();
        assert(scanrx_calls == 0 && ordinary == 2);
        sc.sc_flags = 0; sc.sc_ic.ic_state = IEEE80211_S_RUN; RX();
        assert(scanrx_calls == 0 && ordinary == 3);
        /* A frame of the BSS in use takes the usual path and keeps
         * the channel of that BSS; a result of another BSS is consumed. */
        sc.sc_flags = IWX_FLAG_BGSCAN; scanrx = 0;
        sc.sc_ic.ic_bss = &rxnode; rxnode.ni_chan = &sc.sc_ic.ic_channels[7];
        RX();
        assert(scanrx_calls == 1 && ordinary == 4);
        assert(rxnode.ni_chan == &sc.sc_ic.ic_channels[7]);
        scanrx = 1; freed = 0; RX();
        assert(scanrx_calls == 2 && ordinary == 4 && freed == 1);
        assert(rxnode.ni_chan == &sc.sc_ic.ic_channels[7]);
#undef RX
        freed = 0; scanrx = 0; sc.sc_flags = 0;
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
    puts("PASS: iwx management admission, tag ownership and common action output");
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
