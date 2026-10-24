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
    CHECK(!sc->sc_phy.channel_valid);
    CHECK(sc->sc_phy.gain_valid && sc->sc_phy.generation==sc->sc_generation &&
        sc->sc_phy.gain_low==20 && sc->sc_phy.gain_high==40);
    uint32_t regs[]={0x1a98,0x1aa8,0x1a70};fault.read_4=signal_read4;
    for(unsigned i=0;i<3;i++) {
        sc->sc_ops=&fault;signal_fail_reg=regs[i];
        CHECK(rtw8822c_signal_cache(sc)==EIO);
        CHECK(!sc->sc_phy.channel_valid);
        CHECK(!sc->sc_phy.gain_valid && sc->sc_phy.gain_error==EIO &&
            sc->sc_phy.generation==sc->sc_generation);
    }
    sc->sc_ops=&ops;signal_fail_reg=0;
    signal_set32(0x1a98,3U<<6);signal_set32(0x1a70,15U<<24);
    CHECK(rtw8822c_signal_cache(sc)==EINVAL && !sc->sc_phy.gain_valid);
    signal_set32(0x1a98,(1U<<6)|(2U<<14));signal_set32(0x1a70,4U<<24);
    CHECK(rtw8822c_signal_cache(sc)==0);
}
/* These tests start the actual core. No channel or PHY state is supplied. */
static int context_write4(void *cookie,uint32_t reg,uint32_t v) {
    int e=write_4(cookie,reg,v);
    return reg==0x18a0 ? EIO : e; /* A failed write may already mutate hardware. */
}
static void context_rx(struct rtw88_softc *sc,unsigned n) {
    signal_packet(sc,n,0,60);
    uint8_t *p=sc->sc_rxbuf[n].vaddr;
    p[12]=0x2c;p[29]=0;p[30]=36;
    f.rx_hw_idx=n+1;set_hisr0(1);
    CHECK(rtw88_intr(sc)==1 && signal_count==n+1);
}
static void context_success(void) {
    struct rtw88_softc sc;
    if(!start_ok(&sc))return;
    CHECK(rtw88_channel(&sc,36)==0);
    sc.sc_rx_frame=signal_capture;signal_count=0;
    context_rx(&sc,0);
    CHECK(signal_observed[0].valid && signal_observed[0].quality==50);
    const char *path=getenv("RTW88_SIGNAL_CONTEXT");
    if(path!=NULL) {
        FILE *file=fopen(path,"wb");CHECK(file!=NULL);
        if(file!=NULL){CHECK(fwrite(&sc.sc_phy,sizeof(sc.sc_phy),1,file)==1);CHECK(fclose(file)==0);}
    }
    CHECK(rtw88_stop(&sc)==0 && rtw88_core_detach(&sc)==0);
}
static void context_failure(void) {
    struct rtw88_softc sc;struct rtw88_bus_ops fault=ops;
    uint8_t data[24]={8,1},mgmt[24]={0x80};unsigned wp[RTW88_NTXQ];
    struct rtw88_tx_fixed fixed={.associated=1,.channel=36,.rate500=12,
        .peer_mask=0xff0,.mode=RTW88_FIXED_11A};
    if(!start_ok(&sc))return;
    CHECK(rtw88_channel(&sc,36)==0);
    for(unsigned q=0;q<RTW88_NTXQ;q++)CHECK(rtw88_tx_available(&sc,q)>0);
    CHECK(rtw88_tx_fixed(&sc,RTW88_TXQ_BE,data,sizeof(data),&fixed)==0);
    CHECK(rtw88_tx_raw(&sc,RTW88_TXQ_MGMT,mgmt,sizeof(mgmt))==0);
    const uint8_t bssid[6]={2,1,2,3,4,5};
    struct rtw88_ra_config cfg={.epoch=9,.channel=36,.common_rates=0xff0,.mode=RTW88_FIXED_11A};
    CHECK(rtw88_station_config(&sc,bssid,1,0x150,9,1)==0);
    CHECK(rtw88_ra_configure(&sc,&cfg)==0);
    CHECK(rtw88_tx_auto(&sc,RTW88_TXQ_BE,data,sizeof(data),9,0)==0);
    fault.write_4=context_write4;sc.sc_ops=&fault;
    CHECK(rtw88_channel(&sc,36)==EIO);
    sc.sc_ops=&ops;sc.sc_rx_frame=signal_capture;signal_count=0;
    context_rx(&sc,0);CHECK(!signal_observed[0].valid && signal_observed[0].quality==0);
    for(unsigned q=0;q<RTW88_NTXQ;q++){
        CHECK(rtw88_tx_available(&sc,q)==0);wp[q]=sc.sc_txq[q].wp;
    }
    unsigned writes=f.writes;
    CHECK(rtw88_tx_fixed(&sc,RTW88_TXQ_BE,data,sizeof(data),&fixed)==ENXIO);
    CHECK(rtw88_tx_raw(&sc,RTW88_TXQ_MGMT,mgmt,sizeof(mgmt))==ENXIO);
    for(unsigned q=0;q<RTW88_NTXQ;q++)CHECK(sc.sc_txq[q].wp==wp[q]);
    CHECK(rtw88_tx_auto(&sc,RTW88_TXQ_BE,data,sizeof(data),9,0)==EBUSY);
    CHECK(f.writes==writes && rtw88_channel(&sc,40)==ENXIO);
    CHECK(rtw88_start(&sc)==EBUSY);
    CHECK(rtw8822c_set_channel(&sc,36)==EINTR && sc.sc_stop_requested);
    f.off_error=1;CHECK(rtw88_stop(&sc)==EIO);
    CHECK(sc.sc_power_error==EIO && sc.sc_power_dirty && rtw88_start(&sc)==EBUSY);
    f.off_error=0;CHECK(rtw88_stop(&sc)==0 && rtw88_start(&sc)==0);
    CHECK(rtw88_channel(&sc,36)==0);
    sc.sc_rx_frame=signal_capture;signal_count=0;context_rx(&sc,0);
    CHECK(signal_observed[0].valid && signal_observed[0].quality==50);
    CHECK(rtw88_stop(&sc)==0 && rtw88_core_detach(&sc)==0);
}
static void context_power_validation(void) {
    struct rtw88_softc sc;
    if(!start_ok(&sc))return;
    CHECK(rtw88_channel(&sc,36)==0);
    /* Same calibrated physical-record corruption as the owning start suite.
     * PHY succeeds; TXAGC arithmetic rejects before its first register I/O. */
    sc.sc_efuse_phys[1]=0;
    CHECK(rtw88_channel(&sc,1)==ERANGE);
    CHECK(sc.sc_stop_requested && !sc.sc_phy.channel_valid && sc.sc_channel==36);
    sc.sc_rx_frame=signal_capture;signal_count=0;context_rx(&sc,0);
    CHECK(!signal_observed[0].valid && signal_observed[0].quality==0);
    CHECK(rtw88_stop(&sc)==0 && rtw88_core_detach(&sc)==0);
}
static unsigned context_hci_failure_seen;
static int context_hci_write(void *cookie,uint32_t reg,uint32_t value) {
    if(reg==0xb0 && value!=0) {
        CHECK(f.sc->sc_phy.channel_valid);
        context_hci_failure_seen++;
        return EIO;
    }
    return write_4(cookie,reg,value);
}
static void context_start_failure(void) {
    struct rtw88_softc sc;struct rtw88_bus_ops fault=ops;
    reset_bus(&sc);if(!attach_ok(&sc))return;
    fault.write_4=context_hci_write;sc.sc_ops=&fault;context_hci_failure_seen=0;
    CHECK(rtw88_start(&sc)==EIO);
    CHECK(context_hci_failure_seen==1 && !sc.sc_phy.channel_valid && sc.sc_stop_requested);
    sc.sc_ops=&ops;CHECK(rtw88_core_detach(&sc)==0);
}
/* One operation stream covers actual BB/MAC/RF/IGI/TXAGC and pause restore. */
struct context_access { uint32_t reg; unsigned width,write; };
static struct context_access context_trace[1024];
static unsigned context_accesses,context_fault,context_cancel,context_change;
static int context_step(uint32_t reg,unsigned width,unsigned write) {
    unsigned n=++context_accesses;
    CHECK(n<=1024 && !f.sc->sc_phy.channel_valid);
    if(n<=1024)context_trace[n-1]=(struct context_access){reg,width,write};
    if(n==context_cancel) {
        if(context_change)f.sc->sc_generation++;else f.sc->sc_stop_requested=1;
    }
    return n==context_fault;
}
#define CONTEXT_READ(n,type) \
static int context_r##n(void *c,uint32_t r,type *v) { \
    if(context_step(r,n,0)){return EIO;} return read_##n(c,r,v); }
#define CONTEXT_WRITE(n,type) \
static int context_w##n(void *c,uint32_t r,type v) { \
    int fault=context_step(r,n,1);int e=write_##n(c,r,v);return fault?EIO:e; }
CONTEXT_READ(1,uint8_t)
CONTEXT_READ(2,uint16_t)
CONTEXT_READ(4,uint32_t)
CONTEXT_WRITE(1,uint8_t)
CONTEXT_WRITE(2,uint16_t)
CONTEXT_WRITE(4,uint32_t)
#undef CONTEXT_READ
#undef CONTEXT_WRITE
static struct rtw88_bus_ops context_ops(void) {
    struct rtw88_bus_ops v=ops;
    v.read_1=context_r1;v.read_2=context_r2;v.read_4=context_r4;
    v.write_1=context_w1;v.write_2=context_w2;v.write_4=context_w4;return v;
}
static void context_stream_reset(void) {
    context_accesses=context_fault=context_cancel=context_change=0;
}
static void context_parser(const struct rtw88_phy_state *producer,unsigned channel) {
    struct rtw88_phy_state phy=*producer;
    struct rtw88_rx_info rx={.flags=RTW88_RX_PHY_STATUS,.rate=0x2c};
    struct rtw88_rx_signal out;
    uint8_t p[124]={1,60,50,0,0,0,0};p[6]=(uint8_t)channel;
    CHECK(phy.channel_valid);
    for(unsigned rate=0x2c;rate<=0x3f;rate++)for(unsigned low=0;low<16;low++) {
        rx.rate=(uint8_t)rate;p[5]=(uint8_t)low;
        CHECK(rtw88_rx_signal_decode(p,8,&rx,&phy,phy.generation,channel,&out)==0);
        CHECK(out.valid && out.quality==50 && out.rxsc==0);
    }
    rx.rate=0x2c;
    for(unsigned shift=0;shift<4;shift++) {
        uint8_t exact[11]={0};memcpy(exact+shift,p,8);
        CHECK(rtw88_rx_signal_decode(exact+shift,8,&rx,&phy,phy.generation,channel,&out)==0);
    }
    for(unsigned bw=1;bw<4;bw++) {
        rx.bandwidth=(uint8_t)bw;
        CHECK(rtw88_rx_signal_decode(p,8,&rx,&phy,phy.generation,channel,&out)==ENOENT && out.reason==RTW88_SIGNAL_WIDTH);
    }
    rx.bandwidth=0;
    CHECK(rtw88_rx_signal_decode(p,121,&rx,&phy,phy.generation,channel,&out)==ENOENT && out.reason==RTW88_SIGNAL_SHORT);
    phy.channel_valid=0;
    CHECK(rtw88_rx_signal_decode(p,8,&rx,&phy,phy.generation,channel,&out)==ENOENT && out.reason==RTW88_SIGNAL_WIDTH && !out.valid && out.quality==0);
    /* Invalid context does not gate explicit20 or legacy observations. */
    p[5]=0x19;
    CHECK(rtw88_rx_signal_decode(p,8,&rx,&phy,phy.generation,channel,&out)==0);
    rx.rate=4;p[5]=0;
    CHECK(rtw88_rx_signal_decode(p,8,&rx,&phy,phy.generation,channel,&out)==0);
}
static void context_matrix(void) {
    struct rtw88_softc sc;struct rtw88_bus_ops trace=context_ops();
    struct context_access baseline[1024];unsigned total;
    if(!start_ok(&sc))return;
    CHECK(sc.sc_phy.channel_valid && sc.sc_channel==1);
    CHECK(rtw88_channel(&sc,36)==0);context_parser(&sc.sc_phy,36);
    CHECK(rtw88_channel(&sc,40)==0);context_parser(&sc.sc_phy,40);
    unsigned writes=f.writes;
    CHECK(rtw88_channel(&sc,15)==EINVAL && sc.sc_phy.channel_valid && f.writes==writes);
    CHECK(rtw8822c_set_channel(&sc,36)==0 && !sc.sc_phy.channel_valid);
    CHECK(rtw88_channel(&sc,36)==0 && sc.sc_phy.channel_valid);
    struct rtw8822c_efuse efuse;
    CHECK(rtw8822c_efuse_decode(sc.sc_efuse_phys,sizeof(sc.sc_efuse_phys),&efuse)==0);
    CHECK(rtw8822c_set_tx_power(&sc,&efuse,36,12)==0 && !sc.sc_phy.channel_valid);
    CHECK(rtw88_channel(&sc,36)==0 && sc.sc_phy.channel_valid);
    CHECK(rtw8822c_signal_cache(&sc)==0 && !sc.sc_phy.channel_valid);
    struct rtw88_bus_ops gain_fault=ops;gain_fault.read_4=signal_read4;
    signal_fail_reg=0x1a98;sc.sc_ops=&gain_fault;
    CHECK(rtw8822c_signal_cache(&sc)==EIO && !sc.sc_phy.channel_valid && !sc.sc_phy.gain_valid);
    sc.sc_ops=&ops;signal_fail_reg=0;
    CHECK(rtw88_channel(&sc,36)==0 && sc.sc_phy.channel_valid && !sc.sc_phy.gain_valid);
    sc.sc_ops=&trace;context_stream_reset();
    CHECK(rtw88_channel(&sc,36)==0 && sc.sc_phy.channel_valid);
    total=context_accesses;CHECK(total>50 && total<=1024);
    memcpy(baseline,context_trace,total*sizeof(baseline[0]));
    printf("retune I/O baseline: %u accesses\n",total);
    for(unsigned i=0;i<total;i++)printf("retune I/O %u: %c%u %#x\n",i+1,baseline[i].write?'W':'R',baseline[i].width,baseline[i].reg);
    sc.sc_ops=&ops;CHECK(rtw88_stop(&sc)==0 && rtw88_core_detach(&sc)==0);
    for(unsigned fault=1;fault<=total;fault++) {
        if(!start_ok(&sc))return;
        CHECK(rtw88_channel(&sc,36)==0);
        sc.sc_ops=&trace;context_stream_reset();context_fault=fault;
        CHECK(rtw88_channel(&sc,40)==EIO);
        CHECK(context_accesses==fault && !sc.sc_phy.channel_valid && sc.sc_channel==36);
        CHECK(!sc.sc_ra.active && !sc.sc_ra.configured);
        CHECK(sc.sc_stop_requested==(fault!=1));
        if(fault!=1)CHECK(rtw88_channel(&sc,36)==ENXIO);
        sc.sc_ops=&ops;
        if(fault==1)CHECK(rtw88_channel(&sc,36)==0 && sc.sc_phy.channel_valid);
        CHECK(rtw88_stop(&sc)==0 && rtw88_core_detach(&sc)==0);
    }
    /* The last access of each actual phase, plus power and restore boundaries. */
    unsigned boundaries[7]={2,0,0,0,0,total-1,total};
    for(unsigned i=0;i<total;i++)if(baseline[i].write) {
        if(baseline[i].reg==0xcbc)boundaries[1]=i+1;
        if(baseline[i].reg==0x454 && baseline[i].width==1)boundaries[2]=i+1;
        if(baseline[i].reg==0x4130)boundaries[3]=i+1;
        if(baseline[i].reg==0x1d70)boundaries[4]=i+1;
    }
    for(unsigned mode=0;mode<2;mode++)for(unsigned b=0;b<7;b++) {
        CHECK(boundaries[b]!=0);
        if(!start_ok(&sc))return;
        CHECK(rtw88_channel(&sc,36)==0);
        sc.sc_ops=&trace;context_stream_reset();context_cancel=boundaries[b];context_change=mode;
        CHECK(rtw88_channel(&sc,40)==EINTR);
        CHECK(context_accesses==boundaries[b] && !sc.sc_phy.channel_valid && sc.sc_stop_requested && sc.sc_channel==36);
        sc.sc_ops=&ops;CHECK(rtw88_stop(&sc)==0 && rtw88_core_detach(&sc)==0);
    }
    /* First bus error wins when cancellation is observed at the same boundary. */
    if(!start_ok(&sc))return;
    sc.sc_ops=&trace;context_stream_reset();context_fault=context_cancel=2;context_change=1;
    CHECK(rtw88_channel(&sc,36)==EIO && context_accesses==2);
    sc.sc_ops=&ops;CHECK(rtw88_stop(&sc)==0 && rtw88_core_detach(&sc)==0);
}
int main(void) {
    struct rtw88_softc sc;
    const char *only=getenv("RTW88_CONTEXT_ONLY");
    if(only!=NULL) {
        if(strcmp(only,"success")==0)context_success();
        else if(strcmp(only,"matrix")==0)context_matrix();
        else if(strcmp(only,"power-validation")==0)context_power_validation();else context_failure();
        printf("rtw88 context: %u checks, %u failures\n",assertions,failures);
        return failures!=0;
    }
    context_success();context_failure();context_power_validation();context_start_failure();context_matrix();
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
