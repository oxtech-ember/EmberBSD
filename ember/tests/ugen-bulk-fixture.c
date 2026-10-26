/* Test-only USB bus. Production transfer loops are inserted by the runner. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/* CONSTANTS */
#define IN 1
#define OUT 0
#define UGEN_SHORT_OK 1
#define USBD_SHORT_XFER_OK 2
#define USBD_INTERRUPTED 3
#define USBD_TIMEOUT 4
#define USBD_STALLED 5
#define DPRINTFN(...) ((void)0)
#define MIN(a,b) ((a) < (b) ? (a) : (b))
static unsigned uimin(unsigned a, unsigned b) { return a < b ? a : b; }
typedef int usbd_status;
struct ugen_endpoint { void *pipeh; unsigned state, timeout; };
struct ugen_softc { struct ugen_endpoint sc_endpoints[16][2]; char sc_buffer[1024]; };
struct uio { size_t uio_resid; size_t offset; };
struct usbd_xfer { unsigned capacity; unsigned char *data; };
static struct {
    unsigned creates, destroys, moves, transfers, lengths[512], flags, timeout;
    unsigned short_at, short_length, fail_move, fail_transfer, create_error;
    unsigned expected_direction, own_buffer, error_status;
    size_t moved;
} bus;

static int
usbd_create_xfer(void *p, uint32_t cap, int flags, int frames, struct usbd_xfer **x)
{
    bus.creates++;
    if (bus.create_error) return bus.create_error;
    *x = calloc(1, sizeof(**x));
    if (*x == NULL) return ENOMEM;
    (*x)->capacity = cap;
    (*x)->data = malloc(cap ? cap : 1);
    if ((*x)->data == NULL) { free(*x); *x = NULL; return ENOMEM; }
    return 0;
}
static void *usbd_get_buffer(struct usbd_xfer *x) { return x->data; }
static void usbd_destroy_xfer(struct usbd_xfer *x)
{ bus.destroys++; free(x->data); free(x); }
static int
usbd_bulk_transfer(struct usbd_xfer *x, void *p, uint16_t flags,
    uint32_t timeout, void *buf, uint32_t *n)
{
    bus.transfers++;
    if (bus.transfers <= 512) bus.lengths[bus.transfers - 1] = *n;
    bus.flags = flags; bus.timeout = timeout;
    if (buf != x->data) bus.own_buffer = 0;
    if (*n > x->capacity) abort();
    if (bus.fail_transfer == bus.transfers) return bus.error_status;
    if (bus.short_at == bus.transfers) *n = bus.short_length;
    if (bus.expected_direction == IN) memset(buf, 0x5a, *n);
    return 0;
}
static int
uiomove(void *buf, size_t n, struct uio *u)
{
    bus.moves++;
    if (bus.fail_move == bus.moves) return EFAULT;
    if (n > (size_t)u->uio_resid) abort();
    if (bus.expected_direction == OUT) memset(buf, 0x3c, n);
    u->uio_resid -= n; u->offset += n; bus.moved += n;
    return 0;
}
static int actual_read(struct ugen_softc *sc, int endpt, struct uio *uio, int flag)
{
/* READ */
}
static int actual_write(struct ugen_softc *sc, int endpt, struct uio *uio, int flag)
{
/* WRITE */
}
static struct ugen_softc sc;
static unsigned failed, cases;
static void reset(unsigned direction)
{
    memset(&bus, 0, sizeof(bus)); memset(&sc, 0, sizeof(sc));
    bus.own_buffer = 1; bus.expected_direction = direction;
    sc.sc_endpoints[1][direction].timeout = 2345;
    sc.sc_endpoints[1][direction].state = UGEN_SHORT_OK;
}
static void check(const char *name, int ok)
{ cases++; printf("%s %s\n", ok ? "PASS" : "FAIL", name); failed += !ok; }
static int run(unsigned direction, size_t size)
{
    struct uio u = { .uio_resid = size };
    return direction == IN ? actual_read(&sc, 1, &u, 0) : actual_write(&sc, 1, &u, 0);
}
int main(void)
{
    for (unsigned direction = 0; direction < 2; direction++) {
        reset(direction); int e = run(direction, 262144);
        check(direction ? "read four full 64KiB requests" : "write four full 64KiB requests",
            !e && bus.transfers == 4 && bus.lengths[0] == 65536 && bus.lengths[3] == 65536 && bus.moved == 262144);
        check(direction ? "read owns xfer buffer" : "write owns xfer buffer", bus.own_buffer);
        check(direction ? "read destroys xfer" : "write destroys xfer", bus.creates == 1 && bus.destroys == 1);
        check(direction ? "read preserves flags timeout" : "write preserves flags timeout",
            bus.timeout == 2345 && bus.flags == (direction ? USBD_SHORT_XFER_OK : 0));
        reset(direction); e = run(direction, 70000);
        check(direction ? "read remainder" : "write remainder", !e && bus.transfers == 2 && bus.lengths[1] == 4464 && bus.moved == 70000);
        reset(direction); e = run(direction, 0);
        check(direction ? "read zero no xfer" : "write zero no xfer", !e && !bus.creates && !bus.transfers);
        reset(direction); bus.create_error = ENOMEM; e = run(direction, 16);
        check(direction ? "read create failure" : "write create failure", e == ENOMEM && !bus.moves && !bus.destroys);
        reset(direction); bus.fail_move = 1; e = run(direction, 70000);
        check(direction ? "read uiomove failure" : "write uiomove failure", e == EFAULT && bus.destroys == 1 && bus.transfers == (direction ? 1U : 0U));
        for (unsigned error = 3; error <= 5; error++) {
            reset(direction); bus.fail_transfer = 1; bus.error_status = error; e = run(direction, 70000);
            check(direction ? "read USB error maps and cleans" : "write USB error maps and cleans",
                e == (error == 3 ? EINTR : error == 4 ? ETIMEDOUT : EIO) && bus.destroys == 1 && bus.transfers == 1);
        }
        for (size_t extra = 0; extra <= 17; extra += 17) {
            reset(direction); bus.fail_transfer = 1; bus.error_status = USBD_TIMEOUT;
            e = run(direction, (size_t)UINT32_MAX + 1 + extra);
            check(direction ? "read large residual stays bounded" : "write large residual stays bounded",
                e == ETIMEDOUT && bus.transfers == 1 && bus.lengths[0] == 65536 && bus.destroys == 1);
        }
    }
    reset(IN); bus.short_at = 1; bus.short_length = 8;
    int e = run(IN, 1024);
    check("short header stops before following IQ", !e && bus.transfers == 1 && bus.moved == 8);
    reset(IN); bus.short_at = 2; bus.short_length = 13; e = run(IN, 262144);
    check("short after full request stops exactly", !e && bus.transfers == 2 && bus.moved == 65536 + 13);
    printf("TOTAL %u/%u\n", cases - failed, cases);
    return failed ? 1 : 0;
}
