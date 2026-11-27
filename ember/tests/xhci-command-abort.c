/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Origin: EmberBSD (AI-assisted).
 * Host contract for the xHCI command abort (xHCI 4.6.1.2).  The production
 * functions xhci_host_dequeue, xhci_event_cmd, xhci_abort_command and
 * xhci_do_command_locked run against a deterministic model of the CRCR
 * register, the doorbell and the event delivery.  The model checks the
 * order: the abort, the Command Ring Stopped event, CRR clear, the ring
 * clear and DMA sync, then the CRCR write.  It does not emulate DMA.
 */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define XHCIHIST_FUNC()
#define XHCIHIST_CALLARGS(...)
#define XHCIHIST_CALLED()
#define DPRINTFN(...)
#define KASSERT(x) assert(x)
#define KASSERTMSG(x,...) assert(x)
#define cpu_intr_p() 0
#define cpu_softintr_p() 0
#define xhci_polling_p(sc) 0
#define mutex_owned(lock) (*(lock))
#define le64toh(x) (x)
#define XHCI_TRB_2_ERROR_GET(x) ((uint32_t)(x)>>24)
#define XHCI_TRB_3_TYPE_GET(x) (((x)>>10)&63)
#define XHCI_TRB_3_SLOT_GET(x) ((x)>>24)
#define XHCI_TRB_ERROR_CMD_RING_STOP 24
#define XHCI_TRB_ERROR_CMD_ABORTED 25
#define XHCI_TRB_ERROR_SUCCESS 1
#define XHCI_TRB_TYPE_ADDRESS_DEVICE 11
#define XHCI_CRCR_LO_CA 4
#define XHCI_CRCR_LO_CRR 8
#define XHCI_CRCR 1
#define XHCI_USBCMD 2
#define XHCI_USBSTS 3
#define XHCI_IMAN(x) 0
#define XHCI_DOORBELL(x) 0
#define BUS_DMASYNC_PREWRITE 1
#define XHCI_TRB_SIZE 16
#define MAX(a,b) ((a)>(b)?(a):(b))
#define mstohz(x) (x)
#define aprint_error_dev(dev,...) ((void)0)
#define aprint_normal_dev(dev,...) ((void)0)
#define atomic_cas_uint(a,b,c) 0
#define xhci_rt_read_4(sc,o) 0
#define cv_signal(cv) ((void)0)
#define cv_broadcast(cv) ((void)0)
#define cv_wait(cv,mtx) assert(0)
#undef le64toh
#undef le32toh
#define le64toh(x) (x)
#define le32toh(x) (x)
struct xhci_trb { uint64_t trb_0; uint32_t trb_2,trb_3; };
#define xhci_soft_trb xhci_trb
typedef enum {USBD_NORMAL_COMPLETION,USBD_IOERROR,USBD_TIMEOUT} usbd_status;
struct xhci_ring {
    int xr_dma,xr_lock;
    struct xhci_trb xr_trb[8];
    void *xr_cookies[8];
    unsigned xr_ntrb,xr_ep,xr_cs;
#include "xhci-cmd-state.inc"
};
struct xhci_softc {
    struct xhci_ring *sc_cr;
    struct xhci_trb sc_result_trb;
    int sc_lock,sc_command_cv,sc_cmdbusy_cv,sc_suspendresume_failed;
    void *sc_suspender;
    uint64_t sc_command_addr;
    bool sc_resultpending;
};
static void *curlwp;
static struct xhci_ring cr;
static struct xhci_softc sc;
enum event { TIMEOUT,ABORTED,STOP,STOP_SAME,SUCCESS,WRONG,CRR_CLEAR,STOP_RUNNING,RACED_SUCCESS,NOTHING,STRAY_ABORT };
static enum event queue[12];
static unsigned count, index_event, ticks,running,aborts,stops,syncs,rewinds,bells,waits;
static unsigned getticks(void){return ticks;}
static void mutex_enter(int *m){assert(!*m); *m=1;}
static void mutex_exit(int *m){assert(*m); *m=0;}
static unsigned xhci_op_read_4(struct xhci_softc *s,int reg){return reg==XHCI_CRCR?running:0;}
static uint64_t xhci_ring_trbp(struct xhci_ring *r,int i){return 0x10000+i*16;}
static void usb_syncmem(int *dma,int off,int len,int dir) {
    assert(stops && !running && cr.xr_lock && len==sizeof(cr.xr_trb));
    for(unsigned i=0;i<cr.xr_ntrb;i++) assert(!cr.xr_trb[i].trb_3);
    syncs++;
}
static void xhci_op_write_8(struct xhci_softc *s,int reg,uint64_t v) {
    if(v&XHCI_CRCR_LO_CA){assert(s->sc_command_addr && sc.sc_lock && !cr.xr_lock);aborts++;return;}
    assert(stops && !running && syncs && sc.sc_lock && cr.xr_lock);
    assert(v==0x10001 && !cr.xr_ep && cr.xr_cs==1);
    for(unsigned i=0;i<cr.xr_ntrb;i++) assert(!cr.xr_cookies[i]);
    rewinds++;
}
static void xhci_ring_put(struct xhci_softc *s,struct xhci_ring *r,void *p,struct xhci_trb*t,int n) {
    assert(s->sc_lock && r->xr_lock && r->xr_cmd_state!=XHCI_CR_BLOCKED);
    unsigned i=(r->xr_ep==r->xr_ntrb-1)?0:r->xr_ep;
    r->xr_trb[i]=*t;r->xr_trb[i].trb_3|=1;r->xr_ep=i+1;
}
static void xhci_db_write_4(struct xhci_softc*s,int a,int b){bells++;running=XHCI_CRCR_LO_CRR;}
static int cv_timedwait(int*,int*,int);
#include "xhci-command.inc"
static int cv_timedwait(int *cv,int *m,int t) {
    assert(m==&sc.sc_lock && *m && !cr.xr_lock && t>0);
    mutex_exit(m);
    enum event e=index_event<count?queue[index_event++]:NOTHING;
    waits++;
    assert(waits<=5012);
    /* Event consumer takes sc_lock while the submitting thread sleeps. */
    mutex_enter(m);
    struct xhci_trb ev={.trb_0=sc.sc_command_addr};
    unsigned cc=0;
    switch(e){
        case TIMEOUT:case NOTHING:ticks+=t;return EWOULDBLOCK;
        case CRR_CLEAR: running=0;break;
        case ABORTED:case STRAY_ABORT:cc=25;break;
        case STOP:case STOP_SAME:case STOP_RUNNING:
            stops++;cc=24;
            if(e!=STOP_SAME)ev.trb_0+=16;
            if(e!=STOP_RUNNING)running=0;
            break;
        case WRONG: ev.trb_0+=16;cc=1;break;
        case SUCCESS:case RACED_SUCCESS:cc=1;break;
    }
    ticks++;ev.trb_2=cc<<24;
    if(cc)xhci_event_cmd(&sc,&ev);
    return e==RACED_SUCCESS?EWOULDBLOCK:0;
}
static uint32_t last_trb_2;
static usbd_status command(unsigned type){
    struct xhci_trb trb={.trb_3=type<<10};
    usbd_status err=xhci_do_command_locked(&sc,&trb,5000);
    last_trb_2=trb.trb_2;
    return err;
}
static void test(const char *name,const enum event *events,unsigned ne,
    usbd_status first,bool expect_abort,bool restarted,bool blocked) {
    memset(&cr,0,sizeof(cr));memset(&sc,0,sizeof(sc));
    cr.xr_ntrb=8;cr.xr_ep=3;cr.xr_cs=1;
    for(int i=0;i<3;i++){cr.xr_trb[i].trb_3=1;cr.xr_cookies[i]=&sc;}
    sc.sc_cr=&cr;sc.sc_lock=1;
    ticks=0xfffff800U; index_event=running=aborts=stops=syncs=rewinds=bells=waits=0;
    memcpy(queue,events,ne*sizeof(*events)); count=ne;
    assert(command(11)==first && !sc.sc_command_addr && !sc.sc_resultpending);
    assert(aborts==expect_abort && rewinds==restarted && syncs==restarted);
    /* xhci_set_address() sees Command Aborted only after a clean restart. */
    if(first==USBD_TIMEOUT)
        assert((XHCI_TRB_2_ERROR_GET(last_trb_2)==XHCI_TRB_ERROR_CMD_ABORTED)==
            (restarted && events[1]==ABORTED));
    assert((cr.xr_cmd_state==XHCI_CR_BLOCKED)==blocked);
    if(blocked){
        assert(command(9)==USBD_IOERROR && bells==1);
        assert(cr.xr_trb[1].trb_3==1 && !rewinds);
    }else{
        assert(index_event==count);
        queue[count++]=STRAY_ABORT;queue[count++]=STOP_SAME;queue[count++]=WRONG;queue[count++]=SUCCESS;
        assert(command(9)==USBD_NORMAL_COMPLETION && bells==2 && index_event==count);
    }
    printf("PASS %s\n",name);
}
#define TRY(label,status,abort,restart,blocked,...) do {const enum event v[]={__VA_ARGS__};test(label,v,sizeof(v)/sizeof(*v),status,abort,restart,blocked);}while(0)
int main(void){
    TRY("ordinary completion",USBD_NORMAL_COMPLETION,0,0,0,SUCCESS);
    TRY("completion wins timed wait",USBD_NORMAL_COMPLETION,0,0,0,RACED_SUCCESS);
    TRY("aborted then ring stop at next address",USBD_TIMEOUT,1,1,0,TIMEOUT,ABORTED,STOP);
    TRY("stop at command address without aborted event",USBD_TIMEOUT,1,1,0,TIMEOUT,STOP_SAME);
    TRY("late ordinary success before stop",USBD_NORMAL_COMPLETION,1,1,0,TIMEOUT,SUCCESS,STOP);
    TRY("late ordinary success followed by missing stop",USBD_NORMAL_COMPLETION,1,0,1,TIMEOUT,SUCCESS,CRR_CLEAR);
    TRY("CRR clears before stop",USBD_TIMEOUT,1,1,0,TIMEOUT,CRR_CLEAR,STOP);
    TRY("stop before CRR clears",USBD_TIMEOUT,1,1,0,TIMEOUT,STOP_RUNNING,CRR_CLEAR);
    TRY("CRR clear without stop event",USBD_TIMEOUT,1,0,1,TIMEOUT,ABORTED,CRR_CLEAR);
    TRY("stop event but still running",USBD_TIMEOUT,1,0,1,TIMEOUT,STOP_RUNNING);
    TRY("neither indication",USBD_TIMEOUT,1,0,1,TIMEOUT);
}

