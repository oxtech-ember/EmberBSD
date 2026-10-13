/*
 * Origin: EmberBSD (AI-assisted).
 * Compile actual recovery/start functions with a deterministic command backend.
 * This checks software ordering. It does not emulate xHCI hardware or DMA.
 */
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef unsigned int u_int;
typedef int usbd_status;
typedef pthread_mutex_t kmutex_t;
typedef pthread_cond_t kcondvar_t;
typedef int usb_dma_t;
typedef struct {
	uint8_t bmRequestType, bRequest;
	uint16_t wValue, wIndex, wLength;
} usb_device_request_t;
typedef struct { uint8_t bEndpointAddress, bmAttributes; }
    usb_endpoint_descriptor_t;

#define USBD_NOT_STARTED 0
#define USBD_IN_PROGRESS 1
#define USBD_CANCELLED 2
#define USBD_TIMEOUT 3
#define URQ_REQUEST 1
#define USB_TASKQ_HC 0
#define UE_CONTROL 0
#define UE_BULK 2
#define UE_GET_XFERTYPE(a) ((a) & 3)
#define XHCI_EPSTATE_HALTED 1
#define XHCI_EPSTATE_STOPPED 2
#define XHCI_EPSTATE_ERROR 3
#define XHCI_DOORBELL(x) (x)
#define UGETW(x) (x)
#define DMAADDR(d, off) ((uint64_t)*(d) + (off))
#define XHCI_TRB_2_IRQ_SET(x) (x)
#define XHCI_TRB_2_BYTES_SET(x) (x)
#define XHCI_TRB_2_TDSZ_SET(x) (x)
#define XHCI_TRB_3_TYPE_SET(x) (x)
#define XHCI_TRB_3_TRT_NONE 0
#define XHCI_TRB_3_TRT_IN 0
#define XHCI_TRB_3_TRT_OUT 0
#define XHCI_TRB_TYPE_SETUP_STAGE 0
#define XHCI_TRB_TYPE_DATA_STAGE 1
#define XHCI_TRB_TYPE_STATUS_STAGE 2
#define XHCI_TRB_3_IDT_BIT 0
#define XHCI_TRB_3_DIR_IN 0
#define XHCI_TRB_3_ISP_BIT 0
#define XHCI_TRB_3_IOC_BIT 0
#define BUS_DMASYNC_PREREAD 0
#define BUS_DMASYNC_PREWRITE 1
#define KASSERT(a) assert(a)
#define KASSERTMSG(a, ...) assert(a)
#define XHCIHIST_FUNC() ((void)0)
#define XHCIHIST_CALLARGS(...) ((void)0)
#define DPRINTF(...) ((void)0)
#define DPRINTFN(...) ((void)0)
#define device_printf(...) ((void)0)
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define SIMPLEQ_FIRST(q) ((q)->first)

struct xhci_softc { kmutex_t sc_lock; int sc_dev; bool polling; };
struct usbd_bus { int ub_usbctl; struct xhci_softc *sc; };
struct usbd_xfer;
struct xhci_ring {
	bool is_halted;
	kmutex_t xr_lock;
	int state;
};
struct xhci_slot { unsigned xs_idx; struct xhci_ring *xs_xr[2]; };
struct usbd_device { struct usbd_bus *ud_bus; struct xhci_slot *ud_hcpriv; };
struct usbd_endpoint { usb_endpoint_descriptor_t *ue_edesc; };
struct usbd_pipe_methods { usbd_status (*upm_start)(struct usbd_xfer *); };
struct usbd_pipe {
	struct usbd_device *up_dev;
	struct usbd_endpoint *up_endpoint;
	struct { struct usbd_xfer *first; } up_queue;
	const struct usbd_pipe_methods *up_methods;
};
struct usb_task { bool pending; };
struct usbd_xfer {
	struct usbd_pipe *ux_pipe;
	struct usbd_bus *ux_bus;
	usb_device_request_t ux_request;
	usb_dma_t ux_dmabuf;
	unsigned ux_rqflags;
	usbd_status ux_status;
};
struct xhci_xfer { struct usbd_xfer xx_xfer; };
#include "xhci-pipe.h"

#define XHCI_PIPE2SC(p) ((p)->up_dev->ud_bus->sc)
#define XHCI_XFER2SC(x) XHCI_PIPE2SC((x)->ux_pipe)
#define XHCI_XFER2BUS(x) ((x)->ux_bus)
#define XHCI_XFER2XXFER(x) container_of(x, struct xhci_xfer, xx_xfer)

static _Thread_local bool softintr, hardintr, event_thread;
static _Thread_local kmutex_t *held;
static struct xhci_softc controller;
static struct usbd_bus bus;
static struct usbd_device device;
static struct xhci_slot slot;
static struct xhci_ring ring;
static usb_endpoint_descriptor_t descriptor;
static struct usbd_endpoint endpoint;
static struct xhci_pipe test_pipe;
static struct xhci_xfer transfer;
static unsigned resets, stops, dequeues, submissions, timeouts, bells;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cv = PTHREAD_COND_INITIALIZER;
static unsigned pause_command;
static bool reset_entered, release_reset, recovery_waiting;

static void
mutex_enter(kmutex_t *lock)
{
	assert(pthread_mutex_lock(lock) == 0);
	if (lock == &controller.sc_lock) {
		assert(held == NULL);
		held = lock;
	}
}

static void
mutex_exit(kmutex_t *lock)
{
	if (lock == &controller.sc_lock) {
		assert(held == lock);
		held = NULL;
	}
	assert(pthread_mutex_unlock(lock) == 0);
}

#define mutex_owned(lock) (held == (lock))
#define cpu_intr_p() (hardintr)
#define cpu_softintr_p() (softintr)
#define xhci_polling_p(sc) ((sc)->polling)
#define usbd_xfer_isread(x) (((x)->ux_request.bmRequestType & 0x80) != 0)
#define xhci_ep_get_dci(ed) ((ed)->bEndpointAddress == 0 ? 1U : 2U)
#define usb_in_event_thread(dev) (event_thread && (dev) == bus.ub_usbctl)

/* Only changed source calls these wrappers. Macros avoid unused stubs. */
#define cv_broadcast(cv) assert(pthread_cond_broadcast(cv) == 0)
#define cv_wait(cv, lock) recovery_wait((cv), (lock))

static void __attribute__((unused))
recovery_wait(kcondvar_t *cv, kmutex_t *lock)
{
	assert(held == lock && !softintr && !hardintr && !controller.polling);
	assert(pthread_mutex_lock(&gate) == 0);
	recovery_waiting = true;
	assert(pthread_cond_broadcast(&gate_cv) == 0);
	assert(pthread_mutex_unlock(&gate) == 0);
	held = NULL;
	assert(pthread_cond_wait(cv, lock) == 0);
	held = lock;
}

static void
usb_add_task(const struct usbd_device *dev, struct usb_task *task, int queue)
{
	assert(dev == &device && queue == USB_TASKQ_HC);
	task->pending = true;
}

static void xhci_pipe_restart_async_task(void *);
static usbd_status xhci_device_ctrl_start(struct usbd_xfer *);

static bool __attribute__((unused))
usb_rem_task_wait(const struct usbd_device *dev, struct usb_task *task, int queue,
    kmutex_t *lock)
{
	assert(dev == &device && queue == USB_TASKQ_HC);
	assert(!softintr && !hardintr && !controller.polling);
	/* Reproduce a worker which has dequeued the task but needs sc_lock. */
	mutex_exit(lock);
	if (task->pending) {
		task->pending = false;
		xhci_pipe_restart_async_task(&test_pipe.xp_pipe);
	}
	mutex_enter(lock);
	return false;
}

static uint64_t
le64dec(const void *data)
{
	uint64_t value;
	memcpy(&value, data, sizeof(value));
	return value;
}

static int
xhci_get_epstate(const struct xhci_softc *sc, const struct xhci_slot *xs,
    unsigned dci)
{
	assert(sc == &controller && xs == &slot && dci == 1);
	return ring.state;
}

static void
command_pause(unsigned phase)
{
	assert(pthread_mutex_lock(&gate) == 0);
	if (pause_command == phase) {
		pause_command = 0;
		reset_entered = true;
		assert(pthread_cond_broadcast(&gate_cv) == 0);
		mutex_exit(&controller.sc_lock);
		while (!release_reset)
			assert(pthread_cond_wait(&gate_cv, &gate) == 0);
		assert(pthread_mutex_unlock(&gate) == 0);
		mutex_enter(&controller.sc_lock);
	} else {
		assert(pthread_mutex_unlock(&gate) == 0);
	}
}

static void
xhci_reset_endpoint(const struct usbd_pipe *p)
{
	assert(p == &test_pipe.xp_pipe && held == &controller.sc_lock);
	assert(!softintr && !hardintr && !controller.polling);
	resets++;
	command_pause(1);
	ring.state = XHCI_EPSTATE_STOPPED;
}

static void
xhci_stop_endpoint(const struct usbd_pipe *p)
{
	assert(p == &test_pipe.xp_pipe && held == &controller.sc_lock);
	stops++;
	ring.state = XHCI_EPSTATE_STOPPED;
}

static void
xhci_set_dequeue(const struct usbd_pipe *p)
{
	assert(p == &test_pipe.xp_pipe && held == &controller.sc_lock);
	assert(ring.state == XHCI_EPSTATE_STOPPED);
	dequeues++;
	command_pause(2);
}

static void
xhci_xfer_put_trb(const struct xhci_xfer *xx, unsigned i, uint64_t p, uint32_t s,
    uint32_t c)
{
	(void)i; (void)p; (void)s; (void)c;
	assert(xx == &transfer);
}

static void
usb_syncmem(const usb_dma_t *dma, unsigned offset, unsigned len, int operation)
{
	(void)offset; (void)len; (void)operation;
	assert(dma == &transfer.xx_xfer.ux_dmabuf);
}

static void
xhci_ring_put_xfer(const struct xhci_softc *sc, const struct xhci_ring *tr,
    const struct xhci_xfer *xx, unsigned n)
{
	assert(sc == &controller && tr == &ring && xx == &transfer);
	assert(n == 3 && !ring.is_halted);
	assert(test_pipe.xp_pipe.up_queue.first == &transfer.xx_xfer);
	submissions++;
}

static void
xhci_db_write_4(const struct xhci_softc *sc, unsigned slotno, unsigned dci)
{
	assert(sc == &controller && slotno == slot.xs_idx && dci == 1);
	bells++;
}

static void
usbd_xfer_schedule_timeout(const struct usbd_xfer *xfer)
{
	assert(xfer->ux_status == USBD_IN_PROGRESS);
	timeouts++;
}

#include "xhci_pipe_restart.h"
#include "xhci_abortx.h"
#include "xhci_pipe_restart_async_task.h"
#include "xhci_pipe_restart_async.h"
#include "xhci_device_ctrl_start.h"

static const struct usbd_pipe_methods methods = {xhci_device_ctrl_start};

static void
init(void)
{
	assert(pthread_mutex_init(&controller.sc_lock, NULL) == 0);
	assert(pthread_mutex_init(&ring.xr_lock, NULL) == 0);
	controller.sc_dev = 1;
	bus.ub_usbctl = 2;
	bus.sc = &controller;
	device.ud_bus = &bus;
	device.ud_hcpriv = &slot;
	slot.xs_idx = 1;
	slot.xs_xr[1] = &ring;
	descriptor.bmAttributes = UE_CONTROL;
	endpoint.ue_edesc = &descriptor;
	test_pipe.xp_pipe.up_dev = &device;
	test_pipe.xp_pipe.up_endpoint = &endpoint;
	test_pipe.xp_pipe.up_methods = &methods;
	transfer.xx_xfer.ux_pipe = &test_pipe.xp_pipe;
	transfer.xx_xfer.ux_bus = &bus;
	transfer.xx_xfer.ux_rqflags = URQ_REQUEST;
	transfer.xx_xfer.ux_request.wLength = 8;
	assert(transfer.xx_xfer.ux_request.bRequest == 0);
	assert(transfer.xx_xfer.ux_request.wValue == 0);
	assert(transfer.xx_xfer.ux_request.wIndex == 0);
#ifdef XHCI_HAVE_RESTART_CV
	assert(pthread_cond_init(&test_pipe.xp_restart_cv, NULL) == 0);
#endif
}

static void
reset(void)
{
	resets = stops = dequeues = submissions = timeouts = bells = 0;
	softintr = hardintr = event_thread = controller.polling = false;
	test_pipe.xp_async_task.pending = false;
	test_pipe.xp_pipe.up_queue.first = &transfer.xx_xfer;
	transfer.xx_xfer.ux_status = USBD_NOT_STARTED;
	ring.is_halted = true;
	ring.state = XHCI_EPSTATE_HALTED;
	descriptor.bmAttributes = UE_CONTROL;
}

static void
run_task(void)
{
	test_pipe.xp_async_task.pending = false;
	xhci_pipe_restart_async_task(&test_pipe.xp_pipe);
}

static void
contexts(void)
{
	unsigned context;
	for (context = 0; context < 4; context++) {
		reset();
		softintr = context == 0;
		hardintr = context == 1;
		event_thread = context == 2;
		controller.polling = context == 3;
		mutex_enter(&controller.sc_lock);
		assert(xhci_device_ctrl_start(&transfer.xx_xfer) == USBD_IN_PROGRESS);
		assert(resets == 0 && dequeues == 0 && submissions == 0);
		assert(test_pipe.xp_async_task.pending && timeouts == 1);
		mutex_exit(&controller.sc_lock);
		softintr = hardintr = event_thread = controller.polling = false;
		run_task();
		assert(resets == 1 && dequeues == 1 && submissions == 1);
		assert(timeouts == 1 && bells == 1);
	}
}

static void
inline_and_stale_task(void)
{
	reset();
	test_pipe.xp_async_task.pending = true;
	mutex_enter(&controller.sc_lock);
	assert(xhci_device_ctrl_start(&transfer.xx_xfer) == USBD_IN_PROGRESS);
	mutex_exit(&controller.sc_lock);
	assert(resets == 1 && dequeues == 1 && submissions == 1);
	run_task();
	assert(resets == 1 && dequeues == 1 && submissions == 1);
	assert(timeouts == 1 && bells == 1);
}

static void
stall_schedule(void)
{
	reset();
	mutex_enter(&controller.sc_lock);
	xhci_pipe_restart_async(&test_pipe.xp_pipe);
	assert(ring.is_halted && !test_pipe.xp_async_task.pending);
	descriptor.bmAttributes = UE_BULK;
	xhci_pipe_restart_async(&test_pipe.xp_pipe);
	assert(test_pipe.xp_async_task.pending);
	mutex_exit(&controller.sc_lock);
}

static void *
start_thread(void *unused)
{
	(void)unused;
	mutex_enter(&controller.sc_lock);
	assert(xhci_device_ctrl_start(&transfer.xx_xfer) == USBD_IN_PROGRESS);
	mutex_exit(&controller.sc_lock);
	return NULL;
}

static void *
task_thread(void *unused)
{
	(void)unused;
	run_task();
	return NULL;
}

static void *
/* pthread_create requires this exact callback signature. */
// cppcheck-suppress constParameterCallback
abort_thread(void *status)
{
	mutex_enter(&controller.sc_lock);
	/* usbd_xfer_abort/timeout_task claim this status under the bus lock. */
	assert(transfer.xx_xfer.ux_status == USBD_IN_PROGRESS);
	transfer.xx_xfer.ux_status = *(const int *)status;
	xhci_abortx(&transfer.xx_xfer);
	test_pipe.xp_pipe.up_queue.first = NULL;
	mutex_exit(&controller.sc_lock);
	return NULL;
}

static void
cancel_during_reset(void)
{
	unsigned phase, owner;
	int status;
	for (phase = 1; phase <= 2; phase++) {
	for (owner = 0; owner < 2; owner++) {
	for (status = USBD_CANCELLED; status <= USBD_TIMEOUT; status++) {
		pthread_t starter, aborter;
		reset();
		pause_command = phase;
		reset_entered = release_reset = recovery_waiting = false;
		if (owner) {
			softintr = true;
			mutex_enter(&controller.sc_lock);
			assert(xhci_device_ctrl_start(&transfer.xx_xfer) ==
			    USBD_IN_PROGRESS);
			mutex_exit(&controller.sc_lock);
			softintr = false;
		}
		assert(pthread_create(&starter, NULL,
		    owner ? task_thread : start_thread, NULL) == 0);
		assert(pthread_mutex_lock(&gate) == 0);
		while (!reset_entered)
			assert(pthread_cond_wait(&gate_cv, &gate) == 0);
		assert(pthread_mutex_unlock(&gate) == 0);
		assert(pthread_create(&aborter, NULL, abort_thread, &status) == 0);
		assert(pthread_mutex_lock(&gate) == 0);
		while (!recovery_waiting)
			assert(pthread_cond_wait(&gate_cv, &gate) == 0);
		release_reset = true;
		assert(pthread_cond_broadcast(&gate_cv) == 0);
		assert(pthread_mutex_unlock(&gate) == 0);
		assert(pthread_join(starter, NULL) == 0);
		assert(pthread_join(aborter, NULL) == 0);
		assert(submissions == 0 && bells == 0 && timeouts == 1);
		assert(resets == 1 && dequeues >= 1);
		assert(transfer.xx_xfer.ux_status == status);
	}
	}
	}
}

static void
task_during_inline(void)
{
	pthread_t starter, worker;
	reset();
	pause_command = 1;
	reset_entered = release_reset = recovery_waiting = false;
	assert(pthread_create(&starter, NULL, start_thread, NULL) == 0);
	assert(pthread_mutex_lock(&gate) == 0);
	while (!reset_entered)
		assert(pthread_cond_wait(&gate_cv, &gate) == 0);
	assert(pthread_mutex_unlock(&gate) == 0);
	assert(pthread_create(&worker, NULL, task_thread, NULL) == 0);
	assert(pthread_mutex_lock(&gate) == 0);
	while (!recovery_waiting)
		assert(pthread_cond_wait(&gate_cv, &gate) == 0);
	release_reset = true;
	assert(pthread_cond_broadcast(&gate_cv) == 0);
	assert(pthread_mutex_unlock(&gate) == 0);
	assert(pthread_join(starter, NULL) == 0);
	assert(pthread_join(worker, NULL) == 0);
	assert(resets == 1 && dequeues == 1 && submissions == 1);
	assert(timeouts == 1 && bells == 1);
}

int
main(int argc, char **argv)
{
	const char *which = argc == 2 ? argv[1] : "all";
	alarm(15);
	init();
	if (!strcmp(which, "all") || !strcmp(which, "contexts"))
		contexts();
	if (!strcmp(which, "all") || !strcmp(which, "stale-task"))
		inline_and_stale_task();
	if (!strcmp(which, "all") || !strcmp(which, "stall"))
		stall_schedule();
	if (!strcmp(which, "all") || !strcmp(which, "abort"))
		cancel_during_reset();
	if (!strcmp(which, "all") || !strcmp(which, "concurrent-task"))
		task_during_inline();
#ifdef XHCI_HAVE_RESTART_CV
	assert(pthread_cond_destroy(&test_pipe.xp_restart_cv) == 0);
#endif
	assert(pthread_mutex_destroy(&controller.sc_lock) == 0);
	assert(pthread_mutex_destroy(&ring.xr_lock) == 0);
	puts("xHCI control recovery: PASS");
	return 0;
}
