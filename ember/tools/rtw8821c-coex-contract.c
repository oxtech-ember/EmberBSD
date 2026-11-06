/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: FreeBSD a143197 RTL8821C Wi-Fi-only coexistence model. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8821c_coex.h>
static uint8_t reg[65536];
static struct rtw88_softc *active;
static unsigned calls, fail_at, cancel_at, failed, writes_after_failure, lte_count;
static uint32_t lte_masks[4], lte_values[4];
static unsigned checks, failures, command_count;
#define CHECK(x) do { checks++; if (!(x)) { printf("line %d: %s\n", __LINE__, #x); failures++; } } while (0)
static int operation(void) { calls++; if (calls == fail_at) { failed = 1; return EIO; } return 0; }
static void finish(void) { if (calls == cancel_at) active->sc_stop_requested=1; }
static int read_reg(uint32_t a,unsigned n,uint32_t *v) {
	unsigned i; if (operation()) return EIO; if (a+n > sizeof(reg)) return EINVAL;
	*v=0; for (i=0;i<n;i++) *v |= (uint32_t)reg[a+i] << (8*i); finish(); return 0;
}
static int write_reg(uint32_t a,unsigned n,uint32_t v) {
	unsigned i; if (failed) writes_after_failure++;
	if (operation()) return EIO;
	if (a+n > sizeof(reg)) return EINVAL;
	for(i=0;i<n;i++) reg[a+i]=(uint8_t)(v>>(8*i));
	finish(); return 0;
}
static int r1(void *p,uint32_t a,uint8_t *v) { uint32_t t; int e; (void)p; e=read_reg(a,1,&t); if (!e) *v=t; return e; }
static int r2(void *p,uint32_t a,uint16_t *v) { uint32_t t; int e; (void)p; e=read_reg(a,2,&t); if (!e) *v=t; return e; }
static int r4(void *p,uint32_t a,uint32_t *v) { (void)p; return read_reg(a,4,v); }
static int w1(void *p,uint32_t a,uint8_t v) { (void)p; return write_reg(a,1,v); }
static int w2(void *p,uint32_t a,uint16_t v) { (void)p; return write_reg(a,2,v); }
static int w4(void *p,uint32_t a,uint32_t v) { (void)p; return write_reg(a,4,v); }
static void delay(void *p,unsigned us) { (void)p; (void)us; }
static const struct rtw88_bus_ops ops={ .read_1=r1,.read_2=r2,.read_4=r4,.write_1=w1,.write_2=w2,.write_4=w4,.delay_us=delay };
/* Shared transport declaration requires a mutable softc. */
// cppcheck-suppress constParameterPointer
int rtw88_wcpu_lte_update(struct rtw88_softc *sc,uint32_t generation,uint16_t addr,uint32_t mask,uint32_t value) {
	if (failed) writes_after_failure++;
	if (operation()) return EIO;
	if (sc!=active || generation!=sc->sc_generation || addr!=0x38 || lte_count>=4) return EINVAL;
	lte_masks[lte_count]=mask; lte_values[lte_count++]=value; finish(); return 0;
}
int rtw88_wcpu_boot_command(struct rtw88_softc *sc, uint8_t id, const void *p, size_t n) {
	(void)sc; (void)id; (void)p; (void)n;
	command_count++;
	if (failed) writes_after_failure++;
	if (operation()) return EIO;
	finish(); return 0;
}
static void reset(struct rtw88_softc *sc,unsigned rfe) {
	memset(sc,0,sizeof(*sc)); memset(reg,0,sizeof(reg)); sc->sc_ops=&ops; sc->sc_generation=7; sc->sc_rfe=rfe; active=sc;
	calls=fail_at=cancel_at=failed=writes_after_failure=lte_count=command_count=0;
}
int main(void) {
	static const unsigned expected[16]={2,0,2,0,2,0,0,0,0,0,0,0,0,0,0,0};
	static const uint32_t masks[4]={0xc000,0x0c00,0x3000,0x0300};
	static const uint32_t values[4]={0x4000,0x0400,0x3000,0x0300};
	struct rtw88_softc sc; unsigned i,count;
	for (i=0;i<16;i++) {
		reset(&sc,i);
		if (i != 0 && i != 2 && i != 4 && i != 6) {
			CHECK(rtw8821c_coex_init(&sc, 0)==ENOTSUP && calls==0);
			continue;
		}
		CHECK(rtw8821c_coex_init(&sc, 0)==0);
		CHECK(lte_count==4 && memcmp(lte_masks,masks,sizeof(masks))==0 && memcmp(lte_values,values,sizeof(values))==0);
		CHECK((reg[0x73]&4)!=0);
		if (expected[i]) { CHECK((reg[0xcb7]>>4)==expected[i]); CHECK(reg[0xcb4]==0x77); }
		else CHECK(reg[0xcb4]==0 && reg[0xcb7]==0);
	}
	reset(&sc,16); CHECK(rtw8821c_coex_init(&sc, 0)==ENOTSUP && calls==0);
	reset(&sc,0); CHECK(rtw8821c_coex_init(&sc, 1)==0); CHECK(command_count==2 && reg[0xaa]==3 && reg[0xab]==0x80); CHECK(reg[0x6c0]==0x55 && reg[0x6c4]==0x55 && reg[0x6cb]==0xf0); count=calls;
	for(i=1;i<=count;i++) { reset(&sc,0); fail_at=i; CHECK(rtw8821c_coex_init(&sc, 1)==EIO); CHECK(writes_after_failure==0); }
	for(i=1;i<=count;i++) { reset(&sc,0); cancel_at=i; CHECK(rtw8821c_coex_init(&sc, 1)==EINTR); }
	reset(&sc,0); reg[0xaa]=2; CHECK(rtw8821c_coex_init(&sc, 0)==0);
	reset(&sc,2); reg[0x49c]=1; CHECK(rtw8821c_coex_init(&sc, 0)==0);
	printf("RTL8821C WONLY coexistence: %u checks, %u failures\n",checks,failures); return failures!=0;
}
