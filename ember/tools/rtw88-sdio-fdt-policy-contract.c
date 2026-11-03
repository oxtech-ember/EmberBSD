/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D FDT policy contract, AI-assisted. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <dev/ic/rtw88_chip.h>
#include <dev/ic/rtw88_profile.h>
struct rtw88_softc { uint8_t sc_mac[6];const struct rtw88_chip_ops *sc_chip; };
struct rtwsdio_softc { void *sc_dev;struct rtw88_softc sc_core; };
struct object { bool present;uint8_t bytes[6]; };
typedef struct object *prop_object_t;
typedef struct object *prop_data_t;
typedef struct object *prop_dictionary_t;
#define PROP_TYPE_DATA 1
static struct object mac;
static bool child_reg, child_package;static int child_calls;
static char domain[10]="world-min";
static int lengths[3]={10,4,4};
static uint32_t channels=0x7ff,package_id;
static void *device_parent(void *p) { return p; }
static int rtwsdio_host_node(void *p) { return p ? 1 : -1; }
static prop_dictionary_t device_properties(void *p) { return p ? &mac : NULL; }
static prop_object_t prop_dictionary_get(prop_dictionary_t p,const char *name)
{ if (!p)return NULL;if(!strcmp(name,"rtw88-regulatory"))return child_reg?p:NULL;
 if(!strcmp(name,"rtw8723d-phy-package"))return child_package?p:NULL;
 return p->present && !strcmp(name,"mac-address") ? p : NULL; }
static bool prop_dictionary_get_uint32(prop_dictionary_t p,const char *key,uint32_t *out)
{ (void)p;(void)key;if(!child_package)return false;*out=2;return true; }
static int rtw88_profile_from_properties(void *d,const uint8_t *m,const struct rtw88_caps *c,struct rtw88_profile *p)
{ (void)d;(void)m;(void)c;(void)p;child_calls++;return EDOM; }
static int prop_object_type(prop_object_t p) { (void)p;return PROP_TYPE_DATA; }
static size_t prop_data_size(prop_data_t p) { (void)p;return 6; }
static const void *prop_data_value(prop_data_t p) { return p->bytes; }
static int index_of(const char *p)
{ return !strcmp(p,"ember,rtw88-regdomain") ? 0 : !strcmp(p,"ember,rtw88-channels-2ghz") ? 1 : 2; }
static int OF_getproplen(int node,const char *p) { (void)node;return lengths[index_of(p)]; }
static int OF_getprop(int node,const char *p,void *out,int n)
{
 int i=index_of(p);(void)node;if(lengths[i]!=n)return -1;
 if(i==0)memcpy(out,domain,n);else { uint32_t v=i==1?channels:package_id;uint8_t *b=out;
 b[0]=v>>24;b[1]=v>>16;b[2]=v>>8;b[3]=v; } return n;
}
static uint32_t be32dec(const void *p)
{ const uint8_t *b=p;return ((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3]; }
#include "profile-fdt.inc"
int main(void)
{
 struct rtw88_chip_ops chip={.caps={.bands=RTW88_BAND_2G,.widths=RTW88_WIDTH_20}};
 struct rtwsdio_softc sc={.sc_dev=&mac,.sc_core={.sc_chip=&chip,.sc_mac={2,1,2,3,4,5}}};
 struct rtw88_profile profile,before;uint32_t pkg;
 memset(&profile,0xa5,sizeof(profile));before=profile;pkg=99;
 assert(rtwsdio_fdt_profile(&sc,&profile,&pkg)==0);
 assert(profile.domain==RTW88_REG_WORLD_MIN && profile.channels_2g==0x7ff && profile.tx_admitted && pkg==0);
 assert(memcmp(profile.mac,sc.sc_core.sc_mac,6)==0);
 for(int k=0;k<5;k++) {
  profile=before;pkg=99;lengths[1]=4;channels=0x7ff;package_id=0;domain[0]="world-min"[0];
  if(k==0)lengths[1]=-1;if(k==1)channels=0;if(k==2)channels=0x2000;if(k==3)package_id=16;if(k==4)domain[0]="x"[0];
  assert(rtwsdio_fdt_profile(&sc,&profile,&pkg)!=0);
  assert(!memcmp(&profile,&before,sizeof(profile)) && pkg==99);
 }
 domain[0]="w"[0];channels=0x7ff;package_id=3;
 mac.present=true;memcpy(mac.bytes,"\x02\x10\x20\x30\x40\x50",6);
 assert(rtwsdio_fdt_profile(&sc,&profile,&pkg)==0);
 assert(pkg==3 && memcmp(profile.mac,mac.bytes,6)==0);
 child_reg=true;child_package=false;child_calls=0;
 assert(rtwsdio_profile(&sc,&profile,&pkg)==EACCES && child_calls==0);
 child_package=true;domain[0]="x"[0];
 assert(rtwsdio_profile(&sc,&profile,&pkg)==EDOM && child_calls==1);
 return 0;
}
