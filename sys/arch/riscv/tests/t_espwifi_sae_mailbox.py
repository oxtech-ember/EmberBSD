#!/usr/bin/env python3
"""Run the selected IDF SAE callbacks through the production kernel mailbox."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, name, result):
    match = re.search(r"(?m)^static " + re.escape(result) + r"\s+" +
                      re.escape(name) + r"\s*\(.*?^}", source, re.S)
    if match is None:
        match = re.search(r"(?m)^" + re.escape(result) + r"\s+" +
                          re.escape(name) + r"\s*\(.*?^}", source, re.S)
    if match is None:
        raise ValueError(f"Missing production function {name}")
    return match.group(0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--idf-driver-header', required=True, type=Path)
    parser.add_argument('--build-dir', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    arch = root / 'espressif'
    kernel = (arch / 'esp_wifi_if.c').read_text()
    os_source = (arch / 'esp_wifi_os.c').read_text()
    fullmac = (arch / 'wifi/fullmac_wpa2.c').read_text()
    common = (root.parents[1] / 'net80211/ieee80211_ioctl.c').read_text()
    idf = args.idf_driver_header.read_text()
    abi = re.search(r'struct wpa_funcs\s*\{(.*?)\n\};', idf, re.S)
    if abi is None:
        raise ValueError('selected IDF wpa_funcs ABI not found')
    fields = re.findall(r'\(\s*\*\s*(\w+)\s*\)', abi.group(1))
    if fields[18:20] != ['wpa3_build_sae_msg', 'wpa3_parse_sae_msg']:
        raise ValueError('selected IDF SAE callback offsets changed')

    names = [
        ('int', 'espwifi_if_sae_notify'),
        ('bool', 'espwifi_if_sae_current'),
        ('bool', 'espwifi_if_sae_teardown_pending'),
        ('void', 'espwifi_if_sae_teardown_complete'),
        ('int', 'espwifi_if_sae_wait'),
        ('void', 'espwifi_if_sae_clear_locked'),
        ('void', 'espwifi_if_sae_cancel_generation'),
        ('void', 'espwifi_if_sae_cancel'),
        ('int', 'espwifi_if_sae_configure'),
        ('bool', 'espwifi_if_sae_frame_valid'),
        ('int', 'espwifi_if_sae_request'),
        ('int', 'espwifi_if_sae_build'),
        ('int', 'espwifi_if_sae_parse'),
    ]
    kernel_functions = '\n\n'.join(function(kernel, n, r) for r, n in names)
    common_function = function(common, 'ieee80211_sae_auth_frame_validate', 'int')
    os_functions = '\n\n'.join(function(os_source, n, 'int') for n in
        ('espwifi_os_sae_build', 'espwifi_os_sae_parse'))
    callbacks = '\n\n'.join(function(fullmac, n, r) for r, n in
        (('uint8_t *', 'espwifi_wpa3_build_sae_msg'),
         ('int', 'espwifi_wpa3_parse_sae_msg')))

    harness = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef unsigned int u_int;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ETHER_ADDR_LEN 6
#define IEEE80211_ADDR_LEN 6
#define IEEE80211_NWID_LEN 32
#define IEEE80211_SAE_DATA_MAX 1536
#define IEEE80211_SAE_VERSION 1
#define IEEE80211_SAE_CONFIGURE 1
#define IEEE80211_SAE_AUTH_STATUS 2
#define IEEE80211_SAE_TX_FRAME 3
#define IEEE80211_SAE_START 0x100
#define IEEE80211_SAE_RX_FRAME 0x101
#define IEEE80211_SAE_COMMIT 1
#define IEEE80211_SAE_CONFIRM 2
#define ESPWIFI_SAE_COMMIT 1
#define ESPWIFI_SAE_CONFIRM 2
#define ESPWIFI_SAE_WAIT_SECONDS 5
#define ESPWIFI_NET80211_WPA2 1
#define ESPWIFI_SAE_HEADER_LEN 30
#define ESPWIFI_SAE_BODY_MAX (IEEE80211_SAE_DATA_MAX - ESPWIFI_SAE_HEADER_LEN)
#define ESPWIFI_SAE_WAIT_NONE 0
#define ESPWIFI_SAE_WAIT_TX 1
#define ESPWIFI_SAE_WAIT_STATUS 2
#define ESPWIFI_SAE_WAIT_RX 3
#define ESPWIFI_SAE_REPLY_NONE 0
#define ESPWIFI_SAE_REPLY_TX 1
#define ESPWIFI_SAE_REPLY_STATUS 2
#define ESPWIFI_SAE_REPLY_ANY 3
#define IEEE80211_S_AUTH 2
#define RTM_IEEE80211_SAE 109
#define SAE_MSG_COMMIT 1
#define SAE_MSG_CONFIRM 2
#define IEEE80211_M_STA 1
#define KM_NOSLEEP 0
#define MAX(a,b) ((a) > (b) ? (a) : (b))
static const int hz = 100;
static u_int now_ticks;
static int net_ipl, mutex_depth, spin_depth, cv_mode, status_to_send;
static unsigned event_log[16], event_count;
static uint8_t peer[6] = {2,1,2,3,4,5};
static uint8_t local[6] = {2,6,7,8,9,10};
static uint8_t body_data[] = {0x19,0x28,0x37,0x46};
static uint8_t peer_body[] = {0xaa,0xbb,0xcc};
static uint16_t le16dec(const void *v) {
 const uint8_t *p=v; return (uint16_t)p[0] | (uint16_t)p[1]<<8;
}
static void le16enc(void *v, uint16_t x) {
 uint8_t *p=v; p[0]=x; p[1]=x>>8;
}
static int splnet(void) { int s=net_ipl; net_ipl=1; return s; }
static void splx(int s) { net_ipl=s; }
static void mutex_enter(void *m) { (void)m; assert(mutex_depth==0); mutex_depth=1; }
static void mutex_exit(void *m) { (void)m; assert(mutex_depth==1); mutex_depth=0; }
static void mutex_spin_enter(void *m) { (void)m; assert(spin_depth==0); spin_depth=1; }
static void mutex_spin_exit(void *m) { (void)m; assert(spin_depth==1); spin_depth=0; }
static void cv_broadcast(void *c) { (void)c; }
static void *kmem_zalloc(size_t n, int flags) { (void)flags; return calloc(1,n); }
static void kmem_free(void *p, size_t n) { (void)n; free(p); }
static void explicit_memset(void *p, int c, size_t n) {
 volatile uint8_t *v=p; while (n--) *v++=(uint8_t)c;
}
static u_int getticks(void) { return now_ticks; }
struct ieee80211req_sae {
 uint32_t version, generation; uint16_t op, len; uint8_t bssid[6], reserved[2];
 uint8_t data[IEEE80211_SAE_DATA_MAX];
};
struct ifnet { void *if_softc; };
struct ieee80211_node { uint8_t ni_bssid[6], ni_essid[32]; unsigned ni_esslen; };
struct ieee80211com { struct ifnet *ic_ifp; struct ieee80211_node *ic_bss;
 int ic_state; uint8_t ic_myaddr[6]; };
struct espwifi_softc {
 struct { struct ifnet ec_if; } sc_ec;
 struct ieee80211com sc_ic;
 int sc_sae_lock, sc_sae_cv, sc_scan_lock;
 bool sc_join_disconnect_pending, sc_join_barrier_busy;
 bool sc_sae_enabled, sc_sae_start_sent;
 unsigned sc_sae_waiting, sc_sae_reply;
 int sc_sae_reply_status; unsigned sc_sae_reply_len;
 uint32_t sc_sae_generation; uint8_t sc_sae_bssid[6];
 uint8_t sc_sae_reply_frame[IEEE80211_SAE_DATA_MAX];
};
static struct espwifi_softc espwifi_sc;
void rt_ieee80211msg(struct ifnet *, int, const void *, size_t);
static int espwifi_os_in_isr(void) { return 0; }
struct espwifi_fp { int dummy; };
static void espwifi_fp_save(struct espwifi_fp *p) { (void)p; }
static void espwifi_fp_restore(struct espwifi_fp *p) { (void)p; }
static int ieee80211_sae_configure_check(struct ieee80211com *ic, int caps,
    bool enable) { (void)ic;(void)caps;(void)enable; return 0; }
void forced_memzero(void *, size_t);
void forced_memzero(void *p, size_t n) {
 volatile uint8_t *v=p; while (n--) *v++=0;
}
int espwifi_if_sae_build(const unsigned char *, unsigned, unsigned char *,
    unsigned, unsigned *);
int espwifi_if_sae_parse(const unsigned char *, unsigned, unsigned,
 unsigned);
int espwifi_os_sae_build(const unsigned char *, unsigned, unsigned char *,
    unsigned, unsigned *);
int espwifi_os_sae_parse(const unsigned char *, unsigned, unsigned,
    unsigned);
int ieee80211_sae_auth_frame_validate(const uint8_t *, size_t,
    const uint8_t *, const uint8_t *, const uint8_t *, uint16_t);
static void make_tx(struct ieee80211req_sae *, unsigned type, uint32_t);
static int espwifi_if_sae_request(struct ieee80211com *,
    const struct ieee80211req_sae *);
static void espwifi_if_sae_cancel(struct espwifi_softc *);
static void espwifi_if_sae_cancel_generation(struct espwifi_softc *, uint32_t);
static int espwifi_if_sae_configure(struct espwifi_softc *,
    const struct ieee80211req_sae *);
static int cv_timedwait(void *, void *, u_int);
''' + common_function + '\n\n' + kernel_functions + '\n\n' + os_functions + '\n\n' + \
    'static uint8_t espwifi_sae_body[ESPWIFI_SAE_BODY_MAX];\n\n' + callbacks + r'''
static int cv_timedwait(void *cv, void *mutex, u_int ticks)
{
 (void)cv; (void)mutex; assert(mutex_depth==1 && net_ipl==0 && ticks>0);
 mutex_depth=0;
 if (cv_mode==1) { espwifi_if_sae_cancel(&espwifi_sc); mutex_depth=1; return 0; }
 if (cv_mode==2) { now_ticks += ticks; mutex_depth=1; return EWOULDBLOCK; }
 if (cv_mode==3) {
  struct ieee80211req_sae configure, reply;
  int error;
  memset(&configure,0,sizeof(configure)); configure.version=1;
  configure.generation=61; configure.op=IEEE80211_SAE_CONFIGURE;
  configure.len=1; configure.data[0]=1; memcpy(configure.bssid,peer,6);
  error=espwifi_if_sae_configure(&espwifi_sc,&configure);
  assert(error==0);
  make_tx(&reply,IEEE80211_SAE_COMMIT,61);
  error=espwifi_if_sae_request(&espwifi_sc.sc_ic,&reply);
  assert(error==0);
  mutex_depth=1; return 0;
 }
 if (cv_mode==4) {
  assert(espwifi_if_sae_teardown_pending(&espwifi_sc));
  espwifi_sc.sc_join_disconnect_pending=false;
  espwifi_sc.sc_join_barrier_busy=true;
  /* The old STOP callback cancels its mailbox before the barrier completes. */
  espwifi_if_sae_cancel(&espwifi_sc);
  espwifi_sc.sc_join_barrier_busy=false;
  espwifi_if_sae_teardown_complete(&espwifi_sc);
  mutex_depth=1; return 0;
 }
 assert(0 && "unexpected wait without modeled response");
 return EINVAL;
}
static void make_tx(struct ieee80211req_sae *req, unsigned type,
    uint32_t generation)
{
 memset(req,0,sizeof(*req)); req->version=IEEE80211_SAE_VERSION;
 req->generation=generation; req->op=IEEE80211_SAE_TX_FRAME;
 req->len=ESPWIFI_SAE_HEADER_LEN+sizeof(body_data);
 memcpy(req->bssid,peer,6); req->data[0]=0xb0;
 memcpy(req->data+4,peer,6); memcpy(req->data+10,local,6);
 memcpy(req->data+16,peer,6); le16enc(req->data+24,3);
 le16enc(req->data+26,type); memcpy(req->data+30,body_data,sizeof(body_data));
}
static int route_events(struct ifnet *ifp, int type, const void *v, size_t n)
{
 const struct ieee80211req_sae *req=v;
 struct ieee80211req_sae reply;
 assert(ifp==&espwifi_sc.sc_ec.ec_if && type==109);
 assert(n==offsetof(struct ieee80211req_sae,data)+req->len);
 assert(req->version==1 && req->generation==espwifi_sc.sc_sae_generation);
 assert(memcmp(req->bssid,peer,6)==0);
 if (req->op==IEEE80211_SAE_START) {
  assert(req->len==7 && memcmp(req->data,"EmberAP",7)==0);
  event_log[event_count++]=1;
  make_tx(&reply,IEEE80211_SAE_COMMIT,espwifi_sc.sc_sae_generation);
  event_log[event_count++]=2;
  return espwifi_if_sae_request(&espwifi_sc.sc_ic,&reply);
 }
 assert(req->op==IEEE80211_SAE_RX_FRAME && req->len==33);
 assert(req->data[0]==0xb0 && req->data[24]==3);
 assert(memcmp(req->data+4,local,6)==0 && memcmp(req->data+10,peer,6)==0);
 assert(memcmp(req->data+30,peer_body,sizeof(peer_body))==0);
 event_log[event_count++]=3;
 if (le16dec(req->data+26)==IEEE80211_SAE_COMMIT) {
  make_tx(&reply,IEEE80211_SAE_CONFIRM,espwifi_sc.sc_sae_generation);
  event_log[event_count++]=2;
  return espwifi_if_sae_request(&espwifi_sc.sc_ic,&reply);
 }
 assert(le16dec(req->data+26)==IEEE80211_SAE_CONFIRM);
 memset(&reply,0,sizeof(reply)); reply.version=1;
 reply.generation=req->generation; reply.op=IEEE80211_SAE_AUTH_STATUS;
 reply.len=2; memcpy(reply.bssid,peer,6);
 le16enc(reply.data,status_to_send); event_log[event_count++]=4;
 return espwifi_if_sae_request(&espwifi_sc.sc_ic,&reply);
}
void rt_ieee80211msg(struct ifnet *ifp, int type, const void *req, size_t n)
{
 int error=route_events(ifp,type,req,n);
 assert(error==0);
}
int main(void)
{
 struct espwifi_softc *sc=&espwifi_sc;
 struct ieee80211_node node={.ni_bssid={2,1,2,3,4,5},
     .ni_essid="EmberAP",.ni_esslen=7};
	uint8_t *out; uint8_t outbody[16]; size_t outlen; unsigned int bodylen;
	int error;
 sc->sc_ec.ec_if.if_softc=sc; sc->sc_ic.ic_ifp=&sc->sc_ec.ec_if;
 sc->sc_ic.ic_bss=&node; sc->sc_ic.ic_state=IEEE80211_S_AUTH;
 memcpy(sc->sc_ic.ic_myaddr,local,6); sc->sc_sae_enabled=true;
 sc->sc_sae_generation=42; memcpy(sc->sc_sae_bssid,peer,6);
 out=espwifi_wpa3_build_sae_msg(peer,IEEE80211_SAE_COMMIT,&outlen);
 assert(out && outlen==sizeof(body_data) && memcmp(out,body_data,outlen)==0);
 assert(event_count==2 && event_log[0]==1 && event_log[1]==2);
 error=espwifi_wpa3_parse_sae_msg(peer_body,sizeof(peer_body),
     IEEE80211_SAE_COMMIT,0);
 assert(error==ESP_OK && event_count==4 && event_log[2]==3 && event_log[3]==2);
 out=espwifi_wpa3_build_sae_msg(peer,IEEE80211_SAE_CONFIRM,&outlen);
 assert(out && outlen==sizeof(body_data) && memcmp(out,body_data,outlen)==0);
 error=espwifi_wpa3_parse_sae_msg(peer_body,sizeof(peer_body),
     IEEE80211_SAE_CONFIRM,0);
 assert(error==ESP_OK && event_count==6 && event_log[4]==3 && event_log[5]==4);
 status_to_send=17;
 error=espwifi_wpa3_parse_sae_msg(peer_body,sizeof(peer_body),
     IEEE80211_SAE_CONFIRM,0);
 assert(error==ESP_FAIL);

 /* Cancellation wakes a waiter; old generation cannot complete it. */
 sc->sc_sae_waiting=ESPWIFI_SAE_WAIT_TX; cv_mode=1;
 error=espwifi_if_sae_wait(sc,42,ESPWIFI_SAE_REPLY_TX,1,
     (uint8_t *)&out,sizeof(out),NULL);
 assert(error==ESTALE && !sc->sc_sae_enabled);
 struct ieee80211req_sae stale; make_tx(&stale,1,42);
 assert(espwifi_if_sae_request(&sc->sc_ic,&stale)==ESTALE);

 /* A finite wait expires once, invalidates the generation, and scrubs data. */
 sc->sc_sae_enabled=true; sc->sc_sae_generation=50;
 memcpy(sc->sc_sae_bssid,peer,6); sc->sc_sae_waiting=ESPWIFI_SAE_WAIT_TX;
 cv_mode=2;
 error=espwifi_if_sae_wait(sc,50,ESPWIFI_SAE_REPLY_TX,1,
     (uint8_t *)&out,sizeof(out),NULL);
 assert(error==ETIMEDOUT && !sc->sc_sae_enabled &&
     sc->sc_sae_generation==51 && sc->sc_sae_reply==0);
 make_tx(&stale,1,50); assert(espwifi_if_sae_request(&sc->sc_ic,&stale)==ESTALE);
 /* A later connection generation can exchange messages after timeout. */
 sc->sc_sae_enabled=true; sc->sc_sae_generation=52;
 memcpy(sc->sc_sae_bssid,peer,6); sc->sc_sae_waiting=ESPWIFI_SAE_WAIT_NONE;
 out=espwifi_wpa3_build_sae_msg(peer,IEEE80211_SAE_COMMIT,&outlen);
 assert(out && outlen==sizeof(body_data) && memcmp(out,body_data,outlen)==0);
 assert(net_ipl==0 && mutex_depth==0);

 /* A replaced generation owns its queued reply; the old waiter cannot clear it. */
 sc->sc_sae_enabled=true; sc->sc_sae_generation=60;
 memcpy(sc->sc_sae_bssid,peer,6); sc->sc_sae_waiting=ESPWIFI_SAE_WAIT_TX;
 cv_mode=3;
 error=espwifi_if_sae_wait(sc,60,ESPWIFI_SAE_REPLY_TX,
     IEEE80211_SAE_COMMIT,(uint8_t *)&out,sizeof(out),NULL);
 assert(error==ESTALE && sc->sc_sae_generation==61 &&
     sc->sc_sae_enabled && sc->sc_sae_reply==ESPWIFI_SAE_REPLY_TX &&
     sc->sc_sae_reply_len==ESPWIFI_SAE_HEADER_LEN+sizeof(body_data));
 espwifi_if_sae_cancel_generation(sc,60);
 assert(sc->sc_sae_generation==61 && sc->sc_sae_reply==ESPWIFI_SAE_REPLY_TX);
 cv_mode=0;
 error=espwifi_if_sae_wait(sc,61,ESPWIFI_SAE_REPLY_TX,
	    IEEE80211_SAE_COMMIT,outbody,sizeof(outbody),&bodylen);
	assert(error==0 && bodylen==sizeof(body_data) &&
	    memcmp(outbody,body_data,bodylen)==0 &&
	    sc->sc_sae_reply==ESPWIFI_SAE_REPLY_NONE);

	/* A fresh CONFIGURE waits until the old STOP/start barrier completes. */
	struct ieee80211req_sae configure;
	memset(&configure, 0, sizeof(configure));
	configure.version=IEEE80211_SAE_VERSION;
	configure.generation=70; configure.op=IEEE80211_SAE_CONFIGURE;
	configure.len=1; configure.data[0]=1; memcpy(configure.bssid,peer,6);
	sc->sc_sae_enabled=true; sc->sc_sae_generation=69;
	memcpy(sc->sc_sae_bssid,peer,6);
	sc->sc_join_disconnect_pending=true; sc->sc_join_barrier_busy=false;
	cv_mode=4;
	error=espwifi_if_sae_configure(sc,&configure);
	assert(error==0 && sc->sc_sae_generation==70 && sc->sc_sae_enabled &&
	    !sc->sc_join_disconnect_pending && !sc->sc_join_barrier_busy);
	cv_mode=0;
	out=espwifi_wpa3_build_sae_msg(peer,IEEE80211_SAE_COMMIT,&outlen);
	assert(out && outlen==sizeof(body_data) &&
	    sc->sc_sae_generation==70 && sc->sc_sae_reply==ESPWIFI_SAE_REPLY_NONE);
	configure.generation=71;
	sc->sc_join_disconnect_pending=true;
	cv_mode=2;
	error=espwifi_if_sae_configure(sc,&configure);
	assert(error==ETIMEDOUT && sc->sc_sae_generation==70 &&
	    sc->sc_sae_enabled);
 return 0;
}
'''
    if args.build_dir is None:
        temp = tempfile.TemporaryDirectory()
        build = Path(temp.name)
    else:
        build = args.build_dir
        build.mkdir(parents=True, exist_ok=True)
    source = build / 'sae-mailbox.c'
    binary = build / 'sae-mailbox'
    source.write_text(harness)
    subprocess.run([args.cc, '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    print('PASS: actual IDF SAE callbacks and kernel mailbox START/RX/TX/status, cancel, timeout, stale generation, configure behind STOP barrier')


if __name__ == '__main__':
    main()
