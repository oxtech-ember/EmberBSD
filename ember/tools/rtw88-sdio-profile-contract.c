/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D contract tests, AI-assisted. */
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
struct props { bool regulatory, package; };
typedef struct props *prop_dictionary_t;
struct rtwsdio_softc { prop_dictionary_t sc_dev; };
static prop_dictionary_t device_properties(prop_dictionary_t p) { return p; }
static void *prop_dictionary_get(prop_dictionary_t p,const char *key)
{ return (!strcmp(key,"rtw88-regulatory") ? p->regulatory : p->package) ? p : NULL; }
#include "profile.inc"
int main(void) {
 struct props p={0};struct rtwsdio_softc s={&p};
 assert(!rtwsdio_has_radio_profile(&s)); p.package=true;assert(rtwsdio_has_radio_profile(&s));
 p.package=false;p.regulatory=true;assert(rtwsdio_has_radio_profile(&s));
 p.package=true;assert(rtwsdio_has_radio_profile(&s));s.sc_dev=NULL;assert(!rtwsdio_has_radio_profile(&s));
 return 0;
}
