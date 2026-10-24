/* SPDX-License-Identifier: BSD-2-Clause */
/* Execute actual acquisition unwind and cdev admission functions. */
#include <sys/types.h>
#include <sys/queue.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#undef minor
#define minor(dev) ((unsigned)(dev))
#define KM_SLEEP 0
typedef void *device_t;
struct adi_axi_adc_lease;
struct adi_axi_capture_ops { int (*quiesce)(void *,unsigned); };
struct adi_axi_adc_softc {
 device_t sc_dev; int sc_phandle, sc_converter_phandle;
 bool sc_registered; struct adi_axi_adc_lease *sc_lease;
 LIST_ENTRY(adi_axi_adc_softc) sc_next;
};
struct adi_axi_converter {
 int c_phandle, c_lock; bool c_poisoned;
 struct adi_axi_adc_lease *c_lease;
 LIST_ENTRY(adi_axi_converter) c_next;
};
struct adi_axi_adc_lease {
 struct adi_axi_adc_softc *l_adc;
 struct adi_axi_converter *l_converter;
 device_t l_consumer; const struct adi_axi_capture_ops *l_ops;
 void *l_cookie; bool l_poisoned, l_quiet;
};
static LIST_HEAD(,adi_axi_adc_softc) adcs;
static LIST_HEAD(,adi_axi_converter) adi_axi_converters;
static int adi_axi_registry_lock;
static unsigned frees, refs;
static void *last_lease;
static int reserve_error;
static bool release_ok;
static struct adi_axi_adc_softc fixture;
struct cfdriver { int unused; };
struct cfdriver adiaxiadc_cd;
static void adi_axi_registry_init(void) {}
static void mutex_enter(int *p) { (void)p; }
static void mutex_exit(int *p) { (void)p; }
static void *kmem_zalloc(size_t n,int flags) { (void)flags; last_lease=calloc(1,n); assert(last_lease!=NULL); return last_lease; }
static void kmem_free(void *p,size_t n) { (void)n; frees++; free(p); }
static void device_acquire(device_t dev) { assert(dev); refs++; }
static int reserve_dmas(struct adi_axi_adc_lease *l) { assert(l==last_lease); return reserve_error; }
static bool release_dmas(struct adi_axi_adc_lease *l) { assert(l==last_lease); return release_ok; }
void *device_lookup_private(struct cfdriver *d,unsigned unit) { assert(d==&adiaxiadc_cd); return unit==0?&fixture:NULL; }
static unsigned device_unit(device_t dev) { assert(dev==&fixture); return 0; }
static int quiesce(void *p,unsigned t) { (void)p; (void)t; return 0; }
#include "provider-state.inc"
int main(int argc,char **argv)
{
 struct adi_axi_converter c={.c_phandle=2};
 struct adi_axi_capture_ops ops={.quiesce=quiesce};
 struct adi_axi_adc_lease *result=NULL;
 assert(argc==2);
 if (strcmp(argv[1],"lookup")==0) {
  LIST_INIT(&adcs);
  assert(adiaxiadc_cdev_lookup(0)==NULL);
  fixture.sc_dev=&fixture; LIST_INSERT_HEAD(&adcs,&fixture,sc_next);
  assert(adiaxiadc_cdev_lookup(0)==&fixture);
  assert(adiaxiadc_cdev_lookup(1)==NULL);
  puts("PASS failed ADC attach is refused before session lock access");
  return 0;
 }
 fixture.sc_dev=&fixture; fixture.sc_phandle=1; fixture.sc_converter_phandle=2;
 LIST_INIT(&adcs); LIST_INSERT_HEAD(&adcs,&fixture,sc_next);
 LIST_INIT(&adi_axi_converters); LIST_INSERT_HEAD(&adi_axi_converters,&c,c_next);
 reserve_error=EIO; release_ok=false;
 assert(adi_axi_adc_acquire(1,&fixture,&ops,NULL,&result)==EIO);
 assert(frees==0 && refs==2 && result==NULL);
 assert(c.c_lease==last_lease && fixture.sc_lease==last_lease);
 assert(c.c_lease->l_poisoned && c.c_poisoned);
 free(last_lease); /* Model platform restart after retained ownership. */
 c.c_lease=NULL; c.c_poisoned=false; fixture.sc_lease=NULL;
 refs=frees=0; release_ok=true; reserve_error=ENXIO;
 assert(adi_axi_adc_acquire(1,&fixture,&ops,NULL,&result)==ENXIO);
 assert(frees==1 && refs==0 && c.c_lease==NULL && fixture.sc_lease==NULL);
 puts("PASS actual acquire retains poisoned leases and frees a complete unwind");
 return 0;
}
