# SPDX-License-Identifier: BSD-2-Clause
"""Execute the real TX batch publisher against a small mock DMA ring."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

src = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / "sys/dev/ic/dwc_eqos.c"
source = src.read_text()
start = source.index("static void\neqos_start_locked(")
end = source.index("\nstatic void", start + 1)
body = source[start:end]
preamble = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define TX_DESC_COUNT 16
#define TX_MAX_SEGS 4
#define TX_SKIP(n, d) (((n) + (d)) % TX_DESC_COUNT)
#define EQOS_TDES2_TX_IOC (1U << 31)
#define EQOS_TX_IOC_INTERVAL 4
#define htole32(n) (n)
#define EQOS_ASSERT_TXLOCKED(x)
#define DPRINTF(...)
#define BPF_D_OUT 0
#define BUS_DMASYNC_PREREAD 1
#define BUS_DMASYNC_PREWRITE 2
#define DESC_OFF(n) (16 * (n))
struct mbuf { int segments; };
struct ifnet { int if_snd; };
struct desc { uint32_t tdes2; };
struct eqos_softc {
	struct { struct ifnet ec_if; } sc_ec;
	struct { int cur, next, queued, desc_map; struct desc desc_ring[16]; } sc_tx;
	bool sc_txrunning;
	int sc_tx_ioc_interval;
};
static struct mbuf queue[16];
static int pos, count, syncs, writes, final, freed, synced;
#define IFQ_POLL(q, m) ((m) = pos < count ? &queue[pos] : NULL)
#define IFQ_DEQUEUE(q, m) ((m) = &queue[pos++])
#define m_freem(m) (++freed)
#define if_statinc(...)
#define bpf_mtap(...)
static int eqos_setup_txbuf(struct eqos_softc *sc, int first, struct mbuf *m, bool intr, ...)
{
	int n = m->segments;
	if (n > 0) {
		sc->sc_tx.desc_ring[TX_SKIP(first, n-1)].tdes2 =
		    intr ? EQOS_TDES2_TX_IOC : 0;
		sc->sc_tx.queued += n;
	}
	return n;
}
#define eqos_dma_sync(sc, map, start, end, size, flags) do { \
	++syncs; final = TX_SKIP(end, TX_DESC_COUNT-1); \
	synced = !!(sc->sc_tx.desc_ring[final].tdes2 & EQOS_TDES2_TX_IOC); \
} while (0)
#define WR4(sc, reg, end) (++writes)
'''
main = r'''
int main(void)
{
	struct eqos_softc sc = { .sc_txrunning = true, .sc_tx_ioc_interval = 4 };
	sc.sc_tx.cur = 15;
	count = 4;
	queue[0].segments = 2;
	queue[1].segments = -2;
	queue[2].segments = 1;
	queue[3].segments = 0;
	eqos_start_locked(&sc);
	assert(writes == 1 && syncs == 1 && synced && final == 1 && freed == 1);
	assert(sc.sc_tx.desc_ring[0].tdes2 == 0);
	count = pos = 0;
	count = 2;
	queue[0].segments = -2;
	queue[1].segments = 0;
	eqos_start_locked(&sc);
	assert(writes == 1 && syncs == 1 && freed == 2);
	pos = 0; count = 1; queue[0].segments = 1;
	eqos_start_locked(&sc);
	assert(writes == 2 && syncs == 2 && synced && final == 2);
	puts("PASS TX final IOC before DMA sync/tail, ring wrap, short burst, map failure and drop-only");
}
'''
with tempfile.TemporaryDirectory() as work:
    c = Path(work) / "test.c"
    exe = Path(work) / "test"
    c.write_text(preamble + body + main)
    subprocess.run(["cc", "-Wall", "-Wextra", "-Wno-unused-but-set-variable",
                    "-Wno-unused-variable", "-Werror", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
