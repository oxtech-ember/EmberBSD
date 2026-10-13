/* Origin: EmberBSD UPort 1150 USB boundary test doubles, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef UMOXA1150_FAKE_H
#define UMOXA1150_FAKE_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
struct termios { uint32_t c_iflag,c_oflag,c_cflag,c_lflag; uint8_t c_cc[20]; uint32_t c_ispeed,c_ospeed; };
#define CSIZE 0x300
#define CS5 0
#define CS6 0x100
#define CS7 0x200
#define CS8 0x300
#define CSTOPB 0x400
#define PARENB 0x1000
#define PARODD 0x2000
#define CLOCAL 0x8000
#define CRTS_IFLOW 0x10000
#define CCTS_OFLOW 0x10000
#define CDTRCTS 0x20000
#define MDMBUF 0x100000
extern bool root_is_mounted;
#define IXON 0x200
#define IXOFF 0x400
#define VSTART 12
#define VSTOP 13
#define _IOW(g,n,t) (0x80000000UL|sizeof(t)<<16|(g)<<8|(n))
#define _IOR(g,n,t) (0x40000000UL|sizeof(t)<<16|(g)<<8|(n))
#include <assert.h>
#define __UNCONST(a) ((void *)(uintptr_t)(const void *)(a))
#define EPASSTHROUGH (-2)
typedef unsigned char u_char;
typedef unsigned long u_long;
typedef unsigned int u_int;
typedef int proc_t;
typedef int cfdata_t;
typedef struct fake_device *device_t;
struct fake_device { void *softc; unsigned pending; };
typedef int usbd_status;
typedef int kmutex_t;
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define IPL_SOFTUSB 1
#define KM_SLEEP 0
#define USBD_NORMAL_COMPLETION 0
#define USBD_CANCELLED 1
#define USBD_NOT_STARTED 2
#define USBD_EXCLUSIVE_USE 1
#define USBD_SHORT_XFER_OK 2
#define USBD_DEFAULT_INTERVAL 1
#define USB_EVENT_DRIVER_ATTACH 1
#define USB_EVENT_DRIVER_DETACH 2
#define UMATCH_VENDOR_PRODUCT 10
#define UMATCH_NONE 0
#define UT_WRITE_VENDOR_DEVICE 0x40
#define UE_GET_XFERTYPE(a) ((a)&3)
#define UGETW(a) ((uint16_t)(a)[0]|((uint16_t)(a)[1]<<8))
#define USETW(a,v) do { (a)[0]=(v);(a)[1]=(v)>>8; } while(0)
#define CFATTACH_DECL2_NEW(...)
#define CFARGS(...) 0
#define UCOM_SET_DTR 1
#define UCOM_SET_RTS 2
#define UCOM_SET_BREAK 3
#ifndef CRTSCTS
#define CRTSCTS 0x00030000
#endif
#ifndef CRTS_IFLOW
#define CRTS_IFLOW CRTSCTS
#define CCTS_OFLOW CRTSCTS
#endif
struct usbd_pipe { int kind; };
struct usbd_xfer { uint8_t buffer[64]; uint32_t actual; };
typedef struct { uint8_t bNumConfigurations; } usb_device_descriptor_t;
#define USB_CONFIG_DESCRIPTOR_SIZE 9
typedef struct { uint8_t bLength,bDescriptorType,wTotalLength[2],bNumInterface,bConfigurationValue,iConfiguration,bmAttributes,bMaxPower; } usb_config_descriptor_t;
typedef struct { uint8_t bNumEndpoints; } usb_interface_descriptor_t;
typedef struct { uint8_t bEndpointAddress,bmAttributes,wMaxPacketSize[2]; } usb_endpoint_descriptor_t;
typedef struct { uint8_t bmRequestType,bRequest,wValue[2],wIndex[2],wLength[2]; } usb_device_request_t;
struct usbd_interface { usb_interface_descriptor_t id; usb_endpoint_descriptor_t ep[4]; };
struct usbd_device { usb_device_descriptor_t dd; usb_config_descriptor_t cd, configs[2]; struct usbd_interface iface; };
struct usb_attach_arg { uint16_t uaa_vendor,uaa_product; struct usbd_device *uaa_device; };
struct ucom_methods {
 void (*ucom_get_status)(void *,int,u_char *,u_char *);
 void (*ucom_set)(void *,int,int,int);
 int (*ucom_param)(void *,int,struct termios *);
 int (*ucom_ioctl)(void *,int,u_long,void *,int,proc_t *);
 bool ucom_param_strict;
 int (*ucom_open)(void *,int); void (*ucom_close)(void *,int);
};
struct ucom_attach_args { int ucaa_portno,ucaa_bulkin,ucaa_bulkout; unsigned ucaa_ibufsize,ucaa_ibufsizepad,ucaa_obufsize,ucaa_opkthdrlen; const char *ucaa_info;struct usbd_device *ucaa_device;struct usbd_interface *ucaa_iface;const struct ucom_methods *ucaa_methods;void *ucaa_arg; };
struct fake_firmware { const uint8_t *data; size_t size; };
typedef struct fake_firmware *firmware_handle_t;
static struct {
 unsigned requests,reset,created,destroyed,pipes,allocs,child_attach,child_detach,errors,delay;
 unsigned fail_request,short_request; int missing_firmware,short_bulk,fail_intr,fail_child,fail_config_desc;
 struct fake_firmware firmware; struct fake_device child;void (*hook)(device_t);
} fixture;
static inline void mutex_init(kmutex_t *m,int t,int i){(void)t;(void)i;*m=0;}
static inline void mutex_destroy(kmutex_t *m){assert(*m==0);}
static inline void mutex_enter(kmutex_t *m){assert(*m==0);*m=1;}
static inline void mutex_exit(kmutex_t *m){assert(*m==1);*m=0;}
static inline void *device_private(device_t d){return d->softc;}
static inline void *kmem_alloc(size_t n,int f){(void)f;fixture.allocs++;return malloc(n);}
static inline void kmem_free(void *p,size_t n){(void)n;assert(fixture.allocs);fixture.allocs--;free(p);}
#define aprint_normal(...) ((void)0)
#define aprint_naive(...) ((void)0)
#define aprint_normal_dev(...) ((void)0)
#define aprint_error_dev(...) ((void)fixture.errors++)
static inline void config_pending_incr(device_t d){d->pending++;}
static inline void config_pending_decr(device_t d){assert(d->pending);d->pending--;}
static inline void config_mountroot(device_t d,void(*f)(device_t)){(void)d;fixture.hook=f;}
static inline void usbd_add_drv_event(int e,struct usbd_device *u,device_t d){(void)e;(void)u;(void)d;}
static inline usb_device_descriptor_t *usbd_get_device_descriptor(struct usbd_device *u){return &u->dd;}
static inline usb_config_descriptor_t *usbd_get_config_descriptor(struct usbd_device *u){return &u->cd;}
static inline int usbd_get_config_desc(struct usbd_device *u,int i,usb_config_descriptor_t *d){if(fixture.fail_config_desc || i<0 || i>=u->dd.bNumConfigurations)return EIO;*d=u->configs[i];return 0;}
static inline int usbd_set_config_no(struct usbd_device *u,int c,int msg){(void)msg;u->cd.bConfigurationValue=c;return 0;}
static inline int usbd_device2interface_handle(struct usbd_device *u,uint8_t i,struct usbd_interface **p){(void)i;*p=&u->iface;return 0;}
static inline usb_interface_descriptor_t *usbd_get_interface_descriptor(struct usbd_interface *i){return &i->id;}
static inline usb_endpoint_descriptor_t *usbd_interface2endpoint_descriptor(struct usbd_interface *i,uint8_t n){return &i->ep[n];}
static inline int usbd_do_request_flags(struct usbd_device *u,usb_device_request_t *r,void *b,uint16_t f,int *actual,uint32_t timeout){(void)u;(void)b;(void)f;(void)timeout;fixture.requests++;*actual=UGETW(r->wLength);if(fixture.short_request==fixture.requests)(*actual)--;if(fixture.fail_request==fixture.requests)return EIO;if(r->bRequest==12)fixture.reset++;return 0;}
static inline void usbd_delay_ms(struct usbd_device *u,unsigned n){(void)u;fixture.delay+=n;}
static inline int usbd_open_pipe(struct usbd_interface *i,uint8_t a,uint8_t f,struct usbd_pipe **p){(void)i;(void)a;(void)f;*p=malloc(sizeof(**p));fixture.pipes++;return 0;}
static inline int usbd_open_pipe_intr(struct usbd_interface *i,uint8_t a,uint8_t f,struct usbd_pipe **p,void *priv,void *buf,uint32_t n,void(*cb)(struct usbd_xfer*,void*,usbd_status),int interval){(void)priv;(void)buf;(void)n;(void)cb;(void)interval;if(fixture.fail_intr)return EIO;return usbd_open_pipe(i,a,f,p);}
static inline void usbd_abort_pipe(struct usbd_pipe *p){(void)p;}
static inline void usbd_close_pipe(struct usbd_pipe *p){assert(fixture.pipes);fixture.pipes--;free(p);}
static inline int usbd_create_xfer(struct usbd_pipe *p,size_t n,unsigned f,unsigned frames,struct usbd_xfer **x){(void)p;(void)n;(void)f;(void)frames;*x=calloc(1,sizeof(**x));fixture.created++;return 0;}
static inline void usbd_destroy_xfer(struct usbd_xfer *x){fixture.destroyed++;free(x);}
static inline void *usbd_get_buffer(struct usbd_xfer *x){return x->buffer;}
static inline int usbd_bulk_transfer(struct usbd_xfer *x,struct usbd_pipe *p,int f,int timeout,void *buf,uint32_t *n){(void)x;(void)p;(void)f;(void)timeout;(void)buf;if(fixture.short_bulk)(*n)--;return 0;}
static inline void usbd_get_xfer_status(struct usbd_xfer *x,void **priv,void **buffer,uint32_t *n,usbd_status *s){(void)priv;(void)buffer;(void)s;*n=x->actual;}
static inline int firmware_open(const char *driver,const char *name,firmware_handle_t *h){(void)driver;(void)name;if(fixture.missing_firmware)return ENOENT;*h=&fixture.firmware;return 0;}
static inline off_t firmware_get_size(firmware_handle_t h){return h->size;}
static inline int firmware_read(firmware_handle_t h,off_t off,void *out,size_t n){memcpy(out,h->data+off,n);return 0;}
static inline int firmware_close(firmware_handle_t h){(void)h;return 0;}
static inline int ucomprint(void *a,const char *s){(void)a;(void)s;return 0;}
static inline device_t config_found(device_t d,void *a,int(*pr)(void *,const char *),int args){(void)d;(void)a;(void)pr;(void)args;if(fixture.fail_child)return NULL;fixture.child_attach++;return &fixture.child;}
static inline int config_detach(device_t d,int f){(void)d;(void)f;fixture.child_detach++;return 0;}
static inline void ucom_status_change(void *child){(void)child;}
#endif
