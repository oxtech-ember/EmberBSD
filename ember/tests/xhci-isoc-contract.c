/*
 * Origin: EmberBSD (AI-assisted).
 * Compile the actual xhci(4) isochronous submit, stall and ring-restart
 * functions with deterministic stubs.  This checks the TRBs the driver
 * builds and the calls it makes.  It does not emulate xHCI hardware,
 * scheduling or DMA.
 */
#include <sys/time.h>

/* The checks are assertions: keep them in every build. */
#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef unsigned int u_int;
typedef int usbd_status;
typedef int kmutex_t;
typedef int kcondvar_t;
typedef int usb_dma_t;
typedef struct {
	uint8_t bEndpointAddress, bmAttributes, bInterval;
	uint16_t wMaxPacketSize;
} usb_endpoint_descriptor_t;

#define USBD_NORMAL_COMPLETION 0
#define USBD_IN_PROGRESS 1
#define USBD_IOERROR 13
#define USBD_STALLED 15
#define URQ_REQUEST 1
#define UE_ISOCHRONOUS 1
#define UE_BULK 2
#define UE_GET_SIZE(a) ((a) & 0x7ff)
#define UGETW(x) (x)
#define USB_SPEED_FULL 2
#define USB_SPEED_HIGH 3
#define USB_UFRAMES_PER_FRAME 8
#define XHCI_TRB_ERROR_BABBLE 3
#define XHCI_MFINDEX 0
#define XHCI_MFINDEX_GET(x) ((x) & 0x3fff)
#define XHCI_DOORBELL(x) (x)
#define XHCI_HCC_CFC(hcc) (((hcc) & 1) != 0)
#define XHCI_TRB_2_IRQ_SET(x) 0U
#define XHCI_TRB_2_TDSZ_SET(x) 0U
#define XHCI_TRB_2_BYTES_SET(x) ((uint32_t)(x))
#define XHCI_TRB_3_TYPE_SET(x) 0U
#define XHCI_TRB_TYPE_ISOCH 5
#define XHCI_TRB_3_ISP_BIT 0U
#define XHCI_TRB_3_IOC_BIT 0U
#define XHCI_TRB_3_TBC_SET(x) ((uint32_t)(x) & 0U)
#define XHCI_TRB_3_TLBPC_SET(x) ((uint32_t)(x) & 0U)
#define XHCI_TRB_3_FRID_SET(x) (((uint32_t)(x) & 0x7ff) << 20)
#define XHCI_TRB_3_FRID_GET(x) (((x) >> 20) & 0x7ff)
#define XHCI_TRB_3_ISO_SIA_BIT (1U << 31)
#define BUS_DMASYNC_PREREAD 0
#define BUS_DMASYNC_PREWRITE 1
#define DMAADDR(d, off) ((uint64_t)*(d) + (off))
#define KASSERT(a) assert(a)
#define KASSERTMSG(a, ...) assert(a)
#define XHCIHIST_FUNC() ((void)0)
#define XHCIHIST_CALLARGS(...) ((void)0)
#define DPRINTF(...) ((void)0)
#define DPRINTFN(...) ((void)0)
#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#define roundup2(x, m) (((x) + (m) - 1) & ~((m) - 1))
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define SIMPLEQ_FIRST(q) ((q)->first)

struct xhci_softc { kmutex_t sc_lock; int sc_dev; uint32_t sc_hcc; bool sc_dying; };
struct usbd_bus { struct xhci_softc *sc; };
struct usbd_xfer;
struct xhci_ring { kmutex_t xr_lock; };
struct xhci_slot { unsigned xs_idx; struct xhci_ring *xs_xr[3]; };
struct usbd_device {
	struct usbd_bus *ud_bus;
	struct xhci_slot *ud_hcpriv;
	int ud_speed;
};
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
	usb_dma_t ux_dmabuf;
	uint32_t ux_length;
	uint16_t *ux_frlengths;
	int ux_nframes;
	unsigned ux_rqflags;
	usbd_status ux_status;
};
struct xhci_xfer { struct usbd_xfer xx_xfer; int xx_isoc_done; };
#include "xhci-pipe.h"

#define XHCI_PIPE2SC(p) ((p)->up_dev->ud_bus->sc)
#define XHCI_XFER2SC(x) XHCI_PIPE2SC((x)->ux_pipe)
#define XHCI_XFER2XXFER(x) container_of(x, struct xhci_xfer, xx_xfer)
#define xhci_polling_p(sc) (false)
#define xhci_ep_get_dci(ed) (2U)
#define usbd_xfer_isread(x) (true)
#define mutex_owned(lock) (true)
#define mutex_enter(lock) ((void)(lock))
#define mutex_exit(lock) ((void)(lock))
#define usb_syncmem(dma, off, len, ops) ((void)(dma))
#define usbd_xfer_schedule_timeout(x) ((void)(x))
#define xhci_db_write_4(sc, reg, value) ((void)(sc))
#define xhci_rt_read_4(sc, reg) (current_mfindex)

#define NFRAMES 16
static struct xhci_softc controller;
static struct usbd_bus bus = { &controller };
static struct xhci_ring ring;
static struct xhci_slot test_slot = { 1, { NULL, NULL, &ring } };
static struct usbd_device device = { &bus, &test_slot, USB_SPEED_HIGH };
static usb_endpoint_descriptor_t descriptor;
static struct usbd_endpoint endpoint = { &descriptor };
static struct xhci_pipe test_pipe;
static struct xhci_xfer transfer;
static uint16_t lengths[NFRAMES];
static uint32_t controls[NFRAMES];
static uint32_t current_mfindex;
static unsigned starts, restarts_queued, ring_puts, messages;
static bool ring_was_halted;

static void
xhci_xfer_put_trb(struct xhci_xfer *xx, int idx, uint64_t parameter,
    uint32_t status, uint32_t control)
{
	assert(xx == &transfer && idx >= 0 && idx < NFRAMES);
	(void)parameter;
	(void)status;
	controls[idx] = control;
}

static void
xhci_ring_put_xfer(struct xhci_softc *sc, struct xhci_ring *tr,
    struct xhci_xfer *xx, int ntrbs)
{
	assert(sc == &controller && tr == &ring && xx == &transfer);
	assert(ntrbs == NFRAMES);
	ring_puts++;
}

static bool
xhci_pipe_restart(struct usbd_pipe *pipe, bool force)
{
	assert(pipe == &test_pipe.xp_pipe && !force);
	return ring_was_halted;
}

static void
xhci_pipe_restart_async(struct usbd_pipe *pipe)
{
	assert(pipe == &test_pipe.xp_pipe);
	restarts_queued++;
}

static int
ratecheck(struct timeval *last, const struct timeval *interval)
{
	(void)last;
	(void)interval;
	return 1;
}

static void
device_printf(int dev, const char *format, ...)
{
	(void)dev;
	(void)format;
	messages++;
}

static usbd_status
counting_start(struct usbd_xfer *xfer)
{
	assert(xfer == &transfer.xx_xfer);
	starts++;
	return USBD_IN_PROGRESS;
}

#include "xhci_pipe_restart_async_task.h"
#include "xhci_device_isoc_enter.h"
#ifdef XHCI_HAVE_XFER_STALLED
#include "xhci_xfer_stalled.h"
#endif

/*
 * As in xhci_device_isoc_methods, there is no upm_start.  The value is read
 * at run time so that a call through it faults instead of being optimised
 * away as undefined behaviour.
 */
static usbd_status (*volatile no_start)(struct usbd_xfer *);
static struct usbd_pipe_methods isoc_methods;
static const struct usbd_pipe_methods bulk_methods = { counting_start };

static void
reset(int speed, uint8_t interval, bool frame_id_capable,
    const struct usbd_pipe_methods *methods)
{
	isoc_methods.upm_start = no_start;
	memset(&test_pipe, 0, sizeof(test_pipe));
	memset(&transfer, 0, sizeof(transfer));
	memset(controls, 0, sizeof(controls));
	controller.sc_hcc = frame_id_capable ? 1 : 0;
	device.ud_speed = speed;
	descriptor.bInterval = interval;
	descriptor.wMaxPacketSize = 800;
	test_pipe.xp_pipe.up_dev = &device;
	test_pipe.xp_pipe.up_endpoint = &endpoint;
	test_pipe.xp_pipe.up_methods = methods;
	test_pipe.xp_isoc_next = -1;
	test_pipe.xp_maxb = 1;
	for (int i = 0; i < NFRAMES; i++)
		lengths[i] = 800;
	transfer.xx_xfer.ux_pipe = &test_pipe.xp_pipe;
	transfer.xx_xfer.ux_frlengths = lengths;
	transfer.xx_xfer.ux_nframes = NFRAMES;
	transfer.xx_xfer.ux_length = NFRAMES * 800;
	current_mfindex = 80;
	starts = restarts_queued = ring_puts = messages = 0;
	ring_was_halted = true;
}

static void
submit(void)
{
	const usbd_status status = xhci_device_isoc_enter(&transfer.xx_xfer);

	assert(status == USBD_IN_PROGRESS);
	assert(ring_puts == 1);
}

/* 125 us interval: eight TDs belong to one frame, so none can name its own. */
static void
high_speed_microframe_interval_is_scheduled_asap(void)
{
	reset(USB_SPEED_HIGH, 1, true, &isoc_methods);
	submit();
	for (int i = 0; i < NFRAMES; i++)
		assert((controls[i] & XHCI_TRB_3_ISO_SIA_BIT) != 0);
}

/* bInterval 4 at high speed is 8 microframes: one TD per frame. */
static void
high_speed_frame_interval_advances_one_frame_per_td(void)
{
	reset(USB_SPEED_HIGH, 4, true, &isoc_methods);
	submit();
	for (int i = 0; i < NFRAMES; i++)
		assert((controls[i] & XHCI_TRB_3_ISO_SIA_BIT) == 0);
	for (int i = 1; i < NFRAMES; i++)
		assert(XHCI_TRB_3_FRID_GET(controls[i]) ==
		    XHCI_TRB_3_FRID_GET(controls[i - 1]) + 1);
}

/* Full speed counts frames already: the Frame IDs stay as they were. */
static void
full_speed_keeps_one_frame_per_td(void)
{
	reset(USB_SPEED_FULL, 1, true, &isoc_methods);
	submit();
	for (int i = 1; i < NFRAMES; i++) {
		assert((controls[i] & XHCI_TRB_3_ISO_SIA_BIT) == 0);
		assert(XHCI_TRB_3_FRID_GET(controls[i]) ==
		    XHCI_TRB_3_FRID_GET(controls[i - 1]) + 1);
	}
}

static void
controller_without_frame_ids_is_scheduled_asap(void)
{
	reset(USB_SPEED_FULL, 1, false, &isoc_methods);
	submit();
	for (int i = 0; i < NFRAMES; i++)
		assert((controls[i] & XHCI_TRB_3_ISO_SIA_BIT) != 0);
}

/* Isochronous pipe methods have no upm_start: the task must not call it. */
static void
ring_restart_skips_missing_start_method(void)
{
	reset(USB_SPEED_HIGH, 1, true, &isoc_methods);
	transfer.xx_xfer.ux_status = USBD_IN_PROGRESS;
	test_pipe.xp_pipe.up_queue.first = &transfer.xx_xfer;
	xhci_pipe_restart_async_task(&test_pipe.xp_pipe);
	assert(starts == 0);
}

static void
ring_restart_starts_queued_bulk_transfer(void)
{
	reset(USB_SPEED_HIGH, 1, true, &bulk_methods);
	transfer.xx_xfer.ux_status = USBD_IN_PROGRESS;
	test_pipe.xp_pipe.up_queue.first = &transfer.xx_xfer;
	xhci_pipe_restart_async_task(&test_pipe.xp_pipe);
	assert(starts == 1);
}

/* A babble loses the isochronous frame; it does not retire a running ring. */
static void
isochronous_babble_does_not_restart_ring(void)
{
#ifdef XHCI_HAVE_XFER_STALLED
	reset(USB_SPEED_HIGH, 1, true, &isoc_methods);
	assert(xhci_xfer_stalled(&controller, &transfer.xx_xfer,
	    UE_ISOCHRONOUS, XHCI_TRB_ERROR_BABBLE, 1, 2) == USBD_IOERROR);
	assert(restarts_queued == 0 && messages == 1);
	reset(USB_SPEED_HIGH, 1, true, &bulk_methods);
	assert(xhci_xfer_stalled(&controller, &transfer.xx_xfer,
	    UE_BULK, XHCI_TRB_ERROR_BABBLE, 1, 2) == USBD_STALLED);
	assert(restarts_queued == 1);
#else
	(void)restarts_queued;
	(void)messages;
	(void)ratecheck;
	(void)device_printf;
	(void)xhci_pipe_restart_async;
	fprintf(stderr, "xhci_xfer_stalled is missing: an isochronous babble "
	    "restarts the ring\n");
	assert(false);
#endif
}

static const struct {
	const char *name;
	void (*run)(void);
} cases[] = {
	{ "high-speed-microframe", high_speed_microframe_interval_is_scheduled_asap },
	{ "high-speed-frame", high_speed_frame_interval_advances_one_frame_per_td },
	{ "full-speed", full_speed_keeps_one_frame_per_td },
	{ "no-frame-id", controller_without_frame_ids_is_scheduled_asap },
	{ "restart-isoc", ring_restart_skips_missing_start_method },
	{ "restart-bulk", ring_restart_starts_queued_bulk_transfer },
	{ "babble", isochronous_babble_does_not_restart_ring },
};

int
main(int argc, char **argv)
{
	const char *selected = argc > 1 ? argv[1] : "all";
	unsigned ran = 0;

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		if (strcmp(selected, "all") != 0 &&
		    strcmp(selected, cases[i].name) != 0)
			continue;
		cases[i].run();
		printf("ok xhci %s\n", cases[i].name);
		ran++;
	}
	return ran != 0 ? 0 : 2;
}
