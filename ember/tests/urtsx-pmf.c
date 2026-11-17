/* Origin: EmberBSD (AI-assisted). */
/* Run the production PMF state machine. USB transactions, task scheduling and
 * descriptor access are modeled. The hardware sequence has its own tests.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#define atomic_load_relaxed(p) (*(p))
#define atomic_store_relaxed(p,v) (*(p)=(v))
#define __arraycount(a) (sizeof(a)/sizeof((a)[0]))
#define KASSERT assert
#define device_private(d) (d)
#define USB_TASKQ_DRIVER 1
#define URTSX_CARD_EXIST 1
#define URTSX_OCP_STATUS 2
#define URTSX_OCP_MASK 0x30
#define UGETW(p) (p)
typedef int pmf_qual_t;
typedef struct { int idVendor, idProduct; } usb_device_descriptor_t;
typedef struct { int bInterfaceNumber, bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol; } usb_interface_descriptor_t;
struct urtsx_softc {
    int sc_lock, sc_poll;
    unsigned sc_dying, sc_suspended, sc_status, sc_recovery_wait;
    bool sc_ready, sc_unready, sc_power_dirty, sc_powered;
    void *sc_udev, *sc_iface;
    uint8_t sc_response[4];
};
typedef struct urtsx_softc *device_t;
static bool task_queued, task_running;
static int off_error, initialize_error, read_error;
static unsigned drains, initializes, offs, reads;
static bool registered;
static usb_device_descriptor_t dd = {0x0bda,0x0129};
static usb_interface_descriptor_t id = {0,0xff,6,0x50};
static void mutex_enter(int *p) { assert(!*p); *p=1; }
static void mutex_exit(int *p) { assert(*p); *p=0; }
static bool mutex_owned(int *p) { return *p != 0; }
static bool usb_rem_task(void *dev, int *task) { drains++; bool q=task_queued; task_queued=false; return q; }
static void usb_rem_task_wait(void *dev, int *task, int queue, void *lock) {
    assert(!lock && !task_queued); task_running=false; drains++;
}
static usb_device_descriptor_t *usbd_get_device_descriptor(void *dev) { return &dd; }
static usb_interface_descriptor_t *usbd_get_interface_descriptor(void *iface) { return &id; }
static bool pmf_device_register(device_t self,
    bool (*suspend)(device_t,const pmf_qual_t *),
    bool (*resume)(device_t,const pmf_qual_t *)) {
    registered = suspend != NULL && resume != NULL;
    return true;
}
#define aprint_error_dev(...) ((void)0)
static int urtsx_initialize(struct urtsx_softc *sc) {
    assert(sc->sc_lock); initializes++;
    if (initialize_error) { sc->sc_unready=true; return initialize_error; }
    sc->sc_unready=false; return 0;
}
static int urtsx_power_off_locked(struct urtsx_softc *sc) {
    assert(sc->sc_lock); offs++; sc->sc_power_dirty=true;
    if (!off_error) { sc->sc_powered=false; sc->sc_power_dirty=false; }
    return off_error;
}
static int urtsx_read(struct urtsx_softc *sc, const uint16_t *r, unsigned n) {
    assert(sc->sc_lock && n==2); reads++; sc->sc_response[0]=4; sc->sc_response[1]=0;
    return read_error;
}
static int urtsx_bulk_status_decode(uint8_t *p, unsigned n, uint16_t *status) { *status=1; return 0; }
#include "usb-pmf.inc"
static void attach_power(device_t self) {
#include "usb-register.inc"
}
int main(void) {
    (void)task_running;
    (void)drains; (void)initializes; (void)offs; (void)reads;
    struct urtsx_softc sc = {.sc_ready=true,.sc_powered=true};
    attach_power(&sc);
    assert(registered);
#if HAVE_CALLBACKS
    task_queued=task_running=true;
    assert(urtsx_suspend(&sc,NULL));
    assert(sc.sc_suspended && !sc.sc_powered && !task_queued && !task_running && drains==2);
    assert(urtsx_resume(&sc,NULL));
    assert(!sc.sc_suspended && sc.sc_status==1 && !sc.sc_power_dirty);
    unsigned before=initializes;
    assert(urtsx_resume(&sc,NULL) && initializes==before);
    off_error=EIO;
    assert(!urtsx_suspend(&sc,NULL) && !sc.sc_suspended && sc.sc_unready);
    off_error=0;
    assert(urtsx_suspend(&sc,NULL));
    initialize_error=EIO;
    assert(!urtsx_resume(&sc,NULL) && sc.sc_suspended && sc.sc_unready);
    initialize_error=0; read_error=EIO;
    assert(!urtsx_resume(&sc,NULL) && sc.sc_suspended && sc.sc_unready);
    read_error=0;
    assert(urtsx_resume(&sc,NULL) && !sc.sc_suspended);
    dd.idProduct=0;
    assert(urtsx_suspend(&sc,NULL));
    before=initializes;
    assert(!urtsx_resume(&sc,NULL) && sc.sc_suspended && initializes==before);
    dd.idProduct=0x0129;
    assert(urtsx_resume(&sc,NULL));
    sc.sc_dying=1;
    assert(!urtsx_suspend(&sc,NULL));
#endif
    puts("USB SD PMF quiesce/restore: PASS");
    return 0;
}
