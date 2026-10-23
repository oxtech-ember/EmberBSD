/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: native full-chain RFK actual-worker contract, AI-assisted. */
/* The existing complete owning attach/worker/core model is reused. This first
 * case supplies NO fresh RFK completion. It must never reach parent AUTH.
 * It is not a fake successful RFK engine or an HT readiness assertion. */
#include "rtw88-rfk-atomic-model.h"
#include <stddef.h>
#define RTW88_ENABLE_ENTRY rfk_existing_enable_main
#include "rtw88-enable-contract.c"
#undef RTW88_ENABLE_ENTRY
/* Kernel wait/registration services remain models. The queued state worker
 * and the separately extracted native stop body execute their real policies. */
static void
workqueue_wait(struct workqueue *queue, struct work *work)
{
    struct rtw88_net *net=(void *)((char *)work-
        offsetof(struct rtw88_net,state_work));
    CHECK(queue==net->wq && !mutex_owned(net->lock));
    if(net->work_pending)rtw88_net_state_work(work,net);
    CHECK(!net->work_pending);
}
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
static unsigned rfk_notice_shots;
static const uint32_t rfk_original_tx=0x12345678,rfk_original_fifo=0xabcdef01;
static int rfk_is(const char *name) { return strcmp(rfk_case,name)==0; }
static int rfk_rx_quiet(void) { return rfk_is("quiet-rx") || rfk_is("quiet-rx-worker"); }
static int rfk_power_before(void) { return strncmp(rfk_case,"power-before-",13)==0; }
static unsigned rfk_precommit_checks;
static void rfk_advance_us(uint64_t);
static void rfk_real_profile_observe(void);
static void rfk_after_owner_drop(void)
{
    if(!rfk_net || !rfk_net->rfk.ran)return;
    if(rfk_is("env-retire-Q"))atomic_or_uint(rtw88_test_rfk_atomic_word,2);
    if(rfk_is("seal-power"))rfk_net->core->sc_power_program.valid=0;
    if(rfk_is("env-successor-scan"))
        CHECK(rtw88_net_newstate(&rfk_net->ic,IEEE80211_S_SCAN,0)==0);
    if(rfk_is("post-setup-age") || rfk_is("post-setup-budget")) {
        CHECK(rfk_fault==1 && !rfk_net->policy.accepting);
        rfk_precommit_checks++;
    }
    if(rfk_is("post-real-provider"))rfk_real_profile_observe();
    if(rfk_is("post-final-budget"))rfk_advance_us(20000);
    if(rfk_is("post-final-age"))rfk_advance_us(716801);
    rtw88_test_rfk_drop_hook=NULL;
}
static unsigned rfk_last_owned_tick,rfk_owned_at_cancel;
static unsigned rfk_post_mailbox_reads,rfk_post_mailbox_pauses;
static unsigned rfk_post_station_commands;
static unsigned rfk_real_observations;
static uint64_t rfk_coef_at_us,rfk_clock_offset,rfk_first_owned_us;
static unsigned rfk_yields,rfk_coef_reads,rfk_coef_target_reads,rfk_clock_wraps,rfk_min_early;
static unsigned rfk_last_write;
static unsigned rfk_previous_clock;
static int rfk_timing(void) { return strncmp(rfk_case,"timing-",7)==0; }
static int rfk_timing_success(void)
{ return rfk_is("timing-good") || rfk_is("timing-edge") ||
    rfk_is("timing-wrap") || rfk_is("timing-minimum"); }
static uint64_t rfk_now_us(void)
{ return (uint64_t)route_ticks*(1000000/rfk_hz)+rfk_extra_us+rfk_clock_offset; }
static void rfk_advance_us(uint64_t us)
{
    unsigned period=1000000/rfk_hz;
    uint64_t total=(uint64_t)rfk_extra_us+us;
    route_ticks+=(unsigned)(total/period);rfk_extra_us=(unsigned)(total%period);
}
static void nanouptime(struct timespec *ts)
{
    uint64_t now=rfk_now_us();
    if((unsigned)now<rfk_previous_clock)rfk_clock_wraps++;
    rfk_previous_clock=(unsigned)now;
    ts->tv_sec=(time_t)(now/1000000);ts->tv_nsec=(long)(now%1000000)*1000;
}
static void rfk_io_tick(void)
{
    rfk_io++;
    if(rtw8822c_rfk_active(f.sc)) {
        if(!rfk_owned_io)rfk_first_owned_us=rfk_now_us();
        rfk_owned_io++;
        rfk_advance_us(rfk_is("slow-io")?5000:1);
        rfk_last_owned_tick=route_ticks;
    }
}
static int rfk_once(const char *name) {
    if(!rfk_fault && rfk_is(name)) { rfk_fault=1;return 1; }return 0;
}
static unsigned rfk_rf18_writes,rfk_rf18_reads,rfk_rxbb_active,rfk_rxbb_end_io;
static uint64_t rfk_alloc_power_serial;
static unsigned rfk_spacing=16,rfk_ready=0x3f;
static int rfk_profile(struct ieee80211com *ic,struct ieee80211_ht20_profile *p)
{ (void)ic;*p=(struct ieee80211_ht20_profile){7,0xff,0xff,(uint8_t)rfk_ready,(uint8_t)rfk_spacing};return 0; }
static void rfk_real_profile_observe(void)
{
    struct ieee80211_ht20_profile profile={0};
    CHECK(rfk_net->ic.ic_ht20_admit==rtw88_net_ht20_admit);
    CHECK(rtw88_net_ht20_admit(&rfk_net->ic,&profile)==EOPNOTSUPP);
    CHECK(profile.ready==0 && !rfk_net->core->sc_operating.sealed);
    if(rfk_net->rfk.ran)CHECK(rtw88_net_rfk_capture_current(rfk_net)==0);
    rfk_real_observations++;
}
static int rfk_c1_pause(unsigned ticks)
{
    (void)ticks;CHECK(rtw8822c_rfk_active(rfk_net->core));
    rfk_spacing=20;rfk_paused++;
    f.tx_hw_rp[0]=rfk_net->core->sc_txq[0].wp;
    return EWOULDBLOCK;
}
static void rfk_alloc_yield(void)
{
    if(rfk_net && !rfk_paused && (rfk_is("allocation-budget") ||
        rfk_is("presence-wait-allocation"))) {
        rfk_paused=1;
        rfk_advance_us(rfk_is("allocation-budget")?5900000:6000001);
    }
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
static unsigned rfk_beacons,rfk_post_beacons;
static uint64_t rfk_beacon_power_serial;
static void rfk_beacon_emit(struct rtw88_net *net)
{
    uint8_t frame[41]={0x80};memset(frame+4,0xff,6);
    memcpy(frame+10,net->ic.ic_bss->ni_macaddr,6);
    memcpy(frame+16,net->ic.ic_bss->ni_bssid,6);
    frame[32]=100;frame[34]=1;frame[38]=1;frame[39]=1;frame[40]=12;
    struct rtw88_rx_info info={.packet_size=sizeof(frame)};
    info.signal.valid=1;info.signal.channel=(uint8_t)net->channel;
    info.signal.generation=net->core->sc_generation;
    mutex_enter(net->lock);
    rtw88_net_rx_frame(net,frame,sizeof(frame),&info);
    mutex_exit(net->lock);
    if(!rfk_beacons)rfk_beacon_power_serial=net->core->sc_power_program.serial;
    rfk_beacons++;
}
static void rfk_beacon_arrival(void)
{
    if(!rfk_net || (rfk_handshakes && !rfk_net->rfk.ran) || !rfk_net->rfk.drained ||
        rfk_rf18_writes<2 || rfk_net->core->sc_channel!=rfk_net->channel ||
        (rfk_is("presence-wait-timeout") || rfk_is("presence-wait-pretune")))return;
    if(rfk_net->rfk.ran) {
        if(rfk_is("post-only-pre") || rfk_post_beacons)return;
        if(rfk_is("post-real-provider"))rfk_real_profile_observe();
        if(rfk_is("post-generation")) {
            rfk_net->core->sc_generation++;rfk_post_beacons++;
            rfk_owned_at_cancel=rfk_owned_io;return;
        }
        if(rfk_is("post-wait-peer")) {
            rfk_net->ic.ic_bss->ni_authmode^=1;rfk_post_beacons++;return;
        }
        if(rfk_is("post-successor")) {
            CHECK(rtw88_net_newstate(&rfk_net->ic,IEEE80211_S_SCAN,0)==0);
            rfk_post_beacons++;return;
        }
        if(rfk_is("post-withdrawal")) {
            uint8_t notice[42]={0x80};memset(notice+4,0xff,6);
            memcpy(notice+10,rfk_net->ic.ic_bss->ni_macaddr,6);
            memcpy(notice+16,rfk_net->ic.ic_bss->ni_bssid,6);
            notice[36]=37;notice[37]=3;notice[38]=1;notice[39]=40;notice[40]=1;
            struct rtw88_rx_info info={.packet_size=41};
            info.signal.valid=1;info.signal.channel=(uint8_t)rfk_net->channel;
            info.signal.generation=rfk_net->core->sc_generation;
            mutex_enter(rfk_net->lock);rtw88_net_rx_frame(rfk_net,notice,41,&info);
            mutex_exit(rfk_net->lock);rfk_post_beacons++;return;
        }
        if(rfk_is("post-setup-budget") || rfk_is("post-final-budget") ||
            rfk_is("post-mailbox-budget")) {
            uint64_t left=rfk_is("post-setup-budget")?1000:10000;
            CHECK(rfk_now_us()+left<rfk_net->rfk.deadline_us);
            rfk_advance_us(rfk_net->rfk.deadline_us-rfk_now_us()-left);
        }
        rfk_beacon_emit(rfk_net);rfk_post_beacons++;return;
    }
    if(!rfk_paused && rfk_is("presence-wait-late")) {
        rfk_paused=1;rfk_advance_us(6000000);
    }
    if(!rfk_paused && rfk_is("presence-wait-rollback")) {
        rfk_paused=1;route_ticks=0;rfk_extra_us=0;return;
    }
    if(rfk_is("presence-wait-cancel")) {
        if(!rfk_paused) {
            rfk_paused=1;
            CHECK(rtw88_net_newstate(&rfk_net->ic,IEEE80211_S_SCAN,0)==0);
        }
        return;
    }
    rfk_beacon_emit(rfk_net);
}

static int rfk_pause(unsigned ticks)
{
    rfk_beacon_arrival();
    if(rfk_net->rfk.ran && rfk_post_mailbox_reads &&
        !rfk_post_mailbox_pauses && (rfk_is("post-mailbox-retry") ||
        rfk_is("post-mailbox-budget"))) {
        rfk_post_mailbox_pauses++;
        if(rfk_is("post-mailbox-budget"))rfk_advance_us(20000);
    }
    CHECK(!mutex_owned(rfk_net->lock));
    if(!rfk_paused && rfk_rx_quiet() && rfk_gapk) {
        uint8_t notice[42]={0x80};
        struct rtw88_rx_info info={0};
        memcpy(notice+4,rfk_net->ic.ic_myaddr,6);
        memcpy(notice+10,rfk_net->ic.ic_bss->ni_macaddr,6);
        memcpy(notice+16,rfk_net->ic.ic_bss->ni_bssid,6);
        notice[36]=60;notice[37]=4;notice[38]=1;
        notice[39]=118;notice[40]=64;notice[41]=1;
        info.packet_size=sizeof(notice);info.signal.valid=1;
        info.signal.channel=(uint8_t)rfk_net->channel;
        info.signal.generation=rfk_net->core->sc_generation;
        CHECK(!rfk_net->policy.accepting);
        /* Invalid envelope/identity observations cannot withdraw this peer. */
        for(unsigned bad=0;bad<16;bad++) {
            uint8_t invalid[sizeof(notice)];memcpy(invalid,notice,sizeof(notice));
            struct rtw88_rx_info corrupt=info;
            if(bad==0)invalid[9]^=2;
            if(bad==1)invalid[15]^=2;
            if(bad==2)invalid[21]^=2;
            if(bad==3)corrupt.signal.channel++;
            if(bad==4)corrupt.signal.valid=0;
            if(bad==5)corrupt.signal.generation++;
            if(bad==6)corrupt.flags=1;
            if(bad==7)corrupt.flags=2;
            if(bad==8)corrupt.flags=16;
            if(bad==9)corrupt.security=1;
            if(bad==10)corrupt.packet_size--;
            if(bad==11)invalid[1]=0x40;
            if(bad==12)invalid[1]=4;
            if(bad==13)invalid[1]=1;
            if(bad==14)invalid[22]=1;
            if(bad==15)invalid[0]=8;
            mutex_enter(rfk_net->lock);
            rtw88_net_rx_frame(rfk_net,invalid,sizeof(invalid),&corrupt);
            CHECK(!rfk_net->rfk.notice_pending && !rfk_net->tx_quiet);
            mutex_exit(rfk_net->lock);
        }
        /* Stale numeric handoffs cannot act on the current owner. */
        for(unsigned stale=0;stale<3;stale++) {
            mutex_enter(rfk_net->lock);
            rtw88_net_rx_frame(rfk_net,notice,sizeof(notice),&info);
            CHECK(rfk_net->rfk.notice_pending);
            if(stale==0)rfk_net->rfk.notice_epoch++;
            if(stale==1)rfk_net->rfk.notice_generation++;
            if(stale==2)rfk_net->rfk.notice_channel++;
            mutex_exit(rfk_net->lock);
            rtw88_net_rfk_rx_deliver(rfk_net);
            CHECK(!rfk_net->tx_quiet && !rfk_net->rfk.notice_pending);
        }
        mutex_enter(rfk_net->lock);
        rtw88_net_rx_frame(rfk_net,notice,sizeof(notice),&info);
        CHECK(rfk_net->rfk.notice_pending &&
            rfk_net->policy.queued==RTW88_NET_RX_LIMIT);
        mutex_exit(rfk_net->lock);
        rfk_notice_shots=rfk_trace_len;
        if(!rfk_is("quiet-rx-worker"))
            rtw88_net_softintr(rfk_net); /* Giant services the scheduled numeric event. */
        rfk_paused=1;rfk_owned_at_cancel=rfk_owned_io;
        if(!rfk_is("quiet-rx-worker")) {
            CHECK(rfk_net->tx_quiet && rfk_net->core->sc_stop_requested);
            CHECK(rfk_net->policy.queued==0);
        } else {
            CHECK(!rfk_net->tx_quiet);
            mutex_enter(rfk_net->lock);
            for(unsigned q=0;q<RTW88_NTXQ;q++)
                CHECK(rtw88_net_tx_capacity(rfk_net,q,0)==0);
            mutex_exit(rfk_net->lock);
        }
    }
    if(!rfk_paused && rfk_is("quiet-rfk") && rfk_gapk) {
        rfk_paused=1;
        rfk_net->ic.ic_sta_quiet(&rfk_net->ic);
        rfk_owned_at_cancel=rfk_owned_io;
        CHECK(rtw88_net_newstate(&rfk_net->ic,IEEE80211_S_INIT,-1)==0);
    }
    if(rfk_is("timing-minimum") && ticks && !rtw8822c_rfk_active(f.sc)) {
        if(!rfk_min_early) { CHECK(ticks==2);route_ticks--;rfk_min_early=1; }
        else { CHECK(ticks==1);rfk_min_early=2; }
    }
    if(!rfk_paused && rfk_coef && (rfk_is("timing-cancel") ||
       rfk_is("timing-Q") || rfk_is("timing-stall") || rfk_is("timing-long-stall"))) {
        rfk_paused=1;rfk_owned_at_cancel=rfk_owned_io;
        if(rfk_is("timing-cancel"))
            CHECK(rtw88_net_newstate(&rfk_net->ic,IEEE80211_S_SCAN,0)==0);
        else if(rfk_is("timing-Q"))atomic_or_uint(rtw88_test_rfk_atomic_word,2);
        else rfk_advance_us(rfk_is("timing-long-stall")?(UINT64_C(1)<<32)+2000:20000);
    }
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
/* yield is a scheduler service. The actual worker owns minimum-time and
 * input checks. Each service return advances two microseconds, not a tick. */
static void yield(void)
{
    rfk_yields++;
    rfk_advance_us(2);
    (void)rfk_pause(0);
}
static void rfk_delay_bus(void *cookie,unsigned us)
{
    delay_us(cookie,us);
    rfk_advance_us(us);
}
static void rfk_event(unsigned event)
{ CHECK(rfk_trace_len<128);if(rfk_trace_len<128)rfk_trace[rfk_trace_len++]=event; }
static int rfk_write4(void *cookie,uint32_t reg,uint32_t value)
{
    uint32_t old=mmio32(reg);
    unsigned previous_write=rfk_last_write;
    rfk_last_write=reg;
    rfk_io_tick();
    int error=write_4(cookie,reg,value);
    if(!error && reg==0x3a0c && rtw8822c_rfk_active(f.sc) &&
       !rfk_handshakes && rfk_once("power-partial"))return EIO;
    if(error)return error;
    if(rfk_net && rfk_net->rfk.ran && f.sc->sc_mailbox.active &&
        reg>=0x1d0 && reg<=0x1dc && (reg-0x1d0)%4==0 &&
        (value&0xff)==1) {
        rfk_post_station_commands++;
        if(!rfk_fault && (rfk_is("post-setup-budget") || rfk_is("post-setup-age"))) {
            rfk_fault=1;
            rfk_advance_us(rfk_is("post-setup-budget")?2000:716801);
        }
    }
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
            rfk_end_at_us=rfk_now_us();
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
        if(value==0x1149) {
            rfk_coef++;rfk_report_reads=0;rfk_event(50);
            rfk_coef_at_us=rfk_now_us();
        }
        if(value==0x1449 || value==0x1549 || value==0x1649 ||
           value==0x1749 || value==0x1a49 || value==0x1b49 ||
           value==0x1c49 || value==0x1d49) { rfk_dpk++;rfk_report_reads=0;rfk_event(6); }
    }
    /* The final coefficient command follows a plain RXSRAM write after
     * both CAL_PWR operations. Exclude earlier DPK read/modify/writes. */
    if(rfk_cal_pwr==2 && !rfk_coef && previous_write==0x1b00 &&
       reg==0x1bd4 && value==0xf0) {
        if(rfk_is("timing-edge")) {
            unsigned period=1000000/rfk_hz;
            rfk_advance_us((2*period-5-(unsigned)(rfk_now_us()%period))%period);
        }
        if(rfk_once("timing-new-expiry")) {
            uint64_t due=rfk_first_owned_us+5997000;
            CHECK(rfk_now_us()<due);
            rfk_advance_us((unsigned)(due-rfk_now_us()));
        }
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
    rfk_last_write=0;
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
        if(rfk_coef && rfk_timing()) {
            uint64_t elapsed=rfk_now_us()-rfk_coef_at_us;
            unsigned ready=rfk_is("timing-late")?10000:
                rfk_is("timing-new-expiry")?1000:5000;
            rfk_coef_reads++;
            *value=rfk_is("timing-stale")?0x55:
                !rfk_is("timing-none") && elapsed>=ready?0x55:0x10000;
            if(*value==0x55)rfk_coef_target_reads++;
            if(*value==0x55 && rfk_is("timing-read-expiry"))rfk_advance_us(10000);
            if(*value==0x55 && rfk_is("timing-new-expiry"))rfk_advance_us(2500);
            return 0;
        }
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
    if(reg==0x1cc && rfk_net && rfk_net->rfk.ran && rfk_post_beacons) {
        if((rfk_is("post-provider") || rfk_is("post-provider-ready")) && !rfk_fault) {
            rfk_fault=1;
            if(rfk_is("post-provider-ready"))rfk_ready=0x1f;
            else rfk_spacing=20;
        }
        if((rfk_is("post-mailbox-retry") || rfk_is("post-mailbox-budget")) &&
            !rfk_post_mailbox_reads++) {
            *value=(uint8_t)(1U<<(f.sc->sc_h2c_box&3));return 0;
        }
    }
    rfk_io_tick();
    if(reg==0x1cc && rfk_coef && !rfk_paused &&
       (rfk_is("post-complete-change") || rfk_is("quarantine-complete") || rfk_is("power-mailbox"))) {
        *value=(uint8_t)(1U<<(f.sc->sc_h2c_box&3));return 0;
    }
    if(rfk_positive && rtw8822c_rfk_active(f.sc) && reg==0x49c && rfk_handshakes) {
        if(rfk_end_sent) {
            uint64_t now=rfk_now_us();
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


/* Interrupt service hooks exercise the gap after preflight and the final
 * worker lock. They do not replace any production decision or body. */
static struct rtw88_net *rfk_gate_net;
static unsigned rfk_gate_fired;
static void
rfk_race_notice(struct rtw88_net *net)
{
    uint8_t frame[42]={0x80};
    struct rtw88_rx_info info={0};
    memcpy(frame+4,net->ic.ic_myaddr,6);
    memcpy(frame+10,net->ic.ic_bss->ni_macaddr,6);
    memcpy(frame+16,net->ic.ic_bss->ni_bssid,6);
    frame[36]=60;frame[37]=4;frame[38]=1;
    frame[39]=118;frame[40]=64;frame[41]=1;
    info.packet_size=sizeof(frame);info.signal.valid=1;
    info.signal.channel=(uint8_t)net->core->sc_channel;
    info.signal.generation=net->core->sc_generation;
    rtw88_net_rx_frame(net,frame,sizeof(frame),&info);
    CHECK(net->rfk.notice_pending);
}
static void
rfk_race_enter(kmutex_t *lock)
{
    if(rfk_gate_net && lock==rfk_gate_net->lock && !rfk_gate_fired &&
        rfk_gate_net->core->sc_mailbox.active) {
        rfk_gate_fired=1;rfk_race_notice(rfk_gate_net);
    }
}
static void
rfk_race_exit(kmutex_t *lock)
{
    if(rfk_gate_net && lock==rfk_gate_net->lock && !rfk_gate_fired &&
        rfk_gate_net->reset_required && rfk_gate_net->core->sc_started &&
        !(rfk_gate_net->ec.ec_if.if_flags&IFF_RUNNING)) {
        rfk_gate_fired=1;rfk_race_notice(rfk_gate_net);
    }
}
static int
rfk_control_races(struct rtw88_net *initial)
{
    enable_destroy(initial);
    for(unsigned kind=0;kind<9;kind++) {
        struct rtw88_softc sc;kmutex_t lock={.initialized=1};
        struct rtw88_net *net=enable_attach(&sc,&lock);
        CHECK(net!=NULL);if(!net)return 1;
        struct ieee80211com *ic=&net->ic;active_ic=ic;
        struct mbuf *m=enable_response(ic,1,0);
        ieee80211_recv_mgmt_assoc_resp(ic,m,ic->ic_bss,
            IEEE80211_FC0_SUBTYPE_ASSOC_RESP,0,0);
        m_freem(m);
        CHECK(net->requested==IEEE80211_S_RUN);
        if(kind!=5) {
            rtw88_net_state_work(&net->state_work,net);
            CHECK(ic->ic_state==IEEE80211_S_RUN && net->policy.accepting);
        }
        unsigned runs=enable_run_callbacks;
        if(kind<3) {
            unsigned q=kind==0?RTW88_TXQ_MGMT:RTW88_TXQ_BE;
            uint8_t frame[24]={8};frame[4]=kind==1?1:2;
            if(kind==0)frame[0]=0xb0;
            unsigned queued=sc.sc_txq[q].queued,wp=sc.sc_txq[q].wp;
            mutex_enter(&lock);
            CHECK(rtw88_net_tx_capacity(net,q,0));
            rfk_race_notice(net);CHECK(!net->tx_quiet);
            CHECK(rtw88_net_tx_submit(net,ic->ic_bss,q,frame,sizeof(frame))==EBUSY);
            CHECK(sc.sc_txq[q].queued==queued && sc.sc_txq[q].wp==wp);
            mutex_exit(&lock);
        } else if(kind<5) {
            mutex_enter(&lock);rfk_race_notice(net);mutex_exit(&lock);
            CHECK(rtw88_net_newstate(ic,kind==3?IEEE80211_S_ASSOC:IEEE80211_S_RUN,0)==EBUSY);
            CHECK(net->tx_quiet && !net->rfk.notice_pending);
        } else {
            struct rtw88_bus_ops bus=ops;
            if(kind>=6)CHECK(rtw88_net_newstate(ic,IEEE80211_S_RUN,0)==0);
            unsigned loads=f.firmware_loads;
            rfk_gate_net=net;rfk_gate_fired=0;
            if(kind==5) {
                mailbox_writes=0;bus.write_4=mailbox_write4;sc.sc_ops=&bus;
                rfk_mutex_hook=rfk_race_enter;
            } else {
                rfk_unlock_hook=rfk_race_exit;
                if(kind==7)f.off_error=1;
                if(kind==8)f.disable_error=1;
            }
            rtw88_net_state_work(&net->state_work,net);
            rfk_mutex_hook=NULL;rfk_unlock_hook=NULL;rfk_gate_net=NULL;
            CHECK(rfk_gate_fired && net->tx_quiet && !sc.sc_started);
            CHECK(enable_run_callbacks==runs && !sc.sc_mailbox.active);
            if(kind==5)CHECK(mailbox_writes==0);
            else CHECK(f.firmware_loads==loads);
            sc.sc_ops=&ops;
            f.off_error=f.disable_error=0;
        }
        rtw88_net_softintr(net);
        rtw88_net_state_work(&net->state_work,net);
        CHECK(net->tx_quiet && !net->policy.accepting && !sc.sc_started);
        CHECK(net->requested==IEEE80211_S_INIT && !net->rfk.notice_pending);
        enable_destroy(net);
    }
    printf("Native RFK control races: %u checks, %u failures\n",checks,failures);
    return failures||core_failures;
}

static void
rfk_presence_seed(struct rtw88_net *net, uint64_t observed)
{
    net->rfk.beacon_epoch=net->policy.epoch;
    net->rfk.beacon_generation=net->core->sc_generation;
    net->rfk.beacon_channel=64;net->rfk.beacon_interval=100;
    net->rfk.beacon_us=observed;
}
static int
rfk_presence_current(struct rtw88_net *net)
{
    const uint64_t now=(UINT64_C(1)<<32)+10000000;
    net->requested=IEEE80211_S_AUTH;
    net->rfk.epoch=net->rfk.control_epoch=net->policy.epoch;
    net->rfk.generation=net->rfk.control_generation=net->core->sc_generation;
    net->rfk.channel=net->rfk.control_channel=net->core->sc_channel=64;
    rfk_presence_seed(net,now);
    mutex_enter(net->lock);
    CHECK(rtw88_net_rfk_beacon_current(net,now));
    CHECK(rtw88_net_rfk_beacon_current(net,now+716799));
    CHECK(!rtw88_net_rfk_beacon_current(net,now+716800));
    CHECK(!rtw88_net_rfk_beacon_current(net,now+716801));
    CHECK(!rtw88_net_rfk_beacon_current(net,now-1));
    net->rfk.beacon_interval=25;
    CHECK(rtw88_net_rfk_beacon_current(net,now+179199));
    CHECK(!rtw88_net_rfk_beacon_current(net,now+179200));
    net->rfk.beacon_interval=1000;
    CHECK(rtw88_net_rfk_beacon_current(net,now+7167999));
    CHECK(!rtw88_net_rfk_beacon_current(net,now+7168000));
    net->rfk.beacon_interval=24;CHECK(!rtw88_net_rfk_beacon_current(net,now));
    net->rfk.beacon_interval=1001;CHECK(!rtw88_net_rfk_beacon_current(net,now));
    net->rfk.beacon_interval=100;net->rfk.beacon_us=UINT64_MAX-716799;
    CHECK(rtw88_net_rfk_beacon_current(net,UINT64_MAX));
    net->rfk.beacon_us=now;
#define RFK_BAD_FIELD(field) do { (field)++; \
    CHECK(!rtw88_net_rfk_beacon_current(net,now)); (field)--; } while(0)
    RFK_BAD_FIELD(net->rfk.beacon_epoch);RFK_BAD_FIELD(net->rfk.epoch);
    RFK_BAD_FIELD(net->rfk.control_epoch);RFK_BAD_FIELD(net->policy.epoch);
    RFK_BAD_FIELD(net->rfk.beacon_generation);RFK_BAD_FIELD(net->rfk.generation);
    RFK_BAD_FIELD(net->rfk.control_generation);RFK_BAD_FIELD(net->core->sc_generation);
    RFK_BAD_FIELD(net->rfk.beacon_channel);RFK_BAD_FIELD(net->rfk.channel);
    RFK_BAD_FIELD(net->rfk.control_channel);RFK_BAD_FIELD(net->core->sc_channel);
    RFK_BAD_FIELD(net->rfk.notice_pending);RFK_BAD_FIELD(net->policy.dying);
    RFK_BAD_FIELD(net->tx_quiet);RFK_BAD_FIELD(net->core->sc_stop_requested);
#undef RFK_BAD_FIELD
    net->rfk.beacon_epoch=0;CHECK(!rtw88_net_rfk_beacon_current(net,now));
    net->rfk.beacon_epoch=net->policy.epoch;
    net->core->sc_started=0;CHECK(!rtw88_net_rfk_beacon_current(net,now));
    net->core->sc_started=1;
    net->core->sc_running=0;CHECK(!rtw88_net_rfk_beacon_current(net,now));
    net->core->sc_running=1;
    net->core->sc_resources_ready=0;CHECK(!rtw88_net_rfk_beacon_current(net,now));
    net->core->sc_resources_ready=1;
    net->requested=IEEE80211_S_ASSOC;CHECK(!rtw88_net_rfk_beacon_current(net,now));
    net->requested=IEEE80211_S_AUTH;
    mutex_exit(net->lock);
    enable_destroy(net);CHECK(enable_allocations==enable_releases);
    printf("Native RFK current beacon: %u checks, %u failures\n",checks,failures);
    return failures!=0 || core_failures!=0;
}
static struct rtw88_net *rfk_presence_refresh_net;
static unsigned rfk_presence_refresh_hits;
static void
rfk_presence_refresh_unlock(kmutex_t *lock)
{
    struct rtw88_net *net=rfk_presence_refresh_net;
    if(net && lock==net->lock &&
        net->rfk.control_generation==net->core->sc_generation &&
        net->rfk.control_epoch==net->policy.epoch) {
        rfk_presence_refresh_hits++;
        CHECK(net->rfk.beacon_epoch==0);
        rfk_unlock_hook=NULL;
    }
}
static int
rfk_presence_observe(struct rtw88_net *net)
{
    struct ieee80211com *ic=&net->ic;active_ic=ic;
    struct ieee80211_node *ni=ic->ic_bss;
    /* Model a current numeric watch without advertising a DFS capability. */
    ic->ic_channels[64].ic_freq=5320;
    ni->ni_chan=ic->ic_curchan=&ic->ic_channels[64];
    net->channel=net->core->sc_channel=64;
    net->rfk.control_epoch=net->policy.epoch;
    net->rfk.control_generation=net->core->sc_generation;
    net->rfk.control_channel=64;
    memcpy(net->rfk.control_ra,ic->ic_myaddr,6);
    memcpy(net->rfk.control_ta,ni->ni_macaddr,6);
    memcpy(net->rfk.control_bssid,ni->ni_bssid,6);
    uint8_t frame[65]={0x80};memset(frame+4,0xff,6);
    memcpy(frame+10,ni->ni_macaddr,6);memcpy(frame+16,ni->ni_bssid,6);
    frame[32]=100;frame[34]=1;frame[38]=1;frame[39]=1;frame[40]=12;
    struct rtw88_rx_info info={.packet_size=41};
    info.signal.valid=1;info.signal.channel=64;
    info.signal.generation=net->core->sc_generation;
    unsigned io=rfk_io,writes=f.writes;
    net->policy.accepting=0;
    rfk_clock_offset+=(uint64_t)UINT32_MAX+1000000;
    rfk_advance_us(37);uint64_t expected=rtw88_net_rfk_ticks(net);
    mutex_enter(net->lock);rtw88_net_rx_frame(net,frame,41,&info);mutex_exit(net->lock);
    CHECK(net->policy.queued==0 && !net->policy.accepting);
    CHECK(net->rfk.beacon_epoch==net->policy.epoch &&
        net->rfk.beacon_generation==net->core->sc_generation &&
        net->rfk.beacon_channel==64 && net->rfk.beacon_interval==100 &&
        net->rfk.beacon_us==expected);
    /* Fill the actual admission queue, then observe before its full gate. */
    uint8_t data[24]={8,2};struct rtw88_rx_info data_info={.packet_size=24};
    net->policy.accepting=1;
    mutex_enter(net->lock);
    for(unsigned i=0;i<RTW88_NET_RX_LIMIT;i++)
        rtw88_net_rx_frame(net,data,sizeof(data),&data_info);
    mutex_exit(net->lock);CHECK(net->policy.queued==RTW88_NET_RX_LIMIT);
    rfk_advance_us(53);expected=rtw88_net_rfk_ticks(net);
    mutex_enter(net->lock);rtw88_net_rx_frame(net,frame,41,&info);mutex_exit(net->lock);
    CHECK(net->policy.queued==RTW88_NET_RX_LIMIT);
    CHECK(net->rfk.beacon_epoch==net->policy.epoch &&
        net->rfk.beacon_generation==net->core->sc_generation &&
        net->rfk.beacon_channel==64 && net->rfk.beacon_interval==100 &&
        net->rfk.beacon_us==expected);
    /* Each bad observation must retain the preceding numeric proof exactly. */
    for(unsigned bad=0;bad<23;bad++) {
        uint8_t invalid[sizeof(frame)];memcpy(invalid,frame,sizeof(frame));
        struct rtw88_rx_info corrupt=info;size_t len=41;
        if(bad==0)invalid[0]=8;
        if(bad==1)memcpy(invalid+4,ic->ic_myaddr,6);
        if(bad==2)invalid[4]=1;
        if(bad==3)invalid[10]^=2;
        if(bad==4)invalid[16]^=2;
        if(bad==5)invalid[32]=24;
        if(bad==6){invalid[32]=233;invalid[33]=3;}
        if(bad==7)invalid[36]=255;
        if(bad==8)invalid[38]=255;
        if(bad==9){invalid[41]=3;invalid[42]=1;invalid[43]=36;len=44;corrupt.packet_size=44;}
        if(bad==10)corrupt.signal.valid=0;
        if(bad==11)corrupt.signal.channel=36;
        if(bad==12)corrupt.signal.generation++;
        if(bad==13)corrupt.security=1;
        if(bad==14)corrupt.flags=1;
        if(bad==15)corrupt.flags=16;
        if(bad==16)invalid[1]=0x40;
        if(bad==17)invalid[1]=4;
        if(bad==18)invalid[22]=1;
        if(bad==19)invalid[1]=0x80;
        if(bad==20)invalid[34]=0;
        if(bad==21)invalid[34]=3;
        if(bad==22){invalid[41]=61;invalid[42]=22;invalid[43]=36;len=65;corrupt.packet_size=65;}
        struct rtw88_net_rfk previous=net->rfk;
        rfk_advance_us(71);
        mutex_enter(net->lock);rtw88_net_rx_frame(net,invalid,len,&corrupt);mutex_exit(net->lock);
        CHECK(net->rfk.beacon_epoch==previous.beacon_epoch &&
            net->rfk.beacon_generation==previous.beacon_generation &&
            net->rfk.beacon_channel==previous.beacon_channel &&
            net->rfk.beacon_interval==previous.beacon_interval &&
            net->rfk.beacon_us==previous.beacon_us);
    }
    static const unsigned intervals[]={25,1000};
    for(unsigned i=0;i<sizeof(intervals)/sizeof(intervals[0]);i++) {
        frame[32]=(uint8_t)intervals[i];frame[33]=(uint8_t)(intervals[i]>>8);
        rfk_advance_us(17);expected=rtw88_net_rfk_ticks(net);
        mutex_enter(net->lock);rtw88_net_rx_frame(net,frame,41,&info);mutex_exit(net->lock);
        CHECK(net->rfk.beacon_epoch==net->policy.epoch &&
            net->rfk.beacon_interval==intervals[i] && net->rfk.beacon_us==expected);
    }
    frame[32]=100;frame[33]=0;
    /* A current AP withdrawal removes even an existing numeric observation. */
    static const struct {
        size_t len;
        uint8_t data[8];
    } withdrawals[]={
        {5,{37,3,1,64,1}}, {8,{40,6,1,1,1,0,0,0}},
        {6,{60,4,1,118,64,1}}
    };
    for(unsigned quiet=0;quiet<sizeof(withdrawals)/sizeof(withdrawals[0]);quiet++) {
        uint8_t notice[sizeof(frame)];memcpy(notice,frame,sizeof(frame));
        size_t len=41+withdrawals[quiet].len;
        memcpy(notice+41,withdrawals[quiet].data,withdrawals[quiet].len);
        struct rtw88_rx_info notice_info=info;notice_info.packet_size=len;
        rfk_presence_seed(net,rtw88_net_rfk_ticks(net));
        net->rfk.notice_pending=0;
        mutex_enter(net->lock);rtw88_net_rx_frame(net,notice,len,&notice_info);mutex_exit(net->lock);
        CHECK(net->rfk.notice_pending && net->rfk.beacon_epoch==0);
        rfk_advance_us(19);
        mutex_enter(net->lock);rtw88_net_rx_frame(net,frame,41,&info);mutex_exit(net->lock);
        CHECK(net->rfk.notice_pending && net->rfk.beacon_epoch==0);
    }
    CHECK(rfk_io==io && f.writes==writes);
    net->rfk.notice_pending=0;
    rfk_presence_seed(net,rtw88_net_rfk_ticks(net));
    CHECK(rtw88_net_newstate(ic,IEEE80211_S_ASSOC,0)==0);
    CHECK(net->channel==64 && net->rfk.beacon_epoch==0);
    /* Check the actual worker refresh boundary, before later failures or
     * state requests can clear the seeded observation for another reason. */
    CHECK(rtw88_net_newstate(ic,IEEE80211_S_ASSOC,0)==0);
    rfk_presence_seed(net,rtw88_net_rfk_ticks(net));
    CHECK(net->rfk.beacon_epoch!=0 && net->work_pending);
    net->core->sc_generation++;
    CHECK(net->rfk.control_generation!=net->core->sc_generation);
    rfk_presence_refresh_net=net;rfk_presence_refresh_hits=0;
    rfk_unlock_hook=rfk_presence_refresh_unlock;
    rtw88_net_state_work(&net->state_work,net);
    rfk_unlock_hook=NULL;rfk_presence_refresh_net=NULL;
    CHECK(rfk_presence_refresh_hits==1);
    /* Drain actual queued RX before the native stop; only wait/callout and
     * registration services are modeled, not the stop's policy decisions. */
    net->policy.accepting=0;rtw88_net_softintr(net);
    CHECK(net->policy.queued==0 && node_refs==1);
    rfk_presence_seed(net,rtw88_net_rfk_ticks(net));
    unit_rfk_native_stop(&net->ec.ec_if,1);
    CHECK(net->rfk.beacon_epoch==0 && net->requested==IEEE80211_S_INIT);
    CHECK(!net->work_pending && !net->core->sc_started);
    enable_destroy(net);
    printf("Native RFK presence observation: %u checks, %u failures\n",checks,failures);
    return failures||core_failures;
}

int
main(void)
{
    /* Keep service symbols referenced when a causal control links this
     * harness to the old tick-based worker, without disabling warnings. */
    (void)nanouptime;(void)yield;
    struct rtw88_softc sc;
    kmutex_t lock = {.initialized = 1};
    struct rtw88_net *net = enable_attach(&sc, &lock);
    if (net == NULL)
        return 1;
    rfk_case=getenv("RTW88_RFK_CASE");
    if(rfk_case==NULL)rfk_case="none";
    if(rfk_is("presence-observe"))return rfk_presence_observe(net);
    if(rfk_is("presence-current"))return rfk_presence_current(net);
    if(rfk_is("control-races"))return rfk_control_races(net);
    rfk_positive=!rfk_is("none");
    if(rfk_timing())rfk_hz=100;
    rfk_net=net;active_ic=&net->ic;enable_pause_hook=rfk_pause;
    rtw88_test_rfk_drop_hook=rfk_after_owner_drop;
    CHECK(write_4(&f,0x520,rfk_original_tx)==0);
    CHECK(write_4(&f,0x1e70,rfk_original_fifo)==0);
    struct rtw88_bus_ops rfk_bus=ops;
    rfk_bus.write_4=rfk_write4;rfk_bus.read_4=rfk_read4;
    rfk_bus.write_2=rfk_write2;rfk_bus.read_1=rfk_read1;
    rfk_bus.delay_us=rfk_delay_bus;rfk_bus.read_2=rfk_read2;rfk_bus.write_1=rfk_write1;
    sc.sc_ops=&rfk_bus;
    if(rfk_rx_quiet()) {
        uint8_t data[24]={8,2};struct rtw88_rx_info info={.packet_size=24};
        mutex_enter(net->lock);
        for(unsigned i=0;i<RTW88_NET_RX_LIMIT;i++)
            rtw88_net_rx_frame(net,data,sizeof(data),&info);
        CHECK(net->policy.queued==RTW88_NET_RX_LIMIT);
        mutex_exit(net->lock);
    }
    if(rfk_is("env-tmac"))CHECK(write_4(&f,0x1d08,1)==0);
    if(rfk_is("presence-wait-good")) {
        net->ic.ic_channels[64]=net->ic.ic_channels[36];
        net->ic.ic_channels[64].ic_freq=5320;
        net->ic.ic_curchan=&net->ic.ic_channels[64];
        net->ic.ic_bss->ni_chan=net->ic.ic_curchan;
        CHECK(sc.sc_channel!=64);
    }
    if(rfk_is("env-channel149")) {
        CHECK(rtw88_channel(&sc,1)==0);
        net->ic.ic_curchan=&net->ic.ic_channels[149];
        net->ic.ic_bss->ni_chan=net->ic.ic_curchan;
        CHECK(sc.sc_channel==1 && sc.sc_power_program.channel==1);
    }
    net->ic.ic_state = IEEE80211_S_SCAN;
    net->parent_state = rfk_parent_without_completion;
    if(rfk_is("post-provider") || rfk_is("post-provider-ready"))
        net->ic.ic_ht20_admit=rfk_profile;
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
    if(rfk_is("post-real-provider"))rfk_real_profile_observe();
    if(rfk_is("timing-minimum")) {
        static const unsigned intervals[]={10,600,20000};
        CHECK(rtw88_net_rfk_capture(net,net->rfk.values)==0);
        for(unsigned i=0;i<3;i++) {
            uint64_t start=rfk_now_us();
            CHECK(rtw88_net_rfk_wait(net,intervals[i],0)==0);
            CHECK(rfk_now_us()-start==intervals[i]);
        }
        CHECK(rfk_min_early==2);
    }
    /* Place this deterministic bus sequence across the 32-bit boundary.
     * The assertions below require that the coefficient window crosses it. */
    if(rfk_is("timing-wrap"))rfk_clock_offset=UINT_MAX-205000-rfk_now_us();
    if(rfk_is("presence-wait-pretune")) {
        rfk_beacon_emit(net);
        CHECK(net->rfk.beacon_epoch==net->policy.epoch);
    }
    uint64_t presence_deadline=0;
    if(rfk_is("presence-wait-budget")) {
        CHECK(rtw88_net_rfk_begin(net)==0);
        presence_deadline=net->rfk.deadline_us;
        rfk_advance_us(5990000);
    }
    if(rfk_is("presence-wait-overflow"))
        rfk_clock_offset=UINT64_MAX-5000000-rfk_now_us();
    unsigned before=rfk_io,started_tick=route_ticks;
    rtw88_net_state_work(&net->state_work, net);
    if(rfk_is("quiet-rx-worker")) {
        rtw88_net_softintr(net);
        CHECK(net->policy.queued==0);
    }
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
    if(strncmp(rfk_case,"post-",5)==0 && !rfk_is("post-complete-change")) {
        CHECK(rfk_handshakes==2 && rfk_coef==1);
        CHECK(!rtw8822c_rfk_active(&sc) && !rtw8822c_rfk_blocked());
        if(rfk_is("post-good") || rfk_is("post-mailbox-retry") ||
            rfk_is("post-real-provider")) {
            CHECK(rfk_parent_auth==1 && rfk_post_beacons==1);
            CHECK(sc.sc_operating.sealed && net->policy.accepting);
            if(rfk_is("post-mailbox-retry"))CHECK(rfk_post_mailbox_pauses==1);
            if(rfk_is("post-real-provider"))CHECK(rfk_real_observations==3);
        } else if(rfk_is("post-successor")) {
            CHECK(rfk_parent_auth==0 && rfk_parent_scan==1);
            CHECK(net->requested==IEEE80211_S_SCAN && net->policy.accepting);
            CHECK(sc.sc_started && !sc.sc_operating.sealed);
        } else {
            CHECK(rfk_parent_auth==0 && !net->policy.accepting);
            CHECK(!sc.sc_started && !sc.sc_operating.sealed);
            if(rfk_is("post-setup-age") || rfk_is("post-setup-budget"))
                CHECK(rfk_precommit_checks==1);
            if(rfk_is("post-setup-age") || rfk_is("post-final-age")) {
                CHECK(rfk_now_us()<net->rfk.deadline_us);
                CHECK(rfk_now_us()-net->rfk.beacon_us>=716800);
            }
            if(rfk_is("post-setup-budget") || rfk_is("post-final-budget") ||
                rfk_is("post-mailbox-budget")) {
                CHECK(rfk_now_us()>=net->rfk.deadline_us);
                CHECK(rfk_now_us()-net->rfk.beacon_us<716800);
            }
            if(rfk_is("post-provider"))CHECK(rfk_fault && rfk_spacing==20);
            if(rfk_is("post-provider-ready"))CHECK(rfk_fault && rfk_ready==0x1f);
            if(rfk_is("post-withdrawal"))CHECK(net->tx_quiet);
            if(rfk_is("post-generation")) {
                CHECK(rfk_owned_at_cancel==rfk_owned_io);
                CHECK(rfk_post_station_commands==0);
            }
        }
        goto finish;
    }
    if(strncmp(rfk_case,"presence-wait-",14)==0) {
        if(rfk_is("presence-wait-good")) {
            CHECK(rfk_beacons>0 && rfk_parent_auth==1);
            CHECK(rfk_handshakes==2 && sc.sc_operating.sealed);
            CHECK(sc.sc_channel==64 && net->rfk.beacon_channel==64);
            CHECK(sc.sc_power_program.serial==rfk_beacon_power_serial);
        } else {
            CHECK(rfk_rf18_writes==(rfk_is("presence-wait-overflow")?0U:2U));
            CHECK(rfk_parent_auth==0 && rfk_handshakes==0);
            CHECK(rfk_gapk==0 && rfk_iqk==0 && rfk_dpk==0);
            CHECK(!rtw8822c_rfk_active(&sc) && !rtw8822c_rfk_blocked());
            if(rfk_is("presence-wait-timeout") || rfk_is("presence-wait-pretune")) {
                CHECK(rfk_now_us()-rfk_first_owned_us>=6000000);
                CHECK(rfk_now_us()-rfk_first_owned_us<6010000);
                CHECK(!net->policy.accepting && !sc.sc_started);
            } else if(rfk_is("presence-wait-budget")) {
                CHECK(net->rfk.deadline_us==presence_deadline);
                CHECK(net->rfk.deadline_us-net->rfk.started_us==6000000);
                CHECK(rfk_now_us()>=presence_deadline);
            } else if(rfk_is("presence-wait-overflow")) {
                CHECK(!net->policy.accepting && !sc.sc_started);
            } else if(rfk_is("presence-wait-cancel"))
                CHECK(rfk_paused && rfk_parent_scan==1);
            else CHECK(rfk_paused && !net->policy.accepting && !sc.sc_started);
        }
        goto finish;
    }
    /* Enabled experimental AUTH cannot bypass a missing whole-chain receipt. */
    CHECK(rfk_parent_auth == ((rfk_timing_success() || rfk_is("positive") || rfk_is("vendor-end") || rfk_is("newfw-baseline") || rfk_is("detach-baseline") || rfk_is("env-channel149"))?1U:0U));
    CHECK(!net->work_pending);
    if(rfk_timing_success() || rfk_is("positive") || rfk_is("vendor-end") || rfk_is("newfw-baseline") || rfk_is("detach-baseline") || rfk_is("env-channel149") || rfk_is("zero-init") ||
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
    if(rfk_timing()) {
        const struct rtw88_rfk_diag *d=&sc.sc_stats.rfk;
        CHECK(rfk_hz==100 && rfk_yields>0 && rfk_coef==1);
        unsigned completed=rfk_timing_success() || rfk_is("timing-cancel") ||
            rfk_is("timing-Q") || rfk_is("timing-new-expiry");
        CHECK(d->arms[6]==1 && d->done[6]==completed);
        printf("RFK timing %s: first=%llu coef=%llu yields=%u reads=%u error=%d/%d\n",
            rfk_case,(unsigned long long)rfk_first_owned_us,
            (unsigned long long)rfk_coef_at_us,rfk_yields,rfk_coef_reads,
            d->error,d->cleanup_error);
        if(rfk_timing_success()) {
            CHECK(d->error==0 && d->cleanup_error==0 && d->global==0);
            CHECK(d->raw==0x55 && d->non_target && rfk_coef_reads>2);
        } else {
            CHECK(d->global==2 && !net->policy.accepting);
            if(rfk_is("timing-cancel"))CHECK(d->error==EINVAL);
            else if(rfk_is("timing-Q"))CHECK(d->error==EBUSY);
            else CHECK(d->error==ETIMEDOUT);
        }
        if(rfk_is("timing-cancel") || rfk_is("timing-Q")) {
            /* Cancellation withdraws new work. The old owner may still
             * observe the already-triggered coefficient completion. */
            CHECK(rfk_paused && rfk_coef_reads>2 && d->cleanup_error==0);
            CHECK(d->token==1 && rfk_trace[rfk_trace_len-1]==50);
            CHECK(!rfk_parent_auth && (d->flags&2)==0);
        }
        if(rfk_is("timing-stall") || rfk_is("timing-long-stall"))
            CHECK(rfk_paused && rfk_coef_reads==2 && rfk_owned_at_cancel==rfk_owned_io);
        if(rfk_is("timing-edge"))CHECK(rfk_coef_at_us%10000>=9995);
        if(rfk_is("timing-wrap")) {
            CHECK(rfk_first_owned_us<UINT_MAX);
            CHECK(rfk_coef_at_us<UINT_MAX && rfk_coef_at_us+10000>UINT_MAX);
            CHECK(rfk_now_us()>UINT_MAX && rfk_clock_wraps==1);
        }
        if(rfk_is("timing-read-expiry") || rfk_is("timing-new-expiry"))CHECK(d->raw==0x55);
        if(rfk_is("timing-new-expiry")) {
            CHECK(rfk_fault && rfk_coef_at_us-rfk_first_owned_us<6000000);
            CHECK(rfk_now_us()-rfk_coef_at_us<10000);
            /* The first target crosses the new-work deadline. Only a
             * second read by cleanup may accept this existing operation. */
            CHECK(rfk_coef_target_reads==2 && d->cleanup_error==0);
            CHECK(d->token==1 && !rfk_parent_auth && (d->flags&2)==0);
        }
    }
    if(rfk_is("allocation-budget")) {
        CHECK(rfk_paused && rfk_parent_auth==0 && !net->policy.accepting);
        CHECK(sc.sc_stats.rfk.error==ETIMEDOUT);
        CHECK(rfk_now_us()-net->rfk.started_us<6100000);
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
    if(rfk_is("power-mailbox")) {
        CHECK(rfk_paused && rfk_net->rfk.ran && rfk_coef==1);
        CHECK(rfk_post_station_commands==0);
    }
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
    if(rfk_is("quiet-rfk") || rfk_rx_quiet()) {
        CHECK(rfk_paused && net->tx_quiet && rfk_gapk==1 && rfk_iqk==0);
        CHECK(!net->policy.accepting && !sc.sc_started && !sc.sc_dma_master);
        CHECK(!sc.sc_operating.sealed && !sc.sc_power_program.valid);
        if(rfk_is("quiet-rx-worker"))CHECK(rfk_trace_len==rfk_notice_shots);
        else CHECK(rfk_owned_at_cancel==rfk_owned_io);
        CHECK(rtw88_net_newstate(&net->ic,IEEE80211_S_AUTH,0)==EBUSY);
    }
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
    if(rfk_is("env-channel149")) {
        struct ieee80211_ht20_profile profile;
        net->ic.ic_curmode=IEEE80211_MODE_11A;
        net->ic.ic_ifp->if_mtu=1500;
        net->ic.ic_rtsthreshold=net->ic.ic_fragthreshold=2346;
        CHECK(sc.sc_operating.sealed);
        CHECK(rtw88_net_ht20_profile_prepare(net,&profile)==0);
        rtw88_net_ht20_publish(net,&profile);
        CHECK(net->ic.ic_ht20_admit(&net->ic,&profile)==0);
        /* The actual ASSOC and RUN workers must retain the already proven
         * channel. Reprogramming it would withdraw the first HT offer. */
        unsigned channel_writes=rfk_rf18_writes,shots=rfk_trace_len;
        uint64_t power_serial=sc.sc_power_program.serial,token=sc.sc_operating.token;
        const char *retain_fault=getenv("RTW88_RFK_RETAIN_FAULT");
        if(retain_fault!=NULL) {
            if(strcmp(retain_fault,"seal")==0)sc.sc_operating.sealed=0;
            else if(strcmp(retain_fault,"generation")==0)sc.sc_operating.generation++;
            else if(strcmp(retain_fault,"power")==0)sc.sc_power_program.valid=0;
            else if(strcmp(retain_fault,"channel")==0) {
                net->ic.ic_curchan=&net->ic.ic_channels[153];
                net->ic.ic_bss->ni_chan=net->ic.ic_curchan;
            } else CHECK(strcmp(retain_fault,"run-power")==0);
        }
        for(unsigned q=0;q<RTW88_NTXQ;q++)f.tx_hw_rp[q]=sc.sc_txq[q].wp;
        CHECK(rtw88_net_newstate(&net->ic,IEEE80211_S_ASSOC,0)==0);
        rtw88_net_state_work(&net->state_work,net);
        CHECK(net->ic.ic_state==IEEE80211_S_ASSOC && net->policy.accepting);
        if(retain_fault!=NULL && strcmp(retain_fault,"run-power")!=0) {
            CHECK(!sc.sc_operating.sealed && !sc.sc_operating.prepared);
            CHECK(sc.sc_power_program.serial!=power_serial);
            CHECK(sc.sc_channel==(strcmp(retain_fault,"channel")==0?153U:149U));
            CHECK(net->ic.ic_ht20_admit(&net->ic,&profile)==EOPNOTSUPP);
            CHECK(ieee80211_send_mgmt(&net->ic,net->ic.ic_bss,IEEE80211_FC0_SUBTYPE_ASSOC_REQ,0)==0);
            CHECK(!net->ic.ic_bss->ni_ht20.offered);
            CHECK(rfk_trace_len==shots);
        } else if(net->ic.ic_ht20_admit(&net->ic,&profile)==0) {
            CHECK(sc.sc_operating.sealed && sc.sc_power_program.serial==power_serial);
            CHECK(rfk_rf18_writes==channel_writes && rfk_trace_len==shots);
            enable_ht20_exchange(&net->ic,net->ic.ic_ht20_admit,&profile,0);
            if(retain_fault!=NULL)sc.sc_power_program.valid=0;
            for(unsigned q=0;q<RTW88_NTXQ;q++)f.tx_hw_rp[q]=sc.sc_txq[q].wp;
            rtw88_net_state_work(&net->state_work,net);
            if(retain_fault!=NULL) {
                CHECK(net->ic.ic_state==IEEE80211_S_INIT && !net->policy.accepting);
                CHECK(!sc.sc_started && !sc.sc_operating.sealed && !sc.sc_ra.active);
                CHECK(net->ic.ic_ht20_admit(&net->ic,&profile)==EOPNOTSUPP);
            } else {
                CHECK(net->ic.ic_state==IEEE80211_S_RUN && net->policy.accepting);
                CHECK(sc.sc_operating.sealed && sc.sc_operating.token==token);
                CHECK(sc.sc_power_program.serial==power_serial);
                CHECK(sc.sc_ra.active && sc.sc_ra.phy==RTW88_RA_HT20_1SS);
                CHECK(net->ic.ic_ht20_admit(&net->ic,&profile)==0);
            }
            CHECK(rfk_rf18_writes==channel_writes && rfk_trace_len==shots);
        } else CHECK(0);
        struct rtw88_operating_envelope saved=sc.sc_operating;
        unsigned prior=rfk_io;
        sc.sc_operating.sealed=0;
        CHECK(net->ic.ic_ht20_admit(&net->ic,&profile)==EOPNOTSUPP);
        CHECK(rtw88_net_ht20_profile_prepare(net,&profile)==EOPNOTSUPP);
        CHECK(profile.ready==0 && rfk_io==prior);
        sc.sc_operating=saved;
        sc.sc_operating.generation++;
        CHECK(net->ic.ic_ht20_admit(&net->ic,&profile)==EOPNOTSUPP);
        CHECK(rfk_io==prior && !sc.sc_operating.sealed);
        sc.sc_operating=saved;
        sc.sc_power_program.valid=0;
        CHECK(net->ic.ic_ht20_admit(&net->ic,&profile)==EOPNOTSUPP);
        CHECK(rtw88_net_ht20_profile_prepare(net,&profile)==EOPNOTSUPP);
        CHECK(profile.ready==0 && rfk_io==prior);
    }
finish:
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
