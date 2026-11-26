# SPDX-License-Identifier: BSD-2-Clause
"""Run the actual checksum classifier with synthetic packet/status vectors."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

src = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / "sys/dev/ic/dwc_eqos.c"
c = src.read_text()
start = c.index("static size_t\neqos_csum_ipv4(")
end = c.index("\n#endif", start)
regs = "\n".join(
    line for line in src.with_name("dwc_eqos_reg.h").read_text().splitlines()
    if line.startswith("#define") and any(word in line for word in (
        "EQOS_RDES1_", "GMAC_MAC_HW_FEATURE0_TXCOE", "EQOS_TDES3_TX_CIC_",
        "EQOS_TDES3_RX_RS1V", "EQOS_TDES3_RX_ES")))
preamble = r'''
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define __BIT(n) (1u << n)
#define __BITS(h,l) (((1u << (h+1)) - 1u) & ~((1u << l) - 1u))
#define ETHER_HDR_LEN 14
#define ETHER_VLAN_ENCAP_LEN 4
#define ETHER_CRC_LEN 4
#define ETHERTYPE_IP 0x0800
#define ETHERTYPE_VLAN 0x8100
#define ETHERTYPE_QINQ 0x88a8
#define M_CSUM_IPv4 1
#define M_CSUM_TCPv4 2
#define M_CSUM_UDPv4 4
#define M_CSUM_DATA_IPv4_IPHL(n) ((n) >> 16)
#define M_CSUM_DATA_IPv4_OFFSET(n) ((n) & 65535)
#define M_DONTWAIT 0
#define M_COPYALL 0
struct mbuf { struct { int len, csum_flags; unsigned csum_data; } m_pkthdr; unsigned char data[80]; };
struct eqos_softc { uint32_t sc_hw_feature[4]; bool sc_udp_tx_canary; };
static struct mbuf copy;
static unsigned fallback;
static void m_copydata(struct mbuf *m, int off, int len, void *out)
{
	assert(off >= 0 && len >= 0 && off + len <= m->m_pkthdr.len);
	memcpy(out, m->data + off, len);
}
static struct mbuf *m_dup(struct mbuf *m, int off, int count, int wait)
{
	(void)off; (void)count; (void)wait;
	copy = *m;
	return &copy;
}
static struct mbuf *m_pullup(struct mbuf *m, int count)
{
	assert(count <= m->m_pkthdr.len);
	return m;
}
static void in_undefer_cksum(struct mbuf *m, size_t off, int flags)
{
	assert(m == &copy && off >= ETHER_HDR_LEN);
	++fallback;
	m->m_pkthdr.csum_flags &= ~flags;
}
'''
tests = r'''
static struct mbuf
packet(size_t tag, int proto, int frag, int checksum, int trailer)
{
	const size_t off = ETHER_HDR_LEN + tag * ETHER_VLAN_ENCAP_LEN;
	struct mbuf m = {0};
	struct ip ip = {0};
	const uint16_t type = htons(ETHERTYPE_IP), vlan = htons(ETHERTYPE_VLAN);
	const uint16_t sum = htons(checksum);
	ip.ip_v = 4;
	ip.ip_hl = 5;
	ip.ip_off = htons(frag);
	ip.ip_p = proto;
	ip.ip_len = htons(40);
	m.m_pkthdr.len = off + 40 + trailer;
	for (size_t i = 0; i < tag; ++i)
		memcpy(m.data + ETHER_HDR_LEN - 2 + i * 4, &vlan, 2);
	memcpy(m.data + off - 2, &type, 2);
	memcpy(m.data + off, &ip, sizeof(ip));
	if (proto == IPPROTO_UDP) {
		const uint16_t udp_len = htons(20);
		memcpy(m.data + off + 20 + offsetof(struct udphdr, uh_sum), &sum, 2);
		memcpy(m.data + off + 20 + offsetof(struct udphdr, uh_ulen), &udp_len, 2);
	}
	m.m_pkthdr.csum_data = 20 << 16 |
	    (proto == IPPROTO_TCP ? offsetof(struct tcphdr, th_sum) : offsetof(struct udphdr, uh_sum));
	return m;
}
int main(void)
{
	const uint32_t st = EQOS_TDES3_RX_RS1V;
	const unsigned ip = EQOS_RDES1_IPV4;
	const unsigned udp = ip | EQOS_RDES1_PT_UDP;
	const unsigned tcp = ip | EQOS_RDES1_PT_TCP;
	const int enabled = 7;
	struct mbuf m = packet(1, IPPROTO_UDP, 0, 123, 4);
	assert(eqos_rx_csum(&m, udp, st, enabled) == 5);
	assert(eqos_rx_csum(&m, udp, 0, enabled) == 0);
	assert(eqos_rx_csum(&m, udp, st | EQOS_TDES3_RX_ES, enabled) == 0);
	assert(eqos_rx_csum(&m, udp | EQOS_RDES1_BYPASS, st, enabled) == 0);
	assert(eqos_rx_csum(&m, udp | EQOS_RDES1_IPERR, st, enabled) == 0);
	assert(eqos_rx_csum(&m, udp | EQOS_RDES1_PAYERR, st, enabled) == 0);
	assert(eqos_rx_csum(&m, tcp, st, enabled) == 1);
	m = packet(0, IPPROTO_UDP, 0, 0, 4);
	assert(eqos_rx_csum(&m, udp, st, enabled) == 1);
	m = packet(0, IPPROTO_TCP, IP_MF, 0, 4);
	assert(eqos_rx_csum(&m, tcp, st, enabled) == 1);
	m.m_pkthdr.len = 20;
	assert(eqos_rx_csum(&m, tcp, st, enabled) == 0);

	struct eqos_softc sc = { .sc_hw_feature = { GMAC_MAC_HW_FEATURE0_TXCOE } };
	struct mbuf *p = &m;
	uint32_t cic = 0;
	m = packet(0, IPPROTO_TCP, 0, 0, 0);
	m.m_pkthdr.csum_flags = M_CSUM_TCPv4 | M_CSUM_IPv4;
	assert(eqos_tx_csum(&sc, &p, &cic) == 0 && cic == EQOS_TDES3_TX_CIC_PAYLOAD && p == &m && fallback == 0);
	cic = 0; m.m_pkthdr.csum_flags = M_CSUM_IPv4;
	assert(eqos_tx_csum(&sc, &p, &cic) == 0 && cic == EQOS_TDES3_TX_CIC_IP && p == &m && fallback == 0);
	m = packet(1, IPPROTO_TCP, IP_MF, 0, 0);
	cic = 0; m.m_pkthdr.csum_flags = M_CSUM_IPv4;
	assert(eqos_tx_csum(&sc, &p, &cic) == 0 && cic == 0 && p == &copy && fallback == 1);
	assert(m.m_pkthdr.csum_flags == M_CSUM_IPv4 && p->m_pkthdr.csum_flags == 0);
	m.m_pkthdr.csum_flags |= M_CSUM_TCPv4;
	p = &m;
	assert(eqos_tx_csum(&sc, &p, &cic) == EINVAL && p == &m);
	m = packet(0, IPPROTO_UDP, 0, 0, 0);
	m.m_pkthdr.csum_flags = 0;
	assert(eqos_tx_csum(&sc, &p, &cic) == 0 && p == &m && cic == 0 && fallback == 1);
	m.m_pkthdr.csum_flags = M_CSUM_UDPv4 | M_CSUM_IPv4;
	assert(eqos_tx_csum(&sc, &p, &cic) == 0 && p == &copy && cic == 0 && fallback == 2);
	sc.sc_udp_tx_canary = true;
	p = &m;
	assert(eqos_tx_csum(&sc, &p, &cic) == 0 && p == &m &&
	    cic == EQOS_TDES3_TX_CIC_PAYLOAD && fallback == 2);
	puts("PASS checksum status/IPv4 framing and CIC selection including VLAN, fragment and zero UDP");
}
'''
with tempfile.TemporaryDirectory() as work:
    path = Path(work) / "test.c"
    exe = Path(work) / "test"
    path.write_text(preamble + regs + "\n" + c[start:end] + tests)
    subprocess.run([*shlex.split(os.environ.get("CC", "cc")),
                    *shlex.split(os.environ.get("CFLAGS", "")), "-Wall", "-Wextra", "-Werror", str(path), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
