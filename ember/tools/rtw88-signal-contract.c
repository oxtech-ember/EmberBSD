/* SPDX-License-Identifier: BSD-2-Clause */
/* Bounded native PHY metadata. Values are synthetic, not RF measurements. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include "rtw88_hw.h"
static unsigned checks,failures;
#define CHECK(x) do {checks++;if(!(x)){failures++;fprintf(stderr,"line%d: %s\n",__LINE__,#x);}}while(0)
int main(void) {
    struct rtw88_phy_state ctx={.generation=7,.paths=3,.gain_valid=1,.gain_low=20,.gain_high=40};
    struct rtw88_rx_info rx={.flags=RTW88_RX_PHY_STATUS,.rate=4};
    struct rtw88_rx_signal s;
    uint8_t p[32]={1,60,50,0,0,1,8};
    CHECK(rtw88_rx_signal_decode(p,32,&rx,&ctx,7,8,&s)==0);
    CHECK(s.valid && s.quality==50 && s.power[0]==-50 && s.power[1]==-60);
    p[1]=90;CHECK(rtw88_rx_signal_decode(p,32,&rx,&ctx,7,8,&s)==0 && s.quality==80);
    p[0]=0;p[1]=50;p[2]=10;p[16]=60;p[19]=50;rx.rate=0;
    CHECK(rtw88_rx_signal_decode(p,32,&rx,&ctx,7,8,&s)==0);
    CHECK(s.power[0]==-40 && s.power[1]==-70 && s.quality==60);
    for(unsigned n=0;n<20;n++) CHECK(rtw88_rx_signal_decode(p,n,&rx,&ctx,7,8,&s)!=0 && !s.valid);
    ctx.gain_valid=0;CHECK(rtw88_rx_signal_decode(p,32,&rx,&ctx,7,8,&s)!=0 && s.reason==RTW88_SIGNAL_GAIN);
    p[0]=1;rx.rate=4;CHECK(rtw88_rx_signal_decode(p,8,&rx,&ctx,7,8,&s)==0);
    CHECK(rtw88_rx_signal_decode(p,32,&rx,&ctx,8,8,&s)!=0 && s.reason==RTW88_SIGNAL_GENERATION);
    for(unsigned n=0;n<8;n++) CHECK(rtw88_rx_signal_decode(p,n,&rx,&ctx,7,8,&s)!=0 && !s.valid);
    p[0]=9;CHECK(rtw88_rx_signal_decode(p,32,&rx,&ctx,7,8,&s)!=0 && s.reason==RTW88_SIGNAL_PAGE);
    p[0]=1;p[6]=36;CHECK(rtw88_rx_signal_decode(p,32,&rx,&ctx,7,8,&s)!=0 && s.reason==RTW88_SIGNAL_CHANNEL);
    CHECK(rtw88_rx_signal_decode(p,32,&rx,&ctx,7,36,&s)==0);
    p[5]=9;CHECK(rtw88_rx_signal_decode(p,32,&rx,&ctx,7,36,&s)!=0 && s.reason==RTW88_SIGNAL_WIDTH);
    p[5]=1;p[1]=255;CHECK(rtw88_rx_signal_decode(p,32,&rx,&ctx,7,36,&s)!=0 && s.reason==RTW88_SIGNAL_NUMBER);
    p[1]=90;rx.flags=0;CHECK(rtw88_rx_signal_decode(p,32,&rx,&ctx,7,36,&s)!=0 && s.reason==RTW88_SIGNAL_MISSING && s.quality==0);
    rx.flags=RTW88_RX_PHY_STATUS;ctx.gain_valid=1;ctx.gain_error=0;
    p[0]=0;p[1]=50;p[2]=10;p[16]=90;p[19]=30;p[6]=8;rx.rate=0;
    CHECK(rtw88_rx_signal_decode(p,24,&rx,&ctx,7,8,&s)==0 && s.quality==60 && s.power[1]==-20);
    p[1]=20;p[2]=63;CHECK(rtw88_rx_signal_decode(p,24,&rx,&ctx,7,8,&s)!=0 && s.reason==RTW88_SIGNAL_NUMBER);
    p[0]=1;p[5]=1;rx.rate=4;p[1]=60;p[2]=90;
    CHECK(rtw88_rx_signal_decode(p,8,&rx,&ctx,7,8,&s)==0 && s.quality==80);
    for(unsigned raw=0;raw<256;raw++) {
        p[1]=(uint8_t)raw;p[2]=50;
        int error=rtw88_rx_signal_decode(p,8,&rx,&ctx,7,8,&s);
        CHECK((error==0)==(raw<130));
        CHECK(s.valid==(raw<130) && s.quality<=100);
        if(raw>=130)CHECK(s.reason==RTW88_SIGNAL_NUMBER && s.quality==0);
    }
    p[1]=60;
    for(unsigned code=0;code<16;code++) {
        p[5]=(uint8_t)code;
        CHECK((rtw88_rx_signal_decode(p,8,&rx,&ctx,7,8,&s)==0)==(code<=8));
    }
    p[5]=1;rx.bandwidth=1;
    CHECK(rtw88_rx_signal_decode(p,32,&rx,&ctx,7,8,&s)!=0 && s.reason==RTW88_SIGNAL_WIDTH);
    rx.bandwidth=0;rx.rate=12;
    CHECK(rtw88_rx_signal_decode(p,32,&rx,&ctx,7,8,&s)!=0 && s.reason==RTW88_SIGNAL_RATE);
    rx.rate=4;ctx.paths=1;
    CHECK(rtw88_rx_signal_decode(p,32,&rx,&ctx,7,8,&s)!=0 && s.reason==RTW88_SIGNAL_PATH);
    ctx.paths=3;uint8_t unaligned[35];
    for(unsigned shift=0;shift<4;shift++) {
        memcpy(unaligned+shift,p,32);
        CHECK(rtw88_rx_signal_decode(unaligned+shift,32,&rx,&ctx,7,8,&s)==0 && s.quality==50);
    }
    CHECK(rtw88_rx_signal_decode(p,32,&rx,&ctx,7,8,NULL)==EINVAL);
    for(unsigned bad=0;bad<3;bad++) {
        rx.flags=RTW88_RX_PHY_STATUS | (bad==0?RTW88_RX_CRC_ERROR:bad==1?RTW88_RX_ICV_ERROR:RTW88_RX_C2H);
        CHECK(rtw88_rx_signal_decode(p,32,&rx,&ctx,7,8,&s)!=0 && !s.valid && s.quality==0);
    }
    printf("rtw88 signal: %u checks, %u failures\n",checks,failures);return failures!=0;
}
