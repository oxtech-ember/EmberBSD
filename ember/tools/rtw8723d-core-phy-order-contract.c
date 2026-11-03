/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D contract tests, AI-assisted. */
/* Exercise production PHY unwind with a bounded fake chip/HCI. */
#include "rtw8723d-core-phy-order-fixture.h"
#include <assert.h>
#include <string.h>
static int poweroffs;
int rtw8723d_core_power_off(struct rtw8723d_core_boot *ctx)
{ poweroffs++; ctx->owned = false; return 0; }
int rtw8723d_power_compute(const struct rtw8723d_efuse *a, const struct rtw88_profile *b, const struct rtw8723d_power_policy *c, unsigned d, uint8_t *e)
{ (void)a; (void)b; (void)c; (void)d; (void)e; return 0; }
int rtw8723d_mac_configure(struct rtw88_io *io, int (*check)(void *), void *c)
{ (void)io; (void)check; (void)c; return EIO; }
int rtw8723d_swcrypto_configure(struct rtw88_io *i,int(*c)(void *),void *v){(void)i;(void)c;(void)v;return 0;}
int rtw8723d_phy_configure(struct rtw88_io *i,const struct rtw8723d_phy_condition *p,uint8_t a,uint8_t b,int(*c)(void *),void *v){(void)i;(void)p;(void)a;(void)b;(void)c;(void)v;return 0;}
int rtw8723d_dm_before_lck(struct rtw88_io *i,int(*c)(void *),void *v){(void)i;(void)c;(void)v;return 0;}
int rtw8723d_dm_after_lck(struct rtw88_io *i,int(*c)(void *),void *v){(void)i;(void)c;(void)v;return 0;}
int rtw8723d_lck(struct rtw88_io *i,const struct rtw8723d_boot_ops *o){(void)i;(void)o;return 0;}
int rtw8723d_iqk_calibrate(struct rtw88_io *i,const struct rtw8723d_boot_ops *o,struct rtw8723d_iqk_matrix *m){(void)i;(void)o;(void)m;return 0;}
int rtw8723d_channel20(struct rtw88_io *i,const struct rtw8723d_boot_ops *o,unsigned c){(void)i;(void)o;(void)c;return 0;}
int rtw8723d_power_program(struct rtw88_io *i,const struct rtw8723d_efuse *e,const struct rtw88_profile *p,const struct rtw8723d_power_policy *q,unsigned n,int(*c)(void *),void *v){(void)i;(void)e;(void)p;(void)q;(void)n;(void)c;(void)v;return 0;}
static bool revoked(void *p){(void)p;return false;}
static bool cancelled(void *p){(void)p;return false;}
static void wait_ms(void *p,unsigned n){(void)p;(void)n;}
int main(void)
{
 struct rtw88_softc sc={.sc_generation=1,.sc_powered=1,.sc_resources_ready=1};
 struct rtw8723d_core_boot boot={.core=&sc,.generation=1,.owned=true,.firmware_booted=true,.hooks={.revoked=revoked},.ops={.cancelled=cancelled,.wait_ms=wait_ms}};
 struct rtw8723d_core_phy_config cfg={.condition={.interface=RTW8723D_PHY_SDIO}};
 struct rtw8723d_core_phy_result result={0}, before=result;
 assert(rtw8723d_core_phy_configure(&boot,&cfg,&result)==EIO);
 assert(boot.failed && boot.owned && poweroffs==0);
 assert(memcmp(&result,&before,sizeof(result))==0);
 return 0;
}
