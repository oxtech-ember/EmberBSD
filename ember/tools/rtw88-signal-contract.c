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
    /* Native VHT power observation uses HT_RXSC, independent of L_RXSC. */
    rx.flags=RTW88_RX_PHY_STATUS;rx.rate=0x2c;rx.bandwidth=0;
    p[0]=1;p[1]=60;p[2]=50;p[5]=0x19;p[6]=36;
    CHECK(rtw88_rx_signal_decode(p,8,&rx,&ctx,7,36,&s)==0);
    CHECK(s.valid && s.quality==50 && s.power[0]==-50 && s.power[1]==-60);
    /* All known 1SS/2SS raw codes, explicit20 upper nibble, any low nibble. */
    for(unsigned rate=0x2c;rate<=0x3f;rate++)
        for(unsigned upper=1;upper<=8;upper++)
            for(unsigned low=0;low<16;low++) {
                rx.rate=(uint8_t)rate;p[5]=(uint8_t)((upper<<4)|low);
                CHECK(rtw88_rx_signal_decode(p,8,&rx,&ctx,7,36,&s)==0);
                CHECK(s.valid && s.quality==50 && s.rxsc==upper &&
                    s.rate==rate && s.power[0]==-50 && s.power[1]==-60);
            }
    rx.rate=0x2c;
    for(unsigned upper=0;upper<16;upper++) {
        if(upper>=1 && upper<=8)continue;
        p[5]=(uint8_t)((upper<<4)|1);
        CHECK(rtw88_rx_signal_decode(p,8,&rx,&ctx,7,36,&s)==ENOENT);
        CHECK(!s.valid && s.quality==0 && s.reason==RTW88_SIGNAL_WIDTH);
    }
    p[5]=0x19;
    for(unsigned bw=1;bw<4;bw++) {
        rx.bandwidth=(uint8_t)bw;
        CHECK(rtw88_rx_signal_decode(p,8,&rx,&ctx,7,36,&s)==ENOENT &&
            !s.valid && s.quality==0 && s.reason==RTW88_SIGNAL_WIDTH);
    }
    rx.bandwidth=0;
    const uint8_t unknown[]={0x2b,0x40,0x53,0x54,0x7f};
    for(unsigned i=0;i<sizeof(unknown);i++) {
        rx.rate=unknown[i];
        CHECK(rtw88_rx_signal_decode(p,8,&rx,&ctx,7,36,&s)==ENOENT &&
            !s.valid && s.quality==0 && s.reason==RTW88_SIGNAL_RATE);
    }
    rx.rate=0x3f;
    uint8_t exact[8]={1,60,50,0,0,0x19,36,0};
    CHECK(rtw88_rx_signal_decode(exact,sizeof(exact),&rx,&ctx,7,36,&s)==0 && s.quality==50);
    uint8_t storage[123];
    const unsigned lengths[]={8,26,32,120};
    for(unsigned shift=0;shift<4;shift++) {
        for(unsigned i=0;i<sizeof(lengths)/sizeof(lengths[0]);i++) {
            memset(storage,0xa5,sizeof(storage));memcpy(storage+shift,exact,8);
            CHECK(rtw88_rx_signal_decode(storage+shift,lengths[i],&rx,&ctx,7,36,&s)==0 && s.quality==50);
        }
    }
    for(unsigned raw=0;raw<256;raw++) {
        for(unsigned path=0;path<2;path++) {
            memcpy(p,exact,8);p[1+path]=(uint8_t)raw;
            int error=rtw88_rx_signal_decode(p,8,&rx,&ctx,7,36,&s);
            CHECK((error==0)==(raw<130));
            CHECK(s.valid==(raw<130) && s.quality<=100);
            if(raw>=130)CHECK(s.reason==RTW88_SIGNAL_NUMBER && s.quality==0);
        }
    }
    memcpy(p,exact,8);
    for(unsigned len=0;len<8;len++)
        CHECK(rtw88_rx_signal_decode(p,len,&rx,&ctx,7,36,&s)==ENOENT &&
            !s.valid && s.quality==0 && s.reason==RTW88_SIGNAL_SHORT);
    for(unsigned bad=0;bad<3;bad++) {
        rx.flags=RTW88_RX_PHY_STATUS | (bad==0?RTW88_RX_CRC_ERROR:
            bad==1?RTW88_RX_ICV_ERROR:RTW88_RX_C2H);
        CHECK(rtw88_rx_signal_decode(p,8,&rx,&ctx,7,36,&s)==ENOENT &&
            !s.valid && s.quality==0 && s.reason==RTW88_SIGNAL_MISSING);
    }
    rx.flags=RTW88_RX_PHY_STATUS;
    memcpy(p,exact,8);p[1]=60;p[2]=90;
    CHECK(rtw88_rx_signal_decode(p,8,&rx,&ctx,7,36,&s)==0 && s.quality==80);
    for(unsigned bad=0;bad<15;bad++) {
        /* Start from a positive observation to detect stale-quality leakage. */
        memcpy(p,exact,8);ctx.generation=7;ctx.paths=3;
        rx.flags=RTW88_RX_PHY_STATUS;rx.rate=0x3f;
        CHECK(rtw88_rx_signal_decode(p,8,&rx,&ctx,7,36,&s)==0 && s.quality==50);
        size_t len=8;uint32_t gen=7;unsigned channel=36;
        const uint8_t *bytes=p;const struct rtw88_phy_state *state=&ctx;
        unsigned reason=RTW88_SIGNAL_SHORT;
        if(bad==0)bytes=NULL;
        else if(bad==1)len=0;
        else if(bad==2)len=7;
        else if(bad==3)len=121;
        else if(bad==4){p[0]=2;reason=RTW88_SIGNAL_PAGE;}
        else if(bad==5){gen=0;reason=RTW88_SIGNAL_GENERATION;}
        else if(bad==6){ctx.generation=8;reason=RTW88_SIGNAL_GENERATION;}
        else if(bad==7){state=NULL;reason=RTW88_SIGNAL_GENERATION;}
        else if(bad==8){ctx.paths=1;reason=RTW88_SIGNAL_PATH;}
        else if(bad==9){channel=40;reason=RTW88_SIGNAL_CHANNEL;}
        else if(bad==10){channel=p[6]=35;reason=RTW88_SIGNAL_CHANNEL;}
        else if(bad==11){channel=p[6]=8;reason=RTW88_SIGNAL_RATE;}
        else if(bad==12){p[0]=0;len=20;reason=RTW88_SIGNAL_RATE;}
        else if(bad==13){rx.flags=0;reason=RTW88_SIGNAL_MISSING;}
        else {p[5]=1;reason=RTW88_SIGNAL_WIDTH;}
        CHECK(rtw88_rx_signal_decode(bytes,len,&rx,state,gen,channel,&s)==ENOENT);
        CHECK(!s.valid && s.quality==0 && s.reason==reason);
    }
    printf("rtw88 signal: %u checks, %u failures\n",checks,failures);return failures!=0;
}
