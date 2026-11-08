#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors. AI-assisted (OpenAI Codex).
"""Exercise actual iwx native SAE firmware transactions and RX provenance."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess


def function(source, name):
    match = re.search(r'^' + name + r'\([^;]*?\n\{', source, re.M)
    if match is None:
        raise ValueError(name)
    return source[match.start():source.index('\n}', match.end()) + 2]


def structure(source, name):
    return re.search(r'^struct ' + name + r' \{.*?^}[^;]*;',
                     source, re.M | re.S).group()


def macro(source, name):
    lines = source.splitlines()
    for index, line in enumerate(lines):
        if re.match(r'^#define\s+' + re.escape(name) + r'\b', line):
            result = [line]
            while result[-1].rstrip().endswith('\\'):
                index += 1
                result.append(lines[index])
            return '\n'.join(result)
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--cflags', default='')
    parser.add_argument('--mutations', action='store_true',
                        help='prove the fixture rejects representative broken implementations')
    parser.add_argument('--build-dir', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    driver = root / 'ember/development/iwx-be200/src'
    source = (driver / 'if_iwx.c').read_text()
    register = (driver / 'if_iwxreg.h').read_text()
    variables = (driver / 'if_iwxvar.h').read_text()
    wire = (root / 'sys/net80211/ieee80211.h').read_text()
    names = ('IWX_DATA_PATH_GROUP', 'IWX_TRIGGER_RX_QUEUES_NOTIF_CMD',
             'IWX_SEC_KEY_CMD', 'IWX_CMD_FAILED_MSK', 'IWX_FW_CTXT_ACTION_ADD',
             'IWX_FW_CTXT_ACTION_REMOVE', 'IWX_WIDE_ID',
             'IWX_FH_RSCSR_FRAME_SIZE_MSK', 'IWX_RX_MPDU_RES_STATUS_MIC_OK',
             'IWX_RX_MPDU_RES_STATUS_SEC_CCM_ENC',
             'IWX_RX_MPDU_RES_STATUS_SEC_ENC_MSK', 'IWX_RX_MPDU_MFLG2_AMSDU',
             'IWX_RX_MPDU_AMSDU_SUBFRAME_IDX_MASK',
             'IWX_RX_MPDU_AMSDU_LAST_SUBFRAME',
             'IWX_RX_MPDU_RES_STATUS_CRC_OK', 'IWX_RX_MPDU_RES_STATUS_OVERRUN_OK',
             'IWX_RX_MPDU_MFLG1_MIC_CRC_LEN_MASK', 'IWX_RX_MPDU_MFLG1_MIC_CRC_LEN_SHIFT',
             'IWX_RX_MPDU_MFLG2_PAD')
    abi = '\n'.join(re.search(r'^#define\s+' + name + r'\b[^\n]*',
                              register, re.M).group() for name in names)
    abi += '\n' + re.search(r'^#define IWX_RX_DESC_SIZE_V1[^\n]*\n[^\n]*', register, re.M).group()
    abi += '\n' + '\n'.join(structure(wire, name) for name in (
        'ieee80211_frame_cts', 'ieee80211_frame_ack', 'ieee80211_frame_min'))
    abi += '\n' + re.search(r'^enum iwx_sec_key_flags \{.*?^};',
                             register, re.M | re.S).group()
    abi += '\n' + '\n'.join(structure(register, name) for name in (
        'iwx_rxq_sync_cmd', 'iwx_rxq_sync_notification', 'iwx_sec_key_cmd',
        'iwx_cmd_header', 'iwx_rx_packet', 'iwx_rx_mpdu_desc_v1',
        'iwx_rx_mpdu_desc_v3', 'iwx_rx_mpdu_desc'))
    abi += '\n' + re.search(r'^#define IWX_MAX_CMD_TBS_PER_TFD[^\n]*', variables, re.M).group()
    abi += '\n' + re.search(r'^enum IWX_CMD_MODE \{.*?^};', variables, re.M | re.S).group()
    abi += '\n' + structure(variables, 'iwx_host_cmd')
    abi += '\n#define PAGE_SIZE 4096\n' + re.search(r'^#define IWX_CMD_RESP_MAX[^\n]*', variables, re.M).group()
    for name in ('iwx_cmd_opcode', 'iwx_cmd_groupid'):
        abi += '\nstatic inline uint8_t\n' + function(register, name)

    abi += '\nstatic uint32_t\n' + function(register, 'iwx_rx_packet_len')
    abi += '\nstatic uint32_t\n' + function(register, 'iwx_rx_packet_payload_len')
    irq_source = '\n'.join(function(source, name) for name in
                           ('iwx_intr', 'iwx_intr_msix', 'iwx_check_rfkill'))
    needed = set(re.findall(r'\bIWX_[A-Z0-9_]+', irq_source))
    ignored = {'IWX_READ', 'IWX_WRITE', 'IWX_WRITE_1'}
    collected = {}
    while needed:
        name = needed.pop()
        if name in ignored or name in collected:
            continue
        definition = macro(register + '\n' + variables, name)
        if definition is not None:
            collected[name] = definition
            needed.update(set(re.findall(r'\bIWX_[A-Z0-9_]+', definition)) - collected.keys())
    abi += '\n' + '\n'.join(collected.values())
    for name in ('msix_fh_int_causes', 'mix_hw_int_causes'):
        abi += '\n' + re.search(r'^enum ' + name + r' \{.*?^};', register, re.M | re.S).group()

    prelude = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define __packed __attribute__((packed))
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define htole16(x) ((uint16_t)(x))
#define le16toh(x) ((uint16_t)(x))
#define htole32(x) ((uint32_t)(x))
#define htole64(x) ((uint64_t)(x))
#define le32toh(x) ((uint32_t)(x))
#define le64toh(x) ((uint64_t)(x))
#else
#define htole16(x) __builtin_bswap16(x)
#define le16toh(x) __builtin_bswap16(x)
#define htole32(x) __builtin_bswap32(x)
#define htole64(x) __builtin_bswap64(x)
#define le32toh(x) __builtin_bswap32(x)
#define le64toh(x) __builtin_bswap64(x)
#endif
#include "ieee80211_sae.h"
typedef uint8_t u_int8_t;
#define IEEE80211_ADDR_LEN 6
#define IEEE80211_FC0_TYPE_MASK 0x0c
#define IEEE80211_FC0_TYPE_CTL 0x04
#define IEEE80211_FC0_SUBTYPE_MASK 0xf0
#define IEEE80211_FC0_SUBTYPE_CTS 0xc0
#define IEEE80211_FC0_SUBTYPE_ACK 0xd0
#define IEEE80211_CIPHER_AES_CCM 3
#define IEEE80211_CCMP_HDRLEN 8
#define IEEE80211_M_MONITOR 6
#define IEEE80211_FC1_WEP 64
#define IEEE80211_IS_MULTICAST(a) ((a)[0]&1)
#define IWX_STATION_ID 0
#define IWX_MAX_BAID 4
#define IWX_FLAG_STA_ACTIVE 0x20
#define IFF_RUNNING 1
#define IFF_DEBUG 4
#define PCATCH 1
#define SEC_TO_NSEC(n) ((n)*1000000000ULL)
#define IC2IFP(ic) ((ic)->ic_ifp)
#define mtod(m,t) ((t)((m)->m_data ? (m)->m_data : (m)->data))
struct ifnet { void *if_softc; unsigned if_flags; };
struct ieee80211com { struct ifnet *ic_ifp; void *ic_bss;
    int ic_opmode; bool active; uint64_t serial; };
struct ieee80211_frame { uint8_t i_fc[2], duration[2], i_addr1[6],
    i_addr2[6], i_addr3[6], seq[2]; };
struct mbuf { int m_len; uint8_t *m_data; struct { int len; } m_pkthdr; uint8_t data[256]; };
struct iwx_softc {
    struct ieee80211com sc_ic;
    uint64_t sc_sae_epoch, sc_sae_rx_cookie;
    unsigned sc_sae_keys, sc_flags, sc_ba_start, sc_ba_stop, sc_txq_want;
    int sc_generation, init_task; void *sc_systq;
    bool sc_sae_replacing, sc_sae_rx_pending;
    struct { void *vaddr; } ict_dma;
    unsigned ict_cur, sc_intmask, sc_fh_mask, sc_hw_mask;
    int sc_fw_chunk_done, sc_fw, sc_device_family, rxq;
    void *sc_dev;
    struct { bool active; int ro; } sc_ba[IWX_MAX_BAID];
};
static unsigned checks;
#define CHECK(e) do { checks++; if (!(e)) { fprintf(stderr,"FAIL %u: %s\n",(unsigned)__LINE__,#e); exit(1); } } while (0)
static unsigned boundaries, inject_at, fail_at, ack_fail_at, missing_at, replies, freed_replies, resets, drops, wakes;
static bool inject_generation, immediate_sync = true;
static int sleep_error, tag_error;
static unsigned response_shape;
static uint32_t irq_legacy, irq_fh, irq_msix_fh, irq_msix_hw, irq_gp;
static unsigned irq_retired, irq_accepted, irq_blocked, irq_restored;
static char events[128]; static unsigned nevents;
static unsigned tagged_id, tagged_flags, tagged_index;
static uint64_t tagged_epoch;
static bool tagged_last;
static void event(char c) { CHECK(nevents < sizeof(events)); events[nevents++]=c; }
static int boundary(struct iwx_softc *sc) {
    boundaries++;
    if (boundaries == inject_at) {
        if (inject_generation) sc->sc_generation++; else sc->sc_ic.serial++;
    }
    return boundaries == fail_at ? EIO : 0;
}
static void explicit_memset(void *p, int c, size_t n) { memset(p,c,n); }
bool ieee80211_sae_active(const struct ieee80211com *ic) { return ic->active; }
uint64_t ieee80211_sae_serial(const struct ieee80211com *ic) { return ic->serial; }
static unsigned ieee80211_anyhdrsize(const struct ieee80211_frame *wh) {
    return 24 + ((wh->i_fc[1]&3)==3 ? 6 : 0) + ((wh->i_fc[0]&0x8c)==0x88 ? 2 : 0);
}
static void m_adj(struct mbuf *m, unsigned n) {
    CHECK(n <= (unsigned)m->m_len); m->m_data+=n; m->m_len-=n; m->m_pkthdr.len-=n;
}
static void iwx_ba_drop(void *arg, void *m) { (void)arg; (void)m; drops++; }
static void iwx_ht_reorder_flush(int *ro, void (*drop)(void *,void *), void *arg) {
    (void)ro; drop(arg,NULL);
}
static void task_add(void *q, int *task) { (void)q; (void)task; resets++; }
static void wakeup(void *p) { (void)p; wakes++; }
static int iwx_flush_sta(struct iwx_softc *sc, void *node) {
    (void)node; event('F'); return boundary(sc);
}
int ieee80211_sae_tag_rx(struct ieee80211com *ic, struct mbuf *m,
    uint64_t epoch, uint16_t id, uint8_t cipher, unsigned flags) {
    (void)ic; (void)m; CHECK(cipher==3);
    tagged_epoch=epoch; tagged_id=id; tagged_flags=flags; tagged_index=0;
    return tag_error;
}
int ieee80211_sae_tag_rx_amsdu(struct ieee80211com *ic, struct mbuf *m,
    uint64_t epoch, uint16_t id, uint8_t cipher, unsigned flags,
    unsigned index, bool last) {
    int error=ieee80211_sae_tag_rx(ic,m,epoch,id,cipher,flags);
    tagged_index=index; tagged_last=last; return error;
}
'''
    services = r'''
void iwx_sae_rx_synced(struct iwx_softc *, struct iwx_rx_packet *);
static struct iwx_sec_key_cmd commands[32]; static unsigned ncommands;
static void notification(struct iwx_softc *sc, uint64_t cookie, unsigned count,
    unsigned payload) {
    uint8_t bytes[sizeof(struct iwx_rx_packet)+sizeof(struct iwx_rxq_sync_notification)]={0};
    struct iwx_rx_packet *p=(void *)bytes;
    struct iwx_rxq_sync_notification *n=(void *)p->data;
    p->len_n_flags=htole32(sizeof(p->hdr)+payload);
    n->count=htole32(count); n->cookie=htole64(cookie);
    iwx_sae_rx_synced(sc,p);
}
static struct iwx_softc *sleeping_sc;
static int tsleep_nsec(void *p, int flags, const char *label, uint64_t time) {
    (void)p; CHECK(flags==PCATCH); CHECK(strcmp(label,"iwxrxsync")==0);
    CHECK(time==1000000000ULL); event('W');
    int error=boundary(sleeping_sc);
    if (!error && !sleep_error)
        notification(sleeping_sc,sleeping_sc->sc_sae_rx_cookie,8,12);
    return error ? error : sleep_error;
}
static int send_model(struct iwx_softc *sc, unsigned id,
    unsigned flags, size_t length, const void *data) {
    CHECK(flags==0);
    if (id==IWX_WIDE_ID(IWX_DATA_PATH_GROUP,IWX_SEC_KEY_CMD)) {
        CHECK(length==sizeof(struct iwx_sec_key_cmd)); CHECK(ncommands<32);
        commands[ncommands++]=*(const struct iwx_sec_key_cmd *)data;
        event(le32toh(commands[ncommands-1].action)==IWX_FW_CTXT_ACTION_REMOVE?'R':'A');
    } else {
        CHECK(id==IWX_WIDE_ID(IWX_DATA_PATH_GROUP,IWX_TRIGGER_RX_QUEUES_NOTIF_CMD));
        CHECK(length==sizeof(struct iwx_rxq_sync_cmd));
        const struct iwx_rxq_sync_cmd *cmd=data;
        CHECK(le32toh(cmd->flags)==0 && le32toh(cmd->rxq_mask)==1);
        CHECK(le32toh(cmd->count)==8 && le64toh(cmd->cookie)==sc->sc_sae_rx_cookie);
        event('D');
        if (immediate_sync) notification(sc,le64toh(cmd->cookie),8,12);
    }
    return boundary(sc);
}
'''
    services += r'''
static int iwx_send_cmd(struct iwx_softc *sc, struct iwx_host_cmd *cmd) {
    CHECK(cmd->flags == IWX_CMD_WANT_RESP);
    CHECK(cmd->resp_pkt_len == IWX_CMD_RESP_MAX);
    int error = send_model(sc, cmd->id, 0, cmd->len[0], cmd->data[0]);
    if (boundaries != missing_at) {
        cmd->resp_pkt = calloc(1, cmd->resp_pkt_len);
        CHECK(cmd->resp_pkt != NULL); replies++;
        cmd->resp_pkt->hdr.code = iwx_cmd_opcode(cmd->id);
        cmd->resp_pkt->hdr.flags = iwx_cmd_groupid(cmd->id);
        cmd->resp_pkt->len_n_flags = htole32(sizeof(cmd->resp_pkt->hdr));
        if (boundaries == ack_fail_at) {
            switch(response_shape) {
            case 0: cmd->resp_pkt->hdr.flags |= IWX_CMD_FAILED_MSK; break;
            case 1: cmd->resp_pkt->hdr.code++; break;
            case 2: memset(cmd->resp_pkt,0,cmd->resp_pkt_len); break;
            case 3: cmd->resp_pkt->len_n_flags=htole32(sizeof(cmd->resp_pkt->hdr)-1); break;
            case 4: cmd->resp_pkt->len_n_flags=htole32(IWX_CMD_RESP_MAX-3); break;
            default: abort();
            }
        }
    }
    return error;
}
static void iwx_free_resp(struct iwx_softc *sc, struct iwx_host_cmd *cmd) {
    (void)sc; CHECK(cmd->resp_pkt != NULL);
    free(cmd->resp_pkt); cmd->resp_pkt = NULL; freed_replies++;
}
'''
    services += r'''
int iwx_sae_rx(struct iwx_softc *, struct mbuf *, const struct iwx_rx_mpdu_desc *);
static uint32_t read_register(struct iwx_softc *sc, unsigned reg) {
    (void)sc;
    switch(reg) {
    case IWX_CSR_INT: return irq_legacy;
    case IWX_CSR_FH_INT_STATUS: return irq_fh;
    case IWX_CSR_MSIX_FH_INT_CAUSES_AD: return irq_msix_fh;
    case IWX_CSR_MSIX_HW_INT_CAUSES_AD: return irq_msix_hw;
    case IWX_CSR_GP_CNTRL: return irq_gp;
    default: abort();
    }
}
static void write_register(struct iwx_softc *sc, unsigned reg, uint32_t value) {
    (void)sc; (void)value; CHECK(reg < 0x10000);
}
#define IWX_READ(sc,reg) read_register(sc,reg)
#define IWX_WRITE(sc,reg,value) write_register(sc,reg,value)
#define IWX_WRITE_1(sc,reg,value) write_register(sc,reg,value)
void ieee80211_sae_retire(struct ieee80211com *ic) {
    struct iwx_softc *sc=IC2IFP(ic)->if_softc;
    event('T'); irq_retired++; sc->sc_sae_epoch=0; ic->serial++;
}
static void iwx_notif_intr(struct iwx_softc *sc) {
    struct mbuf m={.m_len=32};
    struct iwx_rx_mpdu_desc desc={0};
    m.data[0]=8; m.data[1]=64; m.data[4]=2;
    desc.status=htole32(IWX_RX_MPDU_RES_STATUS_SEC_CCM_ENC|IWX_RX_MPDU_RES_STATUS_MIC_OK);
    event('N');
    if (iwx_sae_rx(sc,&m,&desc)==0) irq_accepted++; else irq_blocked++;
}
static void iwx_restore_interrupts(struct iwx_softc *sc) { (void)sc; irq_restored++; }
static void iwx_update_rx_desc(struct iwx_softc *sc, int *q, unsigned index) {
    (void)sc; (void)q; CHECK(index<IWX_RX_MQ_RING_COUNT);
}
static void iwx_nic_error(struct iwx_softc *sc) { (void)sc; }
static void iwx_dump_driver_status(struct iwx_softc *sc) { (void)sc; }
static const char *device_xname(void *device) { (void)device; return "fixture-iwx"; }
'''
    implementation = '\n'.join(result + '\n' + function(source, name)
        for name, result in (('iwx_sae_fence','void'), ('iwx_sae_send_cmd','int'), ('iwx_sae_sync_rx','int'),
            ('iwx_sae_rx_synced','void'), ('iwx_sae_key_cmd','int'),
            ('iwx_sae_replace','int'), ('iwx_sae_rx','int'),
            ('iwx_rx_mpdu_prepare','int'), ('iwx_check_rfkill','int'),
            ('iwx_intr','int'), ('iwx_intr_msix','int')))
    tests = r'''
static void setup(struct iwx_softc *sc, struct ifnet *ifp) {
    memset(sc,0,sizeof(*sc)); memset(ifp,0,sizeof(*ifp));
    ifp->if_softc=sc; ifp->if_flags=IFF_RUNNING;
    sc->sc_ic.ic_ifp=ifp; sc->sc_ic.serial=42; sc->sc_ic.active=true;
    sc->sc_flags=IWX_FLAG_STA_ACTIVE; sc->sc_generation=3; sleeping_sc=sc;
    CHECK(replies == freed_replies);
    boundaries=inject_at=fail_at=ack_fail_at=missing_at=resets=drops=wakes=nevents=ncommands=0;
    replies=freed_replies=0;
    immediate_sync=true; sleep_error=tag_error=0; response_shape=0;
    irq_legacy=irq_fh=irq_msix_fh=irq_msix_hw=irq_gp=0;
    irq_retired=irq_accepted=irq_blocked=irq_restored=0;
    sc->sc_intmask=sc->sc_fh_mask=sc->sc_hw_mask=UINT32_MAX;
}
static void transactions(void) {
    struct iwx_softc sc; struct ifnet ifp;
    struct ieee80211_sae_hw_key keys[3] = {
        {.id=IEEE80211_SAE_HW_PTK,.cipher=3,.key={0x11}},
        {.id=1,.cipher=3,.key={0x22}},
        {.id=4,.cipher=IEEE80211_SAE_HW_BIP,.key={0x33}}};
    setup(&sc,&ifp); sc.sc_sae_keys=0x25;
    sc.sc_ba[0].active=true; sc.sc_ba_start=sc.sc_ba_stop=sc.sc_txq_want=7;
    CHECK(iwx_sae_replace(&sc.sc_ic,42,keys,3)==0);
    CHECK(nevents==8 && memcmp(events,"FRRRDAAA",8)==0);
    CHECK(sc.sc_sae_keys==0x25 && sc.sc_sae_epoch==42 && !sc.sc_sae_replacing);
    CHECK(drops==1 && sc.sc_ba_start==0 && sc.sc_ba_stop==0 && sc.sc_txq_want==0);
    for(unsigned i=0;i<3;i++) {
        const struct iwx_sec_key_cmd *cmd=&commands[i+3];
        CHECK(le32toh(cmd->action)==1 && le32toh(cmd->u.add.sta_mask)==1);
        CHECK(le32toh(cmd->u.add.key_id)==(i==0?0:i==1?1:4));
        CHECK(le32toh(cmd->u.add.key_flags)==(i==0?0x2a:i==1?0x4a:0x6a));
        CHECK(memcmp(cmd->u.add.key,keys[i].key,16)==0);
        CHECK(cmd->u.add.rx_seq==0 && cmd->u.add.tx_seq==0);
        for(unsigned j=16;j<32;j++) CHECK(cmd->u.add.key[j]==0);
    }
    /* Every sleeping command boundary must reject a changed lifetime. */
    for(unsigned change=0;change<2;change++) for(unsigned at=1;at<=8;at++) {
        setup(&sc,&ifp); sc.sc_sae_keys=0x25;
        inject_at=at; inject_generation=change!=0;
        CHECK(iwx_sae_replace(&sc.sc_ic,42,keys,3)!=0);
        CHECK(sc.sc_sae_epoch==0 && !sc.sc_sae_replacing && resets==1);
    }
    for(unsigned at=1;at<=8;at++) {
        setup(&sc,&ifp); sc.sc_sae_keys=0x25; fail_at=at;
        CHECK(iwx_sae_replace(&sc.sc_ic,42,keys,3)==EIO);
        CHECK(sc.sc_sae_epoch==0 && resets==1);
    }
    for(unsigned at=2;at<=8;at++) for(unsigned missing=0;missing<2;missing++) {
        setup(&sc,&ifp); sc.sc_sae_keys=0x25;
        if (missing) missing_at=at; else ack_fail_at=at;
        CHECK(iwx_sae_replace(&sc.sc_ic,42,keys,3)==EIO);
        CHECK(sc.sc_sae_epoch==0 && resets==1 && replies==freed_replies);
        if (at==2) CHECK(sc.sc_sae_keys==0x25);
        if (at==6) CHECK(sc.sc_sae_keys==0);
    }
    for(unsigned shape=1;shape<5;shape++) {
        setup(&sc,&ifp); sc.sc_sae_keys=0x25; ack_fail_at=2; response_shape=shape;
        CHECK(iwx_sae_replace(&sc.sc_ic,42,keys,3)==EIO);
        CHECK(sc.sc_sae_epoch==0 && sc.sc_sae_keys==0x25 && replies==freed_replies);
    }
    setup(&sc,&ifp); immediate_sync=false; sleep_error=EWOULDBLOCK;
    CHECK(iwx_sae_replace(&sc.sc_ic,42,keys,3)==EWOULDBLOCK);
    CHECK(sc.sc_sae_epoch==0 && !sc.sc_sae_rx_pending && resets==1);
    setup(&sc,&ifp); immediate_sync=false; inject_at=3;
    CHECK(iwx_sae_replace(&sc.sc_ic,42,keys,3)!=0); CHECK(sc.sc_sae_epoch==0);
    setup(&sc,&ifp); ifp.if_flags=0;
    CHECK(iwx_sae_replace(&sc.sc_ic,42,NULL,0)==0 && nevents==0 && sc.sc_sae_epoch==42);
    CHECK(iwx_sae_replace(&sc.sc_ic,42,keys,3)==ENETDOWN && sc.sc_sae_epoch==0);
    setup(&sc,&ifp); sc.sc_flags=0;
    CHECK(iwx_sae_replace(&sc.sc_ic,42,NULL,0)==0 && nevents==1 && events[0]=='D');
    CHECK(iwx_sae_replace(&sc.sc_ic,42,keys,3)==ENOTCONN);
    setup(&sc,&ifp); CHECK(iwx_sae_replace(&sc.sc_ic,42,keys,8)==EBUSY);
    keys[1].id=keys[0].id; CHECK(iwx_sae_replace(&sc.sc_ic,42,keys,3)==EINVAL);
    keys[1].id=7; CHECK(iwx_sae_replace(&sc.sc_ic,42,keys,3)==EINVAL);
    keys[1].id=1; keys[1].cipher=IEEE80211_SAE_HW_BIP;
    CHECK(iwx_sae_replace(&sc.sc_ic,42,keys,3)==EINVAL);
    CHECK(iwx_sae_key_cmd(&sc,7,NULL)==EINVAL && nevents==0);
    sleeping_sc=NULL;
}
static void cookies(void) {
    struct iwx_softc sc; struct ifnet ifp; setup(&sc,&ifp);
    sc.sc_sae_rx_pending=true; sc.sc_sae_rx_cookie=9;
    notification(&sc,8,8,12); CHECK(sc.sc_sae_rx_pending && wakes==0);
    notification(&sc,9,7,12); CHECK(sc.sc_sae_rx_pending && wakes==0);
    notification(&sc,9,8,11); CHECK(sc.sc_sae_rx_pending && wakes==0);
    notification(&sc,9,8,13); CHECK(sc.sc_sae_rx_pending && wakes==0);
    notification(&sc,9,8,12); CHECK(!sc.sc_sae_rx_pending && wakes==1);
    notification(&sc,9,8,12); CHECK(wakes==1);
    CHECK(iwx_sae_sync_rx(&sc)==0 && sc.sc_sae_rx_cookie==10);
    sc.sc_sae_rx_cookie=UINT64_MAX;
    CHECK(iwx_sae_sync_rx(&sc)==EOVERFLOW && sc.sc_sae_rx_cookie==UINT64_MAX);
    sleeping_sc=NULL;
}
static void receive(void) {
    struct iwx_softc sc; struct ifnet ifp; setup(&sc,&ifp);
    struct mbuf m={.m_len=32}; m.data[0]=8; m.data[1]=64; m.data[4]=2;
    struct iwx_rx_mpdu_desc desc={0}; desc.status=htole32((2<<8)|(1<<6));
    sc.sc_sae_epoch=42; sc.sc_sae_keys=1;
    CHECK(iwx_sae_rx(&sc,&m,&desc)==0);
    CHECK(tagged_epoch==42 && tagged_id==65535 && tagged_flags==7 && tagged_index==0);
    sc.sc_sae_keys=0; CHECK(iwx_sae_rx(&sc,&m,&desc)==EACCES); sc.sc_sae_keys=1;
    desc.status=htole32(2<<8); CHECK(iwx_sae_rx(&sc,&m,&desc)==EACCES);
    desc.status=htole32((1<<8)|(1<<6)); CHECK(iwx_sae_rx(&sc,&m,&desc)==EACCES);
    desc.status=htole32((2<<8)|(1<<6)); m.m_len=31;
    CHECK(iwx_sae_rx(&sc,&m,&desc)==EINVAL); m.m_len=32;
    m.data[27]=64; CHECK(iwx_sae_rx(&sc,&m,&desc)==EACCES);
    m.data[4]=1; sc.sc_sae_keys=4;
    CHECK(iwx_sae_rx(&sc,&m,&desc)==0 && tagged_id==1);
    m.data[0]=0x88; m.m_len=34; m.data[27]=0; m.data[29]=64;
    desc.mac_flags2=IWX_RX_MPDU_MFLG2_AMSDU; desc.amsdu_info=0x83;
    CHECK(iwx_sae_rx(&sc,&m,&desc)==0 && tagged_index==3 && tagged_last);
    tag_error=ENOMEM; CHECK(iwx_sae_rx(&sc,&m,&desc)==ENOMEM); tag_error=0;
    sc.sc_sae_replacing=true; CHECK(iwx_sae_rx(&sc,&m,&desc)==EACCES);
    sc.sc_sae_replacing=false; sc.sc_sae_epoch=0; CHECK(iwx_sae_rx(&sc,&m,&desc)==EACCES);
    m.data[0]=0xc0; m.data[1]=0; CHECK(iwx_sae_rx(&sc,&m,&desc)==0);
    m.m_len=23; CHECK(iwx_sae_rx(&sc,&m,&desc)==EINVAL);
    sc.sc_ic.active=false; CHECK(iwx_sae_rx(&sc,&m,&desc)==0);
    sleeping_sc=NULL;
}
static void normalization(void) {
    struct iwx_softc sc; struct ifnet ifp; setup(&sc,&ifp);
    uint8_t bytes[sizeof(struct iwx_rx_mpdu_desc)+256];
    const size_t sizes[]={sizeof(struct iwx_rx_mpdu_desc),IWX_RX_DESC_SIZE_V1};
    for(unsigned variant=0;variant<2;variant++) {
        size_t size=sizes[variant]; memset(bytes,0,sizeof(bytes));
        struct iwx_rx_mpdu_desc *desc=(void *)bytes;
        uint8_t *frame=bytes+size; struct mbuf m={0};
        desc->status=htole32(IWX_RX_MPDU_RES_STATUS_CRC_OK|IWX_RX_MPDU_RES_STATUS_OVERRUN_OK);
        desc->mpdu_len=htole16(32); frame[0]=8;
        CHECK(iwx_rx_mpdu_prepare(&sc.sc_ic,&m,bytes,size-1,size)==EINVAL);
        CHECK(iwx_rx_mpdu_prepare(&sc.sc_ic,&m,bytes,sizeof(bytes),size-1)==EINVAL);
        CHECK(iwx_rx_mpdu_prepare(&sc.sc_ic,&m,bytes,size+31,size)==EINVAL);
        desc->status=0; CHECK(iwx_rx_mpdu_prepare(&sc.sc_ic,&m,bytes,sizeof(bytes),size)==EIO);
        desc->status=htole32(IWX_RX_MPDU_RES_STATUS_CRC_OK);
        CHECK(iwx_rx_mpdu_prepare(&sc.sc_ic,&m,bytes,sizeof(bytes),size)==EIO);
        desc->status=htole32(IWX_RX_MPDU_RES_STATUS_CRC_OK|IWX_RX_MPDU_RES_STATUS_OVERRUN_OK);
        desc->mac_flags1=0xf0;
        CHECK(iwx_rx_mpdu_prepare(&sc.sc_ic,&m,bytes,sizeof(bytes),size)==EINVAL);
        desc->mac_flags1=8<<3;
        CHECK(iwx_rx_mpdu_prepare(&sc.sc_ic,&m,bytes,sizeof(bytes),size)==0);
        CHECK(m.m_len==24 && m.m_pkthdr.len==24 && m.m_data==frame);
        sc.sc_ic.ic_opmode=IEEE80211_M_MONITOR;
        CHECK(iwx_rx_mpdu_prepare(&sc.sc_ic,&m,bytes,sizeof(bytes),size)==0);
        CHECK(m.m_len==32); sc.sc_ic.ic_opmode=0;
        desc->mpdu_len=htole16(42); desc->mac_flags1=4<<3;
        desc->mac_flags2=IWX_RX_MPDU_MFLG2_PAD;
        desc->status|=htole32(IWX_RX_MPDU_RES_STATUS_SEC_CCM_ENC);
        for(unsigned i=0;i<42;i++) frame[i]=(uint8_t)i;
        frame[0]=8; frame[1]=64;
        uint8_t saved[32]; memcpy(saved,frame,32);
        CHECK(iwx_rx_mpdu_prepare(&sc.sc_ic,&m,bytes,sizeof(bytes),size)==0);
        CHECK(m.m_len==36 && memcmp(m.m_data,saved,32)==0);
        CHECK(m.m_data[32]==34 && m.m_data[35]==37);
        /* Unprotected BIP never treats its first body bytes as a CCMP IV. */
        memset(frame,0,42); frame[0]=0xc0; frame[26]=0xaa; frame[27]=0xbb;
        desc->mac_flags1=0; desc->mpdu_len=htole16(28);
        CHECK(iwx_rx_mpdu_prepare(&sc.sc_ic,&m,bytes,sizeof(bytes),size)==0);
        CHECK(m.m_len==26 && m.m_data[24]==0xaa && m.m_data[25]==0xbb);
        frame[0]=8; frame[1]=64; desc->mpdu_len=htole16(33);
        CHECK(iwx_rx_mpdu_prepare(&sc.sc_ic,&m,bytes,sizeof(bytes),size)==EINVAL);
        frame[1]=3; desc->mpdu_len=htole16(26);
        CHECK(iwx_rx_mpdu_prepare(&sc.sc_ic,&m,bytes,sizeof(bytes),size)==EINVAL);
        sc.sc_ic.ic_opmode=IEEE80211_M_MONITOR;
        for(unsigned subtype=0;subtype<3;subtype++) {
            unsigned header=subtype<2?10:16;
            memset(frame,0,42); frame[0]=(subtype==0?0xc0:subtype==1?0xd0:0xb0)|4;
            desc->mpdu_len=htole16(header+2); desc->status&=htole32(~IWX_RX_MPDU_RES_STATUS_SEC_ENC_MSK);
            CHECK(iwx_rx_mpdu_prepare(&sc.sc_ic,&m,bytes,sizeof(bytes),size)==0);
            CHECK(m.m_len==(int)header);
        }
        sc.sc_ic.ic_opmode=0;
    }
    sleeping_sc=NULL;
}
static void interrupts(void) {
    struct iwx_softc sc; struct ifnet ifp;
    const uint32_t legacy[]={IWX_CSR_INT_BIT_SW_ERR,IWX_CSR_INT_BIT_HW_ERR,IWX_CSR_INT_BIT_RF_KILL};
    const uint32_t hardware[]={IWX_MSIX_HW_INT_CAUSES_REG_TOP_FATAL_ERR,
        IWX_MSIX_HW_INT_CAUSES_REG_SW_ERR,IWX_MSIX_HW_INT_CAUSES_REG_SW_ERR_V2,
        IWX_MSIX_HW_INT_CAUSES_REG_HW_ERR,IWX_MSIX_HW_INT_CAUSES_REG_RF_KILL};
    setup(&sc,&ifp); sc.sc_sae_epoch=42; sc.sc_sae_keys=1;
    irq_legacy=IWX_CSR_INT_BIT_FH_RX;
    CHECK(iwx_intr(&sc)==1 && irq_accepted==1 && irq_retired==0);
    for(unsigned i=0;i<sizeof(legacy)/sizeof(legacy[0]);i++) {
        setup(&sc,&ifp); sc.sc_sae_epoch=42; sc.sc_sae_keys=1;
        irq_legacy=IWX_CSR_INT_BIT_FH_RX|legacy[i];
        CHECK(iwx_intr(&sc)==1);
        CHECK(irq_retired>0 && irq_accepted==0 && sc.sc_sae_epoch==0);
        CHECK(resets==1);
    }
    setup(&sc,&ifp); sc.sc_sae_epoch=42; sc.sc_sae_keys=1;
    irq_msix_fh=IWX_MSIX_FH_INT_CAUSES_Q0;
    CHECK(iwx_intr_msix(&sc)==1 && irq_accepted==1 && irq_retired==0);
    for(unsigned queue=0;queue<2;queue++) {
        for(unsigned i=0;i<sizeof(hardware)/sizeof(hardware[0]);i++) {
            setup(&sc,&ifp); sc.sc_sae_epoch=42; sc.sc_sae_keys=1;
            irq_msix_fh=queue?IWX_MSIX_FH_INT_CAUSES_Q1:IWX_MSIX_FH_INT_CAUSES_Q0;
            irq_msix_hw=hardware[i];
            CHECK(iwx_intr_msix(&sc)==1);
            CHECK(irq_retired>0 && irq_accepted==0 && sc.sc_sae_epoch==0);
            CHECK(irq_blocked==1 && events[0]=='T' && resets==1);
        }
        setup(&sc,&ifp); sc.sc_sae_epoch=42; sc.sc_sae_keys=1;
        irq_msix_fh=(queue?IWX_MSIX_FH_INT_CAUSES_Q1:IWX_MSIX_FH_INT_CAUSES_Q0)|IWX_MSIX_FH_INT_CAUSES_FH_ERR;
        CHECK(iwx_intr_msix(&sc)==1);
        CHECK(irq_retired>0 && irq_accepted==0 && sc.sc_sae_epoch==0);
        CHECK(irq_blocked==1 && events[0]=='T' && resets==1);
    }
    setup(&sc,&ifp); sc.sc_sae_epoch=42; sc.sc_sae_keys=1;
    irq_msix_fh=IWX_MSIX_FH_INT_CAUSES_Q0;
    irq_msix_hw=IWX_MSIX_HW_INT_CAUSES_REG_RF_KILL;
    irq_gp=IWX_CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW;
    CHECK(iwx_intr_msix(&sc)==1);
    CHECK(irq_retired==1 && irq_accepted==0 && irq_blocked==1);
    sleeping_sc=NULL;
}
int main(void) { transactions(); cookies(); receive(); normalization(); interrupts();
    printf("iwx SAE firmware: %u checks passed\n",checks); return 0; }
'''
    args.build_dir.mkdir(parents=True, exist_ok=True)
    check = args.build_dir / 'iwx-sae-firmware-contract.c'
    check.write_text(prelude + abi + services + implementation + tests)
    binary = args.build_dir / 'iwx-sae-firmware-contract'
    command = shlex.split(args.cc) + ['-std=c11', '-Wall', '-Wextra', '-Werror']
    command += shlex.split(args.cflags) + ['-I'+str(root/'sys/net80211'), str(check), '-o', str(binary)]
    (args.build_dir/'compile_commands.json').write_text(json.dumps([{
        'directory':str(root), 'file':str(check.resolve()), 'arguments':command}], indent=2)+'\n')
    subprocess.run(command, check=True)
    runner = shlex.split(os.environ.get("TEST_RUNNER", ""))
    subprocess.run(runner + [str(binary.resolve())], check=True)
    if args.mutations:
        replacements = {
            'tx-key': ('IWX_SEC_KEY_FLAG_CIPHER_CCMP | IWX_SEC_KEY_FLAG_NO_TX',
                       'IWX_SEC_KEY_FLAG_CIPHER_CCMP'),
            'stale-owner': ('epoch != ieee80211_sae_serial(ic)', 'false'),
            'no-rx-drain': ('error = iwx_sae_sync_rx(sc);', 'error = 0;'),
            'failed-ack': ('cmd.resp_pkt->hdr.flags != iwx_cmd_groupid(id)', 'false'),
            'short-payload': ('len > maxlen - desc_size', 'false'),
            'bad-mic': ('!(status & IWX_RX_MPDU_RES_STATUS_MIC_OK)', 'false'),
        }
        legacy = function(source, 'iwx_intr')
        replacements['legacy-no-retire'] = (
            legacy, legacy.replace('ieee80211_sae_retire(ic);', '(void)0;'))
        msix = function(source, 'iwx_intr_msix')
        start = msix.index('\t/* Fatal and RF-kill causes')
        end = msix.index('\n\tif (inta_fh & IWX_MSIX_FH_INT_CAUSES_Q0', start)
        retirement = msix[start:end]
        delayed = msix[:start] + msix[end:]
        delayed = delayed.replace('\t\tiwx_notif_intr(sc);\n\t}',
                                  '\t\tiwx_notif_intr(sc);\n\t}\n' + retirement, 1)
        replacements['msix-late-retire'] = (msix, delayed)
        for name, (before, after) in replacements.items():
            if before not in implementation:
                raise ValueError('mutation no longer matches: ' + name)
            mutant = args.build_dir / ('mutant-' + name + '.c')
            executable = args.build_dir / ('mutant-' + name)
            mutant.write_text(prelude + abi + services +
                              implementation.replace(before, after) + tests)
            mutation_command = [str(mutant) if part == str(check) else
                                str(executable) if part == str(binary) else part
                                for part in command]
            subprocess.run(mutation_command, check=True)
            result = subprocess.run(runner + [str(executable.resolve())], capture_output=True, text=True)
            if result.returncode != 1 or 'FAIL ' not in result.stderr:
                raise AssertionError('mutation did not fail causally: ' + name)
            print('PASS: fixture rejects ' + name, flush=True)


if __name__ == '__main__':
    main()
