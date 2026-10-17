/* Origin: EmberBSD independent G2 control/firmware contracts, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "umoxa_g2_protocol.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#x); return 0; } } while (0)
struct request { int input; uint8_t req; uint16_t value,index; size_t len; uint8_t data[64]; };
struct trace { struct request r[32]; unsigned n,fail,delays; uint8_t version[4],reg[4]; };
static int control(void *arg,int input,uint8_t req,uint16_t value,uint16_t index,uint8_t *data,size_t len) {
 struct trace *t=arg;struct request *r;
 if(t->n>=32 || len>64)return EOVERFLOW;
 r=&t->r[t->n++];*r=(struct request){input,req,value,index,len,{0}};
 if(t->fail==t->n)return EIO;
 if(input && len==4)memcpy(data,req==0x81?t->version:t->reg,4);
 if(len)memcpy(r->data,data,len);
 return 0;
}
static void delay(void *arg,unsigned ms){struct trace *t=arg;t->delays+=ms;}
static struct umoxa_g2_settings standard={9600,8,0,1,0,0,3};
static struct umoxa_g2_io io(struct trace *t){return (struct umoxa_g2_io){t,control,delay};}
static int configuration_literal(void) {
 uint8_t line[4],baud[4];struct umoxa_g2_settings s=standard;
 CHECK(umoxa_g2_config(&s,line,baud)==0);
 CHECK(!memcmp(line,(uint8_t[]){8,0,0,0},4) && !memcmp(baud,(uint8_t[]){0x80,0x25,0,0},4));
 s.baud=115200;s.bits=7;s.parity=2;s.stops=2;s.rtscts=1;
 CHECK(umoxa_g2_config(&s,line,baud)==0);
 CHECK(!memcmp(line,(uint8_t[]){7,2,2,0},4) && !memcmp(baud,(uint8_t[]){0,0xc2,1,0},4));return 1;
}
static int invalid_configuration_atomic(void) {
 struct umoxa_g2_settings s;uint8_t line[4],baud[4];unsigned kind;
 for(kind=0;kind<9;kind++) {
  s=standard;memset(line,0xa5,4);memset(baud,0x5a,4);
  switch(kind){case 0:s.baud=0;break;case 1:s.baud=49;break;case 2:s.baud=921601;break;case 3:s.bits=4;break;case 4:s.parity=3;break;case 5:s.stops=3;break;case 6:s.mode=4;break;case 7:s.rtscts=2;break;default:s.mcr=4;break;}
  CHECK(umoxa_g2_config(&s,line,baud)==EINVAL);
  CHECK(!memcmp(line,(uint8_t[]){0xa5,0xa5,0xa5,0xa5},4) && !memcmp(baud,(uint8_t[]){0x5a,0x5a,0x5a,0x5a},4));
 }
 s=standard;s.mode=1;s.rtscts=1;CHECK(umoxa_g2_config(&s,line,baud)==EINVAL);return 1;
}
static int descriptor_roles(void) {
 struct umoxa_g2_endpoint ep[]={{0x85,2,1024},{7,2,1024},{0x82,2,1024}};struct umoxa_g2_layout l;
 CHECK(umoxa_g2_endpoints(ep,3,&l)==0 && l.data_in==0x85 && l.event_in==0x82 && l.out==7 && l.packet==1024);
 ep[0].packet=ep[1].packet=512;ep[2].packet=64;
 CHECK(umoxa_g2_endpoints(ep,3,&l)==0 && l.packet==512);return 1;
}
static int descriptor_reject_atomic(void) {
 struct umoxa_g2_endpoint ep[3];struct umoxa_g2_layout l,before;unsigned kind;
 for(kind=0;kind<6;kind++){
  memcpy(ep,(struct umoxa_g2_endpoint[]){{0x81,2,1024},{1,2,1024},{0x83,2,1024}},sizeof(ep));memset(&l,0xa5,sizeof(l));before=l;
  switch(kind){case 0:ep[2].address=0x81;break;case 1:ep[1].address=0;break;case 2:ep[0].type=3;break;case 3:ep[1].packet=513;break;case 4:ep[2].address=2;break;default:ep[0].packet=512;break;}
  CHECK(umoxa_g2_endpoints(ep,3,&l)==EPROTO && !memcmp(&l,&before,sizeof(l)));
 }
 CHECK(umoxa_g2_endpoints(ep,2,&l)==EPROTO);return 1;
}
static int probe_state(void) {
 unsigned remap;for(remap=0;remap<3;remap++) {
  struct trace t={0};struct umoxa_g2_io b=io(&t);uint32_t version=99;int map=99;
  memcpy(t.version,(uint8_t[]){1,2,3,0x55},4);t.reg[0]=(uint8_t)(0x80|remap);
  CHECK(umoxa_g2_probe(&b,&version,&map)==0 && version==0x010203 && map==(int)remap && t.n==2);
  CHECK(t.r[0].input==1 && t.r[0].req==0x81 && t.r[0].value==0 && t.r[0].index==0 && t.r[0].len==4);
  CHECK(t.r[1].input==1 && t.r[1].req==0x34 && t.r[1].value==0x20 && t.r[1].index==0 && t.r[1].len==4);
 }return 1;
}
static int probe_fail_atomic(void) {
 unsigned fail;for(fail=1;fail<=2;fail++) {
  struct trace t={0};struct umoxa_g2_io b=io(&t);uint32_t version=99;int map=99;t.fail=fail;
  CHECK(umoxa_g2_probe(&b,&version,&map)==EIO && version==99 && map==99 && t.n==fail);
 }
 {struct trace t={0};struct umoxa_g2_io b=io(&t);uint32_t v=99;int m=99;t.reg[0]=3;
 CHECK(umoxa_g2_probe(&b,&v,&m)==EPROTO && v==99 && m==99);}return 1;
}
static int prepare_wire(void) {
 struct trace t={0};struct umoxa_g2_io b=io(&t);struct umoxa_g2_settings s=standard;
 const uint8_t commands[]={0x0f,0x11,0x10,2,6,4,5,1};unsigned i;s.rtscts=1;
 CHECK(umoxa_g2_prepare(&b,3,&s)==0 && t.n==8);
 for(i=0;i<8;i++)CHECK(t.r[i].input==0 && t.r[i].req==commands[i] && t.r[i].index==3);
 CHECK(t.r[2].value==0 && !memcmp(t.r[3].data,(uint8_t[]){8,0,0,0},4) && t.r[3].len==4);
 CHECK(t.r[4].value==0 && t.r[5].value==2 && t.r[6].value==1);
 CHECK(!memcmp(t.r[7].data,(uint8_t[]){0x80,0x25,0,0},4) && t.r[7].len==4);
 memset(&t,0,sizeof(t));s.mode=1;s.rtscts=0;
 CHECK(umoxa_g2_prepare(&b,0,&s)==0 && t.r[2].value==1 && t.r[5].value==5 && t.r[6].value==5);return 1;
}
static int control_failures_and_invalid(void) {
 unsigned fail;for(fail=1;fail<=8;fail++){
  struct trace t={0};struct umoxa_g2_io b=io(&t);t.fail=fail;
  CHECK(umoxa_g2_prepare(&b,1,&standard)==EIO && t.n==fail);
 }
 {struct trace t={0};struct umoxa_g2_io b=io(&t);struct umoxa_g2_settings s=standard;s.baud=0;
 CHECK(umoxa_g2_prepare(&b,1,&s)==EINVAL && t.n==0);
 CHECK(umoxa_g2_prepare(&b,4,&standard)==EINVAL && t.n==0);}return 1;
}
static int port_barriers(void) {
 struct trace t={0};struct umoxa_g2_io b=io(&t);
 CHECK(umoxa_g2_quiesce(&b,2)==0 && t.n==3);
 CHECK(t.r[0].req==7 && t.r[0].value==0 && t.r[1].req==8 && t.r[1].value==0 && t.r[2].req==9 && t.r[2].value==0x0c);
 CHECK(t.r[0].index==2 && t.r[1].index==2 && t.r[2].index==2);
 memset(&t,0,sizeof(t));t.fail=1;CHECK(umoxa_g2_quiesce(&b,2)==EIO && t.n==3);
 memset(&t,0,sizeof(t));CHECK(umoxa_g2_enable(&b,1)==0 && t.n==2 && t.r[0].req==8 && t.r[1].req==7 && t.r[0].value==1 && t.r[1].value==1);
 memset(&t,0,sizeof(t));t.fail=1;CHECK(umoxa_g2_enable(&b,1)==EIO && t.n==1);return 1;
}
static int modem_g2_clear(void) {
 struct trace t={0};struct umoxa_g2_io b=io(&t);struct umoxa_g2_settings s=standard;
 CHECK(umoxa_g2_set(&b,1,1,0,&s)==0 && t.r[0].req==5 && t.r[0].value==5 && t.r[0].index==1);
 CHECK(umoxa_g2_set(&b,1,2,0,&s)==0 && t.r[1].req==4 && t.r[1].value==5);
 CHECK(umoxa_g2_set(&b,1,3,1,&s)==0 && t.r[2].req==11 && t.r[2].value==1);
 s.rtscts=1;CHECK(umoxa_g2_set(&b,1,2,1,&s)==0 && t.r[3].value==2);
 CHECK(umoxa_g2_set(&b,1,2,0,&s)==EBUSY && t.n==4);
 s.rtscts=0;s.mode=1;CHECK(umoxa_g2_set(&b,1,1,1,&s)==EOPNOTSUPP && t.n==4);
 CHECK(umoxa_g2_set(&b,1,99,1,&s)==EINVAL && t.n==4);return 1;
}
static void image(uint8_t *p,size_t n){memset(p,0xa5,n);p[0x86]=1;p[0x88]=2;p[0x8a]=3;}
static int firmware_metadata_bounds(void) {
 uint8_t fw[256];uint32_t v=99;image(fw,sizeof(fw));
 CHECK(umoxa_g2_firmware_version(fw,sizeof(fw),&v)==0 && v==0x010203);
 v=99;CHECK(umoxa_g2_firmware_version(fw,0x8a,&v)==EINVAL && v==99);
 CHECK(umoxa_g2_firmware_version(fw,262145,&v)==EINVAL && v==99);
 fw[0x86]=fw[0x88]=fw[0x8a]=0;CHECK(umoxa_g2_firmware_version(fw,sizeof(fw),&v)==EPROTO && v==99);return 1;
}
static int firmware_complete_and_stop_error(void) {
 unsigned stop;for(stop=0;stop<2;stop++) {
  struct trace t={0};struct umoxa_g2_io b=io(&t);uint8_t fw[256],before[256];int e=99;unsigned i;image(fw,sizeof(fw));memcpy(before,fw,sizeof(fw));t.fail=stop?6:0;
  CHECK(umoxa_g2_download(&b,fw,sizeof(fw),&e)==0 && e==(stop?EIO:0) && t.n==6 && t.delays==4);
  CHECK(t.r[0].req==0x0c && t.r[0].len==0 && t.r[5].req==0x0d && t.r[5].len==0);
  for(i=0;i<4;i++)CHECK(t.r[i+1].req==0x20 && t.r[i+1].len==64 && t.r[i+1].value==0 && t.r[i+1].index==0 && !memcmp(t.r[i+1].data,fw+i*64,64));
  CHECK(!memcmp(fw,before,sizeof(fw)));
 }return 1;
}
static int firmware_failure_no_commit(void) {
 unsigned fail;for(fail=1;fail<=5;fail++) {
  struct trace t={0};struct umoxa_g2_io b=io(&t);uint8_t fw[256];int e=99;image(fw,sizeof(fw));t.fail=fail;
  CHECK(umoxa_g2_download(&b,fw,sizeof(fw),&e)==EIO && t.n==fail && e==0);
  CHECK(t.r[t.n-1].req!=0x0d);
 }
 {struct trace t={0};struct umoxa_g2_io b=io(&t);uint8_t fw[256];int e=99;image(fw,sizeof(fw));
 CHECK(umoxa_g2_download(&b,fw,5,&e)==EINVAL && t.n==0 && e==0);}return 1;
}
int main(void) {
 struct {const char *name;int(*fn)(void);} cases[]={{"configuration_literal",configuration_literal},{"invalid_configuration_atomic",invalid_configuration_atomic},{"descriptor_roles",descriptor_roles},{"descriptor_reject_atomic",descriptor_reject_atomic},{"probe_state",probe_state},{"probe_fail_atomic",probe_fail_atomic},{"prepare_wire",prepare_wire},{"control_failures_and_invalid",control_failures_and_invalid},{"port_barriers",port_barriers},{"modem_g2_clear",modem_g2_clear},{"firmware_metadata_bounds",firmware_metadata_bounds},{"firmware_complete_and_stop_error",firmware_complete_and_stop_error},{"firmware_failure_no_commit",firmware_failure_no_commit}};
 unsigned n=sizeof(cases)/sizeof(cases[0]),i,ok=0;for(i=0;i<n;i++){int pass=cases[i].fn();printf("%s %s\n",pass?"PASS":"FAIL",cases[i].name);ok+=pass;}
 printf("%u/%u G2 protocol groups passed\n",ok,n);return ok==n?0:1;
}
