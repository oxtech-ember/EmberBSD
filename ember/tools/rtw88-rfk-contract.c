/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: native full-chain RFK actual-worker contract, AI-assisted. */
/* The existing complete owning attach/worker/core model is reused. This first
 * case supplies NO fresh RFK completion. It must never reach parent AUTH.
 * It is not a fake successful RFK engine or an HT readiness assertion. */
#define RTW88_ENABLE_ENTRY rfk_existing_enable_main
#include "rtw88-enable-contract.c"
#undef RTW88_ENABLE_ENTRY
#include "rtw88-rfk-atomic-model.h"
volatile unsigned *rtw88_test_rfk_atomic_word;
void (*rtw88_test_rfk_drop_hook)(void);

/* Numeric fake-bus report service. Reports advance ONLY after an actual
 * producer trigger/doorbell, never from a helper claiming phase success. */
static unsigned rfk_parent_scan,rfk_parent_init;
static unsigned rfk_parent_auth, rfk_positive, rfk_handshakes, rfk_iqk;
static unsigned rfk_gapk, rfk_dpk, rfk_coef, rfk_report_reads, rfk_ack_reads;
static unsigned rfk_cal_pwr, rfk_power_reads, rfk_gain_commits;
static uint8_t rfk_end_value;
static unsigned rfk_end_sent,rfk_preend_reads;
static uint64_t rfk_end_at_us;
static unsigned rfk_trace[128],rfk_trace_len;
static const char *rfk_case;
static struct rtw88_net *rfk_net;
static unsigned rfk_fault,rfk_paused,rfk_io,rfk_extra_us,rfk_owned_io;
static const uint32_t rfk_original_tx=0x12345678,rfk_original_fifo=0xabcdef01;
static int rfk_is(const char *name) { return strcmp(rfk_case,name)==0; }
static int rfk_power_before(void) { return strncmp(rfk_case,"power-before-",13)==0; }
static void rfk_after_owner_drop(void)
{
    if(!rfk_net || !rfk_net->rfk.ran)return;
    if(rfk_is("env-retire-Q"))atomic_or_uint(rtw88_test_rfk_atomic_word,2);
    if(rfk_is("seal-power"))rfk_net->core->sc_power_program.valid=0;
    if(rfk_is("env-successor-scan"))
        CHECK(rtw88_net_newstate(&rfk_net->ic,IEEE80211_S_SCAN,0)==0);
    rtw88_test_rfk_drop_hook=NULL;
}
static unsigned rfk_last_owned_tick,rfk_owned_at_cancel;
static void rfk_io_tick(void)
{
    rfk_io++;
    if(rtw8822c_rfk_active(f.sc)) {
        rfk_owned_io++;
        rfk_extra_us+=rfk_is("slow-io")?5000:1;
        route_ticks+=rfk_extra_us/1000;rfk_extra_us%=1000;
        rfk_last_owned_tick=route_ticks;
    }
}
static int rfk_once(const char *name) {
    if(!rfk_fault && rfk_is(name)) { rfk_fault=1;return 1; }return 0;
}
static unsigned rfk_rf18_writes,rfk_rf18_reads,rfk_rxbb_active,rfk_rxbb_end_io;
static uint64_t rfk_alloc_power_serial;
static unsigned rfk_spacing=16;
static int rfk_profile(struct ieee80211com *ic,struct ieee80211_ht20_profile *p)
{ (void)ic;*p=(struct ieee80211_ht20_profile){7,0xff,0xff,0x3f,(uint8_t)rfk_spacing};return 0; }
static int rfk_c1_pause(unsigned ticks)
{
    (void)ticks;CHECK(rtw8822c_rfk_active(rfk_net->core));
    rfk_spacing=20;rfk_paused++;
    f.tx_hw_rp[0]=rfk_net->core->sc_txq[0].wp;
    return EWOULDBLOCK;
}
static void rfk_alloc_yield(void)
{
    if(rfk_net && rfk_is("alloc-Q") && !rfk_paused) {
        CHECK(rtw88_test_rfk_atomic_word!=NULL);
        rfk_alloc_power_serial=rfk_net->core->sc_power_program.serial;
        atomic_or_uint(rtw88_test_rfk_atomic_word,2);
        rfk_paused=1;
    }
    if(rfk_net && rfk_is("alloc-cancel") && !rfk_paused) {
        rfk_paused=1;
        CHECK(rtw88_net_newstate(&rfk_net->ic,IEEE80211_S_SCAN,0)==0);
    }
}
static int rfk_pause(unsigned ticks)
{
    CHECK(!mutex_owned(rfk_net->lock));
    (void)ticks;
    if(!rfk_paused && rfk_end_sent &&
       (rfk_is("end-cancel") || rfk_is("end-Q") || rfk_is("end-deadline"))) {
        rfk_paused=1;
        if(rfk_is("end-Q"))atomic_or_uint(rtw88_test_rfk_atomic_word,2);
        else if(rfk_is("end-cancel"))
            CHECK(rtw88_net_newstate(&rfk_net->ic,IEEE80211_S_SCAN,0)==0);
        else route_ticks+=9000;
    }
    if(!rfk_paused && rfk_is("power-after-bind") && rfk_gapk==1) {
        rfk_net->core->sc_power_program.ht_diff[0]^=1;rfk_paused=1;
    }
    if(!rfk_paused && rfk_is("power-mailbox") && rfk_net->rfk.ran) {
        struct rtw8822c_efuse ef;
        CHECK(rtw8822c_efuse_decode(rfk_net->core->sc_efuse_phys,
            sizeof(rfk_net->core->sc_efuse_phys),&ef)==0);
        CHECK(rtw8822c_set_tx_power(rfk_net->core,&ef,rfk_net->core->sc_channel,12)==0);
        rfk_paused=1;
    }
    if(!rfk_paused && rfk_is("quarantine-complete") && rfk_net->rfk.ran) {
        CHECK(rtw88_test_rfk_atomic_word!=NULL);
        atomic_or_uint(rtw88_test_rfk_atomic_word,2);
        rfk_paused=1;rfk_rxbb_end_io=rfk_owned_io;
    }
    if(!rfk_paused && rfk_is("quarantine-rxbb") && rfk_rxbb_active) {
        CHECK(rtw88_test_rfk_atomic_word!=NULL);
        atomic_or_uint(rtw88_test_rfk_atomic_word,2);
        rfk_paused=1;rfk_rxbb_end_io=rfk_owned_io;
    }
    if(!rfk_paused && rfk_is("cancel-rxbb") && rfk_rxbb_active) {
        rfk_paused=1;
        CHECK(rtw88_net_newstate(&rfk_net->ic,IEEE80211_S_SCAN,0)==0);
        rfk_rxbb_end_io=rfk_owned_io;
    }
    if(!rfk_paused && ((rfk_gapk==1 && (rfk_is("cancel-gapk") || rfk_is("profile-gapk"))) ||
       (rfk_dpk && (rfk_is("cancel-dpk") || rfk_is("dying-cleanup") || rfk_is("cleanup-expiry"))) ||
       (rfk_iqk && rfk_is("cancel-iqk")) || (rfk_coef && rfk_net->rfk.ran && rfk_is("post-complete-change")))) {
        rfk_paused=1;
        if(rfk_is("profile-gapk"))rfk_net->ic.ic_flags^=IEEE80211_F_USEPROT;
        else if(rfk_is("post-complete-change"))rfk_net->ic.ic_bss->ni_authmode^=1;
        else if(rfk_is("dying-cleanup"))rtw88_net_close(&rfk_net->policy);
        else CHECK(rtw88_net_newstate(&rfk_net->ic,IEEE80211_S_SCAN,0)==0);
        if(rfk_is("cancel-iqk"))f.h2c_rp=rfk_net->core->sc_packet_h2c.wp;
        if(rfk_is("cleanup-expiry")) { route_ticks+=9000;rfk_owned_at_cancel=rfk_owned_io; }
    }
    return EWOULDBLOCK;
}
static void rfk_delay_bus(void *cookie,unsigned us)
{
    delay_us(cookie,us);
    rfk_extra_us+=us;
    route_ticks+=rfk_extra_us/1000;rfk_extra_us%=1000;
}
static void rfk_event(unsigned event)
{ CHECK(rfk_trace_len<128);if(rfk_trace_len<128)rfk_trace[rfk_trace_len++]=event; }
static int rfk_write4(void *cookie,uint32_t reg,uint32_t value)
{
    uint32_t old=mmio32(reg);
    rfk_io_tick();
    int error=write_4(cookie,reg,value);
    if(!error && reg==0x3a0c && rtw8822c_rfk_active(f.sc) &&
       !rfk_handshakes && rfk_once("power-partial"))return EIO;
    if(error)return error;
    if(!rtw8822c_rfk_active(f.sc))return 0;
    if(reg>=0x1f0 && reg<=0x1fc && (reg-0x1f0)%4==0 &&
       rfk_handshakes==1 && rfk_gapk==2 && rfk_once("end-ext-error"))return EIO;
    if(reg==0x3c60 || reg==0x4c60)rfk_rf18_writes++;
    if(reg>=0x1d0 && reg<=0x1dc && (reg-0x1d0)%4==0 &&
       (value&0xff)==0x6d) {
        rfk_handshakes++;rfk_ack_reads=0;rfk_event((value&0x100)?1:4);
        /* Vendor END clears BIT0 only after the actual payload0 doorbell.
         * This is register state, not a success returned by read count. */
        if(!(value&0x100)) {
            rfk_end_sent=1;
            rfk_end_at_us=(uint64_t)route_ticks*1000+rfk_extra_us;
            rfk_end_value=rfk_is("end-held")?1:0;
            if(rfk_once("end-write-error"))return EIO; /* Doorbell landed. */
        }
        if(!(value&0x100) && rfk_is("positive"))CHECK(rfk_gain_commits==22);
    }
    if(reg==0x3e48 || reg==0x4e48) {
        rfk_rxbb_active=(value&0xfffff)==0x84801;
        if(rfk_rxbb_active && rfk_once("write-rxbb")) {
            rfk_rxbb_end_io=rfk_owned_io;
            return EIO;
        }
    }
    if(rfk_handshakes==1 && (reg==0x3cfc || reg==0x4cfc))rfk_gain_commits++;
    /* The primary emits each shot as an even/odd command pair. Observe
     * that producer edge, not later subpage RMWs preserving its low bit. */
    if(reg==0x1b00 && (value&1) && !(old&1)) {
        if(value==0xd19 || value==0xd2b) { rfk_gapk++;rfk_report_reads=0;rfk_event(value==0xd19?2:3); }
        if(value==0x1149) { rfk_coef++;rfk_report_reads=0;rfk_event(50); }
        if(value==0x1449 || value==0x1549 || value==0x1649 ||
           value==0x1749 || value==0x1a49 || value==0x1b49 ||
           value==0x1c49 || value==0x1d49) { rfk_dpk++;rfk_report_reads=0;rfk_event(6); }
    }
    if(reg==0x1bb4 && (value&(1U<<12)) && !(old&(1U<<12))) { rfk_cal_pwr++;rfk_power_reads=0; }
    if((reg==0x1b00 && value==0xd19 && rfk_once("write-gapk")) ||
       (reg==0x1b00 && value==0x1649 && rfk_once("write-dpk")) ||
       (reg==0x520 && value==rfk_original_tx && rfk_coef && rfk_once("restore-first")) ||
       (reg==0x1e70 && value==rfk_original_fifo && rfk_coef && rfk_once("restore-second")))
        return EIO; /* The write already landed: retain uncertainty. */
    return 0;
}
static int rfk_read4(void *cookie,uint32_t reg,uint32_t *value)
{
    rfk_io_tick();
    /* Native channel_rf writes both paths first. Fault only the later
     * owning full-chain readback, never the preparatory channel RMW. */
    if((reg==0x3c60 || reg==0x4c60) && rfk_rf18_writes>=2 &&
       rtw8822c_rfk_active(f.sc) && rfk_handshakes==0) {
        rfk_rf18_reads++;
        if(strncmp(rfk_case,"rf18-",5)==0) {
            if(rfk_is("rf18-read-error"))return EIO;
            *value=mmio32(reg);
            if(rfk_is("rf18-width"))*value^=0x1000;
            if(rfk_is("rf18-path") && reg==0x4c60)*value^=1;
            if(rfk_is("rf18-channel"))*value^=1;
            if(rfk_is("rf18-band"))*value^=0x10100;
            if(rfk_is("rf18-region"))*value^=0x20000;
            return 0;
        }
    }
    if(rfk_coef && rfk_handshakes==2 && rtw8822c_rfk_active(f.sc)) {
        if((reg==0x3c60 && rfk_is("env-rf18-a")) ||
           (reg==0x4c60 && rfk_is("env-rf18-b"))) {*value=mmio32(reg)^1;return 0;}
        if(reg==0x668 && rfk_is("env-macbw")) {*value=mmio32(reg)|0x80;return 0;}
        if(reg==0x1d08 && rfk_is("env-read-error"))return EIO;
    }
    if(reg==0x132c && rfk_iqk && f.sc->sc_packet_h2c.managed &&
       rfk_once("packet-read-error"))return EIO;
    if((reg==0x520 && rfk_handshakes==0 && rfk_once("snapshot-first")) ||
       (reg==0x1e70 && rfk_handshakes==0 && rfk_once("snapshot-second")))return EIO;
    if(rfk_positive && rtw8822c_rfk_active(f.sc) && reg==0x2d9c && (rfk_gapk||rfk_dpk||rfk_coef)) {
        int stale=(rfk_gapk && !rfk_iqk && rfk_is("stale-gapk")) ||
            (rfk_dpk && !rfk_coef && rfk_is("stale-dpk")) ||
            (rfk_coef && rfk_is("stale-coef"));
        *value=stale?0x55:(rfk_report_reads++?0x55:0);return 0;
    }
    if(rfk_positive && rtw8822c_rfk_active(f.sc) && reg==0x1bfc) {
        uint32_t selector=mmio32(0x1bd4);
        *value=selector==0x000900f0?0x00320032:
            selector==0x00060001?0x20000000:0x04000032;
        if(rfk_cal_pwr && (rfk_is("stale-cal") || rfk_power_reads++))*value|=0x80000000U;
        return 0;
    }
    return read_4(cookie,reg,value);
}
static int rfk_read1(void *cookie,uint32_t reg,uint8_t *value)
{
    rfk_io_tick();
    if(reg==0x1cc && rfk_coef && !rfk_paused &&
       (rfk_is("post-complete-change") || rfk_is("quarantine-complete") || rfk_is("power-mailbox"))) {
        *value=(uint8_t)(1U<<(f.sc->sc_h2c_box&3));return 0;
    }
    if(rfk_positive && rtw8822c_rfk_active(f.sc) && reg==0x49c && rfk_handshakes) {
        if(rfk_end_sent) {
            uint64_t now=(uint64_t)route_ticks*1000+rfk_extra_us;
            if(rfk_is("end-late-report"))
                *value=now-rfk_end_at_us<105000?1:0;
            else if(rfk_is("end-cancel") || rfk_is("end-Q") || rfk_is("end-deadline"))
                *value=now-rfk_end_at_us<2000?1:0;
            else *value=rfk_end_value;
        } else if(rfk_handshakes==1 && rfk_gapk==2) {
            rfk_preend_reads++;
            if(rfk_is("end-read-error")) {rfk_fault=1;return EIO;}
            *value=rfk_is("end-prezero")?0:1;
        } else *value=rfk_is("stale-start")?1:(rfk_ack_reads++?1:0);
        return 0;
    }
    if(rfk_positive && rtw8822c_rfk_active(f.sc) && reg==0x2d9c && rfk_iqk) {
        *value=rfk_is("stale-iqk")?0xaa:(rfk_report_reads++?0xaa:0);return 0;
    }
    if(reg==0x483 && rfk_coef && rfk_is("env-datasc")) {*value=1;return 0;}
    return read_1(cookie,reg,value);
}
static int rfk_read2(void *cookie,uint32_t reg,uint16_t *value)
{ rfk_io_tick();return read_2(cookie,reg,value); }
static int rfk_write1(void *cookie,uint32_t reg,uint8_t value)
{
    rfk_io_tick();int error=write_1(cookie,reg,value);
    if(!error && reg==0x522 && rtw8822c_rfk_active(f.sc) && !rfk_handshakes && rfk_power_before()) {
        struct rtw88_power_program *p=&f.sc->sc_power_program;
        if(rfk_is("power-before-missing"))p->valid=0;
        if(rfk_is("power-before-fw"))p->generation++;
        if(rfk_is("power-before-channel"))p->channel++;
        if(rfk_is("power-before-cut"))p->cut++;
        if(rfk_is("power-before-rfe"))p->rfe++;
        if(rfk_is("power-before-regd"))p->regd=2;
        if(rfk_is("power-before-serial"))p->serial=0;
    }
    return error;
}
static int rfk_write2(void *cookie,uint32_t reg,uint16_t value)
{
    rfk_io_tick();
    if(reg!=0x132c || !rtw8822c_rfk_active(f.sc))return write_2(cookie,reg,value);
    struct rtw88_softc *sc=f.sc;
    unsigned count=(unsigned)(sc->sc_ring[7].size/16);
    CHECK(value<count);
    unsigned old=(value+count-1)%count;
    uint8_t *bd=(uint8_t *)sc->sc_ring[7].vaddr+old*16;
    uint8_t *packet=sc->sc_fw_dma.vaddr;
    CHECK(bd[0]==48 && bd[2]==1 && bd[8]==32);
    CHECK(packet[0]==32 && packet[5]==19);
    CHECK(packet[48]==1 && packet[49]==0xff && packet[50]==0x0e);
    CHECK(packet[52]==9 && packet[56]==1); /* clear1, segment0 */
    rfk_iqk++;rfk_report_reads=0;f.h2c_sent++;rfk_event(5);
    f.mmio[reg]=(uint8_t)value;f.mmio[reg+1]=(uint8_t)(value>>8);
    f.h2c_rp=rfk_is("cancel-iqk")?old:value;
    /* Bus model consumption, NOT semantic IQK completion. */
    return rfk_once("packet-write-error")?EIO:0;
}
static int
rfk_parent_without_completion(struct ieee80211com *ic,
    enum ieee80211_state state, int arg)
{
    ic->ic_state = state;
    CHECK(!mutex_owned(rfk_net->lock));
    if(state==IEEE80211_S_SCAN)rfk_parent_scan++;
    if(state==IEEE80211_S_INIT)rfk_parent_init++;
    if (state == IEEE80211_S_AUTH)
        {
        if(rfk_is("env-successor-scan")) {rfk_parent_auth++;CHECK(0);return 0;}
        rfk_parent_auth++;
        const struct rtw88_operating_envelope *e=&rfk_net->core->sc_operating;
        const struct rtw88_power_program *p=&rfk_net->core->sc_power_program;
        CHECK(e->prepared && e->sealed && !rtw8822c_rfk_active(rfk_net->core));
        CHECK(e->generation==rfk_net->core->sc_generation && e->token!=0);
        CHECK(e->power.valid && e->power.serial==p->serial && p->serial!=0);
        CHECK(p->channel==rfk_net->core->sc_channel && p->regd==12 && p->ht_mcs==0xff);
        CHECK(e->bandwidth==20 && e->streams==1 && !e->tmac && !e->mac_bw && !e->data_sc);
        CHECK(e->power.ht_ref[0]==p->ht_ref[0] && e->power.ht_ref[1]==p->ht_ref[1]);
        CHECK(e->power.ht_diff[0]==p->ht_diff[0] && e->power.ht_diff[1]==p->ht_diff[1]);
        CHECK(p->ht_ref[0]==((mmio32(0x18e8)&0x1fc00)>>10));
        CHECK(p->ht_ref[1]==((mmio32(0x41e8)&0x1fc00)>>10));
        CHECK(p->ht_diff[0]==mmio32(0x3a0c) && p->ht_diff[1]==mmio32(0x3a10));
        CHECK(rtw88_start(rfk_net->core)==EBUSY && e->sealed && p->valid);
        unsigned posted=rfk_net->core->sc_txq[RTW88_TXQ_MGMT].queued;
        CHECK(ieee80211_send_mgmt(ic,ic->ic_bss,IEEE80211_FC0_SUBTYPE_AUTH,1)==0);
        CHECK(rfk_net->core->sc_txq[RTW88_TXQ_MGMT].queued==posted+1);
        /* station_config intentionally pauses only DATA's low four bits.
         * It must not inherit the RFK all-queue 0xff pause. */
        CHECK(mmio32(0x520)==(rfk_original_tx|0x000f0000));
        CHECK(mmio32(0x1e70)==rfk_original_fifo);
        if(rfk_is("positive")) {
            printf("RFK numeric trace: handshake=%u gapk=%u iqk=%u dpk=%u coef=%u cal=%u\n",
                rfk_handshakes,rfk_gapk,rfk_iqk,rfk_dpk,rfk_coef,rfk_cal_pwr);
            CHECK(rfk_handshakes==2 && rfk_gapk==2 && rfk_iqk==1);
            CHECK(rfk_dpk>=6 && rfk_coef==1 && rfk_cal_pwr==2);
            const struct rtw88_rfk_diag *d=&rfk_net->core->sc_stats.rfk;
            CHECK(d->token!=0 && d->generation==d->baseline_generation && d->global==0);
            CHECK((d->flags&3)==2 && d->kind==7 && d->raw_valid && d->raw==0x55);
            CHECK(d->arms[0]==1 && d->done[0]==1 && d->arms[1]==1 && d->done[1]==1);
            CHECK(d->arms[2]==2 && d->done[2]==2 && d->arms[3]==1 && d->done[3]==1);
            CHECK(d->arms[4]==rfk_dpk && d->done[4]==rfk_dpk);
            CHECK(d->arms[5]==2 && d->done[5]==2 && d->arms[6]==1 && d->done[6]==1);
            static const unsigned prefix[5]={1,2,3,4,5};
            CHECK(rfk_trace_len>=6);
            if(rfk_trace_len>=6) {
                CHECK(memcmp(rfk_trace,prefix,sizeof(prefix))==0);
                CHECK(rfk_trace[rfk_trace_len-1]==50);
            }
        }
    }
    ic->ic_state = state;
    (void)arg;
    return 0;
}

int
main(void)
{
    struct rtw88_softc sc;
    kmutex_t lock = {.initialized = 1};
    struct rtw88_net *net = enable_attach(&sc, &lock);
    if (net == NULL)
        return 1;
    rfk_case=getenv("RTW88_RFK_CASE");
    if(rfk_case==NULL)rfk_case="none";
    rfk_positive=!rfk_is("none");
    rfk_net=net;active_ic=&net->ic;enable_pause_hook=rfk_pause;
    rtw88_test_rfk_drop_hook=rfk_after_owner_drop;
    CHECK(write_4(&f,0x520,rfk_original_tx)==0);
    CHECK(write_4(&f,0x1e70,rfk_original_fifo)==0);
    struct rtw88_bus_ops rfk_bus=ops;
    rfk_bus.write_4=rfk_write4;rfk_bus.read_4=rfk_read4;
    rfk_bus.write_2=rfk_write2;rfk_bus.read_1=rfk_read1;
    rfk_bus.delay_us=rfk_delay_bus;rfk_bus.read_2=rfk_read2;rfk_bus.write_1=rfk_write1;
    sc.sc_ops=&rfk_bus;
    if(rfk_is("env-tmac"))CHECK(write_4(&f,0x1d08,1)==0);
    if(rfk_is("env-channel149")) {
        CHECK(rtw88_channel(&sc,1)==0);
        net->ic.ic_curchan=&net->ic.ic_channels[149];
        net->ic.ic_bss->ni_chan=net->ic.ic_curchan;
        CHECK(sc.sc_channel==1 && sc.sc_power_program.channel==1);
    }
    net->ic.ic_state = IEEE80211_S_SCAN;
    net->parent_state = rfk_parent_without_completion;
    if(rfk_is("c1-spacing")) {
        net->ic.ic_ht20_admit=rfk_profile;
        active_ic=&net->ic;
        CHECK(ieee80211_send_mgmt(&net->ic,net->ic.ic_bss,IEEE80211_FC0_SUBTYPE_AUTH,1)==0);
        CHECK(sc.sc_txq[RTW88_TXQ_MGMT].queued==1);
        enable_drain_pause_hook=rfk_c1_pause;
    }
    if(rfk_is("zero-init")) {
        CHECK(rtw88_stop(&sc)==0);
        CHECK(rtw88_net_newstate(&net->ic,IEEE80211_S_INIT,0)==0);
    } else CHECK(rtw88_net_newstate(&net->ic, IEEE80211_S_AUTH, 0) == 0);
    unsigned before=rfk_io,started_tick=route_ticks;
    rtw88_net_state_work(&net->state_work, net);
    if(rfk_is("newfw-baseline")) {
        CHECK(rfk_parent_auth==1);
        CHECK(rtw88_stop(&sc)==0 && rtw88_start(&sc)==0);
        unsigned prior_owned=rfk_owned_io,prior_shots=rfk_trace_len;
        CHECK(rtw88_net_newstate(&net->ic,IEEE80211_S_AUTH,0)==0);
        rtw88_net_state_work(&net->state_work,net);
        CHECK(rfk_parent_auth==1);
        CHECK(rfk_owned_io==prior_owned && rfk_trace_len==prior_shots);
        CHECK(!net->policy.accepting && !sc.sc_started);
    }
    /* Enabled experimental AUTH cannot bypass a missing whole-chain receipt. */
    CHECK(rfk_parent_auth == ((rfk_is("positive") || rfk_is("vendor-end") || rfk_is("newfw-baseline") || rfk_is("detach-baseline") || rfk_is("env-channel149"))?1U:0U));
    CHECK(!net->work_pending);
    if(rfk_is("positive") || rfk_is("vendor-end") || rfk_is("newfw-baseline") || rfk_is("detach-baseline") || rfk_is("env-channel149") || rfk_is("zero-init") ||
       rfk_is("c1-spacing") || rfk_is("alloc-cancel") || rfk_is("post-complete-change") ||
       (strncmp(rfk_case,"rf18-",5)==0 || rfk_power_before() ||
        rfk_is("power-partial") || rfk_is("seal-power") || rfk_is("env-successor-scan")))CHECK(!rtw8822c_rfk_blocked());
    else {
        CHECK(rtw8822c_rfk_blocked());
        /* The bus service models one live device. With a valid quarantine,
         * these actual early guards must reject before allocating another.
         * A mutant losing quarantine is already a functional failure above. */
        if(rtw8822c_rfk_blocked()) {
            unsigned prior=rfk_io;
            CHECK(rtw88_start(&sc)==EBUSY);CHECK(rfk_io==prior);
            struct rtw88_softc other;memset(&other,0,sizeof(other));
            other.sc_ops=&rfk_bus;other.sc_bus=&f;
            int attached=rtw88_core_attach(&other);
            CHECK(attached==EBUSY);
            CHECK(rfk_io==prior);CHECK(!rtw8822c_rfk_active(&sc));
            if(attached==0)CHECK(rtw88_core_detach(&other)==0);
        }
    }
    if(rfk_power_before() || rfk_is("power-partial")) {
        CHECK(rfk_handshakes==0 && rfk_gapk==0 && rfk_iqk==0 && rfk_dpk==0);
        CHECK(sc.sc_stats.rfk.operations==0);
        CHECK(sc.sc_stats.rfk.error==(rfk_is("power-partial")?ECANCELED:ESTALE));
        if(rfk_is("power-partial"))CHECK(rfk_fault && !sc.sc_power_program.valid);
        CHECK(!sc.sc_started && !net->policy.accepting);
    }
    if(rfk_is("alloc-Q")) {
        CHECK(rfk_paused && rfk_rf18_writes==0);
        CHECK(sc.sc_power_program.serial==rfk_alloc_power_serial);
        CHECK(rfk_handshakes==0 && rfk_gapk==0 && rfk_iqk==0 && rfk_dpk==0);
        CHECK(sc.sc_stats.rfk.operations==0 && !rtw8822c_rfk_active(&sc));
        CHECK(!net->policy.accepting && !sc.sc_started);
        CHECK(rtw8822c_rfk_blocked());
    }
    if(strncmp(rfk_case,"end-",4)==0) {
        CHECK(rfk_gapk==2 && rfk_iqk==0 && rfk_dpk==0);
        CHECK(!net->policy.accepting && !sc.sc_operating.sealed);
        CHECK(rtw8822c_rfk_blocked());
        if(rfk_is("end-prezero") || rfk_is("end-read-error")) {
            CHECK(rfk_preend_reads>=1 && !rfk_end_sent && rfk_handshakes==1);
            CHECK(sc.sc_stats.rfk.arms[1]==0 && sc.sc_stats.rfk.done[1]==0);
        }
        if(rfk_is("end-held"))CHECK(rfk_end_sent && sc.sc_stats.rfk.done[1]==0);
        if(rfk_is("end-ext-error"))CHECK(rfk_fault && !rfk_end_sent);
        if(rfk_is("end-write-error"))CHECK(rfk_fault && rfk_end_sent);
        if(rfk_is("end-late-report")) {
            CHECK(rfk_end_sent && rfk_handshakes==2);
            CHECK(sc.sc_stats.rfk.error==ETIMEDOUT);
            CHECK(sc.sc_stats.rfk.cleanup_error==0);
            CHECK(sc.sc_stats.rfk.arms[1]==1 && sc.sc_stats.rfk.done[1]==1);
            CHECK(sc.sc_stats.rfk.global==2);
        }
        if(rfk_is("end-cancel") || rfk_is("end-Q") || rfk_is("end-deadline"))CHECK(rfk_paused);
    }
    if(rfk_is("vendor-end")) {
        CHECK(rfk_handshakes==2 && rfk_iqk==1 && rfk_coef==1);
        CHECK(sc.sc_stats.rfk.arms[1]==1 && sc.sc_stats.rfk.done[1]==1);
    }
    if(rfk_is("power-after-bind"))CHECK(rfk_paused && rfk_gapk==1 && rfk_iqk==0);
    if(rfk_is("power-mailbox"))CHECK(rfk_paused && rfk_net->rfk.ran && rfk_coef==1);
    if(strncmp(rfk_case,"env-",4)==0 && !rfk_is("env-channel149") && !rfk_is("env-successor-scan")) {
        CHECK(rfk_coef==1 && !sc.sc_operating.sealed && !sc.sc_operating.prepared);
        CHECK(!sc.sc_started && (net->ec.ec_if.if_flags&IFF_RUNNING)==0);
    }
    if(rfk_is("seal-power") || rfk_is("env-retire-Q")) {
        CHECK(!sc.sc_started && !sc.sc_operating.prepared && !sc.sc_operating.sealed);
        CHECK(!net->policy.accepting && !sc.sc_ra.configured && !sc.sc_ra.active);
        struct ifnet *iface=&net->ec.ec_if;
        struct mbuf *held=ether_packet(0,0,0);
        unsigned crypto=route_crypto;
        uint16_t seq[17];memcpy(seq,net->ic.ic_bss->ni_txseqs,sizeof(seq));
        uint64_t posted[RTW88_NTXQ];memcpy(posted,sc.sc_stats.tx_posted,sizeof(posted));
        queue_packet(&iface->if_snd,held);
        rtw88_net_start(iface);
        CHECK(iface->if_snd.m==held && route_crypto==crypto);
        CHECK(memcmp(seq,net->ic.ic_bss->ni_txseqs,sizeof(seq))==0);
        CHECK(memcmp(posted,sc.sc_stats.tx_posted,sizeof(posted))==0);
        iface->if_snd.m=iface->if_snd.ifq_tail=NULL;iface->if_snd.ifq_len=0;m_freem(held);
    }
    if(rfk_is("env-successor-scan")) {
        CHECK(rfk_parent_auth==0 && rfk_parent_init==0 && rfk_parent_scan==1);
        CHECK(net->requested==IEEE80211_S_SCAN && net->ic.ic_state==IEEE80211_S_SCAN);
        CHECK(net->policy.accepting && sc.sc_started && !sc.sc_operating.sealed);
    }
    if(rfk_is("env-channel149"))CHECK(sc.sc_channel==149 && sc.sc_operating.channel==149);
    if(rfk_is("cancel-gapk") || rfk_is("profile-gapk")) {
        CHECK(rfk_paused==1 && rfk_gapk==1 && rfk_dpk==0 && rfk_iqk==0);
    }
    if(rfk_is("cancel-dpk"))CHECK(rfk_paused==1 && rfk_dpk==1 && rfk_cal_pwr==0 && rfk_coef==0);
    if(rfk_is("cancel-iqk"))CHECK(rfk_paused==1 && rfk_iqk==1 && rfk_dpk==0);
    if(rfk_is("slow-io"))CHECK(rfk_last_owned_tick-started_tick<=8005 && rfk_parent_auth==0);
    if(rfk_is("cleanup-expiry"))CHECK(rfk_paused && rfk_owned_at_cancel==rfk_owned_io);
    if(rfk_is("dying-cleanup"))CHECK(rfk_paused && net->policy.dying && sc.sc_started && sc.sc_resources_ready);
    if(rfk_is("quarantine-complete")) {
        CHECK(rfk_paused && rfk_net->rfk.ran && rfk_coef==1 && rfk_iqk==1);
        CHECK(rfk_rxbb_end_io==rfk_owned_io && rfk_parent_auth==0);
        CHECK(sc.sc_stats.rfk.flags&2);
        CHECK(!net->policy.accepting && !sc.sc_started);
    }
    if(rfk_is("post-complete-change"))CHECK(rfk_paused && rfk_coef==1 && rfk_handshakes==2 && rfk_gapk==2 && rfk_iqk==1);
    if(rfk_is("write-rxbb") || rfk_is("cancel-rxbb") || rfk_is("quarantine-rxbb")) {
        CHECK(rfk_rxbb_active && rfk_rxbb_end_io==rfk_owned_io);
        CHECK((sc.sc_stats.rfk.flags&8)!=0);
        CHECK(rfk_dpk==0 && rfk_coef==0 && rfk_parent_auth==0);
        CHECK(rfk_is("write-rxbb")?rfk_fault==1:rfk_paused==1);
    }
    if(rfk_is("packet-write-error") || rfk_is("packet-read-error")) {
        CHECK(rfk_fault==1 && rfk_iqk==1 && rfk_dpk==0);
        CHECK(sc.sc_packet_h2c.dirty && sc.sc_packet_h2c.error==EIO);
    }
    if(strncmp(rfk_case,"snapshot-",9)==0 || strncmp(rfk_case,"restore-",8)==0 ||
       strncmp(rfk_case,"write-",6)==0)CHECK(rfk_fault==1);
    if(rfk_is("zero-init"))CHECK(rfk_io==before && rfk_handshakes==0 && !sc.sc_rfk);
    if(rfk_is("c1-spacing") || rfk_is("alloc-cancel"))
        CHECK(rfk_paused==1 && rfk_handshakes==0 && rfk_gapk==0 && rfk_iqk==0 && rfk_dpk==0);
    if(strncmp(rfk_case,"rf18-",5)==0) {
        CHECK(rfk_rf18_reads>=1);
        CHECK(rfk_handshakes==0 && rfk_gapk==0 && rfk_iqk==0 && rfk_dpk==0);
        CHECK(sc.sc_stats.rfk.kind==0 && sc.sc_stats.rfk.operations==rfk_rf18_reads);
        CHECK(sc.sc_stats.rfk.error==(rfk_is("rf18-read-error")?EIO:EPROTO));
        CHECK(!net->policy.accepting && !sc.sc_started);
    }
    enable_pause_hook=NULL;enable_drain_pause_hook=NULL;active_ic=NULL;
    rtw88_test_rfk_drop_hook=NULL;
    sc.sc_ops=&ops;
    enable_destroy(net);
    CHECK(enable_allocations == enable_releases);
    if(rfk_is("detach-baseline")) {
        CHECK(rtw8822c_rfk_blocked());
        unsigned prior=rfk_io;
        struct rtw88_softc fresh;reset_bus(&fresh);fresh.sc_ops=&rfk_bus;
        int attached=rtw88_core_attach(&fresh);
        CHECK(attached==EBUSY);
        CHECK(rfk_io==prior && fresh.sc_rfk==NULL);
        if(attached==0)CHECK(rtw88_core_detach(&fresh)==0);
    }
    printf("Native full-chain RFK %s: %u checks, %u failures\n",
        rfk_case, checks, failures);
    return failures != 0 || core_failures != 0;
}
