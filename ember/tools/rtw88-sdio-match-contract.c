/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D contract tests, AI-assisted. */
/* Function-specific identity stays valid; absent F1 CIS may use common CIS. */
#include <assert.h>
#include <stddef.h>
#define __unused __attribute__((unused))
typedef void *device_t; typedef void *cfdata_t;
struct cis { unsigned manufacturer, product; };
struct sdmmc_softc;
struct sdmmc_function { unsigned number; struct cis cis; struct sdmmc_softc *sc; };
struct sdmmc_softc { struct sdmmc_function *sc_fn0; };
struct sdmmc_attach_args { struct sdmmc_function *sf; };
static int rtwsdio_admitted(device_t d){return d!=NULL;}
#include "sdio-match.inc"
int main(void)
{
 struct sdmmc_function zero={.cis={0x024c,0xd723}},one={.number=1,.cis={0x024c,0xd723}};
 struct sdmmc_softc host={.sc_fn0=&zero}; struct sdmmc_attach_args a={.sf=&one};
 one.sc=&host;assert(rtwsdio_match(&host,NULL,&a));
 one.cis=(struct cis){0,0};assert(rtwsdio_match(&host,NULL,&a));
 one.cis=(struct cis){0x1234,0x5678};assert(!rtwsdio_match(&host,NULL,&a));
 one.cis=(struct cis){0,0};zero.cis.product=1;assert(!rtwsdio_match(&host,NULL,&a));
 one.number=2;zero.cis.product=0xd723;assert(!rtwsdio_match(&host,NULL,&a));
 return 0;
}
