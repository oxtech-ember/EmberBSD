/* Origin: EmberBSD UPort 1150 executable wire/lifecycle tests, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "umoxa1150_protocol.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#x); return 0; } } while (0)
static struct umoxa1150_settings settings = {9600,8,0,1,0,0,0,0,0,0x11,0x13};
struct trace { unsigned n, fail, bulk_n; int short_bulk, zero_bulk, bulk_error;
 uint8_t req[16], data[16][10]; uint16_t val[16], index[16]; size_t len[16], bytes; };
static int control(void *a,uint8_t req,uint16_t val,uint16_t index,const uint8_t *data,size_t len) {
 struct trace *t=a; unsigned n=t->n++;
 if(n>=16 || len>10) return EOVERFLOW;
 t->req[n]=req;t->val[n]=val;t->index[n]=index;t->len[n]=len;
 if(len) memcpy(t->data[n],data,len);
 return t->fail==n+1 ? EIO : 0;
}
static int bulk(void *a,const uint8_t *data,size_t len,size_t *actual) {
 struct trace *t=a; (void)data; t->bulk_n++;
 *actual=t->zero_bulk?0:(t->short_bulk?len-1:len);t->bytes+=*actual;
 return t->bulk_error?EIO:0;
}
static int config_wire(void) {
 uint8_t out[10];struct umoxa1150_settings s=settings;
 const uint8_t want[]={0,96,0x60,0,3,0,0,0x11,0x13,0};
 CHECK(umoxa1150_config(&s,out)==0 && !memcmp(out,want,10));
 s.baud=115200;s.bits=7;s.parity=2;s.stops=2;s.rtscts=1;s.ixon=1;s.ixoff=1;s.mode=1;s.send_break=1;
 {const uint8_t w[]={0,8,0xe1,0x69,2,2,2,0x11,0x13,1};
 CHECK(umoxa1150_config(&s,out)==0 && !memcmp(out,w,10));}
 s.mode=2;CHECK(umoxa1150_config(&s,out)==0 && out[9]==2);
 return 1;
}
static int config_reject(void) {
 struct umoxa1150_settings s=settings;uint8_t out[10],before[10];
 memset(out,0xa5,10);memcpy(before,out,10);
 s.baud=0;CHECK(umoxa1150_config(&s,out)==EINVAL && !memcmp(out,before,10));
 s=settings;s.baud=49;CHECK(umoxa1150_config(&s,out)==EINVAL);
 s.baud=921601;CHECK(umoxa1150_config(&s,out)==EINVAL);
 s=settings;s.bits=4;CHECK(umoxa1150_config(&s,out)==EINVAL);
 s=settings;s.parity=3;CHECK(umoxa1150_config(&s,out)==EINVAL);
 s=settings;s.stops=0;CHECK(umoxa1150_config(&s,out)==EINVAL);
 s=settings;s.mode=3;CHECK(umoxa1150_config(&s,out)==EINVAL);
 s=settings;s.ixon=2;CHECK(umoxa1150_config(&s,out)==EINVAL);
 CHECK(umoxa1150_config(NULL,out)==EINVAL && umoxa1150_config(&settings,NULL)==EINVAL);
 return 1;
}
static int firmware_wire(void) {
 const uint8_t src[]={0,0,0,1,2,3};uint8_t *out=malloc(UMOXA1150_IMAGE_SIZE);size_t i;
 CHECK(out!=NULL);CHECK(umoxa1150_firmware(src,sizeof(src),out,UMOXA1150_IMAGE_SIZE)==0);
 CHECK(out[0]==0x9c && out[1]==0x3f && out[2]==0x6d);
 CHECK(!memcmp(out+3,src+3,3));for(i=6;i<UMOXA1150_IMAGE_SIZE;i++)CHECK(out[i]==0xff);
 free(out);return 1;
}
static int firmware_bounds(void) {
 uint8_t *out=malloc(UMOXA1150_IMAGE_SIZE),src[3]={0};CHECK(out!=NULL);memset(out,0xa5,UMOXA1150_IMAGE_SIZE);
 CHECK(umoxa1150_firmware(src,2,out,UMOXA1150_IMAGE_SIZE)==EINVAL);
 CHECK(umoxa1150_firmware(src,UMOXA1150_IMAGE_SIZE+1,out,UMOXA1150_IMAGE_SIZE)==EINVAL);
 CHECK(umoxa1150_firmware(src,3,out,UMOXA1150_IMAGE_SIZE-1)==EMSGSIZE);
 CHECK(out[0]==0xa5 && out[UMOXA1150_IMAGE_SIZE-1]==0xa5);
 free(out);return 1;
}
static int endpoint_warm(void) {
 const struct umoxa1150_endpoint ep[]={{0x81,2,64},{2,2,64},{0x83,3,2}};
 struct umoxa1150_layout l={0};CHECK(umoxa1150_endpoints(ep,3,0,&l)==0);
 CHECK(l.in==0x81 && l.out==2 && l.interrupt==0x83 && l.interrupt_size==2);return 1;
}
static int endpoint_reject(void) {
 struct umoxa1150_endpoint ep[]={{0x81,2,64},{2,2,64},{0x83,3,1}};struct umoxa1150_layout l={7,7,7,7};
 CHECK(umoxa1150_endpoints(ep,3,0,&l)==EPROTO && l.in==7);
 ep[2].packet=2;ep[2].address=0x81;CHECK(umoxa1150_endpoints(ep,3,0,&l)==EPROTO);
 CHECK(umoxa1150_endpoints(ep,2,0,&l)==EPROTO);
 ep[0].address=2;CHECK(umoxa1150_endpoints(ep,1,1,&l)==0 && l.out==2 && l.in==0);
 ep[0].address=0;CHECK(umoxa1150_endpoints(ep,1,1,&l)==EPROTO);return 1;
}
static int event_decode(void) {
 struct umoxa1150_event e;const uint8_t modem[]={0x34,0xb0},line[]={0x33,0x0f};
 CHECK(umoxa1150_event(modem,2,&e)==0 && e.kind==4 && e.msr==0xb0);
 CHECK(umoxa1150_event(line,2,&e)==0 && e.kind==3 && e.lsr==0x1e);return 1;
}
static int event_reject(void) {
 struct umoxa1150_event e={9,9,9};uint8_t d[]={0x24,0};
 CHECK(umoxa1150_event(d,2,&e)==EPROTO && e.kind==9);
 d[0]=0x44;CHECK(umoxa1150_event(d,2,&e)==EPROTO);
 d[0]=0x35;CHECK(umoxa1150_event(d,2,&e)==EPROTO);
 d[0]=0xff;CHECK(umoxa1150_event(d,2,&e)==EIO);
 CHECK(umoxa1150_event(d,1,&e)==EPROTO && e.kind==9);return 1;
}
static int open_wire(void) {
 struct trace t={0};struct umoxa1150_io io={&t,control,bulk};unsigned i;
 const uint8_t req[]={5,0x80,6,8,11,11};const uint8_t mcr[]={0x30,1,1,0,0,0xff,0xa4,0x34,0x30};
 CHECK(umoxa1150_start(&io,&settings,0x30)==0 && t.n==6);
 CHECK(!memcmp(t.req,req,6));CHECK(t.val[2]==0x89 && t.val[4]==0x80 && t.val[5]==0);
 CHECK(t.index[1]==5 && !memcmp(t.data[1],mcr,9));
 for(i=0;i<6;i++)CHECK(t.index[i]==(i==1?5:3));return 1;
}
static int open_failure(void) {
 unsigned n;for(n=1;n<=6;n++) {struct trace t={0};struct umoxa1150_io io={&t,control,bulk};t.fail=n;
 CHECK(umoxa1150_start(&io,&settings,0x30)==EIO);
 CHECK(t.n==n+2 && t.req[n]==9 && t.req[n+1]==7);}
 {struct trace t={0};struct umoxa1150_io io={&t,control,bulk};
 CHECK(umoxa1150_start(&io,&settings,0xff)==EINVAL && t.n==0);
 t.fail=1;CHECK(umoxa1150_stop(&io)==EIO && t.n==2 && t.req[1]==7);}
 return 1;
}
static int upload_complete(void) {
 uint8_t raw[4]={0,0,0,1},*image=malloc(UMOXA1150_IMAGE_SIZE);struct trace t={0};struct umoxa1150_io io={&t,control,bulk};CHECK(image!=NULL);
 CHECK(umoxa1150_firmware(raw,4,image,UMOXA1150_IMAGE_SIZE)==0);
 CHECK(umoxa1150_upload(&io,image,UMOXA1150_IMAGE_SIZE)==0);
 CHECK(t.bulk_n==255 && t.bytes==16287 && t.n==1 && t.req[0]==12 && t.index[0]==0);
 free(image);return 1;
}
static int upload_failure(void) {
 uint8_t raw[4]={0,0,0,1},*image=malloc(UMOXA1150_IMAGE_SIZE);struct trace t={0};struct umoxa1150_io io={&t,control,bulk};CHECK(image!=NULL);
 CHECK(umoxa1150_firmware(raw,4,image,UMOXA1150_IMAGE_SIZE)==0);
 t.zero_bulk=1;CHECK(umoxa1150_upload(&io,image,UMOXA1150_IMAGE_SIZE)==EIO && t.bulk_n==1 && t.n==0);
 memset(&t,0,sizeof(t));t.short_bulk=1;CHECK(umoxa1150_upload(&io,image,UMOXA1150_IMAGE_SIZE)==EIO && t.bulk_n==1 && t.n==0);
 memset(&t,0,sizeof(t));t.bulk_error=1;CHECK(umoxa1150_upload(&io,image,UMOXA1150_IMAGE_SIZE)==EIO && t.bulk_n==1 && t.n==0);
 memset(&t,0,sizeof(t));image[3]^=1;CHECK(umoxa1150_upload(&io,image,UMOXA1150_IMAGE_SIZE)==EPROTO && t.bulk_n==0);
 free(image);return 1;
}
int main(void) {
 struct {const char *name;int(*fn)(void);} tests[]={
 {"config_wire",config_wire},{"config_reject",config_reject},{"firmware_wire",firmware_wire},{"firmware_bounds",firmware_bounds},
 {"endpoint_warm",endpoint_warm},{"endpoint_reject",endpoint_reject},{"event_decode",event_decode},{"event_reject",event_reject},
 {"open_wire",open_wire},{"open_failure",open_failure},{"upload_complete",upload_complete},{"upload_failure",upload_failure}};
 unsigned i,passed=0;for(i=0;i<sizeof(tests)/sizeof(tests[0]);i++){int ok=tests[i].fn();printf("%s %s\n",ok?"PASS":"FAIL",tests[i].name);passed+=ok;}
 printf("%u/12 groups passed\n",passed);return passed==12?0:1;
}
