/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted RTL8723D EDCA failure contracts. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <dev/ic/rtw88_hw.h>
#include <dev/ic/rtw88_io.h>
#include <dev/ic/rtw8723d_edca.h>

struct bus { unsigned reads, writes, fail_at; uint32_t address[8], value[8]; };
static int r1(void *p, uint32_t a, uint8_t *v) { struct bus *b=p; assert(a==0x522); b->reads++; *v=0x35; return 0; }
static int r2(void *p, uint32_t a, uint16_t *v) { (void)p; (void)a; (void)v; return EIO; }
static int r4(void *p, uint32_t a, uint32_t *v) { (void)p; (void)a; (void)v; return EIO; }
static int wr(struct bus *b, uint32_t a, uint32_t v) {
 unsigned i=b->writes++; assert(i<8); b->address[i]=a; b->value[i]=v;
 return b->fail_at==b->writes ? EIO : 0;
}
static int w1(void *p, uint32_t a, uint8_t v) { return wr(p,a,v); }
static int w2(void *p, uint32_t a, uint16_t v) { return wr(p,a,v); }
static int w4(void *p, uint32_t a, uint32_t v) { return wr(p,a,v); }
static void delay(void *p, unsigned us) { (void)p; (void)us; }
static int cancel_after_one_write(void *p) { unsigned *n=p; return ++*n==4 ? ECANCELED : 0; }
static const struct rtw88_bus_ops ops = {
 .read_1=r1, .read_2=r2, .read_4=r4, .write_1=w1, .write_2=w2, .write_4=w4, .delay_us=delay
};
int main(void) {
 struct rtw88_edca_params p[4]={{2,3,4,5},{2,3,4,5},{2,3,4,5},{2,3,4,5}};
 struct bus b={0}; struct rtw88_io io;
 const uint32_t expected[6]={0x522,0x508,0x50c,0x504,0x500,0x522};
 rtw88_io_init(&io,&ops,&b);
 assert(rtw8723d_edca_apply(&io,p,9,NULL,NULL)==0 && b.reads==1 && b.writes==6);
 for (unsigned i=0;i<6;i++) assert(b.address[i]==expected[i]);
 assert(b.value[0]==0xff && b.value[5]==0x35);
 for (unsigned i=1;i<5;i++) assert(b.value[i]==0x0005431c);
 b=(struct bus){.fail_at=3}; rtw88_io_init(&io,&ops,&b);
 assert(rtw8723d_edca_apply(&io,p,9,NULL,NULL)==EIO && io.error==EIO);
 assert(b.writes==4 && b.address[3]==0x522 && b.value[3]==0xff);
 b=(struct bus){0}; rtw88_io_init(&io,&ops,&b); p[3].ecw_min=9;
 assert(rtw8723d_edca_apply(&io,p,9,NULL,NULL)==EINVAL && b.reads==0 && b.writes==0);
 p[3].ecw_min=3; b=(struct bus){0}; rtw88_io_init(&io,&ops,&b);
 unsigned checks=0;
 assert(rtw8723d_edca_apply(&io,p,9,cancel_after_one_write,&checks)==ECANCELED);
 assert(io.error==ECANCELED && b.writes==3 && b.address[2]==0x522 && b.value[2]==0xff);
 puts("PASS: 8723D EDCA prevalidation, write order and error pause");
 return 0;
}
