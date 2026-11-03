/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D contract tests, AI-assisted. */
/* A corrupt data frame must not discard a good neighbour in an SDIO batch. */
#include <assert.h>
#include <string.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8723d_rx.h>
#include <dev/sdmmc/rtw88_sdio_rx.h>
int rtw8723d_rx_desc(struct rtw88_softc *,const uint8_t *,size_t,struct rtw88_rx_info *);
int main(void)
{
 struct rtw88_softc sc={0};
 struct rtw88_chip_ops chip={0};
 struct rtw88_sdio_rx_batch batch={0};
 uint8_t frames[74]={0};
 chip.rx_desc=rtw8723d_rx_desc; chip.caps.rx_desc_size=24;
 sc.sc_chip=&chip; sc.sc_generation=1; sc.sc_phy.generation=1;
 sc.sc_phy.channel_valid=1; sc.sc_phy.paths=1;
 sc.sc_phy.channel.primary=sc.sc_phy.channel.center=1;
 sc.sc_phy.channel.width_mhz=20;
 frames[0]=10; frames[1]=0x40; /* CRC-bad DATA, bounded. */
 frames[40]=10; /* Followed by a valid DATA record. */
 assert(rtw88_sdio_rx_validate(&sc,frames,sizeof(frames),&batch)==0);
 assert(batch.count==2 && (batch.records[0].info.flags & RTW88_RX_CRC_ERROR));
 assert(!(batch.records[1].info.flags & RTW88_RX_CRC_ERROR));
 frames[11]=0x10; /* C2H plus CRC error must be rejected. */
 assert(rtw88_sdio_rx_validate(&sc,frames,sizeof(frames),&batch)==EBADMSG);
 memset(frames,0,sizeof(frames));
 frames[0]=10; frames[3]=4; /* PHY-present with no signal bytes. */
 assert(rtw88_sdio_rx_validate(&sc,frames,34,&batch)==0);
 assert(batch.count==1 && !batch.records[0].info.signal.valid);
 return 0;
}
