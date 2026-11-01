#!/usr/bin/env python3
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
p=argparse.ArgumentParser(); p.add_argument('--cc',required=True); a=p.parse_args()
radio = (Path(__file__).resolve().parents[1] / 'espressif/esp_wifi_os.c').read_text()
main_fn = re.search(r'\nstatic void\nespwifi_main\(void \*v\)\n\{.*?\n\}', radio, re.S)
assert main_fn
code = r'''
#include <assert.h>
#include <stdio.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ESPWIFI_TICK_MS 10
typedef unsigned int u_int;
static int ready, seeded, started;
static int cprng_fast_reseed_sync(void) { seeded++; return !ready; }
static int espwifi_start(void) { assert(seeded && ready); started++; return 1; }
static void espwifi_if_radio_ready(void) { assert(started); }
static int espwifi_linked(void) { return 0; }
static int espwifi_join(void) { return 0; }
static void espwifi_os_task_delay(unsigned n) { (void)n; assert(!started); ready=1; }
''' + main_fn.group() + r'''
int main(void) {
 espwifi_main(0); assert(seeded==2 && started==1);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c = Path(td) / 'radio.c'; out = Path(td) / 'radio'
    c.write_text(code)
    subprocess.run([a.cc, '-Werror', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(c), '-o', str(out)], check=True)
    subprocess.run([str(out)], check=True)
print('radio starts only after synchronous seed succeeds')

# The interface is visible during autoconf, before the radio is seeded.
# Exercise its actual TX loop with a queued packet while link is down.
source = (Path(__file__).resolve().parents[1] / 'espressif/esp_wifi_if.c').read_text()
tx = re.search(r'\nstatic void\nespwifi_if_txthread\(void \*v\)\n\{.*?\n\}', source, re.S)
assert tx
code = r'''
#include <assert.h>
#include <setjmp.h>
#include <stddef.h>
typedef unsigned u_int;
struct mbuf { struct { unsigned len; } m_pkthdr; };
struct ifnet { int if_snd; };
struct espwifi_softc { struct { struct ifnet ec_if; } sc_ec; void *sc_txsem;
 int sc_link; char sc_txbuf[1600]; };
static jmp_buf done; static int waits, queued, dropped, sent;
static struct mbuf packet={{20}};
#define ESPWIFI_WAIT_FOREVER 0
#define ESPWIFI_MTU_FRAME 1600
#define BPF_D_OUT 0
#define IFQ_DEQUEUE(q,m) ((void)(q), (m)=queued++ ? NULL : &packet)
#define if_statinc(ifp,field) ((void)(ifp))
static void espwifi_os_sem_take(void *s,int wait) {
 (void)s;(void)wait; if (++waits==2) longjmp(done,1); }
static int splnet(void) { return 0; }
static void splx(int s) { (void)s; }
static void m_freem(struct mbuf *m) { assert(m==&packet); dropped++; }
static void m_copydata(struct mbuf *m,int o,unsigned n,void *d) { (void)m;(void)o;(void)n;(void)d; }
static void bpf_mtap(struct ifnet *i,struct mbuf *m,int d) { (void)i;(void)m;(void)d; }
static int espwifi_tx(void *p,unsigned n) { (void)p;(void)n; sent++; return 0; }
''' + tx.group() + r'''
int main(void) {
 struct espwifi_softc sc={0};
 if (setjmp(done)==0) espwifi_if_txthread(&sc);
 assert(dropped==1 && sent==0);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c = Path(td) / 'tx.c'; out = Path(td) / 'tx'
    c.write_text(code)
    subprocess.run([a.cc, '-Werror', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(c), '-o', str(out)], check=True)
    subprocess.run([str(out)], check=True)
print('unseeded down interface drops TX before vendor callbacks')
