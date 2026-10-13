/* Origin: EmberBSD native G2 fixture USB-only doubles, 2026-10-14. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/proc.h>
#include <dev/firmload.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usbdi_util.h>
#include "g2_test.h"
struct usbd_device g2_device;
static struct usbd_interface iface;
static usb_device_descriptor_t dd;
static usb_config_descriptor_t cd;
static usb_interface_descriptor_t id;
static usb_endpoint_descriptor_t ed[3];
struct gpipe { uint8_t ep; bool aborted; };
struct gxfer { struct gpipe *pipe; uint8_t buf[64]; void *data; uint32_t size,actual,timeout; usbd_status status; };
static kmutex_t lock;
static kcondvar_t cv;
static struct g2_record record;
static bool gate,short_next;
static uint8_t rx[64],event[8];
static uint32_t rxlen,eventlen;
extern void rump_g2_ready(struct g2_record *);
void g2_usb_init(void);
void g2_usb_init(void) {
 mutex_init(&lock,MUTEX_DEFAULT,IPL_NONE);cv_init(&cv,"g2test");
 memset(&dd,0,sizeof(dd));dd.bNumConfigurations=1;
 memset(&cd,0,sizeof(cd));cd.bLength=USB_CONFIG_DESCRIPTOR_SIZE;cd.bDescriptorType=UDESC_CONFIG;USETW(cd.wTotalLength,39);cd.bNumInterface=1;cd.bConfigurationValue=1;
 memset(&id,0,sizeof(id));id.bNumEndpoints=3;id.bInterfaceClass=UICLASS_VENDOR;
 for(unsigned i=0;i<3;i++){ed[i].bEndpointAddress=i==0?0x81:i==1?2:0x83;ed[i].bmAttributes=UE_BULK;USETW(ed[i].wMaxPacketSize,64);}
}
void rump_g2_snapshot(struct g2_record *out) { mutex_enter(&lock);*out=record;mutex_exit(&lock);rump_g2_ready(out); }
void rump_g2_gate(int on) { mutex_enter(&lock);gate=on!=0;cv_broadcast(&cv);mutex_exit(&lock); }
void rump_g2_short(void) { mutex_enter(&lock);short_next=true;mutex_exit(&lock); }
usb_device_descriptor_t *usbd_get_device_descriptor(struct usbd_device *d) { KASSERT(d==&g2_device);return &dd; }
usb_config_descriptor_t *usbd_get_config_descriptor(struct usbd_device *d) { KASSERT(d==&g2_device);return &cd; }
usbd_status usbd_set_config_index(struct usbd_device *d,int i,int msg) { (void)d;(void)i;(void)msg;return USBD_IOERROR; }
usbd_status usbd_device2interface_handle(struct usbd_device *d,uint8_t i,struct usbd_interface **out) { KASSERT(d==&g2_device && !i);*out=&iface;return 0; }
usb_interface_descriptor_t *usbd_get_interface_descriptor(struct usbd_interface *i) { KASSERT(i==&iface);return &id; }
usb_endpoint_descriptor_t *usbd_interface2endpoint_descriptor(struct usbd_interface *i,uint8_t n) { KASSERT(i==&iface && n<3);return &ed[n]; }
void usbd_add_drv_event(int e,struct usbd_device *d,device_t self) { (void)e;(void)d;(void)self; }
usbd_status usbd_do_request_flags(struct usbd_device *d,usb_device_request_t *r,void *out,uint16_t flags,int *actual,uint32_t timeout) {
 (void)flags;KASSERT(d==&g2_device && timeout<=1000);*actual=UGETW(r->wLength);
 mutex_enter(&lock);record.controls++;mutex_exit(&lock);
 if(r->bmRequestType==UT_READ_VENDOR_DEVICE){
  if(r->bRequest==0x81 && *actual==4)memcpy(out,"\1\2\3\0",4);
  else if(r->bRequest==0x34 && *actual==4)memcpy(out,"\2\0\0\0",4);
  else if(r->bRequest==0x86 && *actual==1)*(uint8_t *)out=0x90;
  else return USBD_IOERROR;
 }
 return 0;
}
void usbd_delay_ms(struct usbd_device *d,u_int n) { (void)d;kpause("g2delay",false,MAX(1,mstohz(n)),NULL); }
usbd_status usbd_open_pipe(struct usbd_interface *i,uint8_t ep,uint8_t f,struct usbd_pipe **out) {
 struct gpipe *p=kmem_zalloc(sizeof(*p),KM_SLEEP);(void)f;KASSERT(i==&iface);p->ep=ep;*out=(void *)p;mutex_enter(&lock);record.pipes++;mutex_exit(&lock);return 0;
}
void usbd_close_pipe(struct usbd_pipe *arg) { mutex_enter(&lock);record.pipes--;mutex_exit(&lock);kmem_free(arg,sizeof(struct gpipe)); }
void usbd_abort_pipe(struct usbd_pipe *arg) { struct gpipe *p=(void *)arg;mutex_enter(&lock);p->aborted=true;cv_broadcast(&cv);mutex_exit(&lock); }
int usbd_create_xfer(struct usbd_pipe *arg,size_t n,unsigned flags,unsigned frames,struct usbd_xfer **out) {
 struct gxfer *x=kmem_zalloc(sizeof(*x),KM_SLEEP);(void)flags;(void)frames;KASSERT(n<=64);x->pipe=(void *)arg;*out=(void *)x;mutex_enter(&lock);record.xfers++;mutex_exit(&lock);return 0;
}
void usbd_destroy_xfer(struct usbd_xfer *arg) { mutex_enter(&lock);record.xfers--;mutex_exit(&lock);kmem_free(arg,sizeof(struct gxfer)); }
void *usbd_get_buffer(struct usbd_xfer *arg) { return ((struct gxfer *)(void *)arg)->buf; }
void usbd_setup_xfer(struct usbd_xfer *arg,void *priv,void *data,uint32_t n,uint16_t flags,uint32_t timeout,usbd_callback cb) {
 struct gxfer *x=(void *)arg;(void)priv;(void)flags;KASSERT(n<=64 && cb==NULL);x->data=data;x->size=n;x->timeout=timeout;x->actual=0;
}
usbd_status usbd_sync_transfer(struct usbd_xfer *arg) {
 struct gxfer *x=(void *)arg;struct gpipe *p=x->pipe;uint8_t *b=x->data;
 mutex_enter(&lock);
 if(!(p->ep&0x80)){
  record.blocked=1;cv_broadcast(&cv);
  while(gate && !p->aborted){if(cv_timedwait(&cv,&lock,MAX(1,mstohz(x->timeout)))==EWOULDBLOCK)break;}
  record.blocked=0;
  if(p->aborted){x->status=USBD_CANCELLED;goto out;}
  if(gate){x->status=USBD_TIMEOUT;goto out;}
  KASSERT(x->size>=4 && b[1]<4 && !rxlen);record.writes++;x->actual=x->size;
  if(short_next){short_next=false;x->actual--;x->status=0;goto out;}
  memcpy(rx,b,x->size);rx[0]=0;rxlen=x->size;
  if(b[0]&0x80){KASSERT(!eventlen);memset(event,0,8);event[1]=b[1];event[3]=2;eventlen=8;}
  x->status=0;
 }else if(p->aborted){x->status=USBD_CANCELLED;
 }else if(p->ep==0x81 && rxlen){KASSERT(rxlen<=x->size);memcpy(b,rx,rxlen);x->actual=rxlen;rxlen=0;x->status=0;
 }else if(p->ep==0x83 && eventlen){KASSERT(eventlen<=x->size);memcpy(b,event,eventlen);x->actual=eventlen;eventlen=0;x->status=0;
 }else{x->status=USBD_TIMEOUT;}
out:
 mutex_exit(&lock);return x->status;
}
usbd_status usbd_bulk_transfer(struct usbd_xfer *arg,struct usbd_pipe *pipe,uint16_t flags,uint32_t timeout,void *data,uint32_t *n) {
 usbd_status s;(void)pipe;usbd_setup_xfer(arg,NULL,data,*n,flags,timeout,NULL);s=usbd_sync_transfer(arg);*n=((struct gxfer *)(void *)arg)->actual;return s;
}
void usbd_get_xfer_status(struct usbd_xfer *arg,void **priv,void **buf,uint32_t *actual,usbd_status *status) {
 struct gxfer *x=(void *)arg;if(priv)*priv=NULL;if(buf)*buf=x->data;if(actual)*actual=x->actual;if(status)*status=x->status;
}
#define FAIL() panic("unexpected legacy USB in native G2 fixture")
usbd_status usbd_transfer(struct usbd_xfer *x) { (void)x;FAIL();return USBD_IOERROR; }
void usbd_clear_endpoint_stall_async(struct usbd_pipe *p) { (void)p;FAIL(); }
const char *usbd_errstr(usbd_status s) { (void)s;return "G2 test USB"; }
int firmware_open(const char *d,const char *name,firmware_handle_t *h) { (void)d;(void)name;(void)h;return ENOENT; }
int firmware_close(firmware_handle_t h) { (void)h;FAIL();return EIO; }
off_t firmware_get_size(firmware_handle_t h) { (void)h;FAIL();return 0; }
int firmware_read(firmware_handle_t h,off_t off,void *p,size_t n) { (void)h;(void)off;(void)p;(void)n;FAIL();return EIO; }
