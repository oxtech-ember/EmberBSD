/*
 * Origin: EmberBSD (AI-assisted).
 * Compile the actual uvideo(4) isochronous completion callback with stubs.
 * This checks which completions keep the transfer queued.  It does not
 * emulate a camera or a host controller.
 */
/* The checks are assertions: keep them in every build. */
#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int usbd_status;

#define USBD_NORMAL_COMPLETION 0
#define USBD_CANCELLED 6
#define USBD_IOERROR 13
#define USBD_STALLED 15
#define DPRINTF(x) ((void)0)

struct usbd_xfer { uint32_t actual; };
struct usbd_pipe { unsigned halts_cleared; };
struct uvideo_stream { unsigned processed; };
struct uvideo_isoc_xfer {
	struct usbd_pipe *ix_pipe;
	int ix_nframes;
	uint32_t ix_uframe_len;
};
struct uvideo_isoc {
	struct uvideo_stream *i_vs;
	struct uvideo_isoc_xfer *i_ix;
	uint8_t *i_buf;
	uint16_t *i_frlengths;
};

#define NFRAMES 4
static struct usbd_pipe test_pipe;
static struct uvideo_stream stream;
static struct uvideo_isoc_xfer isoc_xfer = { &test_pipe, NFRAMES, 8 };
static uint8_t buffer[NFRAMES * 8];
static uint16_t lengths[NFRAMES];
static struct uvideo_isoc isoc = { &stream, &isoc_xfer, buffer, lengths };
static struct usbd_xfer transfer;
static unsigned resubmitted;

static void
usbd_get_xfer_status(struct usbd_xfer *xfer, void **priv, void **buf,
    uint32_t *count, usbd_status *status)
{
	assert(xfer == &transfer && priv == NULL && buf == NULL &&
	    status == NULL);
	*count = xfer->actual;
}

static usbd_status
uvideo_stream_recv_process(struct uvideo_stream *vs, uint8_t *buf,
    uint32_t len)
{
	assert(vs == &stream && buf >= buffer && buf < buffer + sizeof(buffer));
	(void)len;
	vs->processed++;
	return USBD_NORMAL_COMPLETION;
}

static usbd_status
uvideo_stream_recv_isoc_start1(struct uvideo_isoc *started)
{
	assert(started == &isoc);
	resubmitted++;
	return USBD_NORMAL_COMPLETION;
}

/* Called only by the callback before the repair. */
static void __attribute__((unused))
usbd_clear_endpoint_stall_async(struct usbd_pipe *pipe)
{
	assert(pipe == &test_pipe);
	pipe->halts_cleared++;
}

#include "uvideo_stream_recv_isoc_complete.h"

static void
complete(usbd_status status)
{
	assert(isoc_xfer.ix_pipe == &test_pipe);
	resubmitted = stream.processed = test_pipe.halts_cleared = 0;
	transfer.actual = NFRAMES * 8;
	uvideo_stream_recv_isoc_complete(&transfer, &isoc, status);
}

static void
successful_transfer_is_processed_and_resubmitted(void)
{
	complete(USBD_NORMAL_COMPLETION);
	assert(stream.processed == NFRAMES && resubmitted == 1);
}

/* A lost transfer must stay queued, or the stream dies after UVIDEO_NXFERS. */
static void
failed_transfer_is_resubmitted(void)
{
	complete(USBD_IOERROR);
	assert(stream.processed == 0 && resubmitted == 1);
}

/* An isochronous endpoint has no halt feature to clear. */
static void
stalled_transfer_is_resubmitted_without_clearing_halt(void)
{
	complete(USBD_STALLED);
	assert(resubmitted == 1 && test_pipe.halts_cleared == 0);
}

static void
cancelled_transfer_stops(void)
{
	complete(USBD_CANCELLED);
	assert(stream.processed == 0 && resubmitted == 0);
}

static const struct {
	const char *name;
	void (*run)(void);
} cases[] = {
	{ "uvideo-success", successful_transfer_is_processed_and_resubmitted },
	{ "uvideo-error", failed_transfer_is_resubmitted },
	{ "uvideo-stall", stalled_transfer_is_resubmitted_without_clearing_halt },
	{ "uvideo-cancel", cancelled_transfer_stops },
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
		printf("ok uvideo %s\n", cases[i].name);
		ran++;
	}
	return ran != 0 ? 0 : 2;
}
