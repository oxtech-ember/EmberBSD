/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual core RX -> bounded callback copy -> separate actual scan consumer.
 * DMA device and callback storage are modeled, not a full ieee80211_input path. */
#define RTW88_START_CORE_ONLY 1
#define main signal_existing_start_main
#include "rtw88-start-contract.c"
#undef main
int rtw88_signal_scan_check(unsigned,unsigned);
static struct rtw88_rx_signal signal_observed[16];
static unsigned signal_count;
static void signal_capture(void *cookie,const uint8_t *frame,size_t len,
    const struct rtw88_rx_info *info) {
    (void)cookie;CHECK(frame[0]==0x08 && frame[1]==0x02 && len==24 && signal_count<16);
    for(unsigned i=2;i<24;i++)CHECK(frame[i]==0);
    if(signal_count<16) signal_observed[signal_count++]=info->signal;
}
static void signal_packet(struct rtw88_softc *sc,unsigned n,unsigned shift,unsigned strength) {
    uint8_t frame[28]={0x08,0x02};
    rx_put(sc,n,28,0,NULL,(uint16_t)n);
    uint8_t *p=sc->sc_rxbuf[n].vaddr;
    uint32_t w=28U|(4U<<16)|(shift<<24)|(1U<<26);
    for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(w>>(8*i));
    p[12]=4;uint8_t *phy=p+24+shift;memset(phy,0,32);
    phy[0]=1;phy[1]=(uint8_t)strength;phy[2]=50;phy[5]=1;phy[6]=1;
    memcpy(phy+32,frame,28);
}
static uint32_t signal_fail_reg;
static int signal_read4(void *cookie,uint32_t reg,uint32_t *out) {
    if(reg==signal_fail_reg)return EIO;
    return read_4(cookie,reg,out);
}
static void signal_set32(uint32_t reg,uint32_t value) {
    for(unsigned i=0;i<4;i++)f.mmio[reg+i]=(uint8_t)(value>>(8*i));
}
static void test_signal_cache(struct rtw88_softc *sc) {
    struct rtw88_bus_ops fault=ops;
    signal_set32(0x1a98,(1U<<6)|(2U<<14));
    signal_set32(0x1aa8,8U<<16);signal_set32(0x1a70,4U<<24);
    CHECK(rtw8822c_signal_cache(sc)==0);
    CHECK(sc->sc_phy.gain_valid && sc->sc_phy.generation==sc->sc_generation &&
        sc->sc_phy.gain_low==20 && sc->sc_phy.gain_high==40);
    uint32_t regs[]={0x1a98,0x1aa8,0x1a70};fault.read_4=signal_read4;
    for(unsigned i=0;i<3;i++) {
        sc->sc_ops=&fault;signal_fail_reg=regs[i];
        CHECK(rtw8822c_signal_cache(sc)==EIO);
        CHECK(!sc->sc_phy.gain_valid && sc->sc_phy.gain_error==EIO &&
            sc->sc_phy.generation==sc->sc_generation);
    }
    sc->sc_ops=&ops;signal_fail_reg=0;
    signal_set32(0x1a98,3U<<6);signal_set32(0x1a70,15U<<24);
    CHECK(rtw8822c_signal_cache(sc)==EINVAL && !sc->sc_phy.gain_valid);
    signal_set32(0x1a98,(1U<<6)|(2U<<14));signal_set32(0x1a70,4U<<24);
    CHECK(rtw8822c_signal_cache(sc)==0);
}
int main(void) {
    struct rtw88_softc sc;
    if(!start_ok(&sc))return 1;
    test_signal_cache(&sc);sc.sc_rx_frame=signal_capture;signal_count=0;
    signal_packet(&sc,0,1,60);signal_packet(&sc,1,3,90);
    f.rx_hw_idx=2;set_hisr0(1);CHECK(rtw88_intr(&sc)==1 && signal_count==2);
    CHECK(signal_observed[0].valid && signal_observed[0].quality==50);
    CHECK(signal_observed[1].valid && signal_observed[1].quality==80);
    CHECK(rtw88_signal_scan_check(signal_observed[0].quality,signal_observed[1].quality)==0);
    /* Missing status does not inherit the last strong sample or drop DATA. */
    signal_packet(&sc,2,0,90);((uint8_t *)sc.sc_rxbuf[2].vaddr)[3]&=(uint8_t)~4U;
    f.rx_hw_idx=3;set_hisr0(1);CHECK(rtw88_intr(&sc)==1 && signal_count==3);
    CHECK(!signal_observed[2].valid && signal_observed[2].quality==0);
    signal_packet(&sc,3,0,60);
    uint8_t *shortbuf=sc.sc_rxbuf[3].vaddr;
    memmove(shortbuf+24+8,shortbuf+24+32,28);
    shortbuf[2]=(shortbuf[2]&0xf0)|1;shortbuf[12]=0;shortbuf[24]=0;
    f.rx_hw_idx=4;set_hisr0(1);CHECK(rtw88_intr(&sc)==1 && signal_count==4);
    CHECK(!signal_observed[3].valid && signal_observed[3].reason==RTW88_SIGNAL_SHORT);
    signal_packet(&sc,4,0,60);((uint8_t *)sc.sc_rxbuf[4].vaddr)[1]|=0x40;
    f.rx_hw_idx=5;set_hisr0(1);CHECK(rtw88_intr(&sc)==1 && signal_count==4);
    CHECK(sc.sc_stats.signal_reasons[RTW88_SIGNAL_SHORT]==1);
    CHECK(sc.sc_stats.signal_reasons[0]==2 && sc.sc_stats.signal_reasons[RTW88_SIGNAL_MISSING]==1);
    /* Actual descriptor -> core -> callback; optional signal does not own DATA. */
    sc.sc_channel=36;
    signal_packet(&sc,5,1,60);
    uint8_t *vht=sc.sc_rxbuf[5].vaddr;
    vht[12]=0x2c;vht[24+1+5]=0x19;vht[24+1+6]=36;
    f.rx_hw_idx=6;set_hisr0(1);CHECK(rtw88_intr(&sc)==1 && signal_count==5);
    CHECK(signal_observed[4].valid && signal_observed[4].quality==50);
    const uint8_t codes[]={0x35,0x36,0x3f,0x2c,0x2c,0x40};
    const uint8_t rxsc[]={0x89,0x19,0x19,0x01,0x91,0x19};
    for(unsigned i=0;i<6;i++) {
        unsigned n=6+i;signal_packet(&sc,n,0,90);
        vht=sc.sc_rxbuf[n].vaddr;vht[12]=codes[i];vht[29]=rxsc[i];vht[30]=36;
        f.rx_hw_idx=(uint16_t)(n+1);set_hisr0(1);
        CHECK(rtw88_intr(&sc)==1 && signal_count==n);
        CHECK(signal_observed[n-1].quality==(i<3?80:0));
        CHECK(signal_observed[n-1].valid==(i<3));
        if(i>=3)CHECK(signal_observed[n-1].reason==
            (i==5?RTW88_SIGNAL_RATE:RTW88_SIGNAL_WIDTH));
    }
    /* ICV, like CRC above, keeps its separate payload rejection. */
    signal_packet(&sc,12,0,60);vht=sc.sc_rxbuf[12].vaddr;
    vht[1]|=0x80;f.rx_hw_idx=13;set_hisr0(1);
    CHECK(rtw88_intr(&sc)==1 && signal_count==11);
    rtw88_stop(&sc);CHECK(sc.sc_phy.generation==0 && !sc.sc_phy.gain_valid);rtw88_core_detach(&sc);
    printf("rtw88 signal core: %u checks, %u failures\n",assertions,failures);
    return failures!=0;
}
