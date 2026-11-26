# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the actual RX descriptor repost and RIWT drain logic."""
from pathlib import Path
import subprocess
import sys
import tempfile

p = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / "sys/dev/ic/dwc_eqos.c"
s = p.read_text()
start = s.index("static void\neqos_setup_rxdesc(")
end = s.index("\nstatic int", start)
mock = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define DPRINTF(...)
#define htole32(x) (x)
#define DESC_OFF(x) (x)
#define EQOS_RX_IOC_INTERVAL 16
#define EQOS_TDES3_RX_OWN (1u << 31)
#define EQOS_TDES3_RX_IOC (1u << 30)
#define EQOS_TDES3_RX_BUF1V (1u << 24)
#define WR4(sc, reg, value) do { (sc)->hardware = value; ++(sc)->writes; } while (0)
#define bus_dmamap_sync(...)
typedef uint64_t bus_addr_t;
struct desc { uint32_t tdes0, tdes1, tdes2, tdes3; };
struct eqos_softc {
	int sc_rx_riwt, sc_rx_riwt_programmed, sc_rx_moderated_count;
	int hardware, writes;
	bool sc_rx_moderated[32];
	struct { struct desc desc_ring[32]; } sc_rx;
};
'''
cases = r'''
int main(void)
{
	struct eqos_softc sc = {0};
	eqos_setup_rxdesc(&sc, 5, 0x1234);
	assert(sc.sc_rx.desc_ring[5].tdes3 & EQOS_TDES3_RX_IOC);
	assert(sc.writes == 0);
	sc.hardware = sc.sc_rx_riwt = sc.sc_rx_riwt_programmed = 64;
	eqos_setup_rxdesc(&sc, 5, 0x1234);
	eqos_setup_rxdesc(&sc, 6, 0x1234);
	eqos_setup_rxdesc(&sc, 16, 0x1234);
	assert(!(sc.sc_rx.desc_ring[5].tdes3 & EQOS_TDES3_RX_IOC));
	assert(sc.sc_rx.desc_ring[16].tdes3 & EQOS_TDES3_RX_IOC);
	assert(sc.sc_rx_moderated_count == 2);
	eqos_setup_rxdesc(&sc, 5, 0x1234);
	assert(sc.sc_rx_moderated_count == 2);
	sc.sc_rx_riwt = 0;
	eqos_setup_rxdesc(&sc, 5, 0x1234);
	assert(sc.sc_rx.desc_ring[5].tdes3 & EQOS_TDES3_RX_IOC);
	assert(sc.sc_rx_moderated_count == 1 && sc.hardware == 64 && sc.writes == 0);
	eqos_setup_rxdesc(&sc, 6, 0x1234);
	assert(sc.sc_rx_moderated_count == 0 && sc.hardware == 0 && sc.writes == 1);
	puts("PASS runtime RX watchdog remains armed until final old descriptor retirement");
}
'''
with tempfile.TemporaryDirectory() as work:
    path = Path(work) / "test.c"
    exe = Path(work) / "test"
    path.write_text(mock + s[start:end] + cases)
    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", str(path), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
