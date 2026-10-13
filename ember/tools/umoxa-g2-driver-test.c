/* Origin: EmberBSD actual MOXA G2 USB-parent contracts, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include "../../sys/dev/usb/umoxa_g2.c"
#include "../../sys/dev/usb/umoxa_g2_io.c"
#include "../../sys/dev/usb/umoxa_g2_tty.c"
bool root_is_mounted=true;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#x); return 0; } } while(0)
static uint8_t firmware[256];
static void init(struct umoxa_g2_softc *sc,struct fake_device *d,struct usbd_device *u) {
 memset(&fixture,0,sizeof(fixture));memset(sc,0,sizeof(*sc));memset(d,0,sizeof(*d));memset(u,0,sizeof(*u));d->softc=sc;root_is_mounted=true;
 fixture.overflow_port=-1;fixture.stop_runs=1;fixture.version[0]=1;fixture.version[1]=2;fixture.version[2]=3;fixture.reg[0]=2;
 memset(firmware,0xa5,sizeof(firmware));firmware[0x86]=1;firmware[0x88]=2;firmware[0x8a]=3;fixture.firmware=(struct fake_firmware){firmware,sizeof(firmware)};
 u->dd=(usb_device_descriptor_t){18,1,{0,3},0xff,0,0,9,{0x0a,0x11},{0x52,0x14},{0,1},1,2,0,1};
 u->cd=(usb_config_descriptor_t){9,2,{39,0},1,1,0,0x80,50};
 u->iface.id=(usb_interface_descriptor_t){9,4,0,0,3,0xff,0,0,0};
 u->iface.ep[0]=(usb_endpoint_descriptor_t){7,5,0x81,2,{0,4},0};
 u->iface.ep[1]=(usb_endpoint_descriptor_t){7,5,2,2,{0,4},0};
 u->iface.ep[2]=(usb_endpoint_descriptor_t){7,5,0x83,2,{0,4},0};
}
static void attach(struct fake_device *d,struct usbd_device *u){struct usb_attach_arg a={0x110a,0x1452,u};umoxag_attach(NULL,d,&a);}
static int clean(struct fake_device *d){CHECK(umoxag_detach(d,DETACH_FORCE)==0);CHECK(fixture.pipes==0 && fixture.xfers==0 && fixture.allocs==0 && fixture.wq==NULL && fixture.fw_open==fixture.fw_closed);return 1;}
static int match_exact_g2(void) {
 struct usb_attach_arg a={0x110a,0x1452,NULL};CHECK(umoxag_match(NULL,0,&a)==UMATCH_VENDOR_PRODUCT);
 a.uaa_product=0x1450;CHECK(umoxag_match(NULL,0,&a)==UMATCH_NONE);a.uaa_product=0x1453;CHECK(umoxag_match(NULL,0,&a)==UMATCH_NONE);
 a.uaa_vendor=0xffff;a.uaa_product=0x1452;CHECK(umoxag_match(NULL,0,&a)==UMATCH_NONE);return 1;
}
static int warm_four_children(void) {
 struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;unsigned p;init(&sc,&d,&u);attach(&d,&u);
 CHECK(sc.sc_ready && !sc.sc_dying && fixture.children==4 && fixture.pipes==3 && fixture.xfers==3 && fixture.wq);
 CHECK(fixture.fw_open==0 && fixture.fw_blocks==0 && fixture.stops==0 && fixture.requests==2);
 for(p=0;p<4;p++)CHECK(sc.sc_port[p].sc_child==&fixture.tty[p] && fixture.methods[p]->ucom_transport);
 CHECK(clean(&d));CHECK(fixture.detaches==4);return 1;
}
static int warm_no_asset_needed(void) {
 struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;init(&sc,&d,&u);fixture.missing_firmware=1;root_is_mounted=false;attach(&d,&u);
 CHECK(sc.sc_ready && fixture.children==4 && fixture.fw_open==0);return clean(&d);
}
static int unknown_layout_rejected(void) {
 unsigned kind;for(kind=0;kind<5;kind++){
  struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;init(&sc,&d,&u);
  switch(kind){case 0:u.cd.bNumInterface=2;break;case 1:u.iface.id.bNumEndpoints=2;break;case 2:u.iface.ep[2].bEndpointAddress=0x81;break;case 3:u.iface.id.bInterfaceClass=2;break;default:u.cd.bLength=8;break;}
  attach(&d,&u);CHECK(!sc.sc_ready && fixture.children==0 && fixture.fw_blocks==0 && fixture.errors);CHECK(clean(&d));
 }return 1;
}
static int short_and_failed_readiness(void) {
 unsigned kind;for(kind=0;kind<2;kind++){
  struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;init(&sc,&d,&u);
  if(kind)fixture.fail_request=2;else fixture.short_request=1;attach(&d,&u);
  CHECK(!sc.sc_ready && fixture.children==0 && fixture.fw_blocks==0 && fixture.errors);CHECK(clean(&d));
 }return 1;
}
static int cold_missing_and_preroot(void) {
 unsigned kind;for(kind=0;kind<2;kind++){
  struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;init(&sc,&d,&u);fixture.reg[0]=0;
  if(kind)root_is_mounted=false;else fixture.missing_firmware=1;attach(&d,&u);
  CHECK(!sc.sc_ready && fixture.children==0 && fixture.fw_blocks==0 && fixture.errors && fixture.fw_open==0);CHECK(clean(&d));
 }return 1;
}
static int firmware_verified_and_stop_eio(void) {
 unsigned fail;for(fail=0;fail<2;fail++){
  struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;init(&sc,&d,&u);fixture.reg[0]=0;if(fail)fixture.fail_request=8;attach(&d,&u);
  CHECK(sc.sc_ready && fixture.children==4 && fixture.fw_blocks==4 && fixture.fw_bytes==256 && fixture.stops==1 && fixture.fw_open==1 && fixture.fw_closed==1);
  CHECK(fixture.request[2].command==0x0c && fixture.request[3].command==0x20 && fixture.request[7].command==0x0d);
  CHECK(clean(&d));
 }return 1;
}
static int short_firmware_no_stop(void) {
 struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;init(&sc,&d,&u);fixture.reg[0]=0;fixture.short_request=4;attach(&d,&u);
 CHECK(!sc.sc_ready && fixture.children==0 && fixture.stops==0 && fixture.fw_blocks==1 && fixture.errors);return clean(&d);
}
static int firmware_not_warm_no_children(void) {
 unsigned kind;for(kind=0;kind<2;kind++){
  struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;init(&sc,&d,&u);fixture.reg[0]=0;
  if(kind)fixture.stop_disconnect=1;else fixture.stop_runs=0;attach(&d,&u);
  CHECK(!sc.sc_ready && fixture.children==0 && fixture.fw_blocks==4 && fixture.stops==1);
  CHECK(fixture.allocs==0 && fixture.fw_open==fixture.fw_closed && fixture.requests<=18);CHECK(clean(&d));
 }return 1;
}
static int invalid_firmware_metadata(void) {
 unsigned kind;for(kind=0;kind<3;kind++){
  struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;init(&sc,&d,&u);fixture.reg[0]=0;
  if(kind==0)fixture.firmware.size=10;else if(kind==1)fixture.firmware.size=262145;else firmware[0x86]=firmware[0x88]=firmware[0x8a]=0;
  attach(&d,&u);CHECK(!sc.sc_ready && fixture.children==0 && fixture.fw_blocks==0 && fixture.stops==0 && fixture.errors);CHECK(clean(&d));
 }return 1;
}
static int resource_failure_cleanup(void) {
 unsigned fail;for(fail=1;fail<=7;fail++){
  struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;init(&sc,&d,&u);fixture.fail_resource=fail;attach(&d,&u);
  CHECK(!sc.sc_ready && fixture.children==0 && fixture.pipes==0 && fixture.xfers==0 && fixture.wq==NULL && fixture.errors);CHECK(clean(&d));
 }return 1;
}
static int partial_child_cleanup(void) {
 struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;init(&sc,&d,&u);fixture.fail_child=3;attach(&d,&u);
 CHECK(!sc.sc_ready && fixture.children==2 && fixture.detaches==2 && fixture.wq==NULL && fixture.pipes==0 && fixture.xfers==0 && fixture.errors);return clean(&d);
}
static int openport(struct umoxa_g2_softc *sc,unsigned p,uint64_t epoch) {
 int e=umoxag_uct_start(sc,(int)p,epoch);if(!e)fixture.tty[p].epoch=epoch;return e;
}
static void pump(struct umoxa_g2_softc *sc) { mutex_enter(&sc->sc_lock);umoxa_g2_kick(sc);mutex_exit(&sc->sc_lock);run_work(); }
static void rx(unsigned p,const char *b,size_t n) { fixture.data[0]=0;fixture.data[1]=(uint8_t)p;fixture.data[2]=(uint8_t)(n>>8);fixture.data[3]=(uint8_t)n;memcpy(fixture.data+4,b,n);fixture.data_len=n+4; }
static void event(unsigned p,unsigned code) { memset(fixture.event,0,8);fixture.event[1]=(uint8_t)p;fixture.event[3]=(uint8_t)code;fixture.event_len=8; }
static int four_port_copy_demux(void) {
 struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;unsigned p;uint8_t b[3]={1,2,3};init(&sc,&d,&u);attach(&d,&u);
 for(p=0;p<4;p++)CHECK(!openport(&sc,p,1));
 for(p=0;p<4;p++)CHECK(!umoxag_uct_submit(&sc,p,1,1,b,3));
 memset(b,0,3);
 CHECK(fixture.out_count==0);for(p=0;p<4;p++)pump(&sc);
 CHECK(fixture.out_count==4);for(p=0;p<4;p++){CHECK(fixture.done[p]==1 && fixture.actual[p]==3);CHECK(fixture.out[p][1]==p && fixture.out[p][4]==1);}
 rx(2,"peer",4);pump(&sc);CHECK(fixture.inputs[2]==1 && fixture.received_len[2]==4 && !memcmp(fixture.received[2],"peer",4));return clean(&d);
}
static int malformed_rx_atomic_shared_fault(void) {
 struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;init(&sc,&d,&u);attach(&d,&u);CHECK(!openport(&sc,0,1) && !openport(&sc,1,1));
 rx(0,"a",1);fixture.data[5]=0;fixture.data_len=6;pump(&sc);
 CHECK(!fixture.inputs[0] && fixture.fault[0]==1 && fixture.fault[1]==1 && !fixture.fault[2]);return clean(&d);
}
static int overflow_local_fault(void) {
 struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;init(&sc,&d,&u);attach(&d,&u);CHECK(!openport(&sc,0,1) && !openport(&sc,1,1));
 fixture.overflow_port=0;rx(0,"a",1);pump(&sc);CHECK(fixture.fault[0]==1 && fixture.fault_errno[0]==ENOBUFS && !fixture.fault[1]);
 rx(1,"ok",2);pump(&sc);CHECK(fixture.inputs[1]==1 && sc.sc_txq.ports[1].state==UMOXA_OPEN);return clean(&d);
}
static int short_tx_no_replay(void) {
 struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;init(&sc,&d,&u);attach(&d,&u);CHECK(!openport(&sc,0,1) && !openport(&sc,1,1));
 CHECK(!umoxag_uct_submit(&sc,0,1,1,(const uint8_t *)"x",1));fixture.short_out=1;pump(&sc);pump(&sc);
 CHECK(fixture.out_count==1 && !fixture.done[0] && fixture.fault[0]==1 && fixture.fault[1]==1);return clean(&d);
}
static int credit_timeout_local(void) {
 struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;uint8_t b[600]={0};init(&sc,&d,&u);attach(&d,&u);CHECK(!openport(&sc,0,1) && !openport(&sc,1,1));
 CHECK(!umoxag_uct_submit(&sc,0,1,1,b,sizeof(b)));pump(&sc);CHECK(fixture.out_count==1 && fixture.done[0]==1);CHECK(!umoxag_uct_submit(&sc,0,1,2,b,1));pump(&sc);fixture.now_ms=1001;pump(&sc);
 CHECK(fixture.fault[0]==1 && fixture.fault_errno[0]==ETIMEDOUT && !fixture.fault[1]);CHECK(!umoxag_uct_submit(&sc,1,1,1,b,1));pump(&sc);CHECK(fixture.done[1]==1);return clean(&d);
}
static int credit_and_status_events(void) {
 struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;uint8_t b[600]={0};u_char l,m;init(&sc,&d,&u);attach(&d,&u);CHECK(!openport(&sc,2,1));
 CHECK(!umoxag_uct_submit(&sc,2,1,1,b,sizeof(b)));pump(&sc);CHECK(!umoxag_uct_submit(&sc,2,1,2,b,1));pump(&sc);CHECK(!umoxag_uct_submit(&sc,2,1,3,b,1));pump(&sc);CHECK(fixture.out_count==2);event(2,2);pump(&sc);pump(&sc);CHECK(fixture.done[2]==3 && fixture.actual[2]==1 && fixture.out_count==3);
 event(2,3);fixture.event[6]=0xb0;pump(&sc);event(2,4);fixture.event[4]=0x12;pump(&sc);umoxag_status(&sc,2,&l,&m);CHECK(m==0xb0 && l==0x12 && fixture.status[2]==2);return clean(&d);
}
static int soft_flow_deferred(void) {
 struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;unsigned n;init(&sc,&d,&u);attach(&d,&u);CHECK(!openport(&sc,0,1));n=fixture.requests;
 umoxag_uct_flow(&sc,0,1,1);CHECK(fixture.requests==n);run_work();CHECK(fixture.requests==n+1 && fixture.request[n].command==7 && fixture.request[n].value==0);
 umoxag_uct_flow(&sc,0,1,0);run_work();CHECK(fixture.request[n+1].value==1);return clean(&d);
}
static int params_atomic_cache(void) {
 struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;struct termios t={0};unsigned n;init(&sc,&d,&u);attach(&d,&u);CHECK(!openport(&sc,0,1));n=fixture.requests;t.c_cflag=CS8;
 CHECK(umoxag_param(&sc,0,&t)==EINVAL && fixture.requests==n);t.c_ospeed=t.c_ispeed=115200;t.c_cflag=CS8|MDMBUF;CHECK(umoxag_param(&sc,0,&t)==EINVAL && fixture.requests==n);
 t.c_cflag=CS7|PARENB|PARODD|CSTOPB;CHECK(!umoxag_param(&sc,0,&t));CHECK(sc.sc_port[0].sc_settings.baud==115200 && sc.sc_port[0].sc_settings.bits==7 && sc.sc_port[0].sc_settings.parity==1 && sc.sc_port[0].sc_settings.stops==2);
 t.c_ospeed=t.c_ispeed=9600;fixture.fail_request=fixture.requests+4;CHECK(umoxag_param(&sc,0,&t)==EIO);CHECK(sc.sc_port[0].sc_settings.baud==115200 && fixture.fault[0]==1);return clean(&d);
}
static int non_rs232_preopen_modem(void) {
 struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;int mode=1;unsigned n;init(&sc,&d,&u);attach(&d,&u);
 CHECK(!umoxag_ioctl(&sc,0,UMOXA_G2_SET_MODE,&mode,0,NULL));n=fixture.requests;
 CHECK(!umoxag_uct_set(&sc,0,UCOM_SET_DTR,1) && !umoxag_uct_set(&sc,0,UCOM_SET_RTS,1));CHECK(fixture.requests==n);CHECK(!openport(&sc,0,1));return clean(&d);
}
static int idle_open_mode_change(void) {
 struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;int mode=1;init(&sc,&d,&u);attach(&d,&u);CHECK(!openport(&sc,0,1));
 CHECK(!umoxag_ioctl(&sc,0,UMOXA_G2_SET_MODE,&mode,0,NULL));CHECK(sc.sc_port[0].sc_settings.mode==1 && sc.sc_port[0].sc_open);
 CHECK(!umoxag_uct_submit(&sc,0,1,1,(const uint8_t *)"x",1));mode=0;CHECK(umoxag_ioctl(&sc,0,UMOXA_G2_SET_MODE,&mode,0,NULL)==EBUSY);pump(&sc);CHECK(umoxag_ioctl(&sc,0,UMOXA_G2_SET_MODE,&mode,0,NULL)==EBUSY);return clean(&d);
}
static int modem_mode_controls(void) {
 struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;int mode=1,fault=-1;unsigned n;init(&sc,&d,&u);attach(&d,&u);CHECK(!openport(&sc,0,1));n=fixture.requests;
 CHECK(!umoxag_uct_set(&sc,0,UCOM_SET_DTR,0));CHECK(fixture.request[n].command==5 && fixture.request[n].value==5 && !(sc.sc_port[0].sc_settings.mcr&1));
 CHECK(!umoxag_uct_submit(&sc,0,1,1,(const uint8_t *)"x",1));CHECK(umoxag_ioctl(&sc,0,UMOXA_G2_SET_MODE,&mode,0,NULL)==EBUSY);umoxag_uct_stop(&sc,0,1);fixture.tty[0].epoch=0;
 CHECK(!umoxag_ioctl(&sc,0,UMOXA_G2_SET_MODE,&mode,0,NULL));CHECK(!openport(&sc,0,2));CHECK(umoxag_uct_set(&sc,0,UCOM_SET_DTR,1)==EOPNOTSUPP);
 CHECK(!umoxag_ioctl(&sc,0,UMOXA_G2_GET_FAULT,&fault,0,NULL) && fault==0);CHECK(umoxag_ioctl(&sc,0,1,&fault,0,NULL)==EPASSTHROUGH);return clean(&d);
}
static int close_reopen_drains_old_credit(void) {
 struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;uint8_t b[600]={0};init(&sc,&d,&u);attach(&d,&u);CHECK(!openport(&sc,0,1) && !openport(&sc,1,1));
 CHECK(!umoxag_uct_submit(&sc,0,1,1,b,1) && !umoxag_uct_submit(&sc,1,1,1,b,1));umoxag_uct_stop(&sc,0,1);fixture.tty[0].epoch=0;pump(&sc);
 CHECK(!fixture.done[0] && fixture.done[1]==1);event(0,2);rx(0,"old",3);CHECK(!openport(&sc,0,2));CHECK(!fixture.inputs[0]);
 CHECK(!umoxag_uct_submit(&sc,0,2,1,b,sizeof(b)));pump(&sc);CHECK(!umoxag_uct_submit(&sc,0,2,2,b,1));pump(&sc);CHECK(sc.sc_txq.ports[0].waiting);CHECK(!umoxag_uct_submit(&sc,0,2,3,b,1));pump(&sc);CHECK(fixture.done[0]==2 && sc.sc_txq.ports[0].waiting);return clean(&d);
}
static int busy_drain_bounded(void) {
 struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;unsigned n;init(&sc,&d,&u);attach(&d,&u);CHECK(!openport(&sc,1,1));event(1,3);fixture.event_repeat=1;n=fixture.event_reads;
 CHECK(openport(&sc,0,1)==EBUSY);CHECK(fixture.event_reads-n==8 && !sc.sc_port[0].sc_open && sc.sc_port[1].sc_open);fixture.event_repeat=0;return clean(&d);
}
static int detach_queued_quiesced(void) {
 struct umoxa_g2_softc sc;struct fake_device d;struct usbd_device u;init(&sc,&d,&u);attach(&d,&u);CHECK(!openport(&sc,0,1));CHECK(!umoxag_uct_submit(&sc,0,1,1,(const uint8_t *)"x",1));CHECK(fixture.wq->queued);
 CHECK(clean(&d));CHECK(fixture.out_count==0 && fixture.detaches==4 && !fixture.done[0]);return 1;
}
int main(void) {
 struct {const char *name;int(*fn)(void);} cases[]={{"non_rs232_preopen_modem",non_rs232_preopen_modem},{"idle_open_mode_change",idle_open_mode_change},{"four_port_copy_demux",four_port_copy_demux},{"malformed_rx_atomic_shared_fault",malformed_rx_atomic_shared_fault},{"overflow_local_fault",overflow_local_fault},{"short_tx_no_replay",short_tx_no_replay},{"credit_timeout_local",credit_timeout_local},{"credit_and_status_events",credit_and_status_events},{"soft_flow_deferred",soft_flow_deferred},{"params_atomic_cache",params_atomic_cache},{"modem_mode_controls",modem_mode_controls},{"close_reopen_drains_old_credit",close_reopen_drains_old_credit},{"busy_drain_bounded",busy_drain_bounded},{"detach_queued_quiesced",detach_queued_quiesced},{"match_exact_g2",match_exact_g2},{"warm_four_children",warm_four_children},{"warm_no_asset_needed",warm_no_asset_needed},{"unknown_layout_rejected",unknown_layout_rejected},{"short_and_failed_readiness",short_and_failed_readiness},{"cold_missing_and_preroot",cold_missing_and_preroot},{"firmware_verified_and_stop_eio",firmware_verified_and_stop_eio},{"short_firmware_no_stop",short_firmware_no_stop},{"firmware_not_warm_no_children",firmware_not_warm_no_children},{"invalid_firmware_metadata",invalid_firmware_metadata},{"resource_failure_cleanup",resource_failure_cleanup},{"partial_child_cleanup",partial_child_cleanup}};
 unsigned n=sizeof(cases)/sizeof(cases[0]),i,ok=0;for(i=0;i<n;i++){int pass=cases[i].fn();printf("%s %s\n",pass?"PASS":"FAIL",cases[i].name);ok+=pass;}
 printf("%u/%u actual G2 parent groups passed\n",ok,n);return ok==n?0:1;
}
