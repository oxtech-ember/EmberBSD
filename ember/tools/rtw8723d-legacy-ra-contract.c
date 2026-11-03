/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D contract tests, AI-assisted. */
/* 8723D legacy admission and firmware rate-mask boundary. */
#include <assert.h>
#include <string.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8723d_runtime.h>
#include <dev/ic/rtw8723d_ra.h>
static unsigned sent;
static uint8_t command[7];
int rtw8723d_runtime_check(void *ctx){(void)ctx;return 0;}
int rtw8723d_runtime_command(struct rtw88_softc *s,uint8_t id,const void *p,size_t n)
{(void)s; assert(id==0x40 && n==sizeof(command)); memcpy(command,p,n); sent++; return 0;}
void rtw88_ra_invalidate(struct rtw88_softc *s){s->sc_ra.active=s->sc_ra.configured=0;}
int main(void)
{
 struct rtw88_softc sc={0};
 struct rtw8723d_runtime ctx={0};
 struct rtw88_ra_config cfg={.epoch=1,.channel=1,.common_rates=0xfff,.mode=RTW88_FIXED_11G,.phy=RTW88_RA_LEGACY};
 sc.sc_chip_private=&ctx; ctx.boot.core=&sc; sc.sc_generation=1;
 sc.sc_running=sc.sc_started=sc.sc_rf_enabled=sc.sc_linked=1;
 sc.sc_phy.channel_valid=1; sc.sc_phy.generation=1; sc.sc_channel=1;
 sc.sc_phy.channel.primary=sc.sc_phy.channel.center=1; sc.sc_phy.channel.width_mhz=20;
 ctx.mailbox.active=true; ctx.mailbox.generation=1; ctx.mailbox.total=1;
 assert(rtw8723d_runtime_ra(&sc,&cfg)==0);
 assert(sent==1 && sc.sc_ra.active && sc.sc_ra.rate_id==6 && sc.sc_ra.mask==0xff5);
 assert((command[1] & 0x1f)==6 && command[3]==0xf5 && command[4]==0x0f);
 cfg.phy=RTW88_RA_HT20_1SS; cfg.ht_mcs=1;
 assert(rtw8723d_runtime_ra(&sc,&cfg)==EOPNOTSUPP && sent==1);
 return 0;
}
