/* Origin: EmberBSD UPort 1150 actual USB parent contracts, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include "../../sys/dev/usb/umoxa1150.c"
bool root_is_mounted = true;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#x);return 0;}}while(0)
static void init(struct umoxa1150_softc *sc,struct fake_device *dev,struct usbd_device *usb,int cold) {
 root_is_mounted=true;memset(&fixture,0,sizeof(fixture));memset(sc,0,sizeof(*sc));memset(dev,0,sizeof(*dev));memset(usb,0,sizeof(*usb));dev->softc=sc;
 usb->dd.bNumConfigurations=cold?1:2;usb->cd.bNumInterface=1;
 usb->configs[0]=(usb_config_descriptor_t){9,2,{25,0},1,1,0,0x80,50};usb->configs[1]=(usb_config_descriptor_t){9,2,{39,0},1,2,0,0x80,50};
 usb->iface.id.bNumEndpoints=cold?1:3;
 usb->iface.ep[0]=(usb_endpoint_descriptor_t){cold?1:2,2,{64,0}};
 usb->iface.ep[1]=(usb_endpoint_descriptor_t){0x81,2,{64,0}};
 usb->iface.ep[2]=(usb_endpoint_descriptor_t){0x83,3,{2,0}};
}
static void attach(struct fake_device *d,struct usbd_device *u){struct usb_attach_arg a={0x110a,0x1150,u};umoxa1150_attach(NULL,d,&a);}
static int warm_attach_open_close(void) {
 struct umoxa1150_softc sc;struct fake_device d;struct usbd_device u;
 init(&sc,&d,&u,0);attach(&d,&u);CHECK(fixture.child_attach==1 && !sc.sc_dying);
 CHECK(umoxa1150_open(&sc,0)==0 && fixture.requests==6 && fixture.pipes==1);
 umoxa1150_close(&sc,0);CHECK(fixture.pipes==0 && fixture.allocs==0 && fixture.requests==8);
 CHECK(umoxa1150_detach(&d,0)==0 && fixture.child_detach==1);return 1;
}
static int warm_single_configuration(void) {
 struct umoxa1150_softc sc;struct fake_device d;struct usbd_device u;
 init(&sc,&d,&u,0);u.dd.bNumConfigurations=1;
 u.configs[0]=(usb_config_descriptor_t){9,2,{39,0},1,2,0,0x80,50};u.iface.ep[0].bEndpointAddress=1;
 attach(&d,&u);CHECK(fixture.child_attach==1 && !sc.sc_dying && fixture.reset==0 && fixture.created==0);
 CHECK(u.cd.bConfigurationValue==2 && sc.sc_layout.in==0x81 && sc.sc_layout.out==1 && sc.sc_layout.interrupt==0x83);
 CHECK(umoxa1150_detach(&d,0)==0);return 1;
}
static int configuration_read_failure(void) {
 struct umoxa1150_softc sc;struct fake_device d;struct usbd_device u;
 init(&sc,&d,&u,0);fixture.fail_config_desc=1;attach(&d,&u);
 CHECK(sc.sc_dying && fixture.child_attach==0 && fixture.reset==0);
 CHECK(umoxa1150_detach(&d,0)==0);return 1;
}
static int unknown_configuration_rejected(void) {
 struct umoxa1150_softc sc;struct fake_device d;struct usbd_device u;
 init(&sc,&d,&u,0);u.configs[0].bConfigurationValue=3;attach(&d,&u);
 CHECK(sc.sc_dying && fixture.child_attach==0 && fixture.reset==0);
 CHECK(umoxa1150_detach(&d,0)==0);return 1;
}
static int malformed_unselected_header(void) {
 const uint8_t lengths[][2]={{8,25},{9,8},{10,9}};unsigned i;
 for(i=0;i<3;i++) {struct umoxa1150_softc sc;struct fake_device d;struct usbd_device u;
 init(&sc,&d,&u,0);u.configs[0].bLength=lengths[i][0];u.configs[0].wTotalLength[0]=lengths[i][1];
 attach(&d,&u);CHECK(sc.sc_dying && fixture.child_attach==0 && fixture.reset==0);
 CHECK(umoxa1150_detach(&d,0)==0);}return 1;
}
static int bad_endpoints_no_child(void) {
 struct umoxa1150_softc sc;struct fake_device d;struct usbd_device u;
 init(&sc,&d,&u,0);u.iface.ep[2].wMaxPacketSize[0]=1;attach(&d,&u);
 CHECK(fixture.child_attach==0 && sc.sc_dying && fixture.errors);
 CHECK(umoxa1150_detach(&d,0)==0);return 1;
}
static int open_error_cleanup(void) {
 unsigned fail;for(fail=1;fail<=6;fail++) {
 struct umoxa1150_softc sc;struct fake_device d;struct usbd_device u;
 init(&sc,&d,&u,0);attach(&d,&u);fixture.fail_request=fail;
 CHECK(umoxa1150_open(&sc,0)==EIO && fixture.pipes==0 && fixture.allocs==0);
 CHECK(umoxa1150_detach(&d,0)==0);}
 return 1;
}
static int short_control_error(void) {
 struct umoxa1150_softc sc;struct fake_device d;struct usbd_device u;
 init(&sc,&d,&u,0);attach(&d,&u);fixture.short_request=1;
 CHECK(umoxa1150_open(&sc,0)==EIO && fixture.pipes==0 && fixture.allocs==0);
 CHECK(umoxa1150_detach(&d,0)==0);return 1;
}
static int cold_missing_firmware(void) {
 struct umoxa1150_softc sc;struct fake_device d;struct usbd_device u;
 init(&sc,&d,&u,1);fixture.missing_firmware=1;attach(&d,&u);
 CHECK(d.pending==0 && fixture.child_attach==0 && fixture.errors && fixture.reset==0 && fixture.allocs==0);
 CHECK(umoxa1150_detach(&d,0)==0);return 1;
}
static int cold_upload_cleanup(void) {
 int shorted;for(shorted=0;shorted<=1;shorted++) {
 const uint8_t fw[]={0,0,0,1};struct umoxa1150_softc sc;struct fake_device d;struct usbd_device u;
 init(&sc,&d,&u,1);fixture.firmware.data=fw;fixture.firmware.size=sizeof(fw);fixture.short_bulk=shorted;
 attach(&d,&u);CHECK(d.pending==0 && fixture.child_attach==0 && fixture.allocs==0 && fixture.pipes==0 && fixture.created==fixture.destroyed);
 CHECK(fixture.reset==(shorted?0U:1U));CHECK(umoxa1150_detach(&d,0)==0);}
 return 1;
}
static int detach_pending_and_open(void) {
 struct umoxa1150_softc sc;struct fake_device d;struct usbd_device u;
 init(&sc,&d,&u,1);root_is_mounted=false;attach(&d,&u);
 CHECK(d.pending==0 && fixture.hook==NULL && fixture.reset==0 && fixture.errors);
 CHECK(umoxa1150_detach(&d,0)==0);
 init(&sc,&d,&u,0);attach(&d,&u);CHECK(umoxa1150_open(&sc,0)==0);
 CHECK(umoxa1150_detach(&d,0)==0 && fixture.pipes==0 && fixture.allocs==0 && fixture.child_detach==1);return 1;
}
static int tty_flow_only(void) {
 struct umoxa1150_softc sc;struct fake_device d;struct usbd_device u;struct termios t;
 init(&sc,&d,&u,0);attach(&d,&u);memset(&t,0xa5,sizeof(t));t.c_ispeed=0;t.c_ospeed=9600;t.c_cflag=CS8|CLOCAL;
 CHECK(umoxa1150_param(&sc,0,&t)==0 && sc.sc_settings.ixon==0 && sc.sc_settings.ixoff==0 && sc.sc_settings.xon==0x11 && sc.sc_settings.xoff==0x13);
 CHECK(umoxa1150_detach(&d,0)==0);return 1;
}
static int unsupported_flow_rejected(void) {
 const unsigned flags[]={CDTRCTS,MDMBUF};unsigned i;
 for(i=0;i<2;i++) {struct umoxa1150_softc sc;struct fake_device d;struct usbd_device u;struct termios t;
 init(&sc,&d,&u,0);attach(&d,&u);memset(&t,0,sizeof(t));t.c_ospeed=9600;t.c_cflag=CS8|flags[i];
 CHECK(umoxa1150_param(&sc,0,&t)==EINVAL && fixture.requests==0);
 CHECK(umoxa1150_detach(&d,0)==0);}
 {struct umoxa1150_softc sc;struct fake_device d;struct usbd_device u;struct termios t;
 init(&sc,&d,&u,0);attach(&d,&u);memset(&t,0,sizeof(t));t.c_cflag=CS8;
 CHECK(umoxa1150_param(&sc,0,&t)==EINVAL && fixture.requests==0 && sc.sc_mcr==0x30);
 CHECK(umoxa1150_detach(&d,0)==0);}return 1;
}
static int settings_failure_not_committed(void) {
 struct umoxa1150_softc sc;struct fake_device d;struct usbd_device u;struct termios t;
 init(&sc,&d,&u,0);attach(&d,&u);memset(&t,0,sizeof(t));t.c_ospeed=115200;t.c_cflag=CS8|CLOCAL;
 fixture.fail_request=1;CHECK(umoxa1150_param(&sc,0,&t)==EIO && sc.sc_settings.baud==9600);
 CHECK(umoxa1150_detach(&d,0)==0);return 1;
}
static int interrupted_status_validation(void) {
 struct umoxa1150_softc sc;struct fake_device d;struct usbd_device u;struct usbd_xfer x;
 init(&sc,&d,&u,0);attach(&d,&u);CHECK(umoxa1150_open(&sc,0)==0);
 sc.sc_intr_buf[0]=0x34;sc.sc_intr_buf[1]=0xa0;memset(&x,0,sizeof(x));x.actual=1;
 umoxa1150_intr(&x,&sc,0);CHECK(sc.sc_msr==0 && fixture.errors);
 x.actual=2;umoxa1150_intr(&x,&sc,0);CHECK(sc.sc_msr==0xa0);
 CHECK(umoxa1150_detach(&d,0)==0 && fixture.allocs==0);return 1;
}

static int mode_and_modem_transaction(void) {
 struct umoxa1150_softc sc;struct fake_device d;struct usbd_device u;int mode=1,fault;
 init(&sc,&d,&u,0);attach(&d,&u);
 CHECK(umoxa1150_ioctl(&sc,0,UMOXA1150_SET_MODE,&mode,0,NULL)==0 && sc.sc_settings.mode==1);
 fixture.fail_request=fixture.requests+1;mode=2;
 CHECK(umoxa1150_ioctl(&sc,0,UMOXA1150_SET_MODE,&mode,0,NULL)==EIO && sc.sc_settings.mode==1);
 CHECK(umoxa1150_ioctl(&sc,0,UMOXA1150_GET_FAULT,&fault,0,NULL)==0 && fault==EIO);
 fixture.fail_request=fixture.requests+1;umoxa1150_set(&sc,0,UCOM_SET_DTR,0);
 CHECK(sc.sc_mcr==0x30 && fixture.errors);
 fixture.fail_request=0;umoxa1150_set(&sc,0,UCOM_SET_DTR,0);CHECK(sc.sc_mcr==0x20);
 CHECK(umoxa1150_detach(&d,0)==0);return 1;
}

int main(void) {
 struct {const char *n;int(*f)(void);} cases[]={{"malformed_unselected_header",malformed_unselected_header},{"warm_single_configuration",warm_single_configuration},{"configuration_read_failure",configuration_read_failure},{"unknown_configuration_rejected",unknown_configuration_rejected},{"tty_flow_only",tty_flow_only},{"unsupported_flow_rejected",unsupported_flow_rejected},{"warm_attach_open_close",warm_attach_open_close},{"bad_endpoints_no_child",bad_endpoints_no_child},{"open_error_cleanup",open_error_cleanup},{"short_control_error",short_control_error},{"cold_missing_firmware",cold_missing_firmware},{"cold_upload_cleanup",cold_upload_cleanup},{"detach_pending_and_open",detach_pending_and_open},{"settings_failure_not_committed",settings_failure_not_committed},{"interrupt_status_validation",interrupted_status_validation},{"mode_and_modem_transaction",mode_and_modem_transaction}};
 unsigned i,ok=0;for(i=0;i<16;i++){int pass=cases[i].f();printf("%s %s\n",pass?"PASS":"FAIL",cases[i].n);ok+=pass;}
 printf("%u/16 actual-parent groups passed\n",ok);return ok==16?0:1;
}
