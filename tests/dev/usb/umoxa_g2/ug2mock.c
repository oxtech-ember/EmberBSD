/* Origin: EmberBSD native actual G2 attach/lifetime proxy, 2026-10-14. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/termios.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/ucomvar.h>
#include <dev/usb/umoxa_g2var.h>
#include "g2_test.h"
extern struct cfattach umoxag_ca;
extern struct usbd_device g2_device;
extern void g2_usb_init(void);
static struct umoxa_g2_softc *mock;
static device_t mock_device;
static int match(device_t p,cfdata_t c,void *a) { (void)p;(void)c;(void)a;return 1; }
static void attach(device_t p,device_t self,void *arg) {
 struct usb_attach_arg a; (void)arg;
 g2_usb_init();memset(&a,0,sizeof(a));a.uaa_device=&g2_device;a.uaa_vendor=0x110a;a.uaa_product=0x1452;
 mock=device_private(self);mock_device=self;umoxag_ca.ca_attach(p,self,&a);
}
static int detach(device_t self,int flags) { int e=umoxag_ca.ca_detach(self,flags);if(!e){mock=NULL;mock_device=NULL;}return e; }
static void childdet(device_t self,device_t child) { umoxag_ca.ca_childdetached(self,child); }
CFATTACH_DECL2_NEW(ug2mock,sizeof(struct umoxa_g2_softc),match,attach,detach,NULL,NULL,childdet);
void rump_g2_ready(struct g2_record *r) { if(mock){mutex_enter(&mock->sc_lock);r->ready=mock->sc_ready;mutex_exit(&mock->sc_lock);}else r->ready=0; }
int rump_g2_remove(void) { return mock_device?config_detach(mock_device,DETACH_FORCE):ENXIO; }
