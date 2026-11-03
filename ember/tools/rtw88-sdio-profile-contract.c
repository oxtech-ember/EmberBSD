/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D contract tests, AI-assisted. */
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
struct props { bool regulatory, package; unsigned host_policy; };
static struct props *host_props;
typedef struct props *prop_dictionary_t;
struct rtwsdio_softc { prop_dictionary_t sc_dev; };
static prop_dictionary_t device_properties(prop_dictionary_t p) { return p; }
static __attribute__((unused)) prop_dictionary_t device_parent(prop_dictionary_t p) { return p; }
static __attribute__((unused)) int rtwsdio_host_node(prop_dictionary_t p) { return p ? 1 : -1; }
static __attribute__((unused)) int OF_getproplen(int node,const char *name)
{ unsigned bit=!strcmp(name,"ember,rtw88-regdomain") ? 1 : !strcmp(name,"ember,rtw88-channels-2ghz") ? 2 : 4; (void)node;return host_props && (host_props->host_policy & bit) ? 4 : -1; }
static void *prop_dictionary_get(prop_dictionary_t p,const char *key)
{ return (!strcmp(key,"rtw88-regulatory") ? p->regulatory : p->package) ? p : NULL; }
#include "profile.inc"
int main(void) {
 struct props p={0};struct rtwsdio_softc s={&p};
 assert(!rtwsdio_has_radio_profile(&s)); p.package=true;assert(rtwsdio_has_radio_profile(&s));
 p.package=false;p.regulatory=true;assert(rtwsdio_has_radio_profile(&s));
 p.package=true;assert(rtwsdio_has_radio_profile(&s));
 p.regulatory=p.package=false;host_props=&p;
 for (unsigned bit=1;bit<=4;bit<<=1) { p.host_policy=bit;assert(rtwsdio_has_radio_profile(&s)); }
 p.host_policy=0;assert(!rtwsdio_has_radio_profile(&s));s.sc_dev=NULL;assert(!rtwsdio_has_radio_profile(&s));
 return 0;
}
