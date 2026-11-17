/* Origin: EmberBSD (AI-assisted). */
/* The production callbacks own state. This model replaces kernel scheduling,
 * locks, callouts and the SD card transport. It does not emulate PCI or USB.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>

#define ISSET(a,b) ((a)&(b))
#define SET(a,b) ((a)|=(b))
#define CLR(a,b) ((a)&=~(b))
#define SMC_CAPS_PMF_REINIT 1
#define SMC_CAPS_POLL_CARD_DET 2
#define SMC_CAPS_DMA 4
#define SMC_CAPS_SPI_MODE 8
#define SMF_CARD_PRESENT 1
#define SMF_CARD_ATTACHED 2
#define SMF_SD_MODE 4
#define SMF_MEM_MODE 8
#define SMF_IO_MODE 16
#define SMF_UHS_MODE 32
#define SFF_ERROR 1
#define SFF_SDHC 2
#define SFF_CACHE_ENABLED 4
#define KASSERT assert
#define SDMMC_LOCK(sc) ((void)0)
#define SDMMC_UNLOCK(sc) ((void)0)
#define DPRINTF(...) ((void)0)
#define SDMMC_SECTOR_SIZE 512
#define SDMMC_SDCLK_400K 400
#define MIN(a,b) ((a)<(b)?(a):(b))
#define SIMPLEQ_FIRST(p) (*(p))
#define SIMPLEQ_NEXT(p,m) ((p)->next)
#define TAILQ_FIRST(p) (*(p))
#define DETACH_FORCE 1
typedef unsigned u_int;
typedef uint32_t sdmmc_response[4];
typedef int pmf_qual_t;
struct sdmmc_softc;
struct sdmmc_task { void (*func)(void *); void *arg; struct sdmmc_task *next; };
struct sdmmc_function {
    struct sdmmc_softc *sc;
    struct sdmmc_function *next;
    int rca, number, flags, width, blklen;
    struct { int capacity, read_bl_len, write_bl_len; } csd;
    sdmmc_response raw_cid;
};
typedef struct sdmmc_softc *device_t;
struct sdmmc_softc {
    device_t sc_dev;
    unsigned sc_caps, sc_flags;
    bool sc_pmf_suspended, sc_pmf_card_present;
    int sc_dying, sc_tskq_mtx, sc_mtx, sc_tskq_cv, sc_card_detect_ch;
    void *sc_curtask;
    void *sc_tskq_lwp;
    struct sdmmc_task *sc_tskq, sc_discover_task;
    struct sdmmc_function *sf_head, *sc_card;
    int sc_sct, sc_sch, sc_busclk, sc_clkmax, sc_buswidth;
    bool sc_busddr;
    const char *sc_transfer_mode;
};
static bool (*suspend_callback)(device_t, const pmf_qual_t *);
static bool (*resume_callback)(device_t, const pmf_qual_t *);
static unsigned ticks, waits, timeout_step = 1, poll_starts, poll_stops;
static bool task_completes, present = true;
static int phase_error, init_error;
static unsigned enables, identifications, initializations;
static struct sdmmc_softc *active_sc;
static sdmmc_response card_cid = {1,2,3,4};
static jmp_buf worker_exit;
static unsigned queued_calls;
static bool pmf_device_register(device_t d,
    bool (*s)(device_t,const pmf_qual_t *), bool (*r)(device_t,const pmf_qual_t *)) {
    suspend_callback = s; resume_callback = r; return true;
}
#define device_private(d) (d)
#define aprint_error_dev(...) ((void)0)
static void mutex_enter(int *p) { assert(!*p); *p = 1; }
static void mutex_exit(int *p) { assert(*p); *p = 0; }
static bool mutex_owned(int *p) { return *p != 0; }
static unsigned getticks(void) { return ticks; }
static const unsigned hz = 100;
static int cv_timedwait(int *cv, int *mtx, int timeout) {
    assert(*mtx); waits++; ticks += timeout_step;
    return EWOULDBLOCK;
}
static void cv_broadcast(int *cv) { (void)cv; }
static void cv_wait(int *cv, int *mtx) {
    assert(*mtx); active_sc->sc_dying=1;
}
static void callout_halt(int *c, void *lock) { poll_stops++; }
static void callout_schedule(int *c, unsigned delay) { poll_starts++; }
static void sdmmc_needs_discover(device_t d) { (void)d; }
static int sdmmc_chip_card_detect(int sct, int sch) { return present; }
static int sdmmc_chip_host_ocr(int sct, int sch) { return 1; }
static int sdmmc_chip_bus_power(int sct, int sch, int power) {
    assert(active_sc->sc_pmf_suspended && mutex_owned(&active_sc->sc_mtx));
    enables++;
    return phase_error == 1 ? EIO : 0;
}
static int sdmmc_chip_bus_clock(int sct, int sch, int frequency, bool ddr) { return phase_error == 8 ? EIO : 0; }
static int sdmmc_io_enable(struct sdmmc_softc *sc) { return phase_error == 9 ? EIO : 0; }
static int sdmmc_mem_enable(struct sdmmc_softc *sc) { return phase_error == 10 ? EIO : 0; }
static void sdmmc_pause(unsigned delay, void *lock) { (void)delay; }
static void sdmmc_disable(struct sdmmc_softc *sc) { sc->sc_card=NULL; }
static void sdmmc_discover_task(void *arg) { (void)arg; }
static void config_pending_decr(device_t self) { (void)self; }
static void sdmmc_del_task1(struct sdmmc_softc *sc, struct sdmmc_task *task) {
    assert(sc->sc_tskq==task); sc->sc_tskq=task->next;
}
static void sdmmc_card_detach(struct sdmmc_softc *sc, int flags) { (void)sc; }
static void kthread_exit(int code) { longjmp(worker_exit,1); }
static void queued_work(void *arg) {
    struct sdmmc_softc *sc=arg;
    assert(!sc->sc_pmf_suspended); queued_calls++;
}
static int sdmmc_mem_send_cid(struct sdmmc_softc *sc, sdmmc_response *r) {
    identifications++; memcpy(r,card_cid,sizeof(card_cid)); return phase_error == 2 ? EIO : 0;
}
static int sdmmc_set_relative_addr(struct sdmmc_softc *sc, struct sdmmc_function *sf) {
    sf->rca = 9; return phase_error == 3 ? EIO : 0;
}
static int sdmmc_chip_bus_rod(int sct, int sch, int rod) { return 0; }
static int sdmmc_mem_send_csd(struct sdmmc_softc *sc, struct sdmmc_function *sf, sdmmc_response *r) {
    memset(r,0,sizeof(*r)); return phase_error == 4 ? EIO : 0;
}
static int sdmmc_decode_csd(struct sdmmc_softc *sc, sdmmc_response r, struct sdmmc_function *sf) {
    sf->csd.capacity = phase_error == 7 ? 99 : 100;
    sf->csd.read_bl_len = sf->csd.write_bl_len = 9;
    sf->flags = SFF_SDHC; return 0;
}
static int sdmmc_decode_cid(struct sdmmc_softc *sc, sdmmc_response r, struct sdmmc_function *sf) { return 0; }
static int sdmmc_select_card(struct sdmmc_softc *sc, struct sdmmc_function *sf) {
    assert(sf != sc->sf_head && sc->sc_pmf_suspended);
    if (init_error == 1) return EIO;
    sc->sc_card = sf; return 0;
}
static int sdmmc_mem_set_blocklen(struct sdmmc_softc *sc, struct sdmmc_function *sf, int length) {
    assert(length==512); return init_error == 2 ? EIO : 0;
}
static int sdmmc_mem_sd_init(struct sdmmc_softc *sc, struct sdmmc_function *sf) {
    assert(sc->sc_card==sf && sf != sc->sf_head);
    initializations++; return init_error == 3 ? EIO : 0;
}
static int sdmmc_mem_mmc_init(struct sdmmc_softc *sc, struct sdmmc_function *sf) { assert(0); return EIO; }
#include "pmf-mem-init.inc"
#include "pmf-reinit.inc"
#include "pmf-enable.inc"
#include "pmf-callbacks.inc"
#include "pmf-worker.inc"
static void attach_power(device_t self) {
    struct sdmmc_softc *sc = self;
    (void)sc;
#include "pmf-register.inc"
}
int main(void) {
    (void)waits; (void)poll_starts; (void)poll_stops;
    (void)enables; (void)identifications; (void)initializations;
    static struct sdmmc_softc sc = {.sc_caps = SMC_CAPS_PMF_REINIT | SMC_CAPS_POLL_CARD_DET};
    active_sc = &sc;
    sc.sc_dev = &sc;
    attach_power(&sc);
    assert(suspend_callback != NULL && resume_callback != NULL);
#if HAVE_CALLBACKS
    struct sdmmc_function sf = {.sc=&sc,.rca=1,.number=-1,.width=1,.blklen=512,
        .flags=SFF_SDHC,.csd={100,9,9},.raw_cid={1,2,3,4}};
    sc.sf_head = sc.sc_card = &sf;
    sc.sc_flags = SMF_CARD_PRESENT | SMF_CARD_ATTACHED | SMF_SD_MODE | SMF_MEM_MODE;
    assert(suspend_callback(&sc,NULL) && sc.sc_pmf_suspended);
    assert(resume_callback(&sc,NULL) && !sc.sc_pmf_suspended);
    assert(sf.rca == 9 && sc.sc_card == &sf && enables == 1 && initializations == 1);
    assert(resume_callback(&sc,NULL) && enables == 1);
    struct sdmmc_function saved = sf;
    for (phase_error=1; phase_error<=10; phase_error++) {
        if (phase_error == 5 || phase_error == 6) continue;
        assert(suspend_callback(&sc,NULL));
        assert(!resume_callback(&sc,NULL) && sc.sc_pmf_suspended && sc.sc_card == NULL);
        assert(memcmp(&saved,&sf,sizeof(sf))==0 && queued_calls==0);
    }
    phase_error=0;
    for (init_error=1; init_error<=3; init_error++) {
        assert(!resume_callback(&sc,NULL) && sc.sc_pmf_suspended && sc.sc_card == NULL);
        assert(memcmp(&saved,&sf,sizeof(sf))==0 && queued_calls==0);
    }
    init_error=0; card_cid[1] ^= 1;
    unsigned before=initializations;
    assert(!resume_callback(&sc,NULL) && initializations == before && sc.sc_pmf_suspended);
    card_cid[1] ^= 1; present=false;
    assert(!resume_callback(&sc,NULL) && sc.sc_pmf_suspended);
    present=true;
    card_cid[3] |= 0xff000000; /* R2 padding is not part of the card CID. */
    assert(resume_callback(&sc,NULL) && sc.sc_card == &sf);
    sc.sc_curtask=&sf; ticks=UINT32_MAX-5; timeout_step=100;
    assert(!suspend_callback(&sc,NULL) && !sc.sc_pmf_suspended && waits <= 30);
    sc.sc_curtask=NULL;
    sc.sf_head=NULL; sc.sc_flags=0;
    assert(suspend_callback(&sc,NULL) && resume_callback(&sc,NULL));
    assert(!sc.sc_pmf_suspended);
    struct sdmmc_task queued = {.func=queued_work,.arg=&sc};
    sc.sc_tskq=&queued; sc.sc_tskq_lwp=&sc; sc.sc_pmf_suspended=true;
    if (!setjmp(worker_exit)) sdmmc_task_thread(&sc);
    assert(queued_calls==0 && sc.sc_tskq==&queued && sc.sc_tskq_lwp==NULL);
    sc.sc_tskq_lwp=&sc; sc.sc_pmf_suspended=false;
    if (!setjmp(worker_exit)) sdmmc_task_thread(&sc);
    assert(queued_calls==1 && sc.sc_tskq==NULL && sc.sc_tskq_lwp==NULL);
    sc.sc_caps=0; attach_power(&sc);
    assert(suspend_callback == NULL && resume_callback == NULL);
    (void)task_completes;
#endif
    puts("SDMMC PMF identity and scheduling: PASS");
    return 0;
}
