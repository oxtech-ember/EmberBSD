/* Origin: EmberBSD actual G2 USB-parent boundary doubles, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef UMOXA_G2_FAKE_H
#define UMOXA_G2_FAKE_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <time.h>
typedef unsigned char u_char;
typedef unsigned int u_int;
typedef unsigned long u_long;
typedef int proc_t;
typedef int cfdata_t;
typedef int pri_t;
typedef int kmutex_t;
typedef struct fake_device *device_t;
struct fake_device { void *softc; unsigned unit; };
struct termios { uint32_t c_iflag,c_oflag,c_cflag,c_lflag;uint8_t c_cc[20];uint32_t c_ispeed,c_ospeed; };
#define CSIZE 0x300
#define CS5 0
#define CS6 0x100
#define CS7 0x200
#define CS8 0x300
#define CSTOPB 0x400
#define PARENB 0x1000
#define PARODD 0x2000
#define CLOCAL 0x8000
#define CRTSCTS 0x10000
#define CRTS_IFLOW CRTSCTS
#define CCTS_OFLOW CRTSCTS
#define CDTRCTS 0x20000
#define MDMBUF 0x100000
#define _IOW(g,n,t) (0x80000000UL|sizeof(t)<<16|(g)<<8|(n))
#define _IOR(g,n,t) (0x40000000UL|sizeof(t)<<16|(g)<<8|(n))
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define IPL_SOFTUSB 1
#define KM_SLEEP 0
#define WQ_MPSAFE 1
#define PRI_NONE 0
#define CALLOUT_MPSAFE 1
#define hz 100
#define MAX(a,b) ((a)>(b)?(a):(b))
#define KASSERT assert
#define __UNCONST(a) ((void *)(uintptr_t)(const void *)(a))
#define CFATTACH_DECL2_NEW(...)
#define CFARGS(...) 0
#define DETACH_FORCE 1
#define EPASSTHROUGH (-2)
#define UMATCH_NONE 0
#define UMATCH_VENDOR_PRODUCT 10
#define UCOM_SET_DTR 1
#define UCOM_SET_RTS 2
#define UCOM_SET_BREAK 3
#define UMCR_DTR 1
#define UMCR_RTS 2
#define ULSR_RCV_MASK 0x1f
#define USBD_NORMAL_COMPLETION 0
#define USBD_CANCELLED 6
#define USBD_IOERROR 13
#define USBD_TIMEOUT 15
#define USBD_NOMEM 17
#define USBD_SHORT_XFER_OK 2
#define USBD_EXCLUSIVE_USE 1
#define UT_WRITE_VENDOR_DEVICE 0x40
#define UT_READ_VENDOR_DEVICE 0xc0
#define USB_EVENT_DRIVER_ATTACH 1
#define USB_EVENT_DRIVER_DETACH 2
#define USB_CONFIG_DESCRIPTOR_SIZE 9
#define UDESC_CONFIG 2
#define UICLASS_VENDOR 0xff
#define UE_GET_XFERTYPE(a) ((a)&3)
#define UGETW(a) ((uint16_t)(a)[0]|((uint16_t)(a)[1]<<8))
#define USETW(a,v) do { (a)[0]=(uint8_t)(v);(a)[1]=(uint8_t)((v)>>8); } while(0)
typedef int usbd_status;
typedef struct { uint8_t bLength,bDescriptorType,bcdUSB[2],bDeviceClass,bDeviceSubClass,bDeviceProtocol,bMaxPacketSize,idVendor[2],idProduct[2],bcdDevice[2],iManufacturer,iProduct,iSerialNumber,bNumConfigurations; } usb_device_descriptor_t;
typedef struct { uint8_t bLength,bDescriptorType,wTotalLength[2],bNumInterface,bConfigurationValue,iConfiguration,bmAttributes,bMaxPower; } usb_config_descriptor_t;
typedef struct { uint8_t bLength,bDescriptorType,bInterfaceNumber,bAlternateSetting,bNumEndpoints,bInterfaceClass,bInterfaceSubClass,bInterfaceProtocol,iInterface; } usb_interface_descriptor_t;
typedef struct { uint8_t bLength,bDescriptorType,bEndpointAddress,bmAttributes,wMaxPacketSize[2],bInterval; } usb_endpoint_descriptor_t;
typedef struct { uint8_t bmRequestType,bRequest,wValue[2],wIndex[2],wLength[2]; } usb_device_request_t;
struct usbd_interface { usb_interface_descriptor_t id;usb_endpoint_descriptor_t ep[4]; };
struct usbd_device { usb_device_descriptor_t dd;usb_config_descriptor_t cd;struct usbd_interface iface; };
struct usb_attach_arg { uint16_t uaa_vendor,uaa_product;struct usbd_device *uaa_device; };
struct usbd_pipe { uint8_t endpoint; };
struct usbd_xfer { uint8_t buffer[1024]; };
struct work { void *wk_dummy; };
struct workqueue { void(*fn)(struct work *,void *);void *arg;struct work *queued;int running; };
typedef struct { void(*fn)(void *);void *arg;int scheduled; } callout_t;
struct ucom_softc { void *parent;int port;uint64_t epoch;int live; };
struct ucom_transport_methods {
 int(*uct_attach)(void *,int,struct ucom_softc *);int(*uct_start)(void *,int,uint64_t);
 void(*uct_stop)(void *,int,uint64_t);int(*uct_submit)(void *,int,uint64_t,uint64_t,const uint8_t *,size_t);
 void(*uct_rx_flow)(void *,int,uint64_t,int);int(*uct_set)(void *,int,int,int);void(*uct_detach)(void *,int);
};
struct ucom_methods {
 void(*ucom_get_status)(void *,int,u_char *,u_char *);void(*ucom_set)(void *,int,int,int);
 int(*ucom_param)(void *,int,struct termios *);int(*ucom_ioctl)(void *,int,u_long,void *,int,proc_t *);
 int(*ucom_open)(void *,int);void(*ucom_close)(void *,int);
 void(*ucom_read)(void *,int,u_char **,uint32_t *);void(*ucom_write)(void *,int,u_char *,u_char *,uint32_t *);
 const struct ucom_transport_methods *ucom_transport;bool ucom_param_strict;
};
struct ucom_attach_args { int ucaa_portno,ucaa_bulkin,ucaa_bulkout;u_int ucaa_ibufsize,ucaa_ibufsizepad,ucaa_obufsize,ucaa_opkthdrlen;const char *ucaa_info;struct usbd_device *ucaa_device;struct usbd_interface *ucaa_iface;const struct ucom_methods *ucaa_methods;void *ucaa_arg; };
struct fake_firmware { const uint8_t *data;size_t size; };
typedef struct fake_firmware *firmware_handle_t;
struct fake_request { uint8_t type,command;uint16_t value,index;size_t length;uint8_t data[64]; };
static struct {
 unsigned requests,fail_request,short_request,errors,resources,fail_resource,pipes,xfers,allocs,children,detaches,fail_child,waits;
 unsigned fw_blocks,fw_bytes,fw_open,fw_closed,stops,delay,data_reads,event_reads,out_count;
 int missing_firmware,fw_read_error,stop_runs,stop_disconnect,disconnected,short_out,rx_error,event_error,event_repeat,overflow_port;
 uint8_t version[4],reg[4],data[1024],event[1024],out[16][1024];size_t data_len,event_len,out_len[16];
 unsigned done[4],fault[4],status[4],inputs[4];size_t actual[4];int fault_errno[4];uint8_t received[4][1024];size_t received_len[4];
 struct fake_request request[128];struct fake_firmware firmware;struct fake_device child[4];struct ucom_softc tty[4];
 const struct ucom_methods *methods[4];kmutex_t *state_lock;uint64_t now_ms;struct workqueue *wq;
} fixture;
extern bool root_is_mounted;
static inline bool resource_failure(void){fixture.resources++;return fixture.resources==fixture.fail_resource;}
static inline void mutex_init(kmutex_t *m,int type,int ipl){(void)type;*m=0;if(ipl==IPL_SOFTUSB)fixture.state_lock=m;}
static inline void mutex_enter(kmutex_t *m){assert(!*m);*m=1;}
static inline void mutex_exit(kmutex_t *m){assert(*m);*m=0;}
static inline bool mutex_owned(kmutex_t *m){return *m!=0;}
static inline void mutex_destroy(kmutex_t *m){assert(!*m);}
static inline void *device_private(device_t d){return d->softc;}
static inline const char *device_xname(device_t d){(void)d;return "umoxag0";}
static inline void *kmem_alloc(size_t n,int f){(void)f;fixture.allocs++;return malloc(n);}
static inline void kmem_free(void *p,size_t n){(void)n;assert(fixture.allocs);fixture.allocs--;free(p);}
#define aprint_naive(...) ((void)0)
#define aprint_normal(...) ((void)0)
#define aprint_normal_dev(...) ((void)0)
#define aprint_error_dev(...) ((void)fixture.errors++)
static inline void usbd_add_drv_event(int e,struct usbd_device *u,device_t d){(void)e;(void)u;(void)d;}
static inline usb_device_descriptor_t *usbd_get_device_descriptor(struct usbd_device *u){return &u->dd;}
static inline usb_config_descriptor_t *usbd_get_config_descriptor(struct usbd_device *u){return &u->cd;}
static inline int usbd_set_config_index(struct usbd_device *u,int i,int msg){(void)msg;if(i)return EIO;u->cd.bConfigurationValue=1;return 0;}
static inline int usbd_device2interface_handle(struct usbd_device *u,uint8_t i,struct usbd_interface **out){if(i)return EIO;*out=&u->iface;return 0;}
static inline usb_interface_descriptor_t *usbd_get_interface_descriptor(struct usbd_interface *i){return &i->id;}
static inline usb_endpoint_descriptor_t *usbd_interface2endpoint_descriptor(struct usbd_interface *i,uint8_t n){return n<i->id.bNumEndpoints?&i->ep[n]:NULL;}
static inline int usbd_do_request_flags(struct usbd_device *u,usb_device_request_t *r,void *data,uint16_t flags,int *actual,uint32_t timeout){
 struct fake_request *q;unsigned n=fixture.requests++;(void)u;(void)flags;assert(timeout<=1000 && n<128);
 q=&fixture.request[n];*q=(struct fake_request){r->bmRequestType,r->bRequest,UGETW(r->wValue),UGETW(r->wIndex),UGETW(r->wLength),{0}};
 *actual=(int)q->length;assert(q->length<=64);
 if(fixture.disconnected){*actual=0;return USBD_IOERROR;}
 if(q->type==UT_READ_VENDOR_DEVICE && q->command==0x81 && q->length==4)memcpy(data,fixture.version,4);
 if(q->type==UT_READ_VENDOR_DEVICE && q->command==0x34 && q->length==4)memcpy(data,fixture.reg,4);
 if(q->type==UT_READ_VENDOR_DEVICE && q->command==0x86 && q->length==1)*(uint8_t *)data=0x90;
 if(q->type==UT_WRITE_VENDOR_DEVICE && q->command==0x20){fixture.fw_blocks++;fixture.fw_bytes+=(unsigned)q->length;}
 if(q->type==UT_WRITE_VENDOR_DEVICE && q->command==0x0d){
  fixture.stops++;if(fixture.stop_runs && fixture.firmware.size>=0x8b){fixture.version[0]=fixture.firmware.data[0x86];fixture.version[1]=fixture.firmware.data[0x88];fixture.version[2]=fixture.firmware.data[0x8a];fixture.reg[0]=2;}
  if(fixture.stop_disconnect)fixture.disconnected=1;
 }
 if(q->length)memcpy(q->data,data,q->length);
 if(fixture.short_request==n+1)(*actual)--;
 if(fixture.fail_request==n+1 || fixture.disconnected)return USBD_IOERROR;
 return 0;
}
static inline void usbd_delay_ms(struct usbd_device *u,unsigned ms){(void)u;fixture.delay+=ms;}
static inline int usbd_open_pipe(struct usbd_interface *i,uint8_t endpoint,uint8_t flags,struct usbd_pipe **p){(void)i;(void)flags;if(resource_failure())return USBD_NOMEM;*p=malloc(sizeof(**p));(*p)->endpoint=endpoint;fixture.pipes++;return 0;}
static inline int usbd_create_xfer(struct usbd_pipe *p,size_t size,unsigned flags,unsigned frames,struct usbd_xfer **x){(void)p;(void)flags;(void)frames;assert(size<=1024);if(resource_failure())return USBD_NOMEM;*x=calloc(1,sizeof(**x));fixture.xfers++;return 0;}
static inline void *usbd_get_buffer(struct usbd_xfer *x){return x->buffer;}
static inline void usbd_destroy_xfer(struct usbd_xfer *x){assert(fixture.xfers);fixture.xfers--;free(x);}
static inline void usbd_abort_pipe(struct usbd_pipe *p){(void)p;}
static inline void usbd_close_pipe(struct usbd_pipe *p){assert(fixture.pipes);fixture.pipes--;free(p);}
static inline int usbd_bulk_transfer(struct usbd_xfer *x,struct usbd_pipe *p,int flags,int timeout,void *data,uint32_t *n){
 size_t count;(void)x;(void)flags;assert(timeout<=1000);
 if(!(p->endpoint&0x80)){
  unsigned index=fixture.out_count++;assert(index<16 && *n<=1024);memcpy(fixture.out[index],data,*n);fixture.out_len[index]=*n;
  if(fixture.short_out)(*n)--;
  return 0;
 }
 if(p->endpoint==0x81){fixture.data_reads++;count=fixture.data_len;if(fixture.rx_error){*n=0;return fixture.rx_error;}if(count){assert(count<=*n);memcpy(data,fixture.data,count);fixture.data_len=0;*n=(uint32_t)count;return 0;}}
 else {fixture.event_reads++;count=fixture.event_len;if(fixture.event_error){*n=0;return fixture.event_error;}if(count){assert(count<=*n);memcpy(data,fixture.event,count);if(!fixture.event_repeat)fixture.event_len=0;*n=(uint32_t)count;return 0;}}
 *n=0;return USBD_TIMEOUT;
}
static inline int workqueue_create(struct workqueue **w,const char *name,void(*fn)(struct work *,void *),void *arg,pri_t pri,int ipl,int flags){(void)name;(void)pri;(void)ipl;(void)flags;if(resource_failure())return ENOMEM;*w=calloc(1,sizeof(**w));(*w)->fn=fn;(*w)->arg=arg;fixture.wq=*w;return 0;}
static inline void workqueue_enqueue(struct workqueue *q,struct work *w,void *cpu){(void)cpu;assert(!q->queued);q->queued=w;}
static inline void run_work(void){struct workqueue *q=fixture.wq;struct work *w;if(q && q->queued){w=q->queued;q->queued=NULL;assert(!q->running);q->running=1;q->fn(w,q->arg);q->running=0;}}
static inline void workqueue_wait(struct workqueue *q,struct work *w){(void)w;fixture.waits++;assert(!q->running);run_work();}
static inline void workqueue_destroy(struct workqueue *q){assert(!q->queued && !q->running);free(q);fixture.wq=NULL;}
static inline void callout_init(callout_t *c,int flags){(void)flags;memset(c,0,sizeof(*c));}
static inline void callout_setfunc(callout_t *c,void(*fn)(void *),void *arg){c->fn=fn;c->arg=arg;}
static inline void callout_schedule(callout_t *c,int ticks){assert(ticks>=1);c->scheduled=1;}
static inline bool callout_halt(callout_t *c,kmutex_t *m){(void)m;c->scheduled=0;return true;}
static inline void callout_destroy(callout_t *c){assert(!c->scheduled);c->fn=NULL;}
static inline void getnanouptime(struct timespec *t){t->tv_sec=(time_t)(fixture.now_ms/1000);t->tv_nsec=(long)(fixture.now_ms%1000)*1000000;}
static inline int firmware_open(const char *driver,const char *name,firmware_handle_t *h){assert(!strcmp(driver,"moxa") && !strcmp(name,"moxa-1450-g2.fw"));if(fixture.missing_firmware || !fixture.firmware.data)return ENOENT;*h=&fixture.firmware;fixture.fw_open++;return 0;}
static inline off_t firmware_get_size(firmware_handle_t h){return (off_t)h->size;}
static inline int firmware_read(firmware_handle_t h,off_t off,void *out,size_t n){if(fixture.fw_read_error)return EIO;assert((size_t)off+n<=h->size);memcpy(out,h->data+off,n);return 0;}
static inline int firmware_close(firmware_handle_t h){(void)h;fixture.fw_closed++;return 0;}
static inline int ucomprint(void *arg,const char *parent){(void)arg;(void)parent;return 0;}
static inline void child_unlocked(struct ucom_softc *c){assert(c && c->live);assert(!fixture.state_lock || !*fixture.state_lock);}
static inline int ucom_transport_input(struct ucom_softc *c,uint64_t e,const void *data,size_t n){child_unlocked(c);assert(e==c->epoch);fixture.inputs[c->port]++;if(fixture.overflow_port==c->port)return ENOBUFS;assert(n<=1024);memcpy(fixture.received[c->port],data,n);fixture.received_len[c->port]=n;return 0;}
static inline void ucom_transport_done(struct ucom_softc *c,uint64_t e,uint64_t cookie,size_t n,int error){(void)cookie;child_unlocked(c);assert(e==c->epoch && !error);fixture.done[c->port]++;fixture.actual[c->port]=n;}
static inline void ucom_transport_fault(struct ucom_softc *c,uint64_t e,int error){child_unlocked(c);assert(e==c->epoch);fixture.fault[c->port]++;fixture.fault_errno[c->port]=error;}
static inline void ucom_status_change(struct ucom_softc *c){child_unlocked(c);fixture.status[c->port]++;}
static inline device_t config_found(device_t parent,void *arg,int(*print)(void *,const char *),int flags){
 struct ucom_attach_args *a=arg;unsigned p=(unsigned)a->ucaa_portno;struct ucom_softc *c;(void)parent;(void)print;(void)flags;assert(p<4 && a->ucaa_methods->ucom_transport && a->ucaa_obufsize>0);
 if(fixture.fail_child==p+1)return NULL;
 c=&fixture.tty[p];*c=(struct ucom_softc){a->ucaa_arg,(int)p,0,1};fixture.methods[p]=a->ucaa_methods;
 if(a->ucaa_methods->ucom_transport->uct_attach(a->ucaa_arg,(int)p,c)){c->live=0;return NULL;}
 fixture.child[p]=(struct fake_device){c,p};fixture.children++;return &fixture.child[p];
}
static inline int config_detach(device_t d,int flags){struct ucom_softc *c=d->softc;const struct ucom_transport_methods *m=fixture.methods[c->port]->ucom_transport;(void)flags;if(c->epoch)m->uct_stop(c->parent,c->port,c->epoch);m->uct_detach(c->parent,c->port);c->live=0;fixture.detaches++;return 0;}
#endif
