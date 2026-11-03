/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D contract tests, AI-assisted. */
/* Host model for actual frontend ordering; not a firmware or RF acceptance. */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
struct rtw88_softc;
struct rtw88_io { int error; };
struct rtw8723d_identity { int unused; };
struct rtw8723d_fw { int unused; };
struct rtw8723d_core_boot { int unused; };
struct ops { int (*prepare)(struct rtw88_softc *); };
struct rtw88_softc { struct ops *sc_hci; const void *sc_ops; void *sc_bus; };
struct rtwsdio_softc { struct rtw88_softc sc_core; struct { struct rtw8723d_core_boot boot; } sc_chip; };
static int seq, fail_step, released, irq_count;
static int step(void) { return ++seq == fail_step ? EIO : 0; }
static int rtwsdio_firmware_load(void *p,const char *a,const char *b,const uint8_t **data,size_t *size)
{ static uint8_t fw; (void)p;(void)a;(void)b;*data=&fw;*size=1;return step(); }
static void rtwsdio_firmware_release(void *p) { (void)p; released++; }
static int rtw8723d_fw_parse(const uint8_t *p,size_t n,struct rtw8723d_fw *f)
{ (void)p;(void)n;(void)f;return step(); }
static int prepare(struct rtw88_softc *s) { (void)s; return step(); }
static void rtw88_io_init(struct rtw88_io *io,const void *a,void *b)
{ (void)a;(void)b;io->error=0; }
static int rtw8723d_identify(struct rtw88_io *io,struct rtw8723d_identity *i)
{ (void)io;(void)i;return step(); }
static int rtw8723d_core_power_on(struct rtw8723d_core_boot *b) { (void)b;return step(); }
static int rtw8723d_core_firmware(struct rtw8723d_core_boot *b,const uint8_t *p,size_t n)
{ (void)b;(void)p;(void)n;return step(); }
static int rtwsdio_irq_start(void *p) { (void)p;irq_count++;return step(); }
#include "bootstrap.inc"
int main(void) {
 struct ops ops={.prepare=prepare}; struct rtwsdio_softc sc={.sc_core={.sc_hci=&ops}};
 for (int failure=0;failure<=7;failure++) {
  seq=released=irq_count=0;fail_step=failure;
  int error=rtwsdio_bootstrap_only(&sc);
  assert(error==(failure ? EIO : 0));
  assert(released==(failure==1 ? 0 : 1));
  assert(irq_count==(failure==0 || failure==7 ? 1 : 0));
 }
 return 0;
}
